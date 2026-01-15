#include "CombatStyles.h"
#include "DynamicPackages.h"
#include "Helper.h"
#include "MountedCombat.h"
#include "MultiMountedCombat.h"
#include "SpecialMovesets.h"
#include "WeaponDetection.h"
#include "ArrowSystem.h"
#include "AILogging.h"
#include "NPCProtection.h"  // For AllowTemporaryStagger
#include "CompanionCombat.h"  // For IsCompanion
#include "MagicCastingSystem.h"  // For ResetMageSpellState
#include "FactionData.h"  // For IsActorHostileToActor
#include "config.h"  // For MountedAttackStagger settings
#include "skse64/GameData.h"
#include "skse64/GameReferences.h"
#include "skse64/GameForms.h"
#include "skse64/GameRTTI.h"
#include "skse64/GameInput.h"  // For g_leftHandedMode

namespace MountedNPCCombatVR
{
	// ============================================
	// Configuration
	// ============================================

	const float MELEE_ATTACK_RANGE = 200.0f;
	const float MELEE_CHARGE_RANGE = 512.0f;
	const float RANGED_MIN_RANGE = 333.0f;
	const float RANGED_MAX_RANGE = 2000.0f;
	const float FOLLOW_UPDATE_INTERVAL = 0.1f;  // Update every 100ms for smooth rotation
	const float TARGET_SWITCH_COOLDOWN = 10.0f;  // 10 seconds between target switches
	
	// ============================================
	// Attack Animation Configuration
	// FormIDs from MountedNPCCombat.esp (ESL flagged)
	// Base FormIDs in the plugin (without load order prefix)
	// ============================================
	
	const UInt32 IDLE_ATTACK_LEFT_BASE_FORMID = 0x0008E7;   // Left side attack (base ID in ESP)
	const UInt32 IDLE_ATTACK_RIGHT_BASE_FORMID = 0x0008E8;  // Right side attack (base ID in ESP)
	const char* ESP_NAME = "MountedNPCCombat.esp";
	
	// Power attack idles from Update.esm
	const UInt32 IDLE_POWER_ATTACK_LEFT_FORMID = 0x01000988;   // MountedCombatLeftPower
	const UInt32 IDLE_POWER_ATTACK_RIGHT_FORMID = 0x0100098A;  // MountedCombatRightPower
	
	const float ATTACK_COOLDOWN = 1.0f;  // Seconds between attacks - REDUCED for aggressive combat
	const int POWER_ATTACK_CHANCE = 10; // 10% chance for power attack
	
	// ============================================
	// System State
	// ============================================
	
	static bool g_combatStylesInitialized = false;
	static bool g_attackAnimsInitialized = false;
	
	// Attack animation forms (cached)
	static TESIdleForm* g_idleAttackLeft = nullptr;
	static TESIdleForm* g_idleAttackRight = nullptr;
	static TESIdleForm* g_idlePowerAttackLeft = nullptr;
	static TESIdleForm* g_idlePowerAttackRight = nullptr;
	
	// ============================================
	// Attack Animation Tracking
	// ============================================
	
	// RIDER:
	// Actor variable, formID is the rider's form ID
	// Track the attack state and timing for mounted riders (e.g., NPCs on horses)
	struct RiderAttackData
	{
		UInt32 riderFormID;
		RiderAttackState state;
		float lastAttackTime;     // Time when the last attack occurred
		float stateStartTime;    // Time when the current state was entered
		bool isValid;            // True if this data is valid and in use
	};
	
	// Global arrays to track active riders and their attack data
	static RiderAttackData g_riderAttackData[5];
	static int g_riderAttackCount = 0;
	
	// FOLLOWING NPCs:
	// Actors that are currently following or targeting something (e.g., companions, guards)
	struct FollowingNPCData
	{
		UInt32 actorFormID;
		UInt32 targetFormID;   // The target this NPC is following/attacking
		bool hasInjectedPackage;
		float lastFollowUpdateTime;
		float lastTargetSwitchTime;  // Time when target was last changed (for cooldown)
		int reinforceCount;
		bool isValid;
		bool inMeleeRange;          // True when horse is close enough for melee
		bool inAttackPosition;      // True when horse has turned sideways (90 deg)
	};
	
	static FollowingNPCData g_followingNPCs[5];
	static int g_followingNPCCount = 0;
	
	// Mount Tracking arrays (forward declaration for ResetCombatStylesCache)
	static UInt32 g_controlledMounts[5] = {0};
	static int g_controlledMountCount = 0;
	
	// Hit Detection arrays (forward declaration for ResetCombatStylesCache)
	struct MountedAttackHitData
	{
		UInt32 riderFormID;
		bool hitRegistered;
		bool isPowerAttack;
		float attackStartTime;
		bool isValid;
	};
	
	static MountedAttackHitData g_hitData[5];
	static int g_hitDataCount = 0;
	
	// Animation timing constants
	const float ATTACK_ANIMATION_WINDUP = 0.4f;   // Time before hit can register (animation wind-up)
	const float ATTACK_ANIMATION_WINDOW = 0.8f;   // Window during which hit can register (0.4 - 1.2 seconds)
	
	// ============================================
	// BLOCK STAGGER ANIMATION (for mounted riders)
	// Uses the dedicated mounted stagger animation from Update.esm
	// This looks proper for riders on horseback instead of generic stagger
	// ============================================
	
	static const UInt32 MOUNTED_STAGGER_IDLE_FORMID = 0x000D77F0;  // MountedCombatStagger from Update.esm
	static TESIdleForm* g_mountedStaggerIdle = nullptr;
	static bool g_mountedStaggerIdleInitialized = false;
	
	// ============================================
	// BLOOD IMPACT EFFECT (forward declaration for ResetCombatStylesCache)
	// Full implementation below with sound effects
	// ============================================
	
	static BGSImpactDataSet* g_bloodImpactDataSet = nullptr;
	static bool g_bloodImpactInitialized = false;
	
	// ============================================
	// Reset CombatStyles Cache
	// Called on game load/reload to clear stale pointers
	// ============================================
	
	void ResetCombatStylesCache()
	{
		_MESSAGE("CombatStyles: === RESETTING CACHE ===");
		
		// Reset cached animation forms (they become invalid after reload)
		g_idleAttackLeft = nullptr;
		g_idleAttackRight = nullptr;
		g_idlePowerAttackLeft = nullptr;
		g_idlePowerAttackRight = nullptr;
		g_attackAnimsInitialized = false;
		
		// Reset cached mounted stagger animation
		g_mountedStaggerIdle = nullptr;
		g_mountedStaggerIdleInitialized = false;
		
		// Reset blood impact effect cache
		g_bloodImpactDataSet = nullptr;
		g_bloodImpactInitialized = false;
		
		// Reset combat styles initialization flag
		g_combatStylesInitialized = false;
		
		// Clear all following NPC data
		for (int i = 0; i < 5; i++)
		{
			g_followingNPCs[i].isValid = false;
		}
		g_followingNPCCount = 0;
		
		// Clear all rider attack data
		for (int i = 0; i < 5; i++)
		{
			g_riderAttackData[i].isValid = false;
		}
		g_riderAttackCount = 0;
		
		// Clear all hit detection data
		for (int i = 0; i < 5; i++)
		{
			g_hitData[i].isValid = false;
		}
		g_hitDataCount = 0;
		
		// Clear controlled mounts
		for (int i = 0; i < 5; i++)
		{
			g_controlledMounts[i] = 0;
		}
		g_controlledMountCount = 0;
		
		_MESSAGE("CombatStyles: Cache reset complete");
	}
	
	// ============================================
	// Attack Animation Functions
	// ============================================
	
	// Use shared GetGameTime() from Helper.h instead of local function
	float GetAttackTimeSeconds()
	{
		return GetGameTime();
	}
	
