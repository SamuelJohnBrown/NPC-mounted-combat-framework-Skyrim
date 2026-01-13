#include "ArrowSystem.h"
#include "WeaponDetection.h"
#include "DynamicPackages.h"
#include "Helper.h"
#include "config.h"
#include "skse64/GameRTTI.h"
#include "skse64/GameData.h"
#include "skse64/GameObjects.h"
#include "skse64/GameThreads.h"
#include "skse64/GameReferences.h"  // For g_thePlayer
#include "skse64/PluginAPI.h"
#include "skse64/PapyrusVM.h"
#include "skse64_common/Relocation.h"
#include "skse64_common/SafeWrite.h"
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <mutex>
#include <vector>
#include <unordered_map>

namespace MountedNPCCombatVR
{
	// ============================================
	// EXTERNAL TASK INTERFACE
	// ============================================
	extern SKSETaskInterface* g_task;
	
	// ============================================
	// PROJECTILE CLASS DEFINITION
	// Based on SpellAutoAimVR's reverse engineering
	// ============================================
	
	class Projectile : public TESObjectREFR
	{
	public:
		// Offsets from SpellAutoAimVR Engine.h
		UInt8 pad98[0xF0 - 0x98];		// 098
		NiPoint3 unk0F0;				// 0F0 - point/origin
		NiPoint3 velocity;				// 0FC - velocity vector
		UInt8 pad108[0x120 - 0x108];	// 108
		UInt32 shooter;					// 120 - shooter handle/formID
		UInt32 desiredTarget;			// 124 - target handle
	};
	
	// ============================================
	// PROJECTILE HOOK SYSTEM
	// ============================================
	
	// Pending projectile aim data
	struct PendingProjectileAim
	{
		UInt32 shooterFormID;
		NiPoint3 targetAimPos;
		float registeredTime;
	};
	
	// Projectile redirect tracking
	static std::vector<PendingProjectileAim> g_pendingAims;
	static std::unordered_map<UInt32, bool> g_redirectedProjectiles;
	static std::unordered_map<UInt32, bool> g_loggedProjectiles;
	static std::mutex g_projectileAimMutex;
	static bool g_projectileHookInstalled = false;
	static bool g_hookProcessingEnabled = true;
	
	// Original function pointer
	typedef void (*_UpdateProjectileArrow)(Projectile* proj, float deltaTime);
	_UpdateProjectileArrow g_originalUpdateArrow = nullptr;
	
	// VTable offset for ArrowProjectile
	// ArrowProjectileVtbl_Offset = 0x016F93A8, Update at index 0xAC
	const uintptr_t ArrowProjectileVtbl_Offset = 0x016F93A8;
	const int UpdateFunctionIndex = 0xAC;
	
	RelocPtr<_UpdateProjectileArrow> UpdateProjectileArrow_vtbl(ArrowProjectileVtbl_Offset + UpdateFunctionIndex * 8);
	
	// Note: Arrow aiming configuration is now in config.h
	// ArrowShooterHeightOffset, ArrowTargetFootHeight, ArrowTargetMountedHeight

	// ============================================
	// ARROW SPELL CONFIGURATION
	// ============================================
	
	// Arrow spell FormID from MountedNPCCombat.esp (ESL flagged)
	const UInt32 ARROW_SPELL_BASE_FORMID = 0x0008F0;
	const char* ARROW_SPELL_ESP_NAME = "MountedNPCCombat.esp";
	
	// Spell casting native function
	typedef bool (*_RemoteCast)(VMClassRegistry* registry, UInt32 stackId, SpellItem* spell, TESObjectREFR* akSource, Actor* blameActor, TESObjectREFR* akTarget);
	RelocAddr<_RemoteCast> RemoteCast(0x009BB7F0);
	
	// Cached arrow spell
	static SpellItem* g_arrowSpell = nullptr;
	static bool g_arrowSpellInitialized = false;
	static bool g_arrowSystemInitialized = false;
	
	// ============================================
	// BOW ATTACK ANIMATION CONFIGURATION
	// ============================================
	
	// Bow attack animation FormIDs from MountedNPCCombat.esp
	const UInt32 BOW_ATTACK_CHARGE_BASE_FORMID = 0x0008EA;  // Bow draw/charge
	const UInt32 BOW_ATTACK_RELEASE_BASE_FORMID = 0x0008EB;  // Bow release
	const char* BOW_ESP_NAME = "MountedNPCCombat.esp";
	
	// Bow attack timing configuration
	const float BOW_EQUIP_DELAY = 1.5f;      // Must have bow equipped for 1.5 seconds before drawing
	const float BOW_DRAW_MIN_TIME = 2.0f;    // Minimum draw hold time
	const float BOW_DRAW_MAX_TIME = 3.5f;    // Maximum draw hold time
	const float BOW_STATE_TIMEOUT = 3.5f;    // Force release if stuck in draw/hold for 3.5 seconds (failsafe)
	
	// Cached bow attack idles
	static TESIdleForm* g_bowAttackCharge = nullptr;
	static TESIdleForm* g_bowAttackRelease = nullptr;
	static bool g_bowIdlesInitialized = false;
	
	// Bow attack state tracking per rider
	enum class BowAttackState
	{
		None = 0,
		WaitingToEquip,   // Bow equipped, waiting 1.5 seconds
		Drawing,        // Bow being drawn
		Holding,          // Bow drawn, holding for 2-3.5 seconds
		Released          // Arrow released
	};
	
