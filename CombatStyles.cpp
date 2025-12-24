#include "CombatStyles.h"
#include "DynamicPackages.h"
#include "Helper.h"
#include "SingleMountedCombat.h"
#include "SpecialMovesets.h"
#include "WeaponDetection.h"
#include "AILogging.h"
#include "TargetSelection.h"
#include "skse64/GameData.h"
#include "skse64/GameReferences.h"
#include "skse64/GameForms.h"
#include "skse64/GameRTTI.h"

namespace MountedNPCCombatVR
{
	// ============================================
	// Configuration
	// ============================================
	
	const float MELEE_ATTACK_RANGE = 256.0f;
	const float MELEE_CHARGE_RANGE = 512.0f;
	const float RANGED_MIN_RANGE = 384.0f;
	const float RANGED_MAX_RANGE = 2048.0f;
	const float WEAPON_DRAW_DELAY = 0.2f;  // 200ms delay before drawing weapon
	const float FOLLOW_UPDATE_INTERVAL = 0.1f;  // Update every 100ms for smooth rotation
	const float MAX_COMBAT_DISTANCE = 2300.0f;  // If player gets this far, disengage combat
	
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
	
	const float ATTACK_COOLDOWN = 2.0f;  // Seconds between attacks
	const int POWER_ATTACK_CHANCE = 5; // 5% chance for power attack
	
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
	// Attack Animation Tracking (declared early for reset function)
	// ============================================
	
	struct RiderAttackData
	{
		UInt32 riderFormID;
		RiderAttackState state;
		float lastAttackTime;
		float stateStartTime;
		bool isValid;
	};
	
	static RiderAttackData g_riderAttackData[5];
	static int g_riderAttackCount = 0;
	
	// ============================================
	// Mount Tracking (declared early for reset function)
	// ============================================
	
	static UInt32 g_controlledMounts[5] = {0};
	static int g_controlledMountCount = 0;
	
	// ============================================
	// Follow Player Tracking (declared early for reset function)
	// ============================================
	
	struct FollowingNPCData
	{
		UInt32 actorFormID;
		UInt32 targetFormID;
		bool hasInjectedPackage;
		float lastFollowUpdateTime;
		int reinforceCount;
		bool isValid;
		bool inMeleeRange;
		bool inAttackPosition;
	};
	
	static FollowingNPCData g_followingNPCs[5];
	static int g_followingNPCCount = 0;
	
	// ============================================
	// Hit Detection Data (declared early for reset function)
	// ============================================
	
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
	
	// ============================================
	// Reset Combat Styles System
	// Called on game load/new game to clear stale state
	// ============================================
	
	void ResetCombatStylesSystem()
	{
		_MESSAGE("CombatStyles: Resetting combat styles system...");
		
		// Reset initialization flags - forms will be re-looked up on next use
		g_combatStylesInitialized = false;
		g_attackAnimsInitialized = false;
		
		// Clear cached forms (they may be invalid after load)
		g_idleAttackLeft = nullptr;
		g_idleAttackRight = nullptr;
		g_idlePowerAttackLeft = nullptr;
		g_idlePowerAttackRight = nullptr;
		
		// Clear attack tracking
		g_riderAttackCount = 0;
		for (int i = 0; i < 5; i++)
		{
			g_riderAttackData[i].isValid = false;
			g_riderAttackData[i].riderFormID = 0;
		}
		
		// Clear mount control tracking
		g_controlledMountCount = 0;
		for (int i = 0; i < 5; i++)
		{
			g_controlledMounts[i] = 0;
		}
		
		// Clear follow tracking - IMPORTANT: reset count FIRST
		g_followingNPCCount = 0;
		for (int i = 0; i < 5; i++)
		{
			g_followingNPCs[i].isValid = false;
			g_followingNPCs[i].actorFormID = 0;
			g_followingNPCs[i].targetFormID = 0;
		}
		
		// Clear hit data
		g_hitDataCount = 0;
		for (int i = 0; i < 5; i++)
		{
			g_hitData[i].isValid = false;
			g_hitData[i].riderFormID = 0;
		}
		
		_MESSAGE("CombatStyles: System reset complete");
	}
	
	// ============================================
	// Attack Animation Functions
	// ============================================
	