	bool InitAttackAnimations()
	{
		if (g_attackAnimsInitialized) return true;
		
		_MESSAGE("CombatStyles: Initializing attack animations...");
		
		// Get the full FormIDs using the ESP lookup function
		UInt32 leftFormID = GetFullFormIdMine(ESP_NAME, IDLE_ATTACK_LEFT_BASE_FORMID);
		UInt32 rightFormID = GetFullFormIdMine(ESP_NAME, IDLE_ATTACK_RIGHT_BASE_FORMID);
		
		if (leftFormID != 0)
		{
			TESForm* leftForm = LookupFormByID(leftFormID);
			if (leftForm)
			{
				g_idleAttackLeft = DYNAMIC_CAST(leftForm, TESForm, TESIdleForm);
				if (!g_idleAttackLeft)
				{
					_MESSAGE("CombatStyles: ERROR - FormID %08X is not a TESIdleForm!", leftFormID);
				}
			}
			else
			{
				_MESSAGE("CombatStyles: ERROR - LookupFormByID failed for %08X", leftFormID);
			}
		}
		else
		{
			_MESSAGE("CombatStyles: ERROR - Could not resolve FormID for IDLE_ATTACK_LEFT from %s", ESP_NAME);
		}
		
		if (rightFormID != 0)
		{
			TESForm* rightForm = LookupFormByID(rightFormID);
			if (rightForm)
			{
				g_idleAttackRight = DYNAMIC_CAST(rightForm, TESForm, TESIdleForm);
				if (!g_idleAttackRight)
				{
					_MESSAGE("CombatStyles: ERROR - FormID %08X is not a TESIdleForm!", rightFormID);
				}
			}
			else
			{
				_MESSAGE("CombatStyles: ERROR - LookupFormByID failed for %08X", rightFormID);
			}
		}
		else
		{
			_MESSAGE("CombatStyles: ERROR - Could not resolve FormID for IDLE_ATTACK_RIGHT from %s", ESP_NAME);
		}
		
		// Load Power Attack Idles from Update.esm
		TESForm* powerLeftForm = LookupFormByID(IDLE_POWER_ATTACK_LEFT_FORMID);
		if (powerLeftForm)
		{
			g_idlePowerAttackLeft = DYNAMIC_CAST(powerLeftForm, TESForm, TESIdleForm);
			if (!g_idlePowerAttackLeft)
			{
				_MESSAGE("CombatStyles: ERROR - Power attack left FormID %08X is not a TESIdleForm!", IDLE_POWER_ATTACK_LEFT_FORMID);
			}
		}
		else
		{
			_MESSAGE("CombatStyles: ERROR - Could not find IDLE_POWER_ATTACK_LEFT (FormID: %08X)", IDLE_POWER_ATTACK_LEFT_FORMID);
		}
		
		TESForm* powerRightForm = LookupFormByID(IDLE_POWER_ATTACK_RIGHT_FORMID);
		if (powerRightForm)
		{
			g_idlePowerAttackRight = DYNAMIC_CAST(powerRightForm, TESForm, TESIdleForm);
			if (!g_idlePowerAttackRight)
			{
				_MESSAGE("CombatStyles: ERROR - Power attack right FormID %08X is not a TESIdleForm!", IDLE_POWER_ATTACK_RIGHT_FORMID);
			}
		}
		else
		{
			_MESSAGE("CombatStyles: ERROR - Could not find IDLE_POWER_ATTACK_RIGHT (FormID: %08X)", IDLE_POWER_ATTACK_RIGHT_FORMID);
		}
		
		g_attackAnimsInitialized = true;
		
		bool success = (g_idleAttackLeft != nullptr && g_idleAttackRight != nullptr);
		bool powerSuccess = (g_idlePowerAttackLeft != nullptr && g_idlePowerAttackRight != nullptr);
		_MESSAGE("CombatStyles: Attack animations - Regular: %s, Power: %s", 
			success ? "OK" : "FAILED",
			powerSuccess ? "OK" : "FAILED");
		
		return success;
	}
	
	RiderAttackData* GetOrCreateRiderAttackData(UInt32 riderFormID)
	{
		// Check if already tracked
		for (int i = 0; i < g_riderAttackCount; i++)
		{
			if (g_riderAttackData[i].isValid && g_riderAttackData[i].riderFormID == riderFormID)
			{
				return &g_riderAttackData[i];
			}
		}
		
		// Create new entry
		if (g_riderAttackCount < 5)
		{
			RiderAttackData* data = &g_riderAttackData[g_riderAttackCount];
			data->riderFormID = riderFormID;
			data->state = RiderAttackState::None;
			data->lastAttackTime = -ATTACK_COOLDOWN;  // Allow immediate first attack
			data->stateStartTime = 0;
			data->isValid = true;
			g_riderAttackCount++;
			return data;
		}
		
		return nullptr;
	}
	
	RiderAttackState GetRiderAttackState(Actor* rider)
	{
		if (!rider) return RiderAttackState::None;
		
		for (int i = 0; i < g_riderAttackCount; i++)
		{
			if (g_riderAttackData[i].isValid && g_riderAttackData[i].riderFormID == rider->formID)
			{
				return g_riderAttackData[i].state;
			}
		}
		
		return RiderAttackState::None;
	}
	
	bool IsRiderAttacking(Actor* rider)
	{
		RiderAttackState state = GetRiderAttackState(rider);
		return (state != RiderAttackState::None);
	}
	
	// ============================================
	// Animation Graph Notification (for mounted idles)
	// ============================================
	
	typedef bool (*_IAnimationGraphManagerHolder_NotifyAnimationGraph)(IAnimationGraphManagerHolder* _this, const BSFixedString& eventName);
	
	bool SendAnimationEvent(Actor* actor, const char* eventName)
	{
		if (!actor) return false;
		
		BSFixedString event(eventName);
		
		// Use vtable call to NotifyAnimationGraph (index 0x1 on IAnimationGraphManagerHolder)
		return get_vfunc<_IAnimationGraphManagerHolder_NotifyAnimationGraph>(&actor->animGraphHolder, 0x1)(&actor->animGraphHolder, event);
	}
	
	bool PlayMountedAttackAnimation(Actor* rider, const char* targetSide)
	{
		if (!rider) return false;
		
		// ============================================
		// SAFEGUARD: Only play melee attack if melee weapon is equipped
		// ============================================
		if (!IsMeleeEquipped(rider))
		{
			return false;
		}
		
		// Initialize animations if not done
		if (!g_attackAnimsInitialized)
		{
			InitAttackAnimations();
		}
		
		// Get or create attack data for this rider
		RiderAttackData* attackData = GetOrCreateRiderAttackData(rider->formID);
		if (!attackData) return false;
		
		// Check cooldown
		float currentTime = GetAttackTimeSeconds();
		float timeSinceLastAttack = currentTime - attackData->lastAttackTime;
		
		if (timeSinceLastAttack < ATTACK_COOLDOWN)
		{
			return false;  // Still on cooldown
		}
		
		// Reset attack state if cooldown has passed
		if (attackData->state != RiderAttackState::None && timeSinceLastAttack >= ATTACK_COOLDOWN)
		{
			attackData->state = RiderAttackState::None;
		}
		
		if (attackData->state != RiderAttackState::None)
		{
			return false;
		}
		
		// Determine if this should be a power attack (10% chance)
		bool isPowerAttack = (rand() % 100) < POWER_ATTACK_CHANCE;
		
		// Determine which idle to use
		TESIdleForm* idleToPlay = nullptr;
		const char* animName = "";
		const char* attackType = "";
		
		if (strcmp(targetSide, "LEFT") == 0)
		{
			if (isPowerAttack && g_idlePowerAttackLeft)
			{
				idleToPlay = g_idlePowerAttackLeft;
				animName = "LEFT";
				attackType = "POWER";
			}
			else if (g_idleAttackLeft)
			{
				idleToPlay = g_idleAttackLeft;
				animName = "LEFT";
				attackType = "normal";
			}
			else if (g_idlePowerAttackLeft)
			{
				// Fallback: use power attack if normal attack unavailable
				idleToPlay = g_idlePowerAttackLeft;
				animName = "LEFT";
				attackType = "POWER (fallback)";
				isPowerAttack = true;  // Treat as power attack for damage
			}
		}
		else if (strcmp(targetSide, "RIGHT") == 0)
		{
			if (isPowerAttack && g_idlePowerAttackRight)
			{
				idleToPlay = g_idlePowerAttackRight;
				animName = "RIGHT";
				attackType = "POWER";
			}
			else if (g_idleAttackRight)
			{
				idleToPlay = g_idleAttackRight;
				animName = "RIGHT";
				attackType = "normal";
			}
			else if (g_idlePowerAttackRight)
			{
				// Fallback: use power attack if normal attack unavailable
				idleToPlay = g_idlePowerAttackRight;
				animName = "RIGHT";
				attackType = "POWER (fallback)";
				isPowerAttack = true;  // Treat as power attack for damage
			}
		}
		else
		{
			return false;
		}
		
		if (!idleToPlay)
		{
			return false;
		}
		
		const char* animEventName = idleToPlay->animationEvent.c_str();
		
		if (!animEventName || strlen(animEventName) == 0)
		{
			return false;
		}
		
		if (!rider->GetNiNode())
		{
			attackData->lastAttackTime = currentTime;
			return false;
		}
		
		if (!rider->processManager)
		{
			attackData->lastAttackTime = currentTime;
			return false;
		}
		
		NiPointer<Actor> mount;
		if (!CALL_MEMBER_FN(rider, GetMount)(mount) || !mount)
		{
			attackData->lastAttackTime = currentTime;
			return false;
		}
		
		bool result = SendAnimationEvent(rider, animEventName);
		
		if (result)
		{
			attackData->state = RiderAttackState::WindingUp;
			attackData->stateStartTime = currentTime;
			attackData->lastAttackTime = currentTime;
			
			ResetHitData(rider->formID);
			SetHitDataPowerAttack(rider->formID, isPowerAttack);
			
			// Only log successful attacks (removed rejected animation logs)
			_MESSAGE("CombatStyles: Rider %08X %s %s attack", rider->formID, attackType, animName);
		}
		// Removed the "animation rejected" log - too spammy
		
		return result;
	}
	
	// ============================================
	// Mount Tracking (for cleanup purposes only)
	// Note: g_controlledMounts and g_controlledMountCount defined earlier
	// ============================================
	
	void ReleaseAllMountControl()
	{
		g_controlledMountCount = 0;
		for (int i = 0; i < 5; i++) g_controlledMounts[i] = 0;
	}

