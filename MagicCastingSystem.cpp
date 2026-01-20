#include "MagicCastingSystem.h"
#include "WeaponDetection.h"
#include "DynamicPackages.h"
#include "ArrowSystem.h"
#include "Helper.h"
#include "config.h"
#include "skse64/GameRTTI.h"
#include "skse64/GameData.h"
#include "skse64/GameObjects.h"
#include "skse64/GameReferences.h"
#include "skse64/GameThreads.h"
#include "skse64/PapyrusVM.h"
#include "skse64_common/Relocation.h"
#include "skse64_common/SafeWrite.h"
#include <cmath>
#include <cstdlib>
#include <mutex>
#include <vector>
#include <unordered_map>

namespace MountedNPCCombatVR
{
	// ============================================
	// EXTERNAL REFERENCES
	// ============================================
	extern SKSETaskInterface* g_task;
	
	// ============================================
	// SPELL CASTING NATIVE FUNCTION
	// RemoteCast - same signature as ArrowSystem
	// ============================================
	typedef bool (*_RemoteCast)(VMClassRegistry* registry, UInt32 stackId, SpellItem* spell, TESObjectREFR* akSource, Actor* blameActor, TESObjectREFR* akTarget);
	RelocAddr<_RemoteCast> MageRemoteCast(0x009BB7F0);
	
	// ============================================
	// PROJECTILE CLASS DEFINITION
	// Based on SpellAutoAimVR's reverse engineering
	// ============================================
	
	class MageProjectile : public TESObjectREFR
	{
	public:
		UInt8 pad98[0xF0 - 0x98];     // 098
		NiPoint3 unk0F0;   // 0F0 - point/origin
		NiPoint3 velocity;          // 0FC - velocity vector
		UInt8 pad108[0x120 - 0x108];     // 108
		UInt32 shooter;        // 120 - shooter handle/formID
		UInt32 desiredTarget;            // 124 - target handle
	};
	
	// ============================================
	// MISSILE PROJECTILE HOOK SYSTEM
	// For Fire-and-Forget spells (Firebolt, Fireball, Ice Spike)
	// ============================================
	
	// Pending spell projectile aim data
	struct PendingSpellAim
	{
		UInt32 shooterFormID;
		UInt32 targetFormID;
		NiPoint3 targetAimPos;
		float registeredTime;
	};
	
	// Spell projectile redirect tracking
	static std::vector<PendingSpellAim> g_pendingSpellAims;
	static std::unordered_map<UInt32, bool> g_redirectedSpellProjectiles;
	static std::mutex g_spellAimMutex;
	static bool g_missileHookInstalled = false;
	
	// Original function pointer
	typedef void (*_UpdateMissileProjectile)(MageProjectile* proj, float deltaTime);
	_UpdateMissileProjectile g_originalUpdateMissile = nullptr;
	
	// VTable offset from SpellAutoAimVR
	// MissileProjectile - for fire-and-forget spells (Firebolt, Fireball, Ice Spike)
	const uintptr_t MissileProjectileVtbl_Offset = 0x016FDEF8;
	const int MissileUpdateFunctionIndex = 0xAC;
	RelocPtr<_UpdateMissileProjectile> UpdateMissileProjectile_vtbl(MissileProjectileVtbl_Offset + MissileUpdateFunctionIndex * 8);
	
	// ============================================
	// SPELL PROJECTILE UPDATE HOOK - MISSILE
	// Redirects spell projectiles toward the registered target
	// ============================================
	