	struct RiderBowAttackData
	{
		UInt32 riderFormID;
		BowAttackState state;
		float bowEquipTime;       // When bow was equipped
		float drawStartTime;    // When draw animation started
		float holdDuration;               // Random hold time (2-3.5 seconds)
		float stateEntryTime;        // When current state was entered (for timeout detection)
		bool arrowsEquippedThisSession; // True if arrows were equipped this combat session
		bool isValid;
	};
	
	static RiderBowAttackData g_riderBowData[5];
	static int g_riderBowCount = 0;
	
	// Rapid fire timing constants
	// ============================================
	// RAPID FIRE BOW ATTACK TIMING
	// Each shot cycle: 1.2s draw + instant release = ~1.5s total per shot
	// ============================================
	
	const float RAPID_FIRE_DRAW_TIME = 1.2f;      // Time to draw bow (1.2 seconds)
	const float RAPID_FIRE_RELEASE_TIME = 0.3f;   // Brief pause after release animation before next draw
	
	const float RAPID_FIRE_BETWEEN_SHOTS = 0.2f;  // Time between shots
	
	// Random seed tracking - now handled by shared EnsureRandomSeeded() in Helper.h
	// static bool g_randomSeeded = false;  // REMOVED - using shared version
	
	// Use shared GetGameTime() from Helper.h instead of local function
	static float GetGameTimeSeconds()
	{
		return GetGameTime();
	}
	
	// ============================================
	// RAPID FIRE BOW ATTACK TRACKING
	// ============================================
	// Track rapid fire bow attack state per rider
	
	enum class RapidFireBowState
	{
		None = 0,
		Drawing,       // Drawing bow
		Holding,// Holding at full draw
		Releasing,     // Releasing arrow
		BetweenShots,  // Brief cooldown between shots
		Complete // All shots fired
	};
	
	struct RapidFireBowData
	{
		UInt32 riderFormID;
		RapidFireBowState state;
		float stateStartTime;
		int shotsFired;
		int maxShots;
		float holdDuration;       // Random time to hold at full draw
		bool firedThisRelease;    // Track if arrow was fired in current release state
		bool drewThisDraw;        // Track if bow was drawn in current draw state
		bool isValid;
	};
	
	const int MAX_RAPID_FIRE_RIDERS = 10;
	static RapidFireBowData g_rapidFireBowData[MAX_RAPID_FIRE_RIDERS];
	static int g_rapidFireBowCount = 0;
	
	// Local static animation event function (to avoid conflict with CombatStyles)
	static bool SendBowAnimationEvent(Actor* actor, const char* eventName)
	{
		if (!actor) return false;
		
		BSFixedString event(eventName);
		
		// Use vtable call to NotifyAnimationGraph
		typedef bool (*_IAnimationGraphManagerHolder_NotifyAnimationGraph)(IAnimationGraphManagerHolder* _this, const BSFixedString& eventName);
		return get_vfunc<_IAnimationGraphManagerHolder_NotifyAnimationGraph>(&actor->animGraphHolder, 0x1)(&actor->animGraphHolder, event);
	}
	
	// ============================================
	// PROJECTILE UPDATE HOOK
	// ============================================
	
	void UpdateProjectileArrow_Hook(Projectile* proj, float deltaTime)
	{
		// Call original first
		if (g_originalUpdateArrow)
		{
			g_originalUpdateArrow(proj, deltaTime);
		}
		
		// Safety checks
		if (!g_hookProcessingEnabled || !proj) return;
		if (proj->formID == 0 || proj->formID == 0xFFFFFFFF) return;
		
		try
		{
			// Removed per-projectile logging - too verbose
			
			// Check if we've already redirected this projectile
			{
				std::lock_guard<std::mutex> lock(g_projectileAimMutex);
				if (g_redirectedProjectiles.find(proj->formID) != g_redirectedProjectiles.end())
				{
					return;
				}
				
				if (g_pendingAims.empty())
				{
					return;
				}
			}
			
			// Resolve shooter handle to actual FormID
			UInt32 shooterFormID = 0;
			if (proj->shooter != 0)
			{
				NiPointer<TESObjectREFR> shooterRef;
				LookupREFRByHandle(proj->shooter, shooterRef);
				if (shooterRef)
				{
					shooterFormID = shooterRef->formID;
				}
			}
			
			if (shooterFormID == 0)
			{
				return;
			}
			
			std::lock_guard<std::mutex> lock(g_projectileAimMutex);
			
			for (auto it = g_pendingAims.begin(); it != g_pendingAims.end(); )
			{
				float currentTime = (float)clock() / CLOCKS_PER_SEC;
				
				if (currentTime - it->registeredTime > 2.0f)
				{
					it = g_pendingAims.erase(it);
					continue;
				}
				
				bool match = (shooterFormID == it->shooterFormID);
				
				if (match)
				{
					// Calculate direction from projectile to target aim position
					NiPoint3 projPos = proj->pos;
					NiPoint3 targetPos = it->targetAimPos;
					
					NiPoint3 direction;
					direction.x = targetPos.x - projPos.x;
					direction.y = targetPos.y - projPos.y;
					direction.z = targetPos.z - projPos.z;
					
					float speed = sqrt(proj->velocity.x * proj->velocity.x + 
									   proj->velocity.y * proj->velocity.y + 
									   proj->velocity.z * proj->velocity.z);
					
					if (speed < 100.0f) speed = 3000.0f;
					
					float dirLen = sqrt(direction.x * direction.x + direction.y * direction.y + direction.z * direction.z);
					if (dirLen > 0.0001f)
					{
						proj->velocity.x = (direction.x / dirLen) * speed;
						proj->velocity.y = (direction.y / dirLen) * speed;
						proj->velocity.z = (direction.z / dirLen) * speed;
						
						float normalizedZ = proj->velocity.z / speed;
						proj->rot.x = asin(normalizedZ);
						proj->rot.z = atan2(proj->velocity.x, proj->velocity.y);
						
						if (proj->rot.z < 0.0f)
							proj->rot.z += 3.14159265f;
						if (proj->velocity.x < 0.0f)
							proj->rot.z += 3.14159265f;
						
						// Only log redirects - significant events
						_MESSAGE("ArrowSystem: Redirected arrow %08X from %08X", proj->formID, shooterFormID);
					}
					
					g_redirectedProjectiles[proj->formID] = true;
					it = g_pendingAims.erase(it);
					
					if (g_redirectedProjectiles.size() > 100)
						g_redirectedProjectiles.clear();
					
					return;
				}
				
				++it;
			}
		}
		catch (...)
		{
			// Silent catch
		}
	}
	