	// ============================================
	// Follow Target Tracking
	// Note: FollowingNPCData struct and g_followingNPCs defined earlier
	// ============================================
	
	int FindFollowingNPCSlot(UInt32 formID)
	{
		for (int i = 0; i < g_followingNPCCount; i++)
		{
			if (g_followingNPCs[i].isValid && g_followingNPCs[i].actorFormID == formID)
				return i;
		}
		return -1;
	}
	
	bool IsNPCFollowingTarget(Actor* actor)
	{
		if (!actor) return false;
		return FindFollowingNPCSlot(actor->formID) >= 0;
	}
	
	void SetNPCFollowTarget(Actor* actor, Actor* target)
	{
		if (!actor) return;
		
		// Default to player if no target specified
		if (!target)
		{
			if (!g_thePlayer || !(*g_thePlayer)) return;
			target = *g_thePlayer;
		}
		
		// ============================================
		// CRITICAL: Validate both actors before proceeding
		// Prevents CTD when actors are in invalid state
		// ============================================
		if (!actor->loadedState || !actor->GetNiNode())
		{
			_MESSAGE("CombatStyles: SetNPCFollowTarget - actor %08X invalid state, skipping", actor->formID);
			return;
		}
		
		if (!target->loadedState || !target->GetNiNode())
		{
			_MESSAGE("CombatStyles: SetNPCFollowTarget - target %08X invalid state, skipping", target->formID);
			return;
		}
		
		// Validate target is actually an Actor (formType check)
		if (target->formType != kFormType_Character)
		{
			_MESSAGE("CombatStyles: SetNPCFollowTarget - target %08X is not an Actor (type: %d), skipping", 
				target->formID, target->formType);
			return;
		}
		
		// ============================================
		// EARLY DISTANCE CHECK - Don't set up follow if too far
		// This prevents adding NPCs to tracking that will immediately be removed
		// ============================================
		float dx = target->pos.x - actor->pos.x;
		float dy = target->pos.y - actor->pos.y;
		float distanceToTarget = sqrt(dx * dx + dy * dy);
		
		bool isCompanion = IsCompanion(actor);
		float maxDistance = isCompanion ? MaxCompanionCombatDistance : MaxCombatDistance;
		
		if (distanceToTarget > maxDistance)
		{
			const char* actorName = CALL_MEMBER_FN(actor, GetReferenceName)();
			_MESSAGE("CombatStyles: SetNPCFollowTarget - '%s' (%08X) target too far (%.0f > %.0f), skipping",
				actorName ? actorName : "Unknown", actor->formID, distanceToTarget, maxDistance);
			return;
		}
		
		const char* actorName = CALL_MEMBER_FN(actor, GetReferenceName)();
		const char* targetName = CALL_MEMBER_FN(target, GetReferenceName)();
		
		// Check if already tracked
		int existingSlot = FindFollowingNPCSlot(actor->formID);
		if (existingSlot >= 0)
		{
			// Already tracked - just re-inject the package (no log needed)
			InjectFollowPackage(actor, target);
			g_followingNPCs[existingSlot].lastFollowUpdateTime = GetCurrentGameTime();
			return;
		}
		
		// Only log new follow setups
		_MESSAGE("CombatStyles: Setting up follow - '%s' -> '%s'", 
			actorName ? actorName : "Unknown",
			targetName ? targetName : "Unknown");
		
		// If this is the first NPC to start combat, notify the combat system
		if (g_followingNPCCount == 0)
		{
			NotifyCombatStarted();
		}
		
		// Initialize dynamic package system if needed
		if (!g_combatStylesInitialized)
		{
			InitDynamicPackageSystem();
			g_combatStylesInitialized = true;
		}
		
		// ============================================
		// INITIAL WEAPON EQUIP - USE CENTRALIZED SYSTEM
		// Use the distance already calculated above
		// MAGES: Skip - they keep warstaff equipped, no distance-based switching
		// ============================================
		if (!IsRiderMage(actor->formID))
		{
			RequestWeaponForDistance(actor, distanceToTarget, false);
		}
		
		// Ensure attack flags are set
		actor->flags2 |= Actor::kFlag_kAttackOnSight;
		
		InjectFollowPackage(actor, target);
		
		// Add to tracking list
		if (g_followingNPCCount < 5)
		{
			g_followingNPCs[g_followingNPCCount].actorFormID = actor->formID;
			g_followingNPCs[g_followingNPCCount].targetFormID = target->formID;
			g_followingNPCs[g_followingNPCCount].hasInjectedPackage = true;
			g_followingNPCs[g_followingNPCCount].lastFollowUpdateTime = GetCurrentGameTime();
			g_followingNPCs[g_followingNPCCount].lastTargetSwitchTime = GetCurrentGameTime();
			g_followingNPCs[g_followingNPCCount].reinforceCount = 0;
			g_followingNPCs[g_followingNPCCount].isValid = true;
			g_followingNPCs[g_followingNPCCount].inMeleeRange = false;
			g_followingNPCs[g_followingNPCCount].inAttackPosition = false;
			g_followingNPCCount++;
		}
	}
	
	void ClearNPCFollowTarget(Actor* actor)
	{
		if (!actor) return;
		
		int slot = FindFollowingNPCSlot(actor->formID);
		if (slot >= 0)
		{
			const char* actorName = CALL_MEMBER_FN(actor, GetReferenceName)();
			_MESSAGE("CombatStyles: Clearing follow for '%s'", actorName ? actorName : "Unknown");
			
			ClearInjectedPackages(actor);
			actor->flags2 &= ~Actor::kFlag_kAttackOnSight;
			
			// Clear bow attack states
			ResetBowAttackState(actor->formID);
			ResetRapidFireBowAttack(actor->formID);
			
			// Clear mage spell states
			ResetMageSpellState(actor->formID);
			
			NiPointer<Actor> mount;
			if (CALL_MEMBER_FN(actor, GetMount)(mount) && mount)
			{
				ClearInjectedPackages(mount.get());
				Actor_ClearKeepOffsetFromActor(mount.get());
				ClearAllMovesetData(mount->formID);
				mount->currentCombatTarget = 0;
				mount->flags2 &= ~Actor::kFlag_kAttackOnSight;
			}
			
			// Remove from tracking
			for (int i = slot; i < g_followingNPCCount - 1; i++)
				g_followingNPCs[i] = g_followingNPCs[i + 1];
			g_followingNPCCount--;
		}
	}
	
	void ClearAllFollowingNPCs()
	{
		_MESSAGE("CombatStyles: Clearing all %d following NPCs...", g_followingNPCCount);
		
		for (int i = 0; i < g_followingNPCCount; i++)
		{
			if (g_followingNPCs[i].isValid)
			{
				UInt32 actorFormID = g_followingNPCs[i].actorFormID;
				
				TESForm* form = LookupFormByID(actorFormID);
				if (form && form->formType == kFormType_Character)
				{
					Actor* actor = static_cast<Actor*>(form);
					ClearInjectedPackages(actor);
					actor->flags2 &= ~Actor::kFlag_kAttackOnSight;
				}
				
				// Clear bow and mage states (even if actor lookup failed)
				ResetBowAttackState(actorFormID);
				ResetRapidFireBowAttack(actorFormID);
				ResetMageSpellState(actorFormID);
			}
			g_followingNPCs[i].isValid = false;
		}
		
		g_followingNPCCount = 0;
		_MESSAGE("CombatStyles: All tracking cleared");
	}
	
	// ============================================
	// Continuous Follow Update
	// Re-injects travel package periodically to update destination
	// Stops following if target gets too far away or rider exits combat
	// ============================================
	