	float GetAttackTimeSeconds()
	{
		static auto startTime = clock();
		return (float)(clock() - startTime) / CLOCKS_PER_SEC;
	}
	
	bool InitAttackAnimations()
	{
		if (g_attackAnimsInitialized) return true;
		
		_MESSAGE("CombatStyles: Initializing attack animations from %s...", ESP_NAME);
		
		// Get the full FormIDs using the ESP lookup function
		UInt32 leftFormID = GetFullFormIdMine(ESP_NAME, IDLE_ATTACK_LEFT_BASE_FORMID);
		UInt32 rightFormID = GetFullFormIdMine(ESP_NAME, IDLE_ATTACK_RIGHT_BASE_FORMID);
		
		_MESSAGE("CombatStyles: Looking up LEFT attack - Base: %08X, Full: %08X", 
			IDLE_ATTACK_LEFT_BASE_FORMID, leftFormID);
		_MESSAGE("CombatStyles: Looking up RIGHT attack - Base: %08X, Full: %08X", 
			IDLE_ATTACK_RIGHT_BASE_FORMID, rightFormID);
		
		if (leftFormID != 0)
		{
			TESForm* leftForm = LookupFormByID(leftFormID);
			if (leftForm)
			{
				g_idleAttackLeft = DYNAMIC_CAST(leftForm, TESForm, TESIdleForm);
				if (g_idleAttackLeft)
				{
					_MESSAGE("CombatStyles: Found IDLE_ATTACK_LEFT (FormID: %08X)", leftFormID);
				}
				else
				{
					_MESSAGE("CombatStyles: ERROR - FormID %08X is not a TESIdleForm (type: %d)!", 
 						leftFormID, leftForm->formType);
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
				if (g_idleAttackRight)
				{
					_MESSAGE("CombatStyles: Found IDLE_ATTACK_RIGHT (FormID: %08X)", rightFormID);
				}
				else
				{
					_MESSAGE("CombatStyles: ERROR - FormID %08X is not a TESIdleForm (type: %d)!", 
						rightFormID, rightForm->formType);
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
		
		// ============================================
		// Load Power Attack Idles from Update.esm
		// ============================================
		
		_MESSAGE("CombatStyles: Loading power attack animations from Update.esm...");
		
		// Power attacks use direct FormIDs since Update.esm is always loaded at index 01
		TESForm* powerLeftForm = LookupFormByID(IDLE_POWER_ATTACK_LEFT_FORMID);
		if (powerLeftForm)
		{
			g_idlePowerAttackLeft = DYNAMIC_CAST(powerLeftForm, TESForm, TESIdleForm);
			if (g_idlePowerAttackLeft)
			{
				_MESSAGE("CombatStyles: Found IDLE_POWER_ATTACK_LEFT (FormID: %08X)", IDLE_POWER_ATTACK_LEFT_FORMID);
			}
			else
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
			if (g_idlePowerAttackRight)
			{
				_MESSAGE("CombatStyles: Found IDLE_POWER_ATTACK_RIGHT (FormID: %08X)", IDLE_POWER_ATTACK_RIGHT_FORMID);
			}
			else
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
		_MESSAGE("CombatStyles: Attack animations initialized - Regular: %s, Power: %s", 
			success ? "SUCCESS" : "PARTIAL/FAILED",
			powerSuccess ? "SUCCESS" : "PARTIAL/FAILED");
		
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
	
	bool PlayMountedAttackAnimation(Actor* rider, const char* playerSide)
	{
		if (!rider) return false;
		
		// Initialize animations if not done
		if (!g_attackAnimsInitialized)
		{
			InitAttackAnimations();
		}
		
		// Get or create attack data for this rider
		RiderAttackData* attackData = GetOrCreateRiderAttackData(rider->formID);
		if (!attackData) return false;
		
		// Check cooldown - must wait full cooldown regardless of success/fail
		float currentTime = GetAttackTimeSeconds();
		float timeSinceLastAttack = currentTime - attackData->lastAttackTime;
		
		if (timeSinceLastAttack < ATTACK_COOLDOWN)
		{
			return false;  // Still on cooldown - silent fail, no log spam
		}
		
		// Reset attack state if cooldown has passed (attack animation should be done by now)
		if (attackData->state != RiderAttackState::None && timeSinceLastAttack >= ATTACK_COOLDOWN)
		{
			attackData->state = RiderAttackState::None;
		}
		
		// Check if already attacking (shouldn't happen after above reset, but safety check)
		if (attackData->state != RiderAttackState::None)
		{
			return false;
		}
		
		// Determine if this should be a power attack (5% chance)
		bool isPowerAttack = (rand() % 100) < POWER_ATTACK_CHANCE;
		
		// Determine which idle to use based on player side and attack type
		TESIdleForm* idleToPlay = nullptr;
		const char* animName = "";
		const char* attackType = "";
		
		if (strcmp(playerSide, "LEFT") == 0)
		{
			if (isPowerAttack && g_idlePowerAttackLeft)
			{
				idleToPlay = g_idlePowerAttackLeft;
				animName = "LEFT";
				attackType = "POWER";
			}
			else
			{
				idleToPlay = g_idleAttackLeft;
				animName = "LEFT";
				attackType = "normal";
			}
		}
		else if (strcmp(playerSide, "RIGHT") == 0)
		{
			if (isPowerAttack && g_idlePowerAttackRight)
			{
				idleToPlay = g_idlePowerAttackRight;
				animName = "RIGHT";
				attackType = "POWER";
			}
			else
			{
				idleToPlay = g_idleAttackRight;
				animName = "RIGHT";
				attackType = "normal";
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
		
		// Get the animation event name from the TESIdleForm
		const char* animEventName = idleToPlay->animationEvent.c_str();
		
		if (!animEventName || strlen(animEventName) == 0)
		{
			return false;
		}
		
		// Check if rider is in a valid state for animation
		// Must have 3D loaded and AI enabled
		if (!rider->GetNiNode())
		{
			_MESSAGE("CombatStyles: Rider %08X has no 3D loaded - skipping attack", rider->formID);
			attackData->lastAttackTime = currentTime;  // Still set cooldown
			return false;
		}
		
		if (!rider->processManager)
		{
			_MESSAGE("CombatStyles: Rider %08X has no process manager - skipping attack", rider->formID);
			attackData->lastAttackTime = currentTime;
			return false;
		}
		
		// Check if rider is still mounted
		NiPointer<Actor> mount;
		if (!CALL_MEMBER_FN(rider, GetMount)(mount) || !mount)
		{
			_MESSAGE("CombatStyles: Rider %08X is not mounted - skipping attack", rider->formID);
			attackData->lastAttackTime = currentTime;
			return false;
		}
		
		// Send the animation event via NotifyAnimationGraph
		bool result = SendAnimationEvent(rider, animEventName);
		
		if (result)
		{
			// Update attack state and cooldown only on success
			attackData->state = RiderAttackState::WindingUp;
			attackData->stateStartTime = currentTime;
			attackData->lastAttackTime = currentTime;
			
			// Reset hit detection for this new attack and set power attack flag
			ResetHitData(rider->formID);
			SetHitDataPowerAttack(rider->formID, isPowerAttack);
			
			_MESSAGE("CombatStyles: Rider %08X playing %s %s attack animation (event: %s)", 
				rider->formID, attackType, animName, animEventName);
		}
		else
		{
			// Log failure reason - but don't set cooldown so we can retry quickly
			_MESSAGE("CombatStyles: Rider %08X animation event '%s' rejected (graph busy?)", 
				rider->formID, animEventName);
		}
		
		return result;
	}
	
	// ============================================
	// Mount Control Release (for cleanup)
	// ============================================
	
	void ReleaseAllMountControl()
	{
		g_controlledMountCount = 0;
		for (int i = 0; i < 5; i++) g_controlledMounts[i] = 0;
	}

	// ============================================
	// Follow Player Helper Functions
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
	
	bool IsNPCFollowingPlayer(Actor* actor)
	{
		if (!actor) return false;
		return FindFollowingNPCSlot(actor->formID) >= 0;
	}
	
	void SetNPCFollowPlayer(Actor* actor)
	{
		if (!actor) return;
		
		const char* actorName = CALL_MEMBER_FN(actor, GetReferenceName)();
		
		// Get the rider's combat target (could be player or another NPC)
		Actor* target = GetRiderCombatTarget(actor);
		if (!target)
		{
			// Fallback to player if no valid target
			if (g_thePlayer && *g_thePlayer)
			{
				target = *g_thePlayer;
			}
			else
			{
				_MESSAGE("CombatStyles: No valid target for '%s' - cannot set up follow", 
					actorName ? actorName : "Unknown");
				return;
			}
		}
		
		const char* targetName = CALL_MEMBER_FN(target, GetReferenceName)();
		
		// Check if already tracked
		int existingSlot = FindFollowingNPCSlot(actor->formID);
		if (existingSlot >= 0)
		{
			// Already tracked - update target and re-inject the package
			g_followingNPCs[existingSlot].targetFormID = target->formID;
			
			_MESSAGE("CombatStyles: Re-injecting follow package for '%s' (%08X) targeting '%s' (%08X)", 
				actorName ? actorName : "Unknown", actor->formID,
				targetName ? targetName : "Unknown", target->formID);
			InjectFollowPackage(actor, target);
			g_followingNPCs[existingSlot].lastFollowUpdateTime = GetCurrentGameTime();
			return;
		}
		
		_MESSAGE("CombatStyles: ========================================");
		_MESSAGE("CombatStyles: SETTING UP FOLLOW BEHAVIOR");
		_MESSAGE("CombatStyles: NPC: '%s' (FormID: %08X)", actorName ? actorName : "Unknown", actor->formID);
		_MESSAGE("CombatStyles: Target: '%s' (FormID: %08X)", targetName ? targetName : "Unknown", target->formID);
		_MESSAGE("CombatStyles: ========================================");
		
		// If this is the first NPC to start combat, notify the combat system
		if (g_followingNPCCount == 0)
		{
			NotifyCombatStarted();
			NotifyRangedCombatStarted();
		}
		
		// Initialize dynamic package system if needed
		if (!g_combatStylesInitialized)
		{
			_MESSAGE("CombatStyles: Initializing dynamic package system...");
			InitDynamicPackageSystem();
			g_combatStylesInitialized = true;
		}
		
		// ============================================
		// ADD COMBAT ARROWS TO INVENTORY (for bow users)
		// ============================================
		if (HasBowInInventory(actor))
		{
			AddArrowsToInventory(actor, 100);
			_MESSAGE("CombatStyles: Added 100 arrows to '%s' for combat", actorName ? actorName : "Unknown");
		}
		
		// Ensure weapon is drawn
		SetWeaponDrawn(actor, true);
		
		// Set aggressive flag
		actor->flags2 |= Actor::kFlag_kAttackOnSight;
		
		// Inject the follow package with the target
		if (InjectFollowPackage(actor, target))
		{
			_MESSAGE("CombatStyles: Follow package injected successfully");
		}
		else
		{
			_MESSAGE("CombatStyles: ERROR - Follow package injection failed!");
		}
		
		// Add to tracking list
		if (g_followingNPCCount >= 0 && g_followingNPCCount < 5)
		{
			int slot = g_followingNPCCount;
			g_followingNPCs[slot].actorFormID = actor->formID;
			g_followingNPCs[slot].targetFormID = target->formID;
			g_followingNPCs[slot].hasInjectedPackage = true;
			g_followingNPCs[slot].lastFollowUpdateTime = GetCurrentGameTime();
			g_followingNPCs[slot].reinforceCount = 0;
			g_followingNPCs[slot].isValid = true;
			g_followingNPCs[slot].inMeleeRange = false;
			g_followingNPCs[slot].inAttackPosition = false;
			g_followingNPCCount++;
			
			_MESSAGE("CombatStyles: Added to follow tracking list (count: %d)", g_followingNPCCount);
		}
		else
		{
			_MESSAGE("CombatStyles: ERROR - Cannot add to tracking, count out of bounds: %d", g_followingNPCCount);
		}
		
		_MESSAGE("CombatStyles: ========================================");
	}
	
	void ClearNPCFollowPlayer(Actor* actor)
	{
		if (!actor) return;
		
		const char* actorName = CALL_MEMBER_FN(actor, GetReferenceName)();
		
		int slot = FindFollowingNPCSlot(actor->formID);
		if (slot >= 0 && slot < 5)
		{
			_MESSAGE("CombatStyles: ========================================");
			_MESSAGE("CombatStyles: CLEARING FOLLOW BEHAVIOR");
			_MESSAGE("CombatStyles: NPC: '%s' (FormID: %08X)", actorName ? actorName : "Unknown", actor->formID);
			_MESSAGE("CombatStyles: ========================================");
			
			// Clear any injected packages on the rider
			ClearInjectedPackages(actor);
			
			// Clear attack on sight flag on rider
			actor->flags2 &= ~Actor::kFlag_kAttackOnSight;
			
			// ============================================
			// REMOVE COMBAT ARROWS FROM INVENTORY
			// ============================================
			RemoveArrowsFromInventory(actor, 100);
			
			// ============================================
			// CLEAR HORSE PACKAGES AND OFFSET
			// ============================================
			NiPointer<Actor> mount;
			if (CALL_MEMBER_FN(actor, GetMount)(mount) && mount)
			{
				_MESSAGE("CombatStyles: Clearing horse %08X packages and movement offset", mount->formID);
				
				// Clear any injected packages on the horse (restore original AI)
				ClearInjectedPackages(mount.get());
				
				// Clear any KeepOffset commands on the horse
				Actor_ClearKeepOffsetFromActor(mount.get());
				
				// Clear turn direction tracking
				ClearHorseTurnDirection(mount->formID);
				
				// Clear combat target and flags on horse
				mount->currentCombatTarget = 0;
				mount->flags2 &= ~Actor::kFlag_kAttackOnSight;
			}
			
			// Mark slot as invalid first
			g_followingNPCs[slot].isValid = false;
			
			// Shift remaining entries down (only if not the last entry)
			if (slot < g_followingNPCCount - 1)
			{
				for (int i = slot; i < g_followingNPCCount - 1 && i < 4; i++)
				{
					g_followingNPCs[i] = g_followingNPCs[i + 1];
				}
			}
			
			// Clear the last slot and decrement count
			if (g_followingNPCCount > 0)
			{
				g_followingNPCCount--;
				g_followingNPCs[g_followingNPCCount].isValid = false;
			}
			
			_MESSAGE("CombatStyles: Cleared, removed from tracking (count: %d)", g_followingNPCCount);
		}
	}
	
	void ClearAllFollowingNPCs()
	{
		_MESSAGE("CombatStyles: Clearing all %d following NPCs...", g_followingNPCCount);
		
		for (int i = 0; i < g_followingNPCCount; i++)
		{
			if (g_followingNPCs[i].isValid)
			{
				TESForm* form = LookupFormByID(g_followingNPCs[i].actorFormID);
				if (form && form->formType == kFormType_Character)
				{
					Actor* actor = static_cast<Actor*>(form);
					ClearInjectedPackages(actor);
					actor->flags2 &= ~Actor::kFlag_kAttackOnSight;
				}
			}
			g_followingNPCs[i].isValid = false;
		}
		
		g_followingNPCCount = 0;
		_MESSAGE("CombatStyles: All tracking cleared");
	}
	
	// ============================================
	// Continuous Follow Update
	// Re-injects travel package periodically to update destination
	// Stops following if player gets too far away or rider exits combat
	// ============================================
	
	void UpdateFollowBehavior()
	{
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
			
			// ============================================
			// GET CURRENT COMBAT TARGET (may have changed)
			// ============================================
			Actor* target = GetRiderCombatTarget(actor);
			
			// If target changed, update tracking
			if (target && target->formID != g_followingNPCs[i].targetFormID)
			{
				const char* actorName = CALL_MEMBER_FN(actor, GetReferenceName)();
				const char* targetName = CALL_MEMBER_FN(target, GetReferenceName)();
				_MESSAGE("CombatStyles: Rider '%s' target changed to '%s' (%08X)",
					actorName ? actorName : "Unknown",
					targetName ? targetName : "Unknown", target->formID);
				g_followingNPCs[i].targetFormID = target->formID;
			}
			
			// If no valid target, try to get from stored FormID
			if (!target && g_followingNPCs[i].targetFormID != 0)
			{
				TESForm* targetForm = LookupFormByID(g_followingNPCs[i].targetFormID);
				if (targetForm && targetForm->formType == kFormType_Character)
				{
					target = static_cast<Actor*>(targetForm);
					if (!IsValidCombatTarget(actor, target))
					{
						target = nullptr;
					}
				}
			}
			
			// Fallback to player if still no target
			if (!target && g_thePlayer && *g_thePlayer)
			{
				target = *g_thePlayer;
				g_followingNPCs[i].targetFormID = target->formID;
			}
			
			if (!target)
			{
				// No valid target - clear follow behavior
				const char* actorName = CALL_MEMBER_FN(actor, GetReferenceName)();
				_MESSAGE("CombatStyles: Rider '%s' has no valid target - clearing follow",
					actorName ? actorName : "Unknown");
				ClearNPCFollowPlayer(actor);
				continue;
			}
			
			// ============================================
			// CHECK IF RIDER EXITED COMBAT STATE
			// ============================================
			if (!actor->IsInCombat())
			{
				const char* actorName = CALL_MEMBER_FN(actor, GetReferenceName)();
				_MESSAGE("CombatStyles: Rider '%s' (%08X) exited combat state - clearing follow behavior",
					actorName ? actorName : "Unknown", actor->formID);
				
				// End any active rapid fire
				EndStationaryRapidFire(actor->formID);
				
				// Clear the follow tracking and horse offset
				ClearNPCFollowPlayer(actor);
				continue;
			}
			
			// ============================================
			// CHECK DISTANCE TO TARGET - DISENGAGE IF TOO FAR
			// ============================================
			
			float distanceToTarget = GetDistanceToTarget(actor, target);
			
			if (distanceToTarget > MAX_COMBAT_DISTANCE)
			{
				const char* actorName = CALL_MEMBER_FN(actor, GetReferenceName)();
				const char* targetName = CALL_MEMBER_FN(target, GetReferenceName)();
				_MESSAGE("CombatStyles: Target '%s' too far (%.0f units > %.0f max) - NPC '%s' disengaging",
					targetName ? targetName : "Unknown",
					distanceToTarget, MAX_COMBAT_DISTANCE, actorName ? actorName : "Unknown");
					
				// End any active rapid fire
				EndStationaryRapidFire(actor->formID);
				
				// Rotate horse to face AWAY from target (opposite direction)
				float dx = target->pos.x - actor->pos.x;
				float dy = target->pos.y - actor->pos.y;
				float angleAwayFromTarget = atan2(-dx, -dy);
				mount->rot.z = angleAwayFromTarget;
				
				// Stop the rider's combat
				StopActorCombatAlarm(actor);
				
				// Clear the follow tracking
				ClearNPCFollowPlayer(actor);
				
				continue;
			}
			
			g_followingNPCs[i].lastFollowUpdateTime = currentTime;
			g_followingNPCs[i].reinforceCount++;
			
			// ============================================
			// STATIONARY RAPID FIRE CHECK (Ranged Only)
			// When active, horse STOPS MOVING but still ROTATES to face target
			// ============================================
			
			if (IsBowEquipped(actor))
			{
				// Check if already in rapid fire
				if (IsInStationaryRapidFire(actor->formID))
				{
					// Update rapid fire (fires shots, checks duration)
					bool stillActive = UpdateStationaryRapidFire(actor, mount.get(), target);
					
					if (stillActive)
					{
						// Horse is STATIONARY but ROTATES to face target
						// Faster/smoother rotation while drawing bow
						float dx = target->pos.x - mount->pos.x;
						float dy = target->pos.y - mount->pos.y;
						float angleToTarget = atan2(dx, dy);
						
						float currentAngle = mount->rot.z;
						float angleDiff = angleToTarget - currentAngle;
						while (angleDiff > 3.14159f) angleDiff -= 6.28318f;
						while (angleDiff < -3.14159f) angleDiff += 6.28318f;
						
						if (fabs(angleDiff) > 0.03f)  // Tighter threshold for smoother stop
						{
							// Faster rotation while drawing (0.25), normal speed otherwise (0.15)
							bool isDrawing = IsRapidFireDrawing(actor->formID);
							float rotationSpeed = isDrawing ? 0.25f : 0.15f;
							
							float newAngle = currentAngle + (angleDiff * rotationSpeed);
							while (newAngle > 3.14159f) newAngle -= 6.28318f;
							while (newAngle < -3.14159f) newAngle += 6.28318f;
							mount->rot.z = newAngle;
						}
						
						// Skip follow package injection - horse stays put
						continue;
					}
					else
					{
						// Rapid fire just ended - RESUME FOLLOWING
						_MESSAGE("CombatStyles: Rider %08X rapid fire ENDED - resuming follow!", actor->formID);
						
						// 20% chance to play rear up animation before resuming
						if ((rand() % 100) < 20)
						{
							PlayHorseRearUpAnimation(mount.get());
							_MESSAGE("CombatStyles: Horse %08X REAR UP after rapid fire!", mount->formID);
						}
						
						// Re-inject follow package to get horse moving again
						ForceHorseCombatWithPlayer(mount.get());
						InjectFollowPackage(actor, target);
					}
				}
			}
			
			// ============================================
			// NORMAL FOLLOW BEHAVIOR
			// ============================================
			
			// Re-inject the travel package to update destination
			int attackState = 0;
			InjectFollowPackage(actor, target, &attackState);
			
			// Update attack position tracking
			bool wasInAttackPosition = g_followingNPCs[i].inAttackPosition;
			g_followingNPCs[i].inMeleeRange = (attackState >= 1);
			g_followingNPCs[i].inAttackPosition = (attackState == 2);
			
			// Only log when state changes
			if (attackState == 2 && !wasInAttackPosition)
			{
				_MESSAGE("CombatStyles: NPC %08X IN ATTACK POSITION", actor->formID);
			}
			
			// Ensure they stay aggressive
			actor->flags2 |= Actor::kFlag_kAttackOnSight;
		}
	}
	
	// Called from MountedCombat.cpp's update loop
	void UpdateCombatStylesSystem()
	{
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
			
			// Wait for weapon draw delay, then draw weapon and start following
			if (!npcData->weaponDrawn)
			{
				if ((currentTime - npcData->combatStartTime) >= WEAPON_DRAW_DELAY)
				{
					if (HasWeaponAvailable(actor))
					{
						SetWeaponDrawn(actor, true);
						npcData->weaponDrawn = true;
						npcData->weaponInfo = GetWeaponInfo(actor);
						_MESSAGE("CombatStyles: NPC %08X drew weapon - INJECTING FOLLOW PACKAGE", actor->formID);
						
						// Start the follow behavior
						SetNPCFollowPlayer(actor);
					}
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
	
	namespace HunterCombat
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
	// Mounted Attack Hit Detection Functions
	// (Uses early-declared g_hitData and MountedAttackHitData)
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
		if (!rider) return 10.0f;
		
		TESForm* equippedWeapon = rider->GetEquippedObject(false);
		if (!equippedWeapon) return 10.0f;
		
		TESObjectWEAP* weapon = DYNAMIC_CAST(equippedWeapon, TESForm, TESObjectWEAP);
		if (!weapon) return 10.0f;
		
		return (float)weapon->damage.GetAttackDamage();
	}
	
	// Apply damage to the target actor - INSTANT
	void ApplyMountedAttackDamage(Actor* rider, Actor* target, bool isPowerAttack)
	{
		if (!rider || !target) return;
		
		float baseDamage = GetRiderWeaponDamage(rider);
		
		const float POWER_ATTACK_BONUS = 5.0f;
		if (isPowerAttack)
		{
			baseDamage += POWER_ATTACK_BONUS;
		}
		
		target->actorValueOwner.RestoreActorValue(Actor::kDamage, AV_Health, -baseDamage);
		
		const char* riderName = CALL_MEMBER_FN(rider, GetReferenceName)();
		const char* targetName = CALL_MEMBER_FN(target, GetReferenceName)();
		
		_MESSAGE("CombatStyles: %s dealt %.1f damage to %s%s", 
			riderName ? riderName : "Rider",
			baseDamage,
			targetName ? targetName : "Target",
			isPowerAttack ? " (POWER ATTACK!)" : "");
	}
	
	// Called during attack animation to check for hits
	bool UpdateMountedAttackHitDetection(Actor* rider, Actor* target)
	{
		if (!rider || !target) return false;
		
		MountedAttackHitData* hitData = GetOrCreateHitData(rider->formID);
		if (!hitData) return false;
		
		if (hitData->hitRegistered) return false;
		
		float distance = 0;
		bool inRange = CheckMountedAttackHit(rider, target, &distance);
		
		if (inRange)
		{
			hitData->hitRegistered = true;
			ApplyMountedAttackDamage(rider, target, hitData->isPowerAttack);
			
			_MESSAGE("CombatStyles: MOUNTED HIT! Rider %08X hit target at distance %.1f units",
				rider->formID, distance);
			
			return true;
		}
		
		return false;
	}
}