	// ============================================
	// PROJECTILE HOOK CONTROL FUNCTIONS
	// ============================================
	
	void ClearPendingProjectileAims()
	{
		std::lock_guard<std::mutex> lock(g_projectileAimMutex);
		g_pendingAims.clear();
		g_redirectedProjectiles.clear();
		g_loggedProjectiles.clear();
	}
	
	void SetProjectileHookEnabled(bool enabled)
	{
		g_hookProcessingEnabled = enabled;
	}
	
	void InstallProjectileHook()
	{
		if (g_projectileHookInstalled) return;
		
		// Save original function
		g_originalUpdateArrow = *UpdateProjectileArrow_vtbl;
		
		// Write our hook
		SafeWrite64(UpdateProjectileArrow_vtbl.GetUIntPtr(), (uintptr_t)&UpdateProjectileArrow_Hook);
		
		g_projectileHookInstalled = true;
		_MESSAGE("ArrowSystem: Projectile hook installed");
	}
	
	void RegisterProjectileForRedirect(UInt32 shooterFormID, UInt32 targetFormID, const NiPoint3& targetAimPos)
	{
		std::lock_guard<std::mutex> lock(g_projectileAimMutex);
		
		PendingProjectileAim aim;
		aim.shooterFormID = shooterFormID;
		aim.targetAimPos = targetAimPos;
		aim.registeredTime = (float)clock() / CLOCKS_PER_SEC;
		
		g_pendingAims.push_back(aim);
	}

	// ============================================
	// TASK CLASS FOR DELAYED ARROW FIRING
	// Waits 200ms after release animation before actually firing
	// ============================================
	
	struct DelayedArrowFire
	{
		UInt32 shooterFormID;
		UInt32 targetFormID;
		float scheduledTime;
		bool isValid;
		
		DelayedArrowFire() : shooterFormID(0), targetFormID(0), scheduledTime(0.0f), isValid(false) {}
	};
	
	static DelayedArrowFire g_delayedArrows[10];
	static int g_delayedArrowCount = 0;
	static const float ARROW_FIRE_DELAY = 0.2f;  // 200ms delay
	
	void ScheduleDelayedArrowFire(Actor* shooter, Actor* target)
	{
		if (!shooter || !target) return;
		
		// Find an empty slot
		for (int i = 0; i < 10; i++)
		{
			if (!g_delayedArrows[i].isValid)
			{
				g_delayedArrows[i].shooterFormID = shooter->formID;
				g_delayedArrows[i].targetFormID = target->formID;
				g_delayedArrows[i].scheduledTime = GetGameTimeSeconds() + ARROW_FIRE_DELAY;
				g_delayedArrows[i].isValid = true;
				return;
			}
		}
		
		// If no slot, use first slot (overwrite oldest)
		g_delayedArrows[0].shooterFormID = shooter->formID;
		g_delayedArrows[0].targetFormID = target->formID;
		g_delayedArrows[0].scheduledTime = GetGameTimeSeconds() + ARROW_FIRE_DELAY;
		g_delayedArrows[0].isValid = true;
	}
	
	void UpdateDelayedArrowFires()
	{
		float currentTime = GetGameTimeSeconds();
		
		for (int i = 0; i < 10; i++)
		{
			if (!g_delayedArrows[i].isValid) continue;
			if (currentTime < g_delayedArrows[i].scheduledTime) continue;
			
			// Mark as invalid first to prevent re-entry issues
			g_delayedArrows[i].isValid = false;
			
			// Time to fire!
			UInt32 shooterID = g_delayedArrows[i].shooterFormID;
			UInt32 targetID = g_delayedArrows[i].targetFormID;
			
			// Validate form IDs
			if (shooterID == 0 || targetID == 0) continue;
			
			TESForm* shooterForm = LookupFormByID(shooterID);
			TESForm* targetForm = LookupFormByID(targetID);
			
			if (!shooterForm || !targetForm) continue;
			
			Actor* shooter = DYNAMIC_CAST(shooterForm, TESForm, Actor);
			Actor* target = DYNAMIC_CAST(targetForm, TESForm, Actor);
			
			if (!shooter || !target) continue;
			if (shooter->IsDead(1) || target->IsDead(1)) continue;
			
			FireArrowSpellAtTarget(shooter, target);
		}
	}
	
	void ClearDelayedArrowFires()
	{
		for (int i = 0; i < 10; i++)
		{
			g_delayedArrows[i].isValid = false;
		}
		g_delayedArrowCount = 0;
	}