	void UpdateFollowBehavior()
	{
		// ============================================
		// CRITICAL: EARLY EXIT IF PLAYER IS DEAD
		// Prevents processing actors when game state is invalid
		// This is the fix for CTD when player dies in mounted combat
		// ============================================
		if (g_thePlayer && (*g_thePlayer) && (*g_thePlayer)->IsDead(1))
		{
			// Player is dead - clear all tracking and exit immediately
			// Do NOT call any actor functions - just invalidate entries
			for (int i = 0; i < 5; i++)
			{
				g_followingNPCs[i].isValid = false;
			}
			g_followingNPCCount = 0;
			return;
		}
		
		// ============================================
		// CRITICAL: EARLY EXIT IF MOD IS NOT ACTIVE
		// Prevents processing during game transitions
		// ============================================
		if (!g_modActive)
		{
			return;
		}
		
		float currentTime = GetCurrentGameTime();
		
		for (int i = g_followingNPCCount - 1; i >= 0; i--)
		{
			if (!g_followingNPCs[i].isValid) continue;
			
			// Check if it's time to update
			if ((currentTime - g_followingNPCs[i].lastFollowUpdateTime) < FOLLOW_UPDATE_INTERVAL)
			{
				continue;
			}
			
			TESForm* form = LookupFormByID(g_followingNPCs[i].actorFormID);
			if (!form || form->formType != kFormType_Character)
			{
				g_followingNPCs[i].isValid = false;
				continue;
			}
			
			Actor* actor = static_cast<Actor*>(form);
			
			// ============================================
			// CRITICAL: Validate actor is fully loaded
			// Prevents crashes from accessing unloaded actors
			// ============================================
			if (!actor->loadedState || !actor->GetNiNode())
			{
				g_followingNPCs[i].isValid = false;
				continue;
			}
			
			// Safety check - verify actor has process manager
			if (!actor->processManager)
			{
				_MESSAGE("CombatStyles: NPC %08X has no process manager - removing from tracking", actor->formID);
				g_followingNPCs[i].isValid = false;
				continue;
			}
			
			// Check if still alive
			if (actor->IsDead(1))
			{
				g_followingNPCs[i].isValid = false;
				continue;
			}
			
			// Check if still mounted
			NiPointer<Actor> mount;
			if (!CALL_MEMBER_FN(actor, GetMount)(mount) || !mount)
			{
				g_followingNPCs[i].isValid = false;
				continue;
			}
			
			// Safety check - verify mount has process manager
			if (!mount->processManager)
			{
				_MESSAGE("CombatStyles: Mount %08X has no process manager - removing NPC %08X from tracking", 
					mount->formID, actor->formID);
				g_followingNPCs[i].isValid = false;
				continue;
			}
			
			// ============================================
			// CHECK IF RIDER EXITED COMBAT STATE
			// BUT: Don't clear if player is still in combat and NPC is close
			// This prevents riders from disengaging when their target dies
			// ============================================
			if (!actor->IsInCombat())
			{
				// Check if player is still in combat and NPC is within range
				bool playerInCombat = (g_thePlayer && (*g_thePlayer) && (*g_thePlayer)->IsInCombat());
				
				float distToPlayer = 9999.0f;
				if (g_thePlayer && (*g_thePlayer))
				{
					float dx = (*g_thePlayer)->pos.x - actor->pos.x;
					float dy = (*g_thePlayer)->pos.y - actor->pos.y;
					distToPlayer = sqrt(dx * dx + dy * dy);
				}
				
				// If player is in combat AND NPC is within 1500 units, re-engage with player
				const float REENGAGE_DISTANCE = 1500.0f;
				if (playerInCombat && distToPlayer < REENGAGE_DISTANCE)
				{
					// Only re-engage if NPC is actually hostile to player
					Actor* player = *g_thePlayer;
					bool isHostileToPlayer = IsActorHostileToActor(actor, player);
					
					if (isHostileToPlayer)
					{
						const char* actorName = CALL_MEMBER_FN(actor, GetReferenceName)();
						_MESSAGE("CombatStyles: Rider '%s' (%08X) lost combat state but hostile to player (dist: %.0f) - RE-ENGAGING",
						(actorName ? actorName : "Unknown"), actor->formID, distToPlayer);
						
						// Force re-engage with player
						g_followingNPCs[i].targetFormID = player->formID;
						g_followingNPCs[i].lastTargetSwitchTime = currentTime;
						
						// CRITICAL: Clear weapon switch data to allow immediate re-equip
						ClearWeaponStateData(actor->formID);
						
						// Set combat flags
						actor->flags2 |= Actor::kFlag_kAttackOnSight;
						
						// Update the NPC's combat target to the player
						UInt32 playerHandle = player->CreateRefHandle();
						if (playerHandle != 0 && playerHandle != *g_invalidRefHandle)
						{
							actor->currentCombatTarget = playerHandle;
						}
						
						// Continue processing this NPC with player as target
						// Don't clear follow
					}
					else
					{
						// Not hostile to player - just clear tracking
						const char* actorName = CALL_MEMBER_FN(actor, GetReferenceName)();
						_MESSAGE("CombatStyles: Rider '%s' (%08X) exited combat, not hostile to player - clearing",
							actorName ? actorName : "Unknown", actor->formID);
						ClearNPCFollowTarget(actor);
						continue;
					}
				}
				else
				{
					// Player not in combat or NPC too far - actually clear
					const char* actorName = CALL_MEMBER_FN(actor, GetReferenceName)();
					_MESSAGE("CombatStyles: Rider '%s' (%08X) exited combat - clearing follow",
						actorName ? actorName : "Unknown", actor->formID);
					
					// CRITICAL: Clear weapon state on combat exit
					ClearWeaponStateData(actor->formID);
					
					ClearNPCFollowTarget(actor);
					continue;
				}
			}
			
			// ============================================
			// GET THE TARGET FOR THIS NPC
			// First check the actor's ACTUAL combat target from the game
			// Then fall back to stored target
			// ============================================
			Actor* target = nullptr;
			UInt32 storedTargetFormID = g_followingNPCs[i].targetFormID;
			
			// ============================================
			// PRIORITY 1: Check the actor's actual combat target
			// This allows the game AI to switch targets naturally
			// BUT enforce a 5-second cooldown between switches
			// ============================================
			UInt32 combatTargetHandle = actor->currentCombatTarget;
			if (combatTargetHandle != 0)
			{
				NiPointer<TESObjectREFR> targetRef;
				LookupREFRByHandle(combatTargetHandle, targetRef);
				if (targetRef && targetRef->formType == kFormType_Character)
				{
					Actor* combatTarget = static_cast<Actor*>(targetRef.get());
					if (combatTarget && !combatTarget->IsDead(1))
					{
						// Check if this is a new target (different from stored)
						if (combatTarget->formID != storedTargetFormID && storedTargetFormID != 0)
						{
							// New target - check cooldown
							float timeSinceLastSwitch = currentTime - g_followingNPCs[i].lastTargetSwitchTime;
							
							if (timeSinceLastSwitch < TARGET_SWITCH_COOLDOWN)
							{
								// Still on cooldown - keep current target, ignore the new one
								// Fall through to use stored target
								_MESSAGE("CombatStyles: NPC %08X target switch BLOCKED (%.1fs remaining on cooldown)",
									actor->formID, TARGET_SWITCH_COOLDOWN - timeSinceLastSwitch);
							}
							else
							{
								// Cooldown passed - allow target switch
								target = combatTarget;
								g_followingNPCs[i].targetFormID = combatTarget->formID;
								g_followingNPCs[i].lastTargetSwitchTime = currentTime;
								

								// ============================================
								// CLEAR WEAPON SWITCH DATA ON TARGET CHANGE
								// This allows immediate weapon equip for new target
								// ============================================
								ClearWeaponSwitchData(actor->formID);
								
								// Force weapon draw
								if (!IsWeaponDrawn(actor))
								{
									actor->DrawSheatheWeapon(true);
								}
								

								const char* actorName = CALL_MEMBER_FN(actor, GetReferenceName)();
								const char* targetName = CALL_MEMBER_FN(combatTarget, GetReferenceName)();
								_MESSAGE("CombatStyles: NPC '%s' (%08X) SWITCHED TARGET to '%s' (%08X) - weapon switch reset",
									actorName ? actorName : "Unknown", actor->formID,
									targetName ? targetName : "Unknown", combatTarget->formID);
							}
						}
						else
						{
							// Same target or first target - no cooldown needed
							target = combatTarget;
							
							// Update stored target if it was empty
							if (storedTargetFormID == 0)
							{
								g_followingNPCs[i].targetFormID = combatTarget->formID;
								g_followingNPCs[i].lastTargetSwitchTime = currentTime;
							}
						}
					}
				}
			}
			
			// ============================================
			// PRIORITY 2: Fall back to stored target
			// ============================================
			if (!target && storedTargetFormID != 0)
			{
				TESForm* targetForm = LookupFormByID(storedTargetFormID);
				if (targetForm && targetForm->formType == kFormType_Character)
				{
					target = static_cast<Actor*>(targetForm);
					
					// ============================================
					// CRITICAL: Validate target has valid state before using
					// This prevents CTD when target is in invalid/transitional state
					// ============================================
					if (!target->loadedState || !target->GetNiNode())
					{
						_MESSAGE("CombatStyles: Target %08X has invalid state - skipping", target->formID);
						target = nullptr;
						g_followingNPCs[i].targetFormID = 0;
						ClearWeaponStateData(actor->formID);
					}
					else if (target->IsDead(1))
					{
						const char* actorName = CALL_MEMBER_FN(actor, GetReferenceName)();
						_MESSAGE("CombatStyles: Target died - NPC '%s' checking for new target",
							actorName ? actorName : "Unknown");
						target = nullptr;
						g_followingNPCs[i].targetFormID = 0;
						
						// CRITICAL: Clear weapon state when target dies - allows fresh equip
						ClearWeaponStateData(actor->formID);
						
						// Check if NPC is actually hostile to player before switching to them
						if (g_thePlayer && (*g_thePlayer) && !(*g_thePlayer)->IsDead(1))
						{
							Actor* player = *g_thePlayer;
							
							// Only switch to player if NPC is hostile to player
							bool isHostileToPlayer = IsActorHostileToActor(actor, player);
							
							if (isHostileToPlayer)
							{
								target = player;
								g_followingNPCs[i].targetFormID = target->formID;
								g_followingNPCs[i].lastTargetSwitchTime = currentTime;
								
								// Update the NPC's actual combat target
								UInt32 playerHandle = target->CreateRefHandle();
								if (playerHandle != 0 && playerHandle != *g_invalidRefHandle)
								{
									actor->currentCombatTarget = playerHandle;
								}
								
								// Ensure attack flags are set
								actor->flags2 |= Actor::kFlag_kAttackOnSight;
								
								_MESSAGE("CombatStyles: NPC '%s' now targeting PLAYER after target death (was hostile)",
									actorName ? actorName : "Unknown");
							}
							else
							{
								// Not hostile to player - just clear tracking, let game AI handle
								_MESSAGE("CombatStyles: NPC '%s' target died but not hostile to player - clearing tracking",
									actorName ? actorName : "Unknown");
								ClearNPCFollowTarget(actor);
								continue;
							}
						}
					}
				}
				else
				{
					// Target form invalid - clear it
					g_followingNPCs[i].targetFormID = 0;
					
					// CRITICAL: Clear weapon state when target becomes invalid
					ClearWeaponStateData(actor->formID);
				}
			}
			
			// ============================================
			// PRIORITY 3: Default to player if no target
			// Only if NPC is actually hostile to player!
			// ============================================
			if (!target)
			{
				if (g_thePlayer && (*g_thePlayer))
				{
					Actor* player = *g_thePlayer;
					
					// Only target player if NPC is hostile to them
					if (IsActorHostileToActor(actor, player))
					{
						target = player;
						g_followingNPCs[i].targetFormID = target->formID;
					}
					else
					{
						// Not hostile to player - clear tracking
						_MESSAGE("CombatStyles: NPC %08X has no target and not hostile to player - clearing", actor->formID);
						ClearNPCFollowTarget(actor);
						continue;
					}
				}
				else
				{
					continue;
				}
			}
			
			// ============================================
			// FINAL VALIDATION: Ensure target is still valid before distance calc
			// This is the CRITICAL check that prevents the CTD at line 600
			// ============================================
			if (!target || !target->loadedState || !target->GetNiNode())
			{
				_MESSAGE("CombatStyles: Target became invalid before distance check - skipping NPC %08X", actor->formID);
				continue;
			}
			
			// ============================================
			// CHECK DISTANCE - DISENGAGE IF TOO FAR
			// Extended range for companions engaged in mounted enemies
			// ============================================
			float dx = target->pos.x - actor->pos.x;
			float dy = target->pos.y - actor->pos.y;
			float distanceToTarget = sqrt(dx * dx + dy * dy);
			
			// Check if this is a companion
			bool isCompanion = IsCompanion(actor);
			
			// Use extended distance for companions fighting mounted enemies
			float maxDistance = isCompanion ? MaxCompanionCombatDistance : MaxCombatDistance;
			
			if (distanceToTarget > maxDistance)
			{
				const char* actorName = CALL_MEMBER_FN(actor, GetReferenceName)();
				_MESSAGE("CombatStyles: Target too far (%.0f > %.0f) - NPC '%s' disengaging",
					distanceToTarget, maxDistance, actorName ? actorName : "Unknown");
				
				float angleAwayFromTarget = atan2(-dx, -dy);
				mount->rot.z = angleAwayFromTarget;
				
				// ============================================
				// CRITICAL: ADD TO DISENGAGE COOLDOWN FIRST
				// This prevents immediate re-engagement via OnDismountBlocked
				// ============================================
				AddNPCToDisengageCooldown(actor->formID);
				
				// ============================================
				// CRITICAL: STOP COMBAT COMPLETELY ON DISENGAGE
				// Uses StopActorCombatAlarm - same method as HorseMountScanner
				// This properly clears combat/alarm state and allows return to default AI
				// ============================================
				
				// Stop combat alarm - this properly clears combat state
				StopActorCombatAlarm(actor);
				
				// Sheathe weapon to signal end of combat
				if (IsWeaponDrawn(actor))
				{
					actor->DrawSheatheWeapon(false);
				}
				
				// Clear weapon state data
				ClearWeaponStateData(actor->formID);
				
				// Stop horse sprint
				StopHorseSprint(mount.get());
				
				// Clear horse combat target and flags
				mount->currentCombatTarget = 0;
				mount->flags2 &= ~Actor::kFlag_kAttackOnSight;
				
				// Force AI re-evaluation on horse
				Actor_EvaluatePackage(mount.get(), false, false);
				
				_MESSAGE("CombatStyles: NPC '%s' combat STOPPED via StopActorCombatAlarm (10s cooldown)", actorName ? actorName : "Unknown");
				
				// Clear our follow tracking
				ClearNPCFollowTarget(actor);
				
				// ============================================
				// CRITICAL: Also remove from MountedCombat tracking
				// This prevents MountedCombat from detecting alarm packages
				// and trying to re-apply follow with an unloaded target
				// ============================================
				RemoveNPCFromTracking(actor->formID);
				
				// Also unregister from MultiMountedCombat
				UnregisterMultiRider(actor->formID);
				
				continue;
			}
			
			g_followingNPCs[i].lastFollowUpdateTime = currentTime;
			g_followingNPCs[i].reinforceCount++;
			
			int attackState = 0;
			InjectFollowPackage(actor, target, &attackState);
			
			// ============================================
			// ENSURE WEAPON IS EQUIPPED AND DRAWN
			// Use centralized weapon state machine
			// ============================================
			
			// Skip if weapon is transitioning (sheathing/equipping/drawing)
			if (!IsWeaponTransitioning(actor))
			{
				// ============================================
				// MAGES: Skip weapon switching - they keep warstaff equipped
				// ============================================
				if (IsRiderMage(actor->formID))
				{
					// Mage - just ensure staff is equipped and drawn
					if (!IsStaffEquipped(actor))
					{
						RequestWeaponSwitch(actor, WeaponRequest::Staff);
					}
					else if (!IsWeaponDrawn(actor))
					{
						RequestWeaponDraw(actor);
					}
				}
				// If no melee or bow equipped at all, request weapon based on distance
				else if (!IsMeleeEquipped(actor) && !IsBowEquipped(actor))
				{
					float dx = target->pos.x - actor->pos.x;
					float dy = target->pos.y - actor->pos.y;
					float dist = sqrt(dx * dx + dy * dy);
					
					RequestWeaponForDistance(actor, dist, false);
				}
				// Otherwise just ensure weapon is drawn
				else if (!IsWeaponDrawn(actor))
				{
					RequestWeaponDraw(actor);
				}
			}
			
			// Update attack position tracking (removed verbose per-frame logging)
			bool wasInAttackPosition = g_followingNPCs[i].inAttackPosition;
			g_followingNPCs[i].inMeleeRange = (attackState >= 1);
			g_followingNPCs[i].inAttackPosition = (attackState == 2);
			
			// Only log once whenFIRST entering attack position
			if (attackState == 2 && !wasInAttackPosition)
			{
				_MESSAGE("CombatStyles: NPC %08X entered ATTACK POSITION", actor->formID);
			}
			
			actor->flags2 |= Actor::kFlag_kAttackOnSight;
		}
	}
	