	void UpdateMissileProjectile_Hook(MageProjectile* proj, float deltaTime)
	{
		// Call original first
		if (g_originalUpdateMissile)
		{
			g_originalUpdateMissile(proj, deltaTime);
		}
		
		// Safety checks
		if (!proj) return;
		if (proj->formID == 0 || proj->formID == 0xFFFFFFFF) return;
		
		try
		{
			// Check if we've already redirected this projectile
			{
				std::lock_guard<std::mutex> lock(g_spellAimMutex);
				if (g_redirectedSpellProjectiles.find(proj->formID) != g_redirectedSpellProjectiles.end())
				{
					return;
				}
				
				if (g_pendingSpellAims.empty())
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
			
			std::lock_guard<std::mutex> lock(g_spellAimMutex);
			
			for (auto it = g_pendingSpellAims.begin(); it != g_pendingSpellAims.end(); )
			{
				float currentTime = (float)clock() / CLOCKS_PER_SEC;
				
				// Remove stale entries (older than 2 seconds)
				if (currentTime - it->registeredTime > 2.0f)
				{
					it = g_pendingSpellAims.erase(it);
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
					
					// Get current speed
					float speed = sqrt(proj->velocity.x * proj->velocity.x + 
									 proj->velocity.y * proj->velocity.y + 
									proj->velocity.z * proj->velocity.z);
					
					if (speed < 100.0f) speed = 2000.0f;  // Default spell speed
					
					float dirLen = sqrt(direction.x * direction.x + direction.y * direction.y + direction.z * direction.z);
					if (dirLen > 0.0001f)
					{
						// Normalize and set velocity to redirect projectile toward target
						proj->velocity.x = (direction.x / dirLen) * speed;
						proj->velocity.y = (direction.y / dirLen) * speed;
						proj->velocity.z = (direction.z / dirLen) * speed;
						
						_MESSAGE("MagicCastingSystem: Redirected MISSILE spell %08X from %08X", proj->formID, shooterFormID);
					}
					
					g_redirectedSpellProjectiles[proj->formID] = true;
					it = g_pendingSpellAims.erase(it);
					
					// Clean up old entries
					if (g_redirectedSpellProjectiles.size() > 100)
						g_redirectedSpellProjectiles.clear();
					
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
	// INSTALL MISSILE PROJECTILE HOOK
	// ============================================
	
	static void InstallMissileProjectileHook()
	{
		if (g_missileHookInstalled) return;
		
		g_originalUpdateMissile = *UpdateMissileProjectile_vtbl;
		SafeWrite64(UpdateMissileProjectile_vtbl.GetUIntPtr(), (uintptr_t)&UpdateMissileProjectile_Hook);
		
		g_missileHookInstalled = true;
		_MESSAGE("MagicCastingSystem: Missile projectile hook installed");
	}
	
	// ============================================
	// REGISTER SPELL PROJECTILE FOR REDIRECT
	// ============================================
	
	static void RegisterSpellProjectileForRedirect(UInt32 shooterFormID, UInt32 targetFormID, const NiPoint3& targetAimPos)
	{
		std::lock_guard<std::mutex> lock(g_spellAimMutex);
		
		PendingSpellAim aim;
		aim.shooterFormID = shooterFormID;
		aim.targetFormID = targetFormID;
		aim.targetAimPos = targetAimPos;
		aim.registeredTime = (float)clock() / CLOCKS_PER_SEC;
		
		g_pendingSpellAims.push_back(aim);
	}
	
	// ============================================
	// SPELL FORMIDS FROM Skyrim.esm
	// ============================================
	
	// Fire and Forget Spells (Skyrim.esm base game only) - for LONG RANGE (> 150 units)
	const UInt32 SPELL_FIREBOLT = 0x0012FCD0;     // Firebolt - fire projectile
	const UInt32 SPELL_FIREBALL = 0x0001C789;    // Fireball - fire AoE projectile
	const UInt32 SPELL_ICE_SPIKE = 0x0002B96C;   // Ice Spike - frost projectile
	
	// Concentration Spell (Skyrim.esm) - for CLOSE RANGE (<= 150 units)
	const UInt32 SPELL_FLAMES = 0x00012FCD;      // Flames - fire stream concentration
	
	// Array of fire-and-forget spell FormIDs for random selection
	static const UInt32 FIRE_AND_FORGET_SPELLS[] = {
		SPELL_FIREBOLT,
		SPELL_FIREBALL,
		SPELL_ICE_SPIKE
	};
	static const int FIRE_AND_FORGET_SPELL_COUNT = sizeof(FIRE_AND_FORGET_SPELLS) / sizeof(FIRE_AND_FORGET_SPELLS[0]);
	
	// ============================================
	// RANGE THRESHOLDS
	// ============================================
	const float MAGE_MELEE_RANGE_THRESHOLD = 299.0f;  // At this range or closer, mages use melee combat with staff
	const float MAGE_SPELL_MIN_RANGE = 300.0f;        // Minimum range for fire-and-forget spells
	const float MAGE_SPELL_MAX_RANGE = 1950.0f; // Maximum range for fire-and-forget spells
	const float MAGE_RETURN_TO_SPELL_RANGE = 400.0f;  // Must be beyond this range to return to spell casting (buffer zone)
	const float MAGE_MODE_SWITCH_COOLDOWN = 3.0f;     // Minimum time between mode switches
	
	// ============================================
	// MAGE COMBAT MODE TRACKING
	// MageCombatMode enum is defined in MagicCastingSystem.h
	// ============================================
	
	struct MageCombatModeData
	{
		UInt32 mageFormID;
		MageCombatMode currentMode;
		float lastModeSwitchTime;
		bool isValid;
		
		void Reset()
		{
			mageFormID = 0;
			currentMode = MageCombatMode::Spell;
			lastModeSwitchTime = 0;
			isValid = false;
		}
	};
	
	const int MAX_TRACKED_MAGE_MODES = 10;
	static MageCombatModeData g_mageCombatModes[MAX_TRACKED_MAGE_MODES];
	static int g_mageCombatModeCount = 0;
	
	static MageCombatModeData* GetOrCreateMageCombatModeData(UInt32 mageFormID)
	{
		// Find existing
		for (int i = 0; i < MAX_TRACKED_MAGE_MODES; i++)
		{
			if (g_mageCombatModes[i].isValid && g_mageCombatModes[i].mageFormID == mageFormID)
			{
				return &g_mageCombatModes[i];
			}
		}
		
		// Create new
		for (int i = 0; i < MAX_TRACKED_MAGE_MODES; i++)
		{
			if (!g_mageCombatModes[i].isValid)
			{
				g_mageCombatModes[i].Reset();
				g_mageCombatModes[i].mageFormID = mageFormID;
				g_mageCombatModes[i].isValid = true;
				g_mageCombatModeCount++;
				return &g_mageCombatModes[i];
			}
		}
		
		return nullptr;
	}

	// ============================================
	// SYSTEM STATE
	// ============================================
	
	static bool g_magicSystemInitialized = false;
	
	// Cached spell forms - Fire and Forget
	static SpellItem* g_cachedSpells[FIRE_AND_FORGET_SPELL_COUNT] = { nullptr };
	static bool g_spellsCached = false;
	
	// Cached concentration spell - Flames
	static SpellItem* g_cachedFlamesSpell = nullptr;
	static bool g_flamesSpellCached = false;
	
	// ============================================
	// MAGE SPELL CASTING STATE MACHINE
	// ============================================
	
	enum class MageSpellState
	{
		None = 0,
		Charging,        // Charging up fire-and-forget spell (2.5-3.5 seconds)
		Casting,     // Fire-and-forget spell being cast (brief)
		Cooldown // Cooldown before next spell
	};
	
	struct MageSpellCastData
	{
		UInt32 casterFormID;
		UInt32 targetFormID;
		MageSpellState state;
		float stateStartTime;
		float chargeDuration;
		int selectedSpellIndex;
		bool isValid;
		// Stationary tracking
		NiPoint3 lastPosition;
		float lastPositionCheckTime;
		bool wasStationary;
		// Last spell cast time - for enforcing minimum 3 second gap between ANY spells
		float lastSpellCastTime;
		
		void Reset()
		{
			casterFormID = 0;
			targetFormID = 0;
			state = MageSpellState::None;
			stateStartTime = 0;
			chargeDuration = 0;
			selectedSpellIndex = 0;
			isValid = false;
			lastPosition.x = 0;
			lastPosition.y = 0;
			lastPosition.z = 0;
			lastPositionCheckTime = 0;
			wasStationary = false;
			lastSpellCastTime = 0;
		}
	};
	
	const int MAX_TRACKED_MAGES = 5;
	static MageSpellCastData g_mageSpellData[MAX_TRACKED_MAGES];
	static int g_mageSpellCount = 0;
	
	// ============================================
	// CONCENTRATION SPELL SETTINGS
	// ============================================
	const float CONCENTRATION_BURST_MIN = 3.0f;   // Minimum burst duration (seconds)
	const float CONCENTRATION_BURST_MAX = 6.0f;   // Maximum burst duration (seconds)
	const float CONCENTRATION_RECAST_INTERVAL = 0.5f;  // How often to recast during burst
	
	// Minimum time between ANY spell casts (prevents rapid fire)
	const float MIN_SPELL_CAST_INTERVAL = 3.0f;  // 3 seconds minimum between spells
	
	// ============================================
	// SPELL CACHING
	// ============================================
	
	static void CacheSpells()
	{
		if (g_spellsCached) return;
		
		_MESSAGE("MagicCastingSystem: Caching fire-and-forget spells from Skyrim.esm...");
		
		for (int i = 0; i < FIRE_AND_FORGET_SPELL_COUNT; i++)
		{
			UInt32 spellFormID = FIRE_AND_FORGET_SPELLS[i];
			TESForm* form = LookupFormByID(spellFormID);
			if (form)
			{
				g_cachedSpells[i] = DYNAMIC_CAST(form, TESForm, SpellItem);
				if (g_cachedSpells[i])
				{
					const char* spellName = g_cachedSpells[i]->fullName.name.data;
					_MESSAGE("MagicCastingSystem: Cached spell %d '%s' (FormID: %08X)", 
						i, spellName ? spellName : "Unknown", spellFormID);
				}
			}
		}
		
		g_spellsCached = true;
	}
	
	static void CacheFlamesSpell()
	{
		if (g_flamesSpellCached) return;
		
		_MESSAGE("MagicCastingSystem: Caching Flames concentration spell...");
		
		TESForm* form = LookupFormByID(SPELL_FLAMES);
		if (form)
		{
			g_cachedFlamesSpell = DYNAMIC_CAST(form, TESForm, SpellItem);
			if (g_cachedFlamesSpell)
			{
				const char* spellName = g_cachedFlamesSpell->fullName.name.data;
				_MESSAGE("MagicCastingSystem: Cached Flames spell '%s' (FormID: %08X)", 
					spellName ? spellName : "Unknown", SPELL_FLAMES);
			}
		}
		
		g_flamesSpellCached = true;
	}
	
	static int GetRandomSpellIndex()
	{
		CacheSpells();
		
		int availableCount = 0;
		int availableIndices[FIRE_AND_FORGET_SPELL_COUNT];
		
		for (int i = 0; i < FIRE_AND_FORGET_SPELL_COUNT; i++)
		{
			if (g_cachedSpells[i] != nullptr)
			{
				availableIndices[availableCount] = i;
				availableCount++;
			}
		}
		
		if (availableCount == 0) return -1;
		
		EnsureRandomSeeded();
		int randomIndex = rand() % availableCount;
		return availableIndices[randomIndex];
	}
	
	// ============================================
	// SYSTEM INITIALIZATION
	// ============================================
	
	void InitMagicCastingSystem()
	{
		if (g_magicSystemInitialized) return;
		
		_MESSAGE("MagicCastingSystem: Initializing...");
		
		// Reset all tracking data
		for (int i = 0; i < MAX_TRACKED_MAGES; i++)
		{
			g_mageSpellData[i].Reset();
		}
		g_mageSpellCount = 0;
		
		g_magicSystemInitialized = true;
		_MESSAGE("MagicCastingSystem: Initialized (max %d mages, spell range %.0f-%.0f, melee <=%.0f)", 
			MAX_TRACKED_MAGES, MAGE_SPELL_MIN_RANGE, MAGE_SPELL_MAX_RANGE, MAGE_MELEE_RANGE_THRESHOLD);
	}
	
	void ShutdownMagicCastingSystem()
	{
		if (!g_magicSystemInitialized) return;
		
		_MESSAGE("MagicCastingSystem: Shutting down...");
		
		ResetMagicCastingSystem();
		g_magicSystemInitialized = false;
	}
	
	void ResetMagicCastingSystemCache()
	{
		_MESSAGE("MagicCastingSystem: Resetting cache...");
		
		// Reset cached spell forms - they become invalid after reload
		for (int i = 0; i < FIRE_AND_FORGET_SPELL_COUNT; i++)
		{
			g_cachedSpells[i] = nullptr;
		}
		g_spellsCached = false;
		
		g_cachedFlamesSpell = nullptr;
		g_flamesSpellCached = false;
	}
	
	void ResetMagicCastingSystem()
	{
		_MESSAGE("MagicCastingSystem: === RESETTING ALL STATE ===");
		
		// Reset cached spell forms
		ResetMagicCastingSystemCache();
		
		// Reset all mage spell data
		for (int i = 0; i < MAX_TRACKED_MAGES; i++)
		{
			g_mageSpellData[i].Reset();
		}
		g_mageSpellCount = 0;
		
		// Reset all mage combat mode data
		for (int i = 0; i < MAX_TRACKED_MAGE_MODES; i++)
		{
			g_mageCombatModes[i].Reset();
		}
		g_mageCombatModeCount = 0;
		
		// Reset all mage retreat data
		ResetAllMageRetreats();
		
		// Clear pending spell aims
		{
			std::lock_guard<std::mutex> lock(g_spellAimMutex);
			g_pendingSpellAims.clear();
			g_redirectedSpellProjectiles.clear();
		}
		
		_MESSAGE("MagicCastingSystem: All state reset complete");
	}
	
	// ============================================
	// TASK CLASS FOR CASTING SPELL
	// ============================================
	
	class TaskCastMageSpell : public TaskDelegate
	{
	public:
		UInt32 m_casterFormID;
		UInt32 m_targetFormID;
		UInt32 m_spellFormID;
		float m_targetX;
		float m_targetY;
		float m_targetZ;
		bool m_isConcentration;
		
		TaskCastMageSpell(UInt32 casterFormID, UInt32 targetFormID, UInt32 spellFormID, 
						  float aimX, float aimY, float aimZ, bool isConcentration = false)
			: m_casterFormID(casterFormID)
			, m_targetFormID(targetFormID)
			, m_spellFormID(spellFormID)
			, m_targetX(aimX)
			, m_targetY(aimY)
			, m_targetZ(aimZ)
			, m_isConcentration(isConcentration) {}
		
		virtual void Run() override
		{
			__try
			{
				if (m_casterFormID == 0 || m_targetFormID == 0 || m_spellFormID == 0)
					return;
				
				TESForm* casterForm = LookupFormByID(m_casterFormID);
				TESForm* targetForm = LookupFormByID(m_targetFormID);
				
				if (!casterForm || !targetForm) return;
				
				Actor* caster = DYNAMIC_CAST(casterForm, TESForm, Actor);
				Actor* target = DYNAMIC_CAST(targetForm, TESForm, Actor);
				
				if (!caster || !target) return;
				if (caster->IsDead(1) || target->IsDead(1)) return;
				
				TESForm* spellForm = LookupFormByID(m_spellFormID);
				if (!spellForm) return;
				
				SpellItem* spell = DYNAMIC_CAST(spellForm, TESForm, SpellItem);
				if (!spell) return;
				
				VMClassRegistry* registry = (*g_skyrimVM) ? (*g_skyrimVM)->GetClassRegistry() : nullptr;
				if (!registry) return;
				
				// Register projectile for redirection (only for fire-and-forget)
				if (!m_isConcentration)
				{
					NiPoint3 aimPos;
					aimPos.x = m_targetX;
					aimPos.y = m_targetY;
					aimPos.z = m_targetZ;
					RegisterSpellProjectileForRedirect(m_casterFormID, m_targetFormID, aimPos);
				}
				
				const char* casterName = CALL_MEMBER_FN(caster, GetReferenceName)();
				const char* spellName = spell->fullName.name.data;
				
				if (!m_isConcentration)
				{
					_MESSAGE("MagicCastingSystem: Casting '%s' from '%s' (%08X)",
						spellName ? spellName : "Unknown",
						casterName ? casterName : "Unknown", m_casterFormID);
				}
				
				MageRemoteCast(registry, 0, spell, caster, caster, target);
			}
			__except(EXCEPTION_EXECUTE_HANDLER)
			{
				_MESSAGE("MagicCastingSystem: TaskCastMageSpell - SEH exception caught");
			}
		}
		
		virtual void Dispose() override
		{
			delete this;
		}
	};
	
	// ============================================
	// FIRE SPELL AT TARGET (Fire and Forget)
	// ============================================
	
	static bool FireSpellAtTarget(Actor* caster, Actor* target, int spellIndex)
	{
		if (!caster || !target || !g_task) return false;
		
		if (spellIndex < 0 || spellIndex >= FIRE_AND_FORGET_SPELL_COUNT)
			return false;
		
		UInt32 spellFormID = FIRE_AND_FORGET_SPELLS[spellIndex];
		
		// Install missile projectile hook for fire-and-forget spells
		InstallMissileProjectileHook();
		
		// Calculate target aim position
		NiPoint3 targetPos = target->pos;
		float targetAimZ;
		
		// Adjust target height based on whether target is mounted
		NiPointer<Actor> targetMount;
		if (CALL_MEMBER_FN(target, GetMount)(targetMount) && targetMount)
			targetAimZ = targetPos.z + SpellTargetMountedHeight;
		else
			targetAimZ = targetPos.z + SpellTargetFootHeight;
		
		// Queue the spell cast on game thread
		g_task->AddTask(new TaskCastMageSpell(
			caster->formID, target->formID, spellFormID,
			targetPos.x, targetPos.y, targetAimZ, false
		));
		
		return true;
	}
	
	// ============================================
	// FIRE CONCENTRATION SPELL (Flames) AT TARGET
	// ============================================
	
	static bool FireConcentrationSpellAtTarget(Actor* caster, Actor* target)
	{
		if (!caster || !target || !g_task) return false;
		
		CacheFlamesSpell();
		if (!g_cachedFlamesSpell) return false;
		
		// Calculate target aim position
		NiPoint3 targetPos = target->pos;
		float targetAimZ;
		
		// Adjust target height based on whether target is mounted
		NiPointer<Actor> targetMount;
		if (CALL_MEMBER_FN(target, GetMount)(targetMount) && targetMount)
			targetAimZ = targetPos.z + SpellTargetMountedHeight;
		else
			targetAimZ = targetPos.z + SpellTargetFootHeight;
		
		// Queue the spell cast on game thread (concentration spell)
		g_task->AddTask(new TaskCastMageSpell(
			caster->formID, target->formID, SPELL_FLAMES,
			targetPos.x, targetPos.y, targetAimZ, true
		));
		
		return true;
	}
	
	// ============================================
	// GET OR CREATE MAGE SPELL DATA
	// ============================================
	
	static MageSpellCastData* GetOrCreateMageSpellData(UInt32 casterFormID)
	{
		// Find existing
		for (int i = 0; i < MAX_TRACKED_MAGES; i++)
		{
			if (g_mageSpellData[i].isValid && g_mageSpellData[i].casterFormID == casterFormID)
			{
				return &g_mageSpellData[i];
			}
		}
		
		// Create new
		for (int i = 0; i < MAX_TRACKED_MAGES; i++)
		{
			if (!g_mageSpellData[i].isValid)
			{
				g_mageSpellData[i].Reset();
				g_mageSpellData[i].casterFormID = casterFormID;
				g_mageSpellData[i].isValid = true;
				g_mageSpellCount++;
				return &g_mageSpellData[i];
			}
		}
		
		return nullptr;
	}
	
	// ============================================
	// MAIN SPELL CASTING UPDATE
	// ============================================
	
	// Stationary detection threshold
	const float STATIONARY_THRESHOLD = 10.0f;
	const float STATIONARY_CHECK_INTERVAL = 0.5f;
	
	bool UpdateMageSpellCasting(Actor* caster, Actor* target, float distanceToTarget)
	{
		if (!caster || !target) return false;
		
		// Initialize system if needed
		if (!g_magicSystemInitialized)
		{
			InitMagicCastingSystem();
		}
		
		// ============================================
		// SKIP NORMAL SPELL CASTING IF IN MAGE RAPID FIRE
		// Mage rapid fire handles its own Ice Spike casting
		// with faster timing (no 3-second cooldown)
		// ============================================
		if (IsMageRapidFireActive(caster->formID))
		{
			return false;  // Rapid fire handles spell casting
		}
		
		// ============================================
		// RANGE CHECK FOR SPELL CASTING
		// Only cast fire-and-forget spells between 300-1950 units
		// Below 300 units, mages use melee combat (handled elsewhere)
		// ============================================
		if (distanceToTarget < MAGE_SPELL_MIN_RANGE || distanceToTarget > MAGE_SPELL_MAX_RANGE)
		{
			// Reset state if we're out of spell range
			MageSpellCastData* data = GetOrCreateMageSpellData(caster->formID);
			if (data && data->state != MageSpellState::None)
			{
				data->state = MageSpellState::None;
			}
			return false;
		}
		
		MageSpellCastData* data = GetOrCreateMageSpellData(caster->formID);
		if (!data) return false;
		
		float currentTime = GetGameTime();
		
		// ============================================
		// STATIONARY DETECTION
		// ============================================
		NiPointer<Actor> mount;
		if (CALL_MEMBER_FN(caster, GetMount)(mount) && mount)
		{
			if (currentTime - data->lastPositionCheckTime >= STATIONARY_CHECK_INTERVAL)
			{
				NiPoint3 currentPos = mount->pos;
				float dx = currentPos.x - data->lastPosition.x;
				float dy = currentPos.y - data->lastPosition.y;
				float distMoved = sqrt(dx * dx + dy * dy);
				
				bool isStationary = (distMoved < STATIONARY_THRESHOLD);
				
				if (isStationary && !data->wasStationary)
				{
					const char* casterName = CALL_MEMBER_FN(caster, GetReferenceName)();
					_MESSAGE("MagicCastingSystem: MAGE '%s' (%08X) is now STATIONARY",
						casterName ? casterName : "Unknown", caster->formID);
				}
				else if (!isStationary && data->wasStationary)
				{
					const char* casterName = CALL_MEMBER_FN(caster, GetReferenceName)();
					_MESSAGE("MagicCastingSystem: MAGE '%s' (%08X) is now MOVING",
						casterName ? casterName : "Unknown", caster->formID);
				}
				
				data->wasStationary = isStationary;
				data->lastPosition = currentPos;
				data->lastPositionCheckTime = currentTime;
			}
		}
		
		switch (data->state)
		{
			case MageSpellState::None:
			{
				// Check 3 second minimum gap before starting ANY new spell
				if (data->lastSpellCastTime > 0)
				{
					float timeSinceLastCast = currentTime - data->lastSpellCastTime;
					if (timeSinceLastCast < MIN_SPELL_CAST_INTERVAL)
					{
						return false;
					}
				}
				
				// Start charging fire-and-forget spell
				data->targetFormID = target->formID;
				data->state = MageSpellState::Charging;
				data->stateStartTime = currentTime;
				
				EnsureRandomSeeded();
				float chargeRange = SpellChargeMaxTime - SpellChargeMinTime;
				data->chargeDuration = SpellChargeMinTime + (((float)(rand() % 100)) / 100.0f * chargeRange);
				data->selectedSpellIndex = GetRandomSpellIndex();
				
				const char* casterName = CALL_MEMBER_FN(caster, GetReferenceName)();
				_MESSAGE("MagicCastingSystem: Mage '%s' (%08X) CHARGING spell (%.1fs, dist: %.0f)",
					casterName ? casterName : "Unknown", caster->formID, data->chargeDuration, distanceToTarget);
				
				return true;
			}
			
			case MageSpellState::Charging:
			{
				float timeInState = currentTime - data->stateStartTime;
				
				if (timeInState >= data->chargeDuration)
				{
					TESForm* targetForm = LookupFormByID(data->targetFormID);
					if (!targetForm)
					{
						data->state = MageSpellState::None;
						return false;
					}
					
					Actor* currentTarget = DYNAMIC_CAST(targetForm, TESForm, Actor);
					if (!currentTarget || currentTarget->IsDead(1))
					{
						data->state = MageSpellState::None;
						return false;
					}
					
					if (FireSpellAtTarget(caster, currentTarget, data->selectedSpellIndex))
					{
						data->state = MageSpellState::Casting;
						data->stateStartTime = currentTime;
						data->lastSpellCastTime = currentTime;
						
						const char* casterName = CALL_MEMBER_FN(caster, GetReferenceName)();
						_MESSAGE("MagicCastingSystem: Mage '%s' (%08X) CAST spell %d",
							casterName ? casterName : "Unknown", caster->formID, data->selectedSpellIndex);
					}
					else
					{
						data->state = MageSpellState::None;
					}
				}
				
				return true;
			}
			
			case MageSpellState::Casting:
			{
				data->state = MageSpellState::Cooldown;
				data->stateStartTime = currentTime;
				return false;
			}
			
			case MageSpellState::Cooldown:
			{
				float timeInState = currentTime - data->stateStartTime;
				
				// Use 3 second minimum cooldown
				if (timeInState >= MIN_SPELL_CAST_INTERVAL)
				{
					data->state = MageSpellState::None;
				}
				
				return false;
			}
			
			default:
				return false;
		}
	}
	
	// ============================================
	// CHECK IF MAGE IS CURRENTLY CASTING
	// ============================================
	
	bool IsMageCharging(UInt32 casterFormID)
	{
		for (int i = 0; i < MAX_TRACKED_MAGES; i++)
		{
			if (g_mageSpellData[i].isValid && g_mageSpellData[i].casterFormID == casterFormID)
			{
				return g_mageSpellData[i].state == MageSpellState::Charging;
			}
		}
		return false;
	}
	
	// ============================================
	// CHECK IF MAGE IS IN MELEE MODE
	// Uses buffer zone and cooldown to prevent rapid toggling
	// - Switches TO melee when distance <= 299 units
	// - Switches BACK to spell when distance > 400 units AND cooldown elapsed
	// ============================================
	
	bool IsMageInMeleeRange(float distanceToTarget)
	{
		// Simple range check - used by DynamicPackages for attack decisions
		return distanceToTarget <= MAGE_MELEE_RANGE_THRESHOLD;
	}
	
	MageCombatMode UpdateMageCombatMode(UInt32 mageFormID, float distanceToTarget)
	{
		MageCombatModeData* data = GetOrCreateMageCombatModeData(mageFormID);
		if (!data) return MageCombatMode::Spell;
		
		float currentTime = GetGameTime();
		float timeSinceSwitch = currentTime - data->lastModeSwitchTime;
		
		switch (data->currentMode)
		{
			case MageCombatMode::Spell:
			{
				// Currently in spell mode - check if should switch to melee
				if (distanceToTarget <= MAGE_MELEE_RANGE_THRESHOLD)
				{
					// Switch to melee mode
					data->currentMode = MageCombatMode::Melee;
					data->lastModeSwitchTime = currentTime;
					
					_MESSAGE("MagicCastingSystem: Mage %08X switched to MELEE mode (dist: %.0f <= %.0f)",
						mageFormID, distanceToTarget, MAGE_MELEE_RANGE_THRESHOLD);
				}
				break;
			}
			
			case MageCombatMode::Melee:
			{
				// Currently in melee mode - check if should switch back to spell
				// Requires: beyond buffer zone AND cooldown elapsed
				if (distanceToTarget > MAGE_RETURN_TO_SPELL_RANGE && timeSinceSwitch >= MAGE_MODE_SWITCH_COOLDOWN)
				{
					// Switch back to spell mode
					data->currentMode = MageCombatMode::Spell;
					data->lastModeSwitchTime = currentTime;
					
					_MESSAGE("MagicCastingSystem: Mage %08X switched to SPELL mode (dist: %.0f > %.0f, cooldown: %.1fs)",
						mageFormID, distanceToTarget, MAGE_RETURN_TO_SPELL_RANGE, timeSinceSwitch);
				}
				break;
			}
		}
		
		return data->currentMode;
	}
	
	bool IsMageInMeleeMode(UInt32 mageFormID)
	{
		for (int i = 0; i < MAX_TRACKED_MAGE_MODES; i++)
		{
			if (g_mageCombatModes[i].isValid && g_mageCombatModes[i].mageFormID == mageFormID)
			{
				return g_mageCombatModes[i].currentMode == MageCombatMode::Melee;
			}
		}
		return false;// Default to spell mode if not tracked
	}
	
	void ResetMageCombatMode(UInt32 mageFormID)
	{
		for (int i = 0; i < MAX_TRACKED_MAGE_MODES; i++)
		{
			if (g_mageCombatModes[i].isValid && g_mageCombatModes[i].mageFormID == mageFormID)
			{
				g_mageCombatModes[i].Reset();
				g_mageCombatModeCount--;
				return;
			}
		}
	}
	
	// ============================================
	// MAGE TACTICAL RETREAT SYSTEM
	// ============================================
	// Every 15 seconds in combat, mages have a 25% chance to retreat
	// to a safe distance of 650-700 units before resuming combat.
	// ============================================
	
	const float MAGE_RETREAT_CHECK_INTERVAL = 15.0f;  // Check every 15 seconds
	const float MAGE_RETREAT_CHANCE = 0.25f;    // 25% chance to retreat
	const float MAGE_RETREAT_SAFE_DISTANCE_MIN = 650.0f;  // Minimum safe distance
	const float MAGE_RETREAT_SAFE_DISTANCE_MAX = 700.0f;  // Maximum safe distance
	
	struct MageRetreatData
	{
		UInt32 mageFormID;
		UInt32 horseFormID;
		UInt32 targetFormID;
		bool isRetreating;
		float retreatStartTime;
		float lastRetreatCheckTime;
		float safeDistance;  // Random between 650-700
		bool isValid;
		
		void Reset()
		{
			mageFormID = 0;
			horseFormID = 0;
			targetFormID = 0;
			isRetreating = false;
			retreatStartTime = 0;
			lastRetreatCheckTime = 0;
			safeDistance = 0;
			isValid = false;
		}
	};
	
	const int MAX_MAGE_RETREAT_TRACKED = 10;
	static MageRetreatData g_mageRetreatData[MAX_MAGE_RETREAT_TRACKED];
	static int g_mageRetreatCount = 0;
	
	static MageRetreatData* GetOrCreateMageRetreatData(UInt32 mageFormID)
	{
		// Find existing
		for (int i = 0; i < MAX_MAGE_RETREAT_TRACKED; i++)
		{
			if (g_mageRetreatData[i].isValid && g_mageRetreatData[i].mageFormID == mageFormID)
			{
				return &g_mageRetreatData[i];
			}
		}
		
		// Create new
		for (int i = 0; i < MAX_MAGE_RETREAT_TRACKED; i++)
		{
			if (!g_mageRetreatData[i].isValid)
			{
				g_mageRetreatData[i].Reset();
				g_mageRetreatData[i].mageFormID = mageFormID;
				g_mageRetreatData[i].isValid = true;
				g_mageRetreatCount++;
				return &g_mageRetreatData[i];
			}
		}
		
		return nullptr;
	}
	
	bool IsMageRetreating(UInt32 mageFormID)
	{
		for (int i = 0; i < MAX_MAGE_RETREAT_TRACKED; i++)
		{
			if (g_mageRetreatData[i].isValid && g_mageRetreatData[i].mageFormID == mageFormID)
			{
				return g_mageRetreatData[i].isRetreating;
			}
		}
		return false;
	}
	
	bool StartMageRetreat(Actor* mage, Actor* horse, Actor* target)
	{
		if (!mage || !horse || !target) return false;
		
		MageRetreatData* data = GetOrCreateMageRetreatData(mage->formID);
		if (!data) return false;
		
		// Already retreating?
		if (data->isRetreating) return false;
		
		float currentTime = GetGameTime();
		
		// Set random safe distance between 650-700
		EnsureRandomSeeded();
		data->safeDistance = MAGE_RETREAT_SAFE_DISTANCE_MIN + 
			(((float)(rand() % 100)) / 100.0f * (MAGE_RETREAT_SAFE_DISTANCE_MAX - MAGE_RETREAT_SAFE_DISTANCE_MIN));
		
		data->horseFormID = horse->formID;
		data->targetFormID = target->formID;
		data->isRetreating = true;
		data->retreatStartTime = currentTime;
		
		// Clear existing follow package
		Actor_ClearKeepOffsetFromActor(horse);
		ClearInjectedPackages(horse);
		
		// Create flee package
		TESPackage* fleePackage = CreatePackageByType(TESPackage::kPackageType_Flee);
		if (fleePackage)
		{
			fleePackage->packageFlags |= 6;
			
			PackageLocation packageLocation;
			PackageLocation_CTOR(&packageLocation);
			PackageLocation_SetNearReference(&packageLocation, target);
			TESPackage_SetPackageLocation(fleePackage, &packageLocation);
			
			PackageTarget packageTarget;
			PackageTarget_CTOR(&packageTarget);
			TESPackage_SetPackageTarget(fleePackage, &packageTarget);
			PackageTarget_ResetValueByTargetType((PackageTarget*)fleePackage->unk40, 0);
			PackageTarget_SetFromReference((PackageTarget*)fleePackage->unk40, target);
			
			TESPackage_sub_140439BE0(fleePackage, 0);
			
			ActorProcessManager* process = horse->processManager;
			if (process && process->unk18.package)
			{
				TESPackage_CopyFlagsFromOtherPackage(fleePackage, process->unk18.package);
			}
			
			get_vfunc<_Actor_PutCreatedPackage>(horse, 0xE1)(horse, fleePackage, true, 1);
		}
		
		Actor_EvaluatePackage(horse, false, false);
		
		const char* mageName = CALL_MEMBER_FN(mage, GetReferenceName)();
		_MESSAGE("MagicCastingSystem: ========================================");
		_MESSAGE("MagicCastingSystem: MAGE '%s' (%08X) TACTICAL RETREAT!",
			mageName ? mageName : "Unknown", mage->formID);
		_MESSAGE("MagicCastingSystem: Safe distance: %.0f units", data->safeDistance);
		_MESSAGE("MagicCastingSystem: ========================================");
		
		return true;
	}
	
	void StopMageRetreat(UInt32 mageFormID)
	{
		for (int i = 0; i < MAX_MAGE_RETREAT_TRACKED; i++)
		{
			if (g_mageRetreatData[i].isValid && g_mageRetreatData[i].mageFormID == mageFormID)
			{
				if (!g_mageRetreatData[i].isRetreating) return;
				
				g_mageRetreatData[i].isRetreating = false;
				
				// Restore mage follow package
				TESForm* mageForm = LookupFormByID(mageFormID);
				TESForm* horseForm = LookupFormByID(g_mageRetreatData[i].horseFormID);
				TESForm* targetForm = LookupFormByID(g_mageRetreatData[i].targetFormID);
				
				if (mageForm && horseForm && targetForm)
				{
					Actor* mage = DYNAMIC_CAST(mageForm, TESForm, Actor);
					Actor* horse = DYNAMIC_CAST(horseForm, TESForm, Actor);
					Actor* target = DYNAMIC_CAST(targetForm, TESForm, Actor);
					
					if (mage && horse && target && !mage->IsDead(1) && !horse->IsDead(1))
					{
						// Clear flee package
						Actor_ClearKeepOffsetFromActor(horse);
						ClearInjectedPackages(horse);
						
						// Re-apply mage follow package
						ForceHorseCombatWithTarget(horse, target);
						Actor_EvaluatePackage(horse, false, false);
						
						const char* mageName = CALL_MEMBER_FN(mage, GetReferenceName)();
						_MESSAGE("MagicCastingSystem: ========================================");
						_MESSAGE("MagicCastingSystem: MAGE '%s' (%08X) RETREAT COMPLETE",
							mageName ? mageName : "Unknown", mageFormID);
						_MESSAGE("MagicCastingSystem: Resuming combat!");
						_MESSAGE("MagicCastingSystem: ========================================");
					}
				}
				
				return;
			}
		}
	}
	
	bool CheckAndTriggerMageRetreat(Actor* mage, Actor* horse, Actor* target, float distanceToTarget)
	{
		if (!mage || !horse || !target) return false;
		
		MageRetreatData* data = GetOrCreateMageRetreatData(mage->formID);
		if (!data) return false;
		
		float currentTime = GetGameTime();
		
		// Already retreating - check if safe distance reached
		if (data->isRetreating)
		{
			if (distanceToTarget >= data->safeDistance)
			{
				// Safe distance reached - stop retreating
				StopMageRetreat(mage->formID);
				return false;
			}
			return true;  // Still retreating
		}
		
		// Check if enough time has passed since last check
		if ((currentTime - data->lastRetreatCheckTime) < MAGE_RETREAT_CHECK_INTERVAL)
		{
			return false;
		}
		data->lastRetreatCheckTime = currentTime;
		
		// Roll for retreat chance (25%)
		EnsureRandomSeeded();
		int roll = rand() % 100;
		if (roll >= (int)(MAGE_RETREAT_CHANCE * 100))
		{
			return false;  // Didn't roll retreat
		}
		
		// Start retreat!
		_MESSAGE("MagicCastingSystem: Mage %08X rolled %d < %d - TRIGGERING RETREAT!",
			mage->formID, roll, (int)(MAGE_RETREAT_CHANCE * 100));
		
		return StartMageRetreat(mage, horse, target);
	}
	
	void ResetMageRetreat(UInt32 mageFormID)
	{
		for (int i = 0; i < MAX_MAGE_RETREAT_TRACKED; i++)
		{
			if (g_mageRetreatData[i].isValid && g_mageRetreatData[i].mageFormID == mageFormID)
			{
				g_mageRetreatData[i].Reset();
				g_mageRetreatCount--;
				return;
			}
		}
	}
	
	void ResetAllMageRetreats()
	{
		for (int i = 0; i < MAX_MAGE_RETREAT_TRACKED; i++)
		{
			g_mageRetreatData[i].Reset();
		}
		g_mageRetreatCount = 0;
	}
	
	// Reset all spell casting state for a specific mage
	void ResetMageSpellState(UInt32 casterFormID)
	{
		for (int i =0; i < MAX_TRACKED_MAGES; i++)
		{
			if (g_mageSpellData[i].isValid && g_mageSpellData[i].casterFormID == casterFormID)
			{
				g_mageSpellData[i].Reset();
				g_mageSpellCount--;
				_MESSAGE("MagicCastingSystem: Reset spell state for mage %08X", casterFormID);
				return;
			}
		}
	}
}