	// ============================================
	// TASK CLASS FOR CASTING ARROW SPELL
	// ============================================
	
	class TaskCastArrowSpell : public TaskDelegate
	{
	public:
		UInt32 m_shooterFormID;
		UInt32 m_targetFormID;
		float m_targetX;
		float m_targetY;
		float m_targetZ;
		
		TaskCastArrowSpell(Actor* shooter, Actor* target, float aimX, float aimY, float aimZ)
			: m_shooterFormID(shooter ? shooter->formID : 0)
			, m_targetFormID(target ? target->formID : 0)
			, m_targetX(aimX)
			, m_targetY(aimY)
			, m_targetZ(aimZ) {}
		
		virtual void Run() override
		{
			TESForm* shooterForm = LookupFormByID(m_shooterFormID);
			TESForm* targetForm = LookupFormByID(m_targetFormID);
			
			if (!shooterForm || !targetForm)
			{
				return;
			}
			
			Actor* shooter = DYNAMIC_CAST(shooterForm, TESForm, Actor);
			Actor* target = DYNAMIC_CAST(targetForm, TESForm, Actor);
			
			if (!shooter || !target)
			{
				return;
			}
			
			// Initialize arrow spell if needed
			if (!g_arrowSpellInitialized)
			{
				UInt32 spellFormID = GetFullFormIdMine(ARROW_SPELL_ESP_NAME, ARROW_SPELL_BASE_FORMID);
				if (spellFormID != 0)
				{
					TESForm* spellForm = LookupFormByID(spellFormID);
					if (spellForm)
					{
						g_arrowSpell = DYNAMIC_CAST(spellForm, TESForm, SpellItem);
					}
				}
				g_arrowSpellInitialized = true;
			}
			
			if (!g_arrowSpell)
			{
				_MESSAGE("ArrowSystem: ERROR - Arrow spell not available!");
				return;
			}
			
			VMClassRegistry* registry = (*g_skyrimVM)->GetClassRegistry();
			if (!registry)
			{
				return;
			}
			
			// Register this projectile for redirection BEFORE casting
			NiPoint3 aimPos;
			aimPos.x = m_targetX;
			aimPos.y = m_targetY;
			aimPos.z = m_targetZ;
			RegisterProjectileForRedirect(m_shooterFormID, m_targetFormID, aimPos);
			
			// Cast the spell
			RemoteCast(registry, 0, g_arrowSpell, shooter, shooter, target);
		}
		
		virtual void Dispose() override
		{
			delete this;
		}
	};
	
	// ============================================
	// ARROW SPELL FIRING
	// ============================================
	
	bool FireArrowSpellAtTarget(Actor* shooter, Actor* target)
	{
		if (!shooter || !target)
		{
			return false;
		}
		
		if (!g_task)
		{
			return false;
		}
		
		// Install projectile hook if not already done
		InstallProjectileHook();
		
		NiPoint3 targetPos = target->pos;
		
		// Calculate aim position based on whether target is mounted
		float targetAimZ;
		
		NiPointer<Actor> targetMount;
		if (CALL_MEMBER_FN(target, GetMount)(targetMount) && targetMount)
		{
			targetAimZ = targetPos.z + ArrowTargetMountedHeight;
		}
		else
		{
			targetAimZ = targetPos.z + ArrowTargetFootHeight;
		}
		
		// Log the arrow fire for debugging
		const char* shooterName = CALL_MEMBER_FN(shooter, GetReferenceName)();
		const char* targetName = CALL_MEMBER_FN(target, GetReferenceName)();
		bool targetIsPlayer = (g_thePlayer && (*g_thePlayer) && target == (*g_thePlayer));
		
		_MESSAGE("ArrowSystem: FireArrowSpellAtTarget - Shooter: '%s' (%08X) -> Target: '%s' (%08X) [TargetIsPlayer: %s]",
			shooterName ? shooterName : "Unknown", shooter->formID,
			targetName ? targetName : "Unknown", target->formID,
			targetIsPlayer ? "YES" : "NO");
		
		// Queue the spell cast task
		g_task->AddTask(new TaskCastArrowSpell(shooter, target, targetPos.x, targetPos.y, targetAimZ));
		
		return true;
	}
	
	// ============================================
	// BOW ATTACK ANIMATION INITIALIZATION
	// ============================================
	
	static void InitBowIdles()
	{
		if (g_bowIdlesInitialized) return;
		
		// Silently load bow idles - only log errors
		UInt32 chargeFormID = GetFullFormIdMine(BOW_ESP_NAME, BOW_ATTACK_CHARGE_BASE_FORMID);
		UInt32 releaseFormID = GetFullFormIdMine(BOW_ESP_NAME, BOW_ATTACK_RELEASE_BASE_FORMID);
		
		if (chargeFormID != 0)
		{
			TESForm* chargeForm = LookupFormByID(chargeFormID);
			if (chargeForm)
			{
				g_bowAttackCharge = DYNAMIC_CAST(chargeForm, TESForm, TESIdleForm);
			}
		}
		else
		{
			_MESSAGE("ArrowSystem: ERROR - Could not resolve BOW_ATTACK_CHARGE");
		}
		
		if (releaseFormID != 0)
		{
			TESForm* releaseForm = LookupFormByID(releaseFormID);
			if (releaseForm)
			{
				g_bowAttackRelease = DYNAMIC_CAST(releaseForm, TESForm, TESIdleForm);
			}
		}
		else
		{
			_MESSAGE("ArrowSystem: ERROR - Could not resolve BOW_ATTACK_RELEASE");
		}
		
		g_bowIdlesInitialized = true;
	}
	