	// Called from MountedCombat.cpp's update loop
	void UpdateCombatStylesSystem()
	{
		// Update the centralized weapon state machine FIRST
		UpdateWeaponStates();
		
		UpdateFollowBehavior();
	}

	// ============================================
	// Attack Position Query
	// ============================================
	
	bool IsNPCInMeleeRange(Actor* actor)
	{
		if (!actor) return false;
		int slot = FindFollowingNPCSlot(actor->formID);
		if (slot < 0) return false;
		return g_followingNPCs[slot].inMeleeRange;
	}
	
	bool IsNPCInAttackPosition(Actor* actor)
	{
		if (!actor) return false;
		int slot = FindFollowingNPCSlot(actor->formID);
		if (slot < 0) return false;
		return g_followingNPCs[slot].inAttackPosition;
	}
	
	int GetFollowingNPCCount()
	{
		return g_followingNPCCount;
	}

	// ============================================
	// Weapon Draw/Sheathe
	// ============================================
	
	void SetWeaponDrawn(Actor* actor, bool draw)
	{
		if (!actor) return;
		
		if (draw)
		{
			if (!IsWeaponDrawn(actor))
				actor->DrawSheatheWeapon(true);
		}
		else
		{
			if (IsWeaponDrawn(actor))
				actor->DrawSheatheWeapon(false);
		}
	}

	// ============================================
	// Combat Styles
	// ============================================

	namespace GuardCombat
	{
		MountedCombatState DetermineState(Actor* actor, Actor* mount, Actor* target, MountedWeaponInfo* weaponInfo)
		{
			if (!actor || !mount || !target || !weaponInfo) return MountedCombatState::None;
			
			float distance = GetDistanceBetween(actor, target);
			
			if ((weaponInfo->isBow || weaponInfo->hasBowInInventory) && distance > RANGED_MIN_RANGE && distance <= RANGED_MAX_RANGE)
				return MountedCombatState::RangedAttack;
			
			if (distance <= MELEE_ATTACK_RANGE) return MountedCombatState::Attacking;
			if (distance <= MELEE_CHARGE_RANGE) return MountedCombatState::Charging;
			return MountedCombatState::Engaging;
		}
		
