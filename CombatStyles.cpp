#include "CombatStyles.h"
#include "DynamicPackages.h"
#include "Helper.h"
#include "SingleMountedCombat.h"
#include "MultiMountedCombat.h"
#include "SpecialMovesets.h"
#include "WeaponDetection.h"
#include "ArrowSystem.h"
#include "AILogging.h"
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
	const float MAX_COMBAT_DISTANCE = 3500.0f;  // If target gets this far, disengage combat
	
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
	// Attack Animation Tracking
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
		
		// Determine if this should be a power attack (5% chance)
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
			else
			{
				idleToPlay = g_idleAttackLeft;
				animName = "LEFT";
				attackType = "normal";
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
	// ============================================
	
	static UInt32 g_controlledMounts[5] = {0};
	static int g_controlledMountCount = 0;
	
	void ReleaseAllMountControl()
	{
		g_controlledMountCount = 0;
		for (int i = 0; i < 5; i++) g_controlledMounts[i] = 0;
	}

	// ============================================
	// Follow Target Tracking
	// ============================================
	
	struct FollowingNPCData
	{
		UInt32 actorFormID;
		UInt32 targetFormID;   // The target this NPC is following/attacking
		bool hasInjectedPackage;
		float lastFollowUpdateTime;
		int reinforceCount;
		bool isValid;
		bool inMeleeRange;          // True when horse is close enough for melee
		bool inAttackPosition;      // True when horse has turned sideways (90 deg)
	};
	
	static FollowingNPCData g_followingNPCs[5];
	static int g_followingNPCCount = 0;
	
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
		
		SetWeaponDrawn(actor, true);
		actor->flags2 |= Actor::kFlag_kAttackOnSight;
		
		InjectFollowPackage(actor, target);
		
		// Add to tracking list
		if (g_followingNPCCount < 5)
		{
			g_followingNPCs[g_followingNPCCount].actorFormID = actor->formID;
			g_followingNPCs[g_followingNPCCount].targetFormID = target->formID;
			g_followingNPCs[g_followingNPCCount].hasInjectedPackage = true;
			g_followingNPCs[g_followingNPCCount].lastFollowUpdateTime = GetCurrentGameTime();
			g_followingNPCs[g_followingNPCCount].reinforceCount = 0;
			g_followingNPCs[g_followingNPCCount].isValid = true;
			g_followingNPCs[g_followingNPCCount].inMeleeRange = false;
			g_followingNPCs[g_followingNPCCount].inAttackPosition = false;
			g_followingNPCCount++;
		}
	}
	
	// REMOVED: Single-argument overload that defaulted to player
	// This was causing guards to target the player instead of hostiles!
	// void SetNPCFollowTarget(Actor* actor) - DO NOT USE
	// Always use SetNPCFollowTarget(actor, target) with explicit target
	
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
			
			ResetBowAttackState(actor->formID);
			ResetRapidFireBowAttack(actor->formID);
			
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
	// Stops following if target gets too far away or rider exits combat
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
			// ============================================
			if (!actor->IsInCombat())
			{
				const char* actorName = CALL_MEMBER_FN(actor, GetReferenceName)();
				_MESSAGE("CombatStyles: Rider '%s' (%08X) exited combat - clearing follow",
					actorName ? actorName : "Unknown", actor->formID);
				
				ClearNPCFollowTarget(actor);
				continue;
			}
			
			// ============================================
			// GET THE TARGET FOR THIS NPC
			// ============================================
			Actor* target = nullptr;
			UInt32 targetFormID = g_followingNPCs[i].targetFormID;
			
			if (targetFormID != 0)
			{
				TESForm* targetForm = LookupFormByID(targetFormID);
				if (targetForm && targetForm->formType == kFormType_Character)
				{
					target = static_cast<Actor*>(targetForm);
					
					if (target->IsDead(1))
					{
						const char* actorName = CALL_MEMBER_FN(actor, GetReferenceName)();
						_MESSAGE("CombatStyles: Target died - NPC '%s' disengaging",
							actorName ? actorName : "Unknown");
						ClearNPCFollowTarget(actor);
						continue;
					}
				}
				else
				{
					const char* actorName = CALL_MEMBER_FN(actor, GetReferenceName)();
					_MESSAGE("CombatStyles: Target invalid - NPC '%s' disengaging",
						actorName ? actorName : "Unknown");
					ClearNPCFollowTarget(actor);
					continue;
				}
			}
			else
			{
				if (g_thePlayer && (*g_thePlayer))
				{
					target = *g_thePlayer;
					g_followingNPCs[i].targetFormID = target->formID;
				}
				else
				{
					continue;
				}
			}
			
			// ============================================
			// CHECK DISTANCE - DISENGAGE IF TOO FAR (3500 units)
			// ============================================
			float dx = target->pos.x - actor->pos.x;
			float dy = target->pos.y - actor->pos.y;
			float distanceToTarget = sqrt(dx * dx + dy * dy);
			
			if (distanceToTarget > MAX_COMBAT_DISTANCE)
			{
				const char* actorName = CALL_MEMBER_FN(actor, GetReferenceName)();
				_MESSAGE("CombatStyles: Target too far (%.0f) - NPC '%s' disengaging",
					distanceToTarget, actorName ? actorName : "Unknown");
				
				float angleAwayFromTarget = atan2(-dx, -dy);
				mount->rot.z = angleAwayFromTarget;
				
				StopActorCombatAlarm(actor);
				ClearNPCFollowTarget(actor);
				continue;
			}
			
			g_followingNPCs[i].lastFollowUpdateTime = currentTime;
			g_followingNPCs[i].reinforceCount++;
			
			int attackState = 0;
			InjectFollowPackage(actor, target, &attackState);
			
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
					// ============================================
					// CHECK IF THIS IS A CAPTAIN - SKIP MELEE WEAPON LOGIC
					// Captains are forced to RANGED role in MultiMountedCombat
					// ============================================
					const char* actorName = CALL_MEMBER_FN(actor, GetReferenceName)();
					bool isCaptain = false;
					if (actorName && strstr(actorName, "Captain") != nullptr)
					{
						isCaptain = true;
						_MESSAGE("CombatStyles: '%s' is a CAPTAIN - skipping melee weapon setup (will use bow)", actorName);
					}
					
					bool hasWeapon = HasWeaponAvailable(actor);
					bool hasMeleeInInventory = HasMeleeWeaponInInventory(actor);
					
					// If no weapon equipped but has melee in inventory, equip it (unless Captain)
					if (!isCaptain && !hasWeapon && hasMeleeInInventory)
					{
						_MESSAGE("CombatStyles: NPC '%s' (%08X) has melee in inventory - equipping it", 
							actorName ? actorName : "Unknown", actor->formID);
						EquipBestMeleeWeapon(actor);
						hasWeapon = HasWeaponAvailable(actor);
					}
					
					// If still no weapon, give them an Iron Mace (unless Captain)
					if (!isCaptain && !hasWeapon && !hasMeleeInInventory)
					{
						_MESSAGE("CombatStyles: NPC '%s' (%08X) has NO suitable weapons - giving Iron Mace", 
							actorName ? actorName : "Unknown", actor->formID);
						
						if (GiveDefaultMountedWeapon(actor))
						{
							hasWeapon = HasWeaponAvailable(actor);
						}
					}
					
					// For Captains, let MultiMountedCombat handle weapon setup
					if (isCaptain)
					{
						hasWeapon = true;  // Skip weapon check - bow will be equipped by multi-combat
					}
					
					if (hasWeapon || isCaptain)
					{
						SetWeaponDrawn(actor, true);
						npcData->weaponDrawn = true;
						npcData->weaponInfo = GetWeaponInfo(actor);
						
						// Start the follow behavior - USE THE ACTUAL TARGET, not player!
						if (target)
						{
							_MESSAGE("CombatStyles: NPC %08X drew weapon - following target %08X", 
								actor->formID, target->formID);
							SetNPCFollowTarget(actor, target);
						}
						else
						{
							// No target? Log this problem
							_MESSAGE("CombatStyles: NPC %08X drew weapon but has NO TARGET!", actor->formID);
						}
					}
					else
					{
						// Still no weapon even after trying to give one - set up follow anyway
						_MESSAGE("CombatStyles: NPC '%s' (%08X) UNARMED (couldn't give weapon) - setting up follow anyway", 
							actorName ? actorName : "Unknown", actor->formID);
						npcData->weaponDrawn = true;  // Mark as done so we don't keep trying
						
						if (target)
						{
							SetNPCFollowTarget(actor, target);
						}
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
	// Mounted Attack Hit Detection
	// Called during attack animation to check if weapon hits target
	// ============================================
	
	// Actor Value IDs
	static const UInt32 AV_Health = 24;
	
	struct MountedAttackHitData
	{
		UInt32 riderFormID;
		bool hitRegistered;// Already dealt damage this attack
		bool isPowerAttack;      // Was this a power attack?
		float attackStartTime;
		bool isValid;
	};
	
	static MountedAttackHitData g_hitData[5];
	static int g_hitDataCount = 0;
	
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
		
		// Keep damage log but make it concise
		_MESSAGE("CombatStyles: %s hit %s for %.0f dmg%s", 
			riderName ? riderName : "Rider",
			targetName ? targetName : "Target",
			baseDamage,
			isPowerAttack ? " (POWER)" : "");
	}
	
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
			// Removed verbose "MOUNTED HIT!" log - damage log is sufficient
			return true;
		}
		
		return false;
	}
}