	// ============================================
	// BOW ATTACK STATE MANAGEMENT
	// ============================================
	
	static RiderBowAttackData* GetOrCreateBowAttackData(UInt32 riderFormID)
	{
		// Check if already tracked
		for (int i = 0; i < g_riderBowCount; i++)
		{
			if (g_riderBowData[i].isValid && g_riderBowData[i].riderFormID == riderFormID)
			{
				return &g_riderBowData[i];
			}
		}
		
		// Create new entry
		if (g_riderBowCount < 5)
		{
			RiderBowAttackData* data = &g_riderBowData[g_riderBowCount];
			data->riderFormID = riderFormID;
			data->state = BowAttackState::None;
			data->bowEquipTime = 0;
			data->drawStartTime = 0;
			data->holdDuration = 0;
			data->stateEntryTime = 0;
			data->arrowsEquippedThisSession = false;
			data->isValid = true;
			g_riderBowCount++;
			return data;
		}
		
		return nullptr;
	}
	
	void ResetBowAttackState(UInt32 riderFormID)
	{
		for (int i = 0; i < g_riderBowCount; i++)
		{
			if (g_riderBowData[i].isValid && g_riderBowData[i].riderFormID == riderFormID)
			{
				g_riderBowData[i].state = BowAttackState::None;
				g_riderBowData[i].bowEquipTime = 0;
				g_riderBowData[i].drawStartTime = 0;
				g_riderBowData[i].holdDuration = 0;
				g_riderBowData[i].stateEntryTime = 0;
				return;
			}
		}
	}
	
	// ============================================
	// CHECK IF BOW IS DRAWN AND READY TO FIRE
	// Returns true if rider is in Drawing or Holding state
	// ============================================
	
	bool IsBowDrawnAndReady(UInt32 riderFormID)
	{
		for (int i = 0; i < g_riderBowCount; i++)
		{
			if (g_riderBowData[i].isValid && g_riderBowData[i].riderFormID == riderFormID)
			{
				BowAttackState state = g_riderBowData[i].state;
				return (state == BowAttackState::Drawing || state == BowAttackState::Holding);
			}
		}
		return false;
	}
	
	// ============================================
	// FORCE RELEASE BOW AT TARGET
	// Called when rider needs to switch to melee but has bow drawn
	// Fires the arrow before switching weapons
	// ============================================
	
	bool ForceReleaseBowAtTarget(Actor* rider, Actor* target)
	{
		if (!rider || !target) return false;
		
		for (int i = 0; i < g_riderBowCount; i++)
		{
			if (g_riderBowData[i].isValid && g_riderBowData[i].riderFormID == rider->formID)
			{
				BowAttackState state = g_riderBowData[i].state;
				
				if (state == BowAttackState::Drawing || state == BowAttackState::Holding)
				{
					_MESSAGE("ArrowSystem: FORCE RELEASE - Rider %08X releasing nocked arrow before weapon switch", rider->formID);
					
					if (PlayBowReleaseAnimation(rider, target))
					{
						// Reset state after release
						g_riderBowData[i].state = BowAttackState::Released;
						g_riderBowData[i].stateEntryTime = GetGameTimeSeconds();
						return true;
					}
					else
					{
						// Animation failed but still fire the arrow
						_MESSAGE("ArrowSystem: FORCE RELEASE - Animation failed, firing arrow directly for rider %08X", rider->formID);
						ScheduleDelayedArrowFire(rider, target);
						g_riderBowData[i].state = BowAttackState::None;
						return true;
					}
				}
				
				return false;
			}
		}
		
		return false;
	}
	
	// ============================================
	// BOW ATTACK ANIMATIONS
	// ============================================
	
	bool PlayBowDrawAnimation(Actor* rider)
	{
		if (!rider) return false;
		
		if (!IsBowEquipped(rider))
		{
			_MESSAGE("ArrowSystem: PlayBowDrawAnimation FAILED - bow not equipped for rider %08X", rider->formID);
			return false;
		}
		
		// ============================================
		// CHECK IF RIDER'S WEAPON IS DRAWN
		// Bow animations won't work if weapon is sheathed
		// ============================================
		if (!IsWeaponDrawn(rider))
		{
			_MESSAGE("ArrowSystem: PlayBowDrawAnimation - weapon not drawn, drawing it first for rider %08X", rider->formID);
			rider->DrawSheatheWeapon(true);
			return false;  // Try again next frame after weapon is drawn
		}
		
		InitBowIdles();
		
		if (g_bowAttackCharge)
		{
			const char* eventName = g_bowAttackCharge->animationEvent.c_str();
			if (eventName && strlen(eventName) > 0)
			{
				_MESSAGE("ArrowSystem: Sending bow draw event '%s' to rider %08X", eventName, rider->formID);
				bool result = SendBowAnimationEvent(rider, eventName);
				if (!result)
				{
					_MESSAGE("ArrowSystem: Bow draw animation REJECTED for rider %08X", rider->formID);
				}
				return result;
			}
			else
			{
				_MESSAGE("ArrowSystem: ERROR - g_bowAttackCharge has empty event name");
			}
		}
		else
		{
			_MESSAGE("ArrowSystem: ERROR - g_bowAttackCharge is null");
		}
		
		return false;
	}
	