		void ExecuteBehavior(MountedNPCData* npcData, Actor* actor, Actor* mount, Actor* target)
		{
			if (!npcData || !actor || !mount) return;
			
			float currentTime = GetCurrentGameTime();
			
			// Start following immediately - weapon state machine handles equip/draw timing
			if (!npcData->weaponDrawn)
			{
				// NOTE: Weapon equipping handled by centralized system in DynamicPackages
				// Just mark as ready and start following
				npcData->weaponDrawn = true;
				npcData->weaponInfo = GetWeaponInfo(actor);
				
				// Start the follow behavior - USE THE ACTUAL TARGET, not player!
				if (target)
				{
					SetNPCFollowTarget(actor, target);
				}
				return;
			}
			
			if (!target) return;
			
			MountedCombatState newState = DetermineState(actor, mount, target, &npcData->weaponInfo);
			if (newState != npcData->state && newState != MountedCombatState::None)
			{
				npcData->state = newState;
				npcData->stateStartTime = currentTime;
			}
		}
		
		bool ShouldUseRanged(Actor* actor, Actor* target, MountedWeaponInfo* weaponInfo)
		{
			if (!weaponInfo) return false;
			return (weaponInfo->isBow || weaponInfo->hasBowInInventory) && GetDistanceBetween(actor, target) > RANGED_MIN_RANGE;
		}
	}
	
	namespace SoldierCombat
	{
		MountedCombatState DetermineState(Actor* actor, Actor* mount, Actor* target, MountedWeaponInfo* weaponInfo)
		{
			return GuardCombat::DetermineState(actor, mount, target, weaponInfo);
		}
		
		void ExecuteBehavior(MountedNPCData* npcData, Actor* actor, Actor* mount, Actor* target)
		{
			GuardCombat::ExecuteBehavior(npcData, actor, mount, target);
		}
		
		bool ShouldUseRanged(Actor* actor, Actor* target, MountedWeaponInfo* weaponInfo)
		{
			return GuardCombat::ShouldUseRanged(actor, target, weaponInfo);
		}
	}
	
	namespace BanditCombat
	{
		MountedCombatState DetermineState(Actor* actor, Actor* mount, Actor* target, MountedWeaponInfo* weaponInfo)
		{
			return GuardCombat::DetermineState(actor, mount, target, weaponInfo);
		}
		
		void ExecuteBehavior(MountedNPCData* npcData, Actor* actor, Actor* mount, Actor* target)
		{
			GuardCombat::ExecuteBehavior(npcData, actor, mount, target);
		}
		
		bool ShouldUseMelee(Actor* actor, Actor* target, MountedWeaponInfo* weaponInfo)
		{
			if (!weaponInfo) return true;
			return !weaponInfo->isBow && !weaponInfo->hasBowInInventory;
		}
	}
	
	namespace MageCombat
	{
		MountedCombatState DetermineState(Actor* actor, Actor* mount, Actor* target, MountedWeaponInfo* weaponInfo)
		{
			return GuardCombat::DetermineState(actor, mount, target, weaponInfo);
		}
		
		void ExecuteBehavior(MountedNPCData* npcData, Actor* actor, Actor* mount, Actor* target)
		{
			GuardCombat::ExecuteBehavior(npcData, actor, mount, target);
		}
	}
	// ============================================
	// Mounted Attack Hit Detection
	// Called during attack animation to check if weapon hits target
	// Note: MountedAttackHitData struct and g_hitData defined earlier
	// ============================================

	// Actor Value IDs
	static const UInt32 AV_Health = 24;
	
	MountedAttackHitData* GetOrCreateHitData(UInt32 riderFormID)
	{
		// Check if already tracked
		for (int i = 0; i < g_hitDataCount; i++)
		{
			if (g_hitData[i].isValid && g_hitData[i].riderFormID == riderFormID)
			{
				return &g_hitData[i];
			}
		}
		
		// Create new entry
		if (g_hitDataCount < 5)
		{
			MountedAttackHitData* data = &g_hitData[g_hitDataCount];
			data->riderFormID = riderFormID;
			data->hitRegistered = false;
			data->isPowerAttack = false;
			data->attackStartTime = 0;
			data->isValid = true;
			g_hitDataCount++;
			return data;
		}
		
		return nullptr;
	}
	
	void ResetHitData(UInt32 riderFormID)
	{
		for (int i = 0; i < g_hitDataCount; i++)
		{
			if (g_hitData[i].isValid && g_hitData[i].riderFormID == riderFormID)
			{
				g_hitData[i].hitRegistered = false;
				g_hitData[i].attackStartTime = GetAttackTimeSeconds();
				return;
			}
		}
	}
	
	void SetHitDataPowerAttack(UInt32 riderFormID, bool isPowerAttack)
	{
		MountedAttackHitData* data = GetOrCreateHitData(riderFormID);
		if (data)
		{
			data->isPowerAttack = isPowerAttack;
		}
	}
	
	// Get the base damage of the rider's equipped weapon
	float GetRiderWeaponDamage(Actor* rider)
	{
		if (!rider) return 10.0f;  // Default unarmed damage
		
		TESForm* equippedWeapon = rider->GetEquippedObject(false);  // Right hand
		if (!equippedWeapon) return 10.0f;
		
		TESObjectWEAP* weapon = DYNAMIC_CAST(equippedWeapon, TESForm, TESObjectWEAP);
		if (!weapon) return 10.0f;
		
		return (float)weapon->damage.GetAttackDamage();
	}
	
	// ============================================
	// SOUND FORM IDs FOR HIT IMPACTS
	// These are SOUN (TESSound) records, not SNDR
	// ============================================

	// Sound FormID from Skyrim.esm (SOUN record)
	static const UInt32 SOUND_UNBLOCKED_HIT = 0x0001939D;    // Unblocked hit sound
	static const UInt32 SOUND_WEAPON_BLOCK = 0x0001939B;    // Weapon block sound
	static const UInt32 SOUND_SHIELD_BLOCK = 0x0001939F;    // Shield block sound
	
	// ============================================
	// BLOOD IMPACT EFFECT SYSTEM
	// Uses BGSImpactDataSet from Skyrim.esm to spawn blood splatter
	// Note: g_bloodImpactDataSet and g_bloodImpactInitialized declared above
	// ============================================
	
	// Blood Impact Data Set FormID from Skyrim.esm
	static const UInt32 BLOOD_IMPACT_DATASET_FORMID = 0x0001F82A;
	
	// PlayImpactEffect function - spawns impact VFX on an actor
	// Note: asNodeName must be a BSFixedString, not a raw char*
	typedef bool (*_PlayImpactEffect)(VMClassRegistry* registry, UInt32 stackId, TESObjectREFR* obj, 
		BGSImpactDataSet* impactData, BSFixedString* asNodeName, 
		float afPickDirX, float afPickDirY, float afPickDirZ, 
		float afPickLength, bool abApplyNodeRotation, bool abUseNodeLocalRotation);
	static RelocAddr<_PlayImpactEffect> PlayImpactEffect(0x009D06C0);
	
	// Common bone names for blood spawning
	static const char* BLOOD_BONE_BODY = "NPC Spine2 [Spn2]";
	static const char* BLOOD_BONE_HEAD = "NPC Head [Head]";
	static const char* BLOOD_BONE_RHAND = "NPC R Hand [RHnd]";
	static const char* BLOOD_BONE_LHAND = "NPC L Hand [LHnd]";
	
	// Initialize blood impact data set
	static bool InitBloodImpactEffect()
	{
		if (g_bloodImpactInitialized) return (g_bloodImpactDataSet != nullptr);
		
		g_bloodImpactInitialized = true;
		
		TESForm* form = LookupFormByID(BLOOD_IMPACT_DATASET_FORMID);
		if (!form)
		{
			_MESSAGE("CombatStyles: ERROR - Could not find blood impact dataset (FormID: %08X)", BLOOD_IMPACT_DATASET_FORMID);
			return false;
		}
		
		g_bloodImpactDataSet = DYNAMIC_CAST(form, TESForm, BGSImpactDataSet);
		if (!g_bloodImpactDataSet)
		{
			_MESSAGE("CombatStyles: ERROR - Form %08X is not a BGSImpactDataSet (type: %d)", 
				BLOOD_IMPACT_DATASET_FORMID, form->formType);
			return false;
		}
		
		_MESSAGE("CombatStyles: Blood impact effect initialized (FormID: %08X)", BLOOD_IMPACT_DATASET_FORMID);
		return true;
	}
	
	// Spawn blood effect on target actor
	static void SpawnBloodEffect(Actor* target, Actor* attacker)
	{
		if (!target || !attacker) return;
		
		// Initialize blood impact if needed
		if (!InitBloodImpactEffect()) return;
		if (!g_bloodImpactDataSet) return;
		
		// Validate target has valid 3D
		if (!target->GetNiNode())
		{
			_MESSAGE("CombatStyles: SpawnBloodEffect - target has no 3D, skipping");
			return;
		}
		
		// Get VM registry
		VMClassRegistry* registry = (*g_skyrimVM)->GetClassRegistry();
		if (!registry) return;
		
		// Choose a random bone for variety
		const char* boneNameStr = BLOOD_BONE_BODY;  // Default to body/torso
		int roll = rand() % 100;
		if (roll < 20)
		{
			boneNameStr = BLOOD_BONE_HEAD;  // 20% head
		}
		else if (roll < 40)
		{
			boneNameStr = BLOOD_BONE_RHAND;  // 20% right arm
		}
		else if (roll < 60)
		{
			boneNameStr = BLOOD_BONE_LHAND;  // 20% left arm
		}
		// 40% body (default)
		
		// Create BSFixedString for bone name - CRITICAL: must be BSFixedString not raw char*
		BSFixedString boneName(boneNameStr);
		
		// Calculate direction from attacker to target for blood spray direction
		float dx = target->pos.x - attacker->pos.x;
		float dy = target->pos.y - attacker->pos.y;
		float dz = target->pos.z - attacker->pos.z;
		float len = sqrt(dx * dx + dy * dy + dz * dz);
		
		// Normalize direction (blood sprays away from attacker)
		float dirX = 0.0f, dirY = 0.0f, dirZ = 0.0f;
		if (len > 0.001f)
		{
			dirX = dx / len;
			dirY = dy / len;
			dirZ = dz / len;
		}
		
		// Play the blood impact effect
		// Pass pointer to BSFixedString, not raw char*
		PlayImpactEffect(registry, 0, target, g_bloodImpactDataSet, &boneName,
			dirX, dirY, dirZ,// Pick direction (blood spray direction)
			1.0f,     // Pick length
			true,   // Apply node rotation
			false);    // Use local rotation
		
		_MESSAGE("CombatStyles: Blood effect spawned on target %08X at bone '%s'", target->formID, boneNameStr);
	}
	
	// ============================================
	// PLAY SOUND AT ACTOR LOCATION
	// Uses TESSound and PlaySoundEffect (Papyrus native function)
	// ============================================

	// PlaySoundEffect function signature - same as WeaponThrowVR
	typedef void(*_PlaySoundEffect)(VMClassRegistry* VMinternal, UInt32 stackId, TESSound* sound, TESObjectREFR* source);
	static RelocAddr<_PlaySoundEffect> PlaySoundEffect(0x009EF150);
	
	// PushActorAway function - same as WeaponThrowVR (applies knockback/stagger)
	typedef void(*_PushActorAway)(VMClassRegistry* registry, UInt32 stackId, TESObjectREFR* akSource, Actor* akActor, float afKnockbackForce);
	static RelocAddr<_PushActorAway> PushActorAway(0x009D0E60);
	
	static void PlaySoundAtActor(UInt32 soundFormID, Actor* actor)
	{
		if (!actor) return;
		
		// Look up the sound form (SOUN type)
		TESForm* form = LookupFormByID(soundFormID);
		if (!form) 
		{
			_MESSAGE("CombatStyles: Failed to find sound form %08X", soundFormID);
			return;
		}
		
		// Cast to TESSound (SOUN record)
		TESSound* sound = DYNAMIC_CAST(form, TESForm, TESSound);
		if (!sound)
		{
			_MESSAGE("CombatStyles: Form %08X is not a TESSound (type=%d, expected=%d)", 
				soundFormID, form->formType, kFormType_Sound);
			return;
		}
		
		// Play the sound using the Papyrus native function
		PlaySoundEffect((*g_skyrimVM)->GetClassRegistry(), 0, sound, actor);
	}
	
	// ============================================
	// INITIALIZE MOUNTED STAGGER ANIMATION
	// Looks up the dedicated mounted stagger idle from Update.esm
	// ============================================

	static bool InitMountedStaggerAnimation()
	{
		if (g_mountedStaggerIdle != nullptr) return true;
		
		TESForm* form = LookupFormByID(MOUNTED_STAGGER_IDLE_FORMID);
		if (!form)
		{
			_MESSAGE("CombatStyles: ERROR - Could not find mounted stagger idle (FormID: %08X)", MOUNTED_STAGGER_IDLE_FORMID);
			return false;
		}
		
		g_mountedStaggerIdle = DYNAMIC_CAST(form, TESForm, TESIdleForm);
		if (!g_mountedStaggerIdle)
		{
			_MESSAGE("CombatStyles: ERROR - Form %08X is not a TESIdleForm (type: %d)", MOUNTED_STAGGER_IDLE_FORMID, form->formType);
			return false;
		}
		
		g_mountedStaggerIdleInitialized = true;
		_MESSAGE("CombatStyles: Successfully loaded mounted stagger animation (FormID: %08X)", MOUNTED_STAGGER_IDLE_FORMID);
		return true;
	}
	
	// ============================================
	// APPLY BLOCK STAGGER TO RIDER
	// Called when target successfully blocks the rider's attack
	// Uses the dedicated mounted stagger animation from Update.esm
	// ============================================

	static void ApplyBlockStaggerToRider(Actor* rider, Actor* blocker)
	{
		if (!rider) return;
		
		// Initialize the mounted stagger animation if needed
		if (!InitMountedStaggerAnimation())
		{
			_MESSAGE("CombatStyles: WARNING - Could not apply block stagger (animation not initialized)");
			return;
		}
		
		// Get the animation event name from the idle form
		const char* eventName = g_mountedStaggerIdle->animationEvent.c_str();
		if (!eventName || strlen(eventName) == 0)
		{
			_MESSAGE("CombatStyles: ERROR - Mounted stagger idle has empty animation event");
			return;
		}
		
		// Temporarily allow stagger by removing mass protection for 2.5 seconds
		AllowTemporaryStagger(rider, 2.5f);
		
		// Play the mounted stagger animation on the rider
		bool result = SendAnimationEvent(rider, eventName);
		
		if (result)
		{
			_MESSAGE("CombatStyles: Applied mounted stagger animation to rider %08X (event: %s)", rider->formID, eventName);
		}
		else
		{
			_MESSAGE("CombatStyles: WARNING - Mounted stagger animation rejected for rider %08X", rider->formID);
		}
	}
	
	// ============================================
	// CHECK IF TARGET IS BLOCKING AND WITH WHAT
	// Uses animation graph variable "IsBlocking"
	// Returns: 0 = not blocking, 1 = blocking with weapon, 2 = blocking with shield
	// Accounts for left-handed mode where shield is in the right hand
	// Also checks if target is facing the attacker (can't block what you can't see)
	// ============================================

	static int GetActorBlockingType(Actor* actor, Actor* attacker = nullptr)
	{
		if (!actor) return 0;
		
		// Check animation graph variable "IsBlocking"
		static BSFixedString isBlockingVar("IsBlocking");
		bool isBlocking = false;
		
		typedef bool (*_GetGraphVariableBool)(IAnimationGraphManagerHolder* holder, const BSFixedString& varName, bool& out);
		_GetGraphVariableBool getGraphVarBool = get_vfunc<_GetGraphVariableBool>(&actor->animGraphHolder, 0x12);
		
		if (getGraphVarBool)
		{
			getGraphVarBool(&actor->animGraphHolder, isBlockingVar, isBlocking);
		}
		
		if (!isBlocking)
		{
			return 0;  // Not blocking
		}
		
		// ============================================
		// FIELD OF VIEW CHECK
		// Target must be facing the attacker to block
		// Can't block attacks from directly behind (~150 degree cone in front)
		// ============================================
		if (attacker)
		{
			// Calculate direction from target to attacker
			float dx = attacker->pos.x - actor->pos.x;
			float dy = attacker->pos.y - actor->pos.y;
			float angleToAttacker = atan2(dx, dy);  // Angle from target to attacker
			
			// Get the direction the target is facing
			float targetFacing = actor->rot.z;
			
			// Calculate angle difference
			float angleDiff = angleToAttacker - targetFacing;
			
			// Normalize to -PI to PI
			while (angleDiff > 3.14159f) angleDiff -= 6.28318f;
			while (angleDiff < -3.14159f) angleDiff += 6.28318f;
			
			// Check if attacker is within ~150 degree cone in front (75 degrees each side)
			// fabs(angleDiff) > 1.309 radians = 75 degrees = attacker is outside frontal arc
			const float BLOCK_FOV_HALF_ANGLE = 1.309f;  // 75 degrees in radians
			
			if (fabs(angleDiff) > BLOCK_FOV_HALF_ANGLE)
			{
				// Attacker is behind the target - can't block!
				_MESSAGE("CombatStyles: Target %08X IS blocking but attacker is BEHIND (angle: %.1f deg) - block fails!", 
					actor->formID, angleDiff * 57.2958f);
				return 0;  // Treat as not blocking
			}
		}
		
		// Target IS blocking AND facing the attacker - now determine if shield or weapon
		// Check for left-handed mode - shield is in opposite hand
		bool leftHandedMode = (g_leftHandedMode && *g_leftHandedMode);
		
		// In right-handed mode: shield is in left hand (true)
		// In left-handed mode: shield is in right hand (false)
		bool shieldHand = !leftHandedMode;  // true = left hand, false = right hand
		
		TESForm* shieldHandItem = actor->GetEquippedObject(shieldHand);
		
		if (shieldHandItem && shieldHandItem->formType == kFormType_Armor)
		{
			// Armor in shield hand = shield
			_MESSAGE("CombatStyles: Target %08X BLOCKING WITH SHIELD (leftHanded: %d)", actor->formID, leftHandedMode ? 1 : 0);
			return 2;  // Blocking with shield
		}
		
		// No shield in shield hand, but still blocking = weapon block
		_MESSAGE("CombatStyles: Target %08X BLOCKING WITH WEAPON (leftHanded: %d)", actor->formID, leftHandedMode ? 1 : 0);
		return 1;  // Blocking with weapon
	}
	