	bool PlayBowReleaseAnimation(Actor* rider, Actor* target)
	{
		if (!rider) return false;
		
		if (!IsBowEquipped(rider))
		{
			_MESSAGE("ArrowSystem: PlayBowReleaseAnimation FAILED - bow not equipped for rider %08X", rider->formID);
			return false;
		}
		
		InitBowIdles();
		
		if (g_bowAttackRelease)
		{
			const char* eventName = g_bowAttackRelease->animationEvent.c_str();
			if (eventName && strlen(eventName) > 0)
			{
				if (SendBowAnimationEvent(rider, eventName))
				{
					// Fire arrow IMMEDIATELY when release animation plays
					// Don't use delayed fire - we want the arrow to sync with the animation
					if (target)
					{
						FireArrowSpellAtTarget(rider, target);
					}
					return true;
				}
				else
				{
					_MESSAGE("ArrowSystem: Bow release animation REJECTED for rider %08X", rider->formID);
				}
			}
			else
			{
				_MESSAGE("ArrowSystem: ERROR - g_bowAttackRelease has empty event name");
			}
		}
		else
		{
			_MESSAGE("ArrowSystem: ERROR - g_bowAttackRelease is null");
		}
		
		return false;
	}
	
	// ============================================
	// BOW ATTACK STATE MACHINE
	// ============================================
	
	bool UpdateBowAttack(Actor* rider, bool allowAttack, Actor* target)
	{
		if (!rider) return false;
		
		if (!IsBowEquipped(rider))
		{
			ResetBowAttackState(rider->formID);
			return false;
		}
		
		// ============================================
		// CHECK IF WEAPON IS DRAWN - Don't try bow attacks with sheathed weapon
		// ============================================
		if (!IsWeaponDrawn(rider))
		{
			// Weapon not drawn - reset state and try to draw it
			ResetBowAttackState(rider->formID);
			rider->DrawSheatheWeapon(true);
			return false;
		}
		
		RiderBowAttackData* data = GetOrCreateBowAttackData(rider->formID);
		if (!data) return false;
		
		float currentTime = GetGameTimeSeconds();
		
		// Timeout check - ALSO reset if stuck for too long
		if (data->state != BowAttackState::None && data->stateEntryTime > 0)
		{
			float timeInState = currentTime - data->stateEntryTime;
			
			if (timeInState >= BOW_STATE_TIMEOUT)
			{
				if (data->state == BowAttackState::Drawing || data->state == BowAttackState::Holding)
				{
					// Don't use animation cancel - just reset state cleanly
					// Animation cancel can cause rider to stand on horse
					_MESSAGE("ArrowSystem: Rider %08X bow state TIMEOUT (%.1fs) - resetting state (no animation cancel)", rider->formID, timeInState);
					data->state = BowAttackState::None;
					data->bowEquipTime = currentTime;
					data->stateEntryTime = currentTime;
					return false;
				}
				else if (data->state == BowAttackState::WaitingToEquip)
				{
					// Stuck in waiting state too long - reset
					_MESSAGE("ArrowSystem: Rider %08X stuck in WaitingToEquip for %.1fs - resetting", rider->formID, timeInState);
					data->state = BowAttackState::None;
					data->bowEquipTime = currentTime;
					data->stateEntryTime = currentTime;
					return false;
				}
				else
				{
					data->state = BowAttackState::None;
					data->bowEquipTime = currentTime;
					data->stateEntryTime = currentTime;
					return false;
				}
			}
		}
		
		// Track consecutive animation rejections
		static int s_consecutiveRejections = 0;
		
		switch (data->state)
		{
			case BowAttackState::None:
			{
				data->state = BowAttackState::WaitingToEquip;
				data->bowEquipTime = currentTime;
				data->stateEntryTime = currentTime;
				s_consecutiveRejections = 0;  // Reset rejection counter
				return false;
			}
			
			case BowAttackState::WaitingToEquip:
			{
				if ((currentTime - data->bowEquipTime) >= BOW_EQUIP_DELAY)
				{
					if (allowAttack)
					{
						if (!data->arrowsEquippedThisSession)
						{
							EquipArrows(rider);
							data->arrowsEquippedThisSession = true;
						}
						
						if (PlayBowDrawAnimation(rider))
						{
							data->state = BowAttackState::Drawing;
							data->drawStartTime = currentTime;
							data->stateEntryTime = currentTime;
							s_consecutiveRejections = 0;  // Reset on success
							
							EnsureRandomSeeded();
							float randomRange = BowDrawMaxTime - BowDrawMinTime;
							data->holdDuration = BowDrawMinTime + (((float)(rand() % 100)) / 100.0f * randomRange);
						}
						else
						{
							// Animation rejected - track and reset if too many
							s_consecutiveRejections++;
							
							if (s_consecutiveRejections >= 5)
							{
								_MESSAGE("ArrowSystem: Rider %08X - 5 consecutive animation rejections, full reset", rider->formID);
								data->state = BowAttackState::None;
								data->stateEntryTime = currentTime;
								s_consecutiveRejections = 0;
							}
							else
							{
								// Just reset the equip timer to try again
								data->bowEquipTime = currentTime;
							}
						}
					}
				}
				return false;
			}
			
			case BowAttackState::Drawing:
			{
				data->state = BowAttackState::Holding;
				data->stateEntryTime = currentTime;
				return true;
			}
			
			case BowAttackState::Holding:
			{
				if ((currentTime - data->drawStartTime) >= data->holdDuration)
				{
					if (PlayBowReleaseAnimation(rider, target))
					{
						data->state = BowAttackState::Released;
						data->stateEntryTime = currentTime;
					}
					else
					{
						// Release failed - go back to None and try again
						_MESSAGE("ArrowSystem: Rider %08X bow release failed - resetting", rider->formID);
						data->state = BowAttackState::None;
						data->bowEquipTime = currentTime;
						data->stateEntryTime = currentTime;
					}
				}
				return true;
			}
			
			case BowAttackState::Released:
			{
				data->state = BowAttackState::None;
				data->bowEquipTime = currentTime;
				data->stateEntryTime = currentTime;
				return false;
			}
			
			default:
				return false;
		}
	}
	
	// ============================================
	// INITIALIZATION AND CLEANUP
	// ============================================
	
	void InitArrowSystem()
	{
		if (g_arrowSystemInitialized) return;
		
		EnsureRandomSeeded();
		InitBowIdles();
		g_arrowSystemInitialized = true;
	}
	
	void ResetArrowSystemCache()
	{
		g_arrowSpell = nullptr;
		g_arrowSpellInitialized = false;
		
		g_bowAttackCharge = nullptr;
		g_bowAttackRelease = nullptr;
		g_bowIdlesInitialized = false;
		
		ClearPendingProjectileAims();
		
		g_riderBowCount = 0;
		for (int i = 0; i < 5; i++)
		{
			g_riderBowData[i].isValid = false;
			g_riderBowData[i].riderFormID = 0;
			g_riderBowData[i].arrowsEquippedThisSession = false;
		}
		
		for (int i = 0; i < 5; i++)
		{
			g_rapidFireBowData[i].isValid = false;
		}
		g_rapidFireBowCount = 0;
		
		g_arrowSystemInitialized = false;
	}
	
	// ============================================
	// RAPID FIRE BOW ATTACK SYSTEM
	// Clean state machine: Draw (1.2s) -> Release (instant arrow) -> repeat
	// Total ~1.5s per shot
	// ============================================
	
	static RapidFireBowData* GetOrCreateRapidFireBowData(UInt32 riderFormID)
	{
		// Check if already tracked
		for (int i = 0; i < g_rapidFireBowCount; i++)
		{
			if (g_rapidFireBowData[i].isValid && g_rapidFireBowData[i].riderFormID == riderFormID)
			{
				return &g_rapidFireBowData[i];
			}
		}
		
		// Create new entry
		if (g_rapidFireBowCount < MAX_RAPID_FIRE_RIDERS)
		{
			RapidFireBowData* data = &g_rapidFireBowData[g_rapidFireBowCount];
			data->riderFormID = riderFormID;
			data->state = RapidFireBowState::None;
			data->shotsFired = 0;
			data->maxShots = RapidFireShotCount;
			data->stateStartTime = 0;
			data->firedThisRelease = false;
			data->drewThisDraw = false;
			data->isValid = true;
			g_rapidFireBowCount++;
			return data;
		}
		
		return nullptr;
	}
	
	void StartRapidFireBowAttack(UInt32 riderFormID)
	{
		RapidFireBowData* data = GetOrCreateRapidFireBowData(riderFormID);
		if (!data) return;
		
		float currentTime = GetGameTimeSeconds();
		
		// Reset state for new rapid fire sequence
		data->state = RapidFireBowState::Drawing;
		data->shotsFired = 0;
		data->maxShots = RapidFireShotCount;
		data->stateStartTime = currentTime;
		data->firedThisRelease = false;
		data->drewThisDraw = false;
		
		_MESSAGE("ArrowSystem: === RAPID FIRE START === Rider %08X firing %d shots", riderFormID, RapidFireShotCount);
	}
	