	// Simple helper for backward compatibility
	static bool IsActorBlocking(Actor* actor)
	{
		return GetActorBlockingType(actor, nullptr) > 0;
	}
	
	// Apply damage to the target actor - INSTANT
	void ApplyMountedAttackDamage(Actor* rider, Actor* target, bool isPowerAttack)
	{
		if (!rider || !target) return;
		
		// Check if rider is a companion
		bool riderIsCompanion = IsCompanion(rider);
		
		// Check if target is blocking and with what
		// 0 = not blocking, 1 = weapon block, 2 = shield block
		// Pass rider as attacker for FOV check - can't block attacks from behind!
		int blockType = GetActorBlockingType(target, rider);
		
		float baseDamage = GetRiderWeaponDamage(rider);
		
		const float POWER_ATTACK_BONUS = 5.0f;
		if (isPowerAttack)
		{
			baseDamage += POWER_ATTACK_BONUS;
		}
		
		// ============================================
		// DAMAGE MULTIPLIER FOR MOUNTED COMBAT
		// Applies to ALL targets including the player!
		// Companions: CompanionRiderDamageMultiplier (default 2x)
		// Hostile riders: HostileRiderDamageMultiplier (default 3x)
		// ============================================
		if (riderIsCompanion)
		{
			baseDamage *= CompanionRiderDamageMultiplier;  // Companions use config multiplier
		}
		else
		{
			baseDamage *= HostileRiderDamageMultiplier;  // Hostile riders use config multiplier
		}

		// Blocking effects:
		// - Shield block: 10% damage, costs 20 stamina
		// - Weapon block: 25% damage, costs 30 stamina (less effective than shield)
		// - No stamina: Full damage, no stamina cost (guard broken) - still play block sound
		float actualDamage = baseDamage;
		bool blockSuccessful = false;
		bool guardBroken = false;
		const char* blockTypeStr = "";
		float staminaCost = 0;
		
		if (blockType > 0)
		{
			const UInt32 AV_Stamina = 26;
			float currentStamina = target->actorValueOwner.GetCurrent(AV_Stamina);
			
			if (currentStamina > 0)
			{
				if (blockType == 2)  // Shield block
				{
					actualDamage = baseDamage * 0.1f;   // Only 10% damage
					staminaCost = 20.0f;
					blockTypeStr = "SHIELD";
				}
				else  // Weapon block
				{
					actualDamage = baseDamage * 0.25f;  // 25% damage
					staminaCost = 30.0f;
					blockTypeStr = "WEAPON";
				}
				
				// Drain stamina from blocking the hit
				target->actorValueOwner.RestoreActorValue(Actor::kDamage, AV_Stamina, -staminaCost);
				blockSuccessful = true;
			}
			else
			{
				// No stamina - guard broken, full damage
				guardBroken = true;
			}
		}
		
			// Apply damage
		target->actorValueOwner.RestoreActorValue(Actor::kDamage, AV_Health, -actualDamage);
		
		// ============================================
		// STAGGER ON UNBLOCKED HIT VS NON-PLAYER, NON-MOUNTED TARGETS
		// 20% chance (configurable) to stagger and push back
		// Only applies to NON-PLAYER targets when hit is not blocked
		// Skip if target is mounted (would look weird and could cause issues)
		// Skip if target is the player (player shouldn't be staggered by this system)
		// ============================================
		bool staggerApplied = false;
		if (MountedAttackStaggerEnabled && !blockSuccessful && !guardBroken)
		{
			// Check if target is the player - NEVER stagger the player
			bool targetIsPlayer = (g_thePlayer && (*g_thePlayer) && target == (*g_thePlayer));
			
			if (!targetIsPlayer)
			{
				// Check if target is mounted - skip stagger for mounted targets
				NiPointer<Actor> targetMount;
				bool targetIsMounted = CALL_MEMBER_FN(target, GetMount)(targetMount) && targetMount;
				
				if (!targetIsMounted)
				{
					// Roll for stagger chance
					int roll = rand() % 100;
					if (roll < MountedAttackStaggerChance)
					{
						// Apply stagger using PushActorAway
						// Use rider as the source so target gets pushed away from rider
						PushActorAway((*g_skyrimVM)->GetClassRegistry(), 0, rider, target, MountedAttackStaggerForce);
						staggerApplied = true;
						
						_MESSAGE("CombatStyles: Target %08X STAGGERED (rolled %d < %d%%, force: %.2f)", 
							target->formID, roll, MountedAttackStaggerChance, MountedAttackStaggerForce);
					}
				}
			}
		}
		
		// Play appropriate sound effect
		if (blockSuccessful)
		{
			// Successful block - play block sound and stagger rider
			if (blockType == 2)
			{
				PlaySoundAtActor(SOUND_SHIELD_BLOCK, target);  // Shield block sound
						}
			else
			{
				PlaySoundAtActor(SOUND_WEAPON_BLOCK, target);  // Weapon block sound
			}
			
			// Apply stagger spell to the rider whose attack was blocked
			// Pass target (blocker) as the spell source
			ApplyBlockStaggerToRider(rider, target);
		}
		else if (guardBroken)
		{
			// Guard broken - play block sound (they tried to block) but NO stagger to rider
			if (blockType == 2)
			{
				PlaySoundAtActor(SOUND_SHIELD_BLOCK, target);  // Shield block sound
			}
			else
			{
				PlaySoundAtActor(SOUND_WEAPON_BLOCK, target);  // Weapon block sound
			}
			
			// Guard broken = full damage hit, spawn blood effect
			SpawnBloodEffect(target, rider);
			
			// NO ApplyBlockStaggerToRider - rider doesn't get staggered when guard is broken
		}
		else
		{
			// Unblocked hit - play hit sound and spawn blood effect
			PlaySoundAtActor(SOUND_UNBLOCKED_HIT, target);
			SpawnBloodEffect(target, rider);
		}
		
		const char* riderName = CALL_MEMBER_FN(rider, GetReferenceName)();
		const char* targetName = CALL_MEMBER_FN(target, GetReferenceName)();
		
		// Determine multiplier string for logging
		char multiplierStr[32] = "";
		if (riderIsCompanion)
		{
			sprintf_s(multiplierStr, " [%.1fx ALLY]", CompanionRiderDamageMultiplier);
		}
		else
		{
			sprintf_s(multiplierStr, " [%.1fx MOUNTED]", HostileRiderDamageMultiplier);
		}

		// Log with blocking info and mounted damage bonus
		if (blockType > 0)
		{
			if (blockSuccessful)
			{
				_MESSAGE("CombatStyles: %s hit %s for %.0f dmg (%s BLOCK - reduced from %.0f, -%.0f stamina)%s%s", 
					riderName ? riderName : "Rider",
					targetName ? targetName : "Target",
					actualDamage,
					blockTypeStr,
					baseDamage,
					staminaCost,
					isPowerAttack ? " (POWER)" : "",
					multiplierStr);
			}
			else
			{
				_MESSAGE("CombatStyles: %s hit %s for %.0f dmg (GUARD BROKEN - no stamina!)%s%s", 
					riderName ? riderName : "Rider",
					targetName ? targetName : "Target",
					actualDamage,
					isPowerAttack ? " (POWER)" : "",
					multiplierStr);
			}
		}
		else
		{
			_MESSAGE("CombatStyles: %s hit %s for %.0f dmg%s%s%s", 
				riderName ? riderName : "Rider",
				targetName ? targetName : "Target",
				actualDamage,
				isPowerAttack ? " (POWER)" : "",
				multiplierStr,
				staggerApplied ? " [STAGGERED]" : "");
		}
	}
	
	bool UpdateMountedAttackHitDetection(Actor* rider, Actor* target)
	{
		if (!rider || !target) return false;
		
		MountedAttackHitData* hitData = GetOrCreateHitData(rider->formID);
		if (!hitData) return false;
		
		// Already registered a hit this attack
		if (hitData->hitRegistered) return false;
		
		// Check attack animation timing
		float currentTime = GetAttackTimeSeconds();
		float timeSinceAttackStart = currentTime - hitData->attackStartTime;
		
		// Hit can only register AFTER the wind-up phase (0.4 seconds)
		// and BEFORE the attack window ends (1.2 seconds total)
		if (timeSinceAttackStart < ATTACK_ANIMATION_WINDUP)
		{
			// Still in wind-up phase - too early to hit
			return false;
		}
		
		if (timeSinceAttackStart > (ATTACK_ANIMATION_WINDUP + ATTACK_ANIMATION_WINDOW))
		{
			// Past the attack window - missed the opportunity
			return false;
		}
		
		// We're in the valid attack window - check distance
		float distance = 0;
		bool inRange = CheckMountedAttackHit(rider, target, &distance);
		
		if (inRange)
		{
			hitData->hitRegistered = true;
			ApplyMountedAttackDamage(rider, target, hitData->isPowerAttack);
			return true;
		}
		
		return false;
	}
}