	bool UpdateRapidFireBowAttack(Actor* rider, Actor* target)
	{
		if (!rider || !target) return false;
		
		RapidFireBowData* data = GetOrCreateRapidFireBowData(rider->formID);
		if (!data) return false;
		
		// Not in rapid fire mode
		if (data->state == RapidFireBowState::None || data->state == RapidFireBowState::Complete)
		{
			return false;
		}
		
		float currentTime = GetGameTimeSeconds();
		float timeInState = currentTime - data->stateStartTime;
		
		switch (data->state)
		{
			// ============================================
			// DRAWING STATE - Play draw animation, wait 1.2 seconds
			// ============================================
			case RapidFireBowState::Drawing:
			{
				// Play draw animation ONCE when entering this state
				if (!data->drewThisDraw)
				{
					data->drewThisDraw = true;
					
					_MESSAGE("ArrowSystem: RAPID FIRE [%d/%d] - Drawing bow for rider %08X", 
						data->shotsFired + 1, data->maxShots, rider->formID);
					
					// Play the bow draw animation
					if (!PlayBowDrawAnimation(rider))
					{
						_MESSAGE("ArrowSystem: RAPID FIRE - Draw animation failed, will retry");
						data->drewThisDraw = false;  // Retry next frame
					}
				}
				
				// After 1.2 seconds of drawing, transition to release
				if (timeInState >= RAPID_FIRE_DRAW_TIME)
				{
					data->state = RapidFireBowState::Releasing;
					data->stateStartTime = currentTime;
					data->firedThisRelease = false;
					
					_MESSAGE("ArrowSystem: RAPID FIRE [%d/%d] - Draw complete (%.2fs), releasing", 
						data->shotsFired + 1, data->maxShots, timeInState);
				}
				// FAILSAFE: If stuck for 5+ seconds, abort
				else if (timeInState >= 5.0f)
				{
					_MESSAGE("ArrowSystem: RAPID FIRE TIMEOUT - Rider %08X stuck in draw for %.1fs, aborting", 
						rider->formID, timeInState);
					data->state = RapidFireBowState::Complete;
				}
				
				return true;
			}
			
			// ============================================
			// RELEASING STATE - Play release animation + fire arrow IMMEDIATELY
			// ============================================
			case RapidFireBowState::Releasing:
			{
				// Fire arrow ONCE when entering this state
				if (!data->firedThisRelease)
				{
					data->firedThisRelease = true;
					data->shotsFired++;
					
					_MESSAGE("ArrowSystem: RAPID FIRE [%d/%d] - FIRING arrow for rider %08X", 
						data->shotsFired, data->maxShots, rider->formID);
					
					// Play release animation - this also schedules the arrow to fire
					bool animSuccess = PlayBowReleaseAnimation(rider, target);
					
					if (!animSuccess)
					{
						// Animation failed - fire arrow directly anyway
						_MESSAGE("ArrowSystem: RAPID FIRE - Release animation failed, firing arrow directly");
						FireArrowSpellAtTarget(rider, target);
					}
				}
				
				// Brief pause (0.3s) for release animation, then check if more shots needed
				if (timeInState >= RAPID_FIRE_RELEASE_TIME)
				{
					if (data->shotsFired < data->maxShots)
					{
						// More shots to fire - go back to drawing
						data->state = RapidFireBowState::Drawing;
						data->stateStartTime = currentTime;
						data->drewThisDraw = false;
						
						_MESSAGE("ArrowSystem: RAPID FIRE - Shot %d complete, starting next draw", data->shotsFired);
					}
					else
					{
						// All shots fired - complete
						data->state = RapidFireBowState::Complete;
						
						_MESSAGE("ArrowSystem: === RAPID FIRE COMPLETE === Rider %08X fired %d shots", 
							rider->formID, data->shotsFired);
					}
				}
				
				return true;
			}
			
			// ============================================
			// HOLDING STATE - Not used in rapid fire, skip immediately
			// ============================================
			case RapidFireBowState::Holding:
			case RapidFireBowState::BetweenShots:
			{
				// Skip these states - go straight to releasing
				data->state = RapidFireBowState::Releasing;
				data->stateStartTime = currentTime;
				data->firedThisRelease = false;
				return true;
			}
			
			// ============================================
			// COMPLETE STATE - Rapid fire finished
			// ============================================
			case RapidFireBowState::Complete:
			{
				return false;
			}
			
			default:
				return false;
		}
	}
	
	void ResetRapidFireBowAttack(UInt32 riderFormID)
	{
		for (int i = 0; i < g_rapidFireBowCount; i++)
		{
			if (g_rapidFireBowData[i].isValid && g_rapidFireBowData[i].riderFormID == riderFormID)
			{
				g_rapidFireBowData[i].state = RapidFireBowState::None;
				g_rapidFireBowData[i].shotsFired = 0;
				g_rapidFireBowData[i].stateStartTime = 0;
				g_rapidFireBowData[i].firedThisRelease = false;
				g_rapidFireBowData[i].drewThisDraw = false;
				_MESSAGE("ArrowSystem: Rapid fire reset for rider %08X", riderFormID);
				return;
			}
		}
	}
	
	bool IsRapidFireBowAttackActive(UInt32 riderFormID)
	{
		for (int i = 0; i < g_rapidFireBowCount; i++)
		{
			if (g_rapidFireBowData[i].isValid && g_rapidFireBowData[i].riderFormID == riderFormID)
			{
				return g_rapidFireBowData[i].state != RapidFireBowState::None && 
				       g_rapidFireBowData[i].state != RapidFireBowState::Complete;
			}
		}
		return false;
	}
	
	// ============================================
	// RESET ALL ARROW SYSTEM STATE
	// Call on game load/reload
	// ============================================
	
	void ResetArrowSystem()
	{
		_MESSAGE("ArrowSystem: === RESETTING ALL STATE ===");
		
		// Reset cached forms
		g_arrowSpell = nullptr;
		g_arrowSpellInitialized = false;
		
		g_bowAttackCharge = nullptr;
		g_bowAttackRelease = nullptr;
		g_bowIdlesInitialized = false;
		
		// Clear pending projectile aims and tracking
		ClearPendingProjectileAims();
		
		// Clear delayed arrow fires
		ClearDelayedArrowFires();
		
		// Reset regular bow attack data
		for (int i = 0; i < 5; i++)
		{
			g_riderBowData[i].isValid = false;
			g_riderBowData[i].riderFormID = 0;
			g_riderBowData[i].state = BowAttackState::None;
			g_riderBowData[i].bowEquipTime = 0;
			g_riderBowData[i].drawStartTime = 0;
			g_riderBowData[i].holdDuration = 0;
			g_riderBowData[i].stateEntryTime = 0;
			g_riderBowData[i].arrowsEquippedThisSession = false;
		}
		g_riderBowCount = 0;
		
		// Reset rapid fire bow attack data
		for (int i = 0; i < 5; i++)
		{
			g_rapidFireBowData[i].isValid = false;
			g_rapidFireBowData[i].riderFormID = 0;
			g_rapidFireBowData[i].state = RapidFireBowState::None;
			g_rapidFireBowData[i].shotsFired = 0;
			g_rapidFireBowData[i].stateStartTime = 0;
		}
		g_rapidFireBowCount = 0;
		
		// Reset initialization flags
		g_arrowSystemInitialized = false;
		// Note: Random seeding now handled by shared EnsureRandomSeeded() in Helper.h
	}
}
