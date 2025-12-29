#include "DynamicPackages.h"
#include "CombatStyles.h"
#include "WeaponDetection.h"
#include "SingleMountedCombat.h"
#include "ArrowSystem.h"
#include "MultiMountedCombat.h"
#include "SpecialMovesets.h"
#include "AILogging.h"
#include "skse64/GameRTTI.h"
#include "skse64/GameData.h"
#include <cmath>
#include <cstdlib>
#include <ctime>

namespace MountedNPCCombatVR
{
	// ============================================
	// ADDRESS DEFINITIONS (Skyrim VR 1.4.15)
	// ============================================

	// Package Creation
	RelocAddr<_CreatePackageByType>   CreatePackageByType(0x444410);
	RelocAddr<_PackageLocation_CTOR>  PackageLocation_CTOR(0x450C80);
	RelocAddr<_PackageLocation_SetNearReference> PackageLocation_SetNearReference(0x450FA0);
	RelocAddr<_TESPackage_SetPackageLocation>      TESPackage_SetPackageLocation(0x445510);
	RelocAddr<_PackageTarget_CTOR>      PackageTarget_CTOR(0x452E70);
	RelocAddr<_TESPackage_SetPackageTarget>        TESPackage_SetPackageTarget(0x4459B0);
	RelocAddr<_PackageTarget_ResetValueByTargetType> PackageTarget_ResetValueByTargetType(0x4531E0);
	RelocAddr<_PackageTarget_SetFromReference>     PackageTarget_SetFromReference(0x453250);
	RelocAddr<_TESPackage_sub_140439BE0>           TESPackage_sub_140439BE0(0x449730);
	RelocAddr<_TESPackage_CopyFlagsFromOtherPackage> TESPackage_CopyFlagsFromOtherPackage(0x4447E0);

	// Package Evaluation & AI Control
	RelocAddr<_Actor_EvaluatePackage>     Actor_EvaluatePackage(0x5E3990);
	RelocAddr<_Actor_GetBumped>           Actor_GetBumped(0x5E4B70);
	RelocAddr<_Actor_HasLargeMovementDelta>   Actor_HasLargeMovementDelta(0x6116C0);
	RelocAddr<_Actor_sub_140600400>      Actor_sub_140600400(0x608C10);

	// Bump System
	RelocAddr<_ActorProcess_SetBumpState>          ActorProcess_SetBumpState(0x661A10);
	RelocAddr<_ActorProcess_SetBumpDirection>      ActorProcess_SetBumpDirection(0x664C00);
	RelocAddr<_ActorProcess_ResetBumpWaitTimer>    ActorProcess_ResetBumpWaitTimer(0x661A50);
	RelocAddr<_sub_140654E10>        sub_140654E10(0x654E10);
	RelocAddr<_ActorProcess_PlayIdle>          ActorProcess_PlayIdle(0x654490);
	RelocAddr<_ActorProcess_SetPlayerActionReaction> ActorProcess_SetPlayerActionReaction(0x664870);

	// Keep Offset System (NPC Follow)
	RelocAddr<_Actor_KeepOffsetFromActor>       Actor_KeepOffsetFromActor(0x60C1A0);
	RelocAddr<_Actor_ClearKeepOffsetFromActor>  Actor_ClearKeepOffsetFromActor(0x60C2D0);

	// Dialogue Control
	RelocAddr<_ActorProcess_TriggerDialogue> ActorProcess_TriggerDialogue(0x6580B0);
	RelocAddr<_Actor_IsGhost>          Actor_IsGhost(0x5DAAE0);

	// ============================================
	// SYSTEM STATE
	// ============================================

	static bool g_dynamicPackageSystemInitialized = false;
	
	// ============================================
	// FAILSAFE: Stuck Detection
	// ============================================
	
	struct HorseMovementData
	{
		UInt32 horseFormID;
		NiPoint3 lastPosition;
		float lastMoveTime;     // Last time horse moved significantly
		float stuckCheckTime;     // When we last checked for stuck
		float lastResetTime;      // When we last reset this horse (to prevent rapid resets)
		bool isValid;
	};
	
	static HorseMovementData g_horseMovement[5];
	static int g_horseMovementCount = 0;
	
	const float STUCK_THRESHOLD_DISTANCE = 10.0f;   // Must move at least 10 units
	const float STUCK_TIMEOUT = 5.0f;      // If no movement for 5 seconds, reset
	const float STUCK_CHECK_INTERVAL = 0.5f;        // Check every 500ms
	const float RESET_COOLDOWN = 10.0f;    // Don't reset same horse more than once per 10 seconds
	
	HorseMovementData* GetOrCreateMovementData(UInt32 horseFormID)
	{
		for (int i = 0; i < g_horseMovementCount; i++)
		{
			if (g_horseMovement[i].isValid && g_horseMovement[i].horseFormID == horseFormID)
			{
				return &g_horseMovement[i];
			}
		}
		
		if (g_horseMovementCount < 5)
		{
			HorseMovementData* data = &g_horseMovement[g_horseMovementCount];
			data->horseFormID = horseFormID;
			data->lastPosition = NiPoint3();
			data->lastMoveTime = 0;
			data->stuckCheckTime = 0;
			data->lastResetTime = -RESET_COOLDOWN;  // Allow immediate first reset
			data->isValid = true;
			g_horseMovementCount++;
			return data;
		}
		
		return nullptr;
	}
	
	float GetMovementTime()
	{
		static auto startTime = clock();
		return (float)(clock() - startTime) / CLOCKS_PER_SEC;
	}
	
	// Returns true if horse is stuck and needs reset
	bool CheckHorseStuck(Actor* horse, float distanceToTarget, float meleeRange)
	{
		if (!horse) return false;
		
		// Safety check - verify horse has valid process manager
		if (!horse->processManager || !horse->processManager->middleProcess)
		{
			return false;
		}
		
		HorseMovementData* data = GetOrCreateMovementData(horse->formID);
		if (!data) return false;
		
		float currentTime = GetMovementTime();
		
		// Rate limit checks
		if ((currentTime - data->stuckCheckTime) < STUCK_CHECK_INTERVAL)
		{
			return false;
		}
		data->stuckCheckTime = currentTime;
		
		// Check reset cooldown - don't reset same horse too frequently
		if ((currentTime - data->lastResetTime) < RESET_COOLDOWN)
		{
			return false;
		}
	
		// Calculate distance moved since last check
		float dx = horse->pos.x - data->lastPosition.x;
		float dy = horse->pos.y - data->lastPosition.y;
		float distanceMoved = sqrt(dx * dx + dy * dy);
		
		// If horse moved enough, update position and reset timer
		if (distanceMoved > STUCK_THRESHOLD_DISTANCE)
		{
			data->lastPosition = horse->pos;
			data->lastMoveTime = currentTime;
			return false;
		}
		
		// If in melee range, being stationary is expected - don't trigger stuck
		if (distanceToTarget < meleeRange + 50.0f)
		{
			data->lastMoveTime = currentTime;  // Reset timer when in attack position
			return false;
		}
		
		// Check if stuck for too long
		if (data->lastMoveTime > 0 && (currentTime - data->lastMoveTime) > STUCK_TIMEOUT)
		{
			_MESSAGE("DynamicPackages: Horse %08X STUCK for %.1f seconds - forcing reset!", 
				horse->formID, currentTime - data->lastMoveTime);
			
			// Reset the timer and mark reset time
			data->lastMoveTime = currentTime;
			data->lastPosition = horse->pos;
			data->lastResetTime = currentTime;
			
			return true;
		}
		
		// Initialize if first check
		if (data->lastMoveTime == 0)
		{
			data->lastPosition = horse->pos;
			data->lastMoveTime = currentTime;
		}
		
		return false;
	}
	
	void ResetHorseToDefaultBehavior(Actor* horse, Actor* target)
	{
		if (!horse || !target) return;
		
		// Safety checks - verify actors are valid and have required components
	  if (!horse->processManager)
		{
			_MESSAGE("DynamicPackages: Cannot reset horse %08X - no process manager", horse->formID);
			return;
		}
		
		if (!horse->processManager->middleProcess)
		{
			_MESSAGE("DynamicPackages: Cannot reset horse %08X - no middle process", horse->formID);
			return;
		}

		// Check if horse is dead or disabled
		if (horse->IsDead(1))
		{
			_MESSAGE("DynamicPackages: Cannot reset horse %08X - horse is dead", horse->formID);
			return;
		}
		
		// Check if target is valid
		if (target->IsDead(1))
		{
			_MESSAGE("DynamicPackages: Cannot reset horse %08X - target is dead", horse->formID);
			return;
		}
		
		_MESSAGE("DynamicPackages: Resetting horse %08X to default follow behavior toward target %08X", 
			horse->formID, target->formID);
		
		// Clear all special moveset data (turn direction, charge, etc.)
		ClearAllMovesetData(horse->formID);

		// Only clear keep offset if we have a valid state
		// This is safer than blindly clearing
		Actor_ClearKeepOffsetFromActor(horse);
		
		// Small delay before re-evaluation (let the clear take effect)
		// Note: We can't actually delay here, but we can skip the immediate re-apply
		// and let the next update cycle handle it
		
		// Force AI re-evaluation
		Actor_EvaluatePackage(horse, false, false);
		
		// Re-apply follow behavior to the ACTUAL target (not player!)
		// Only if target is still valid
		if (!target->IsDead(1))
		{
			ForceHorseCombatWithTarget(horse, target);
		}
	}

	// ============================================
	// INITIALIZATION
	// ============================================

	bool InitDynamicPackageSystem()
	{
		if (g_dynamicPackageSystemInitialized)
			return true;

		_MESSAGE("DynamicPackages: Initializing dynamic package system...");

		if (!CreatePackageByType.GetUIntPtr())
		{
			_MESSAGE("DynamicPackages: ERROR - CreatePackageByType address invalid!");
			return false;
		}

		// Initialize combat subsystems
		InitSingleMountedCombat();
		InitMultiMountedCombat();

		g_dynamicPackageSystemInitialized = true;
		_MESSAGE("DynamicPackages: System initialized successfully");
		return true;
	}

	// ============================================
	// FORWARD DECLARATIONS
	// ============================================

	// NOTE: Single-argument overloads removed - they defaulted to player, causing targeting bugs
	// Always use ForceHorseCombatWithTarget(horse, target) with explicit target
	bool ForceHorseCombatWithTarget(Actor* horse, Actor* target);

	// ============================================
	// INJECT FOLLOW PACKAGE
	// ============================================

	bool InjectFollowPackage(Actor* actor, Actor* target, int* outAttackState)
	{
		if (outAttackState) *outAttackState = 0;

		if (!actor || !target)
		{
			return false;
		}

		if (Actor_IsGhost(actor))
		{
			return false;
		}

		ActorProcessManager* process = actor->processManager;
		if (!process)
		{
			return false;
		}

		MiddleProcess* middleProcess = process->middleProcess;
		if (!middleProcess)
		{
			return false;
		}
		
		// ============================================
		// CHECK IF THIS RIDER IS IN RANGED ROLE
		// Ranged riders are handled ENTIRELY by ExecuteRangedRoleBehavior
		// Do NOT inject any follow package for them!
		// ============================================
		if (IsRiderInRangedRole(actor->formID))
		{
			// Ranged riders don't get follow packages - return success without doing anything
			if (outAttackState) *outAttackState = 6;  // Ranged role
			return true;
		}

		// Pause any current dialogue
		get_vfunc<_Actor_PauseCurrentDialogue>(actor, 0x4F)(actor);

		// Create package type 1 (Follow)
		TESPackage* package = CreatePackageByType(kPackageType_Follow);
		if (!package)
		{
			_MESSAGE("DynamicPackages: ERROR - Failed to create follow package!");
			return false;
		}

		package->packageFlags |= 6;

		PackageLocation packageLocation;
		PackageLocation_CTOR(&packageLocation);
		PackageLocation_SetNearReference(&packageLocation, actor);
		TESPackage_SetPackageLocation(package, &packageLocation);

		PackageTarget packageTarget;
		PackageTarget_CTOR(&packageTarget);
		TESPackage_SetPackageTarget(package, &packageTarget);
		PackageTarget_ResetValueByTargetType((PackageTarget*)package->unk40, 0);
		PackageTarget_SetFromReference((PackageTarget*)package->unk40, target);

		TESPackage_sub_140439BE0(package, 0);

		if (TESPackage* currentPackage = process->unk18.package)
		{
			TESPackage_CopyFlagsFromOtherPackage(package, currentPackage);
		}

		get_vfunc<_Actor_PutCreatedPackage>(actor, 0xE1)(actor, package, true, 1);

		// Force the HORSE TO FOLLOW TARGET (use actual target, not player!)
		NiPointer<Actor> mount;
		if (CALL_MEMBER_FN(actor, GetMount)(mount) && mount)
		{
			// Double-check: Don't apply follow to ranged role horses
			if (!IsHorseRiderInRangedRole(mount->formID))
			{
				ForceHorseCombatWithTarget(mount.get(), target);
				int attackState = InjectTravelPackageToHorse(mount.get(), target);
				if (outAttackState) *outAttackState = attackState;
			}
			else
			{
				if (outAttackState) *outAttackState = 6;  // Ranged role
			}
		}

		return true;
	}

	// ============================================
	// INJECT BUMP PACKAGE
	// ============================================

	bool InjectBumpPackage(Actor* actor, Actor* bumper, bool isLargeBump, bool pauseDialogue)
	{
		if (!actor || !bumper)
		{
			return false;
		}

		if (Actor_IsGhost(actor))
		{
			return false;
		}

		ActorProcessManager* process = actor->processManager;
		if (!process)
		{
			return false;
		}

		MiddleProcess* middleProcess = process->middleProcess;
		if (!middleProcess)
		{
			return false;
		}

		TESPackage* runOncePackage = middleProcess->unk058.package;
		if (runOncePackage && runOncePackage->type == kPackageType_BumpReaction)
		{
			return false;
		}

		if (Actor_HasLargeMovementDelta(actor))
		{
			ActorProcess_ResetBumpWaitTimer(process);
		}

		Actor_sub_140600400(actor, 1.0f);

		if (pauseDialogue)
		{
			get_vfunc<_Actor_PauseCurrentDialogue>(actor, 0x4F)(actor);
		}

		TESPackage* package = CreatePackageByType(kPackageType_BumpReaction);
		if (!package)
		{
			return false;
		}

		package->packageFlags |= 6;

		PackageLocation packageLocation;
		PackageLocation_CTOR(&packageLocation);
		PackageLocation_SetNearReference(&packageLocation, actor);
		TESPackage_SetPackageLocation(package, &packageLocation);

		PackageTarget packageTarget;
		PackageTarget_CTOR(&packageTarget);
		TESPackage_SetPackageTarget(package, &packageTarget);
		PackageTarget_ResetValueByTargetType((PackageTarget*)package->unk40, 0);
		PackageTarget_SetFromReference((PackageTarget*)package->unk40, bumper);

		TESPackage_sub_140439BE0(package, 0);

		if (TESPackage* currentPackage = process->unk18.package)
		{
			TESPackage_CopyFlagsFromOtherPackage(package, currentPackage);
		}

		get_vfunc<_Actor_PutCreatedPackage>(actor, 0xE1)(actor, package, true, 1);

		if (isLargeBump)
		{
			Actor_sub_140600400(actor, 1.0f);
			sub_140654E10(process, 1);
			ActorProcess_PlayIdle(process, actor, 90, 0, 1, 0, nullptr);
		}

		ActorProcess_SetPlayerActionReaction(process, 0);

		return true;
	}

	// ============================================
	// CLEAR INJECTED PACKAGES
	// ============================================

	bool ClearInjectedPackages(Actor* actor)
	{
		if (!actor)
		{
			return false;
		}

		Actor_EvaluatePackage(actor, false, false);
		return true;
	}

	// ============================================
	// SET NPC KEEP OFFSET FROM TARGET
	// ============================================

	bool SetNPCKeepOffsetFromTarget(Actor* actor, Actor* target, float catchUpRadius, float followRadius)
	{
		if (!actor || !target)
		{
			return false;
		}

		UInt32 targetHandle = target->CreateRefHandle();

		if (targetHandle == 0 || targetHandle == *g_invalidRefHandle)
		{
			return false;
		}

		NiPoint3 offset;
		offset.x = 0;
		offset.y = 0;
		offset.z = 0;

		NiPoint3 offsetAngle;
		offsetAngle.x = 0;
		offsetAngle.y = 0;
		offsetAngle.z = 0;

		Actor_KeepOffsetFromActor(actor, targetHandle, offset, offsetAngle, catchUpRadius, followRadius);
		return true;
	}
	
	// ============================================
	// SET NPC RANGED FOLLOW (500 units from target)
	// ============================================
	// Similar to regular follow but maintains greater distance
	// Used for ranged combat positioning
	// - Faces target when stationary
	// - Faces target when traveling toward them
	// - Faces travel direction when moving away (no backwards walking)
	
	bool SetNPCRangedFollowFromTarget(Actor* actor, Actor* target)
	{
		if (!actor || !target)
		{
			return false;
		}

		UInt32 targetHandle = target->CreateRefHandle();

		if (targetHandle == 0 || targetHandle == *g_invalidRefHandle)
		{
			return false;
		}

		// Offset 500 units away from target
		NiPoint3 offset;
		offset.x = 0;
		offset.y = -500.0f;// 500 units behind/away from target
		offset.z = 0;

		NiPoint3 offsetAngle;
		offsetAngle.x = 0;
		offsetAngle.y = 0;
		offsetAngle.z = 0;

		// catchUpRadius = 800, followRadius = 500 (maintain ~500 unit distance)
		Actor_KeepOffsetFromActor(actor, targetHandle, offset, offsetAngle, 800.0f, 500.0f);
		
		_MESSAGE("DynamicPackages: Set ranged follow for actor %08X (500 units from target %08X)", actor->formID, target->formID);
		return true;
	}
	
	// ============================================
	// CLEAR NPC KEEP OFFSET
	// ============================================

	bool ClearNPCKeepOffset(Actor* actor)
	{
		if (!actor)
		{
			return false;
		}

		Actor_ClearKeepOffsetFromActor(actor);
		Actor_EvaluatePackage(actor, false, false);
		return true;
	}

	// ============================================
	// FORCE HORSE INTO COMBAT WITH TARGET
	// ============================================

	bool ForceHorseCombatWithTarget(Actor* horse, Actor* target)
	{
		if (!horse || !target)
		{
			return false;
		}
		
		// Safety checks - verify actors are in valid state
		if (horse->IsDead(1) || target->IsDead(1))
		{
			return false;
		}
		
		// Verify horse has required components for movement
		if (!horse->processManager || !horse->processManager->middleProcess)
		{
			return false;
		}

		UInt32 targetHandle = target->CreateRefHandle();
		if (targetHandle == 0 || targetHandle == *g_invalidRefHandle)
		{
			return false;
		}

		horse->currentCombatTarget = targetHandle;
		horse->flags2 |= Actor::kFlag_kAttackOnSight;

		// ============================================
		// CHECK IF THIS HORSE'S RIDER IS IN RANGED ROLE
		// If so, don't apply standard follow - ranged behavior handles movement
		// ============================================
		if (IsHorseRiderInRangedRole(horse->formID))
		{
			// Ranged horses are handled by ExecuteRangedRoleBehavior
			// Don't apply any follow package here - just return
			return true;
		}

		// Standard melee follow - close in on target
		NiPoint3 offset;
		offset.x = 200.0f;
		offset.y = -300.0f;
		offset.z = 0;

		NiPoint3 offsetAngle;
		offsetAngle.x = 0;
		offsetAngle.y = 0;
		offsetAngle.z = 0;

		Actor_KeepOffsetFromActor(horse, targetHandle, offset, offsetAngle, 1000.0f, 300.0f);
		Actor_EvaluatePackage(horse, false, false);

		return true;
	}
	
	// ============================================
	// INJECT TRAVEL PACKAGE TO HORSE
	// Main movement logic - delegates to Single/Multi combat as needed
	// Returns: 0 = traveling, 1 = melee range, 2 = attack position, 3 = special maneuver, 4 = charging, 5 = rapid fire, 6 = ranged role
	// ============================================

	int InjectTravelPackageToHorse(Actor* horse, Actor* target)
	{
		if (!horse || !target)
		{
			return 0;
		}
		
		// Safety checks - verify actors are in valid state
		if (horse->IsDead(1) || target->IsDead(1))
		{
			return 0;
		}
		
		ActorProcessManager* process = horse->processManager;
		if (!process || !process->middleProcess)
		{
			return 0;
		}
		
		// Calculate distance to target
		float dx = target->pos.x - horse->pos.x;
		float dy = target->pos.y - horse->pos.y;
		float distanceToTarget = sqrt(dx * dx + dy * dy);
		
		float angleToTarget = atan2(dx, dy);
		float targetAngle = angleToTarget;
		
		float meleeRange = 150.0f;
		
		NiPointer<Actor> targetMount;
		if (CALL_MEMBER_FN(target, GetMount)(targetMount) && targetMount)
		{
			meleeRange = 300.0f;
		}
		
		// ============================================
		// GET RIDER AND REGISTER WITH MULTI-COMBAT SYSTEM
		// ============================================
		NiPointer<Actor> rider;
		if (CALL_MEMBER_FN(horse, GetMountedBy)(rider) && rider)
		{
			// Register rider with multi-combat system (assigns role)
			MultiCombatRole role = RegisterMultiRider(rider.get(), horse, target);
			
			// ============================================
			// CHECK IF THIS RIDER IS IN RANGED ROLE
			// Ranged riders are handled ENTIRELY by ExecuteRangedRoleBehavior
			// We just update distance and return - no rotation here!
			// ============================================
			if (IsHorseRiderInRangedRole(horse->formID))
			{
				// Update distance for the multi-rider data
				MultiRiderData* data = GetMultiRiderDataByHorse(horse->formID);
				if (data)
				{
					data->distanceToTarget = distanceToTarget;
					
					// Execute ranged behavior (handles rotation, bow equip, firing)
					ExecuteRangedRoleBehavior(data, rider.get(), horse, target);
				}
				
				// Return 6 to skip all other behavior
				return 6;
			}
		}
		
		// ============================================
		// FAILSAFE: Sprint stop at close range
		// ============================================
		const float BREATHING_DISTANCE = 200.0f;
		if (distanceToTarget < BREATHING_DISTANCE)
		{
			float currentAngle = horse->rot.z;
			float angleDiff = angleToTarget - currentAngle;
			while (angleDiff > 3.14159f) angleDiff -= 6.28318f;
			while (angleDiff < -3.14159f) angleDiff += 6.28318f;
			
			if (fabs(angleDiff) < 0.785f)
			{
				StopHorseSprint(horse);
			}
		}
		
		// ============================================
		// FAILSAFE: Check if horse is stuck (skip during rapid fire)
		// ============================================
		
		if (!IsInRapidFire(horse->formID) && !IsHorseRiderInRangedRole(horse->formID))
		{
			ObstructionType obstruction = CheckAndLogHorseObstruction(horse, target, distanceToTarget);
			
			if (obstruction == ObstructionType::Stationary ||
				obstruction == ObstructionType::RunningInPlace || 
				obstruction == ObstructionType::CollisionBlocked)
			{
				if (CheckAndLogSheerDrop(horse))
				{
					// Near sheer drop - avoid maneuvers (log only once via AILogging)
				}
				else
				{
					if (TryHorseJumpToEscape(horse))
					{
						// Jump triggered - log in SpecialMovesets
					}
					else
					{
						if (TryHorseTrotTurnFromObstruction(horse))
						{
							// Trot turn triggered - log in SpecialMovesets
						}
					}
				}
			}
			
			if (CheckHorseStuck(horse, distanceToTarget, meleeRange))
			{
				ResetHorseToDefaultBehavior(horse, target);
			}
		}
		
		// ============================================
		// CHECK FOR MULTI MOUNTED COMBAT MANEUVERS
		// ============================================
		
		int mountedRiderCount = GetFollowingNPCCount();
		
		// ============================================
		// WEAPON SWITCHING (for melee role riders only)
		// ============================================
		
		// Re-get rider reference (already declared above, just reuse)
		if (CALL_MEMBER_FN(horse, GetMountedBy)(rider) && rider)
		{
			// Skip weapon switching for ranged role riders - they keep bow
			if (!IsHorseRiderInRangedRole(horse->formID))
			{
				if (!IsHorseCharging(horse->formID) && !IsInRapidFire(horse->formID))
				{
					bool hasMelee = HasMeleeWeaponInInventory(rider.get());
					bool hasBow = HasBowInInventory(rider.get());
					
					if (distanceToTarget <= WeaponSwitchDistance)
					{
						if (hasMelee)
						{
							if (IsBowEquipped(rider.get()))
							{
								ResetBowAttackState(rider->formID);
								EquipBestMeleeWeapon(rider.get());
								rider->DrawSheatheWeapon(true);
							}
						}
						else if (hasBow && !IsBowEquipped(rider.get()))
						{
							EquipBestBow(rider.get());
							rider->DrawSheatheWeapon(true);
						}
					}
					else
					{
						if (hasBow)
						{
							if (IsMeleeEquipped(rider.get()))
							{
								EquipBestBow(rider.get());
								rider->DrawSheatheWeapon(true);
							}
						}
					}
					
					if (IsBowEquipped(rider.get()) && distanceToTarget > WeaponSwitchDistance)
					{
						UpdateBowAttack(rider.get(), true, target);
					}
				}
			}
		}
		
		int attackState = 0;
		
		// ============================================
		// CHECK IF TARGET IS MOUNTED
		// ============================================
		bool targetIsMounted = IsTargetMounted(target);
		
		// ============================================
		// CHARGE MANEUVER CHECK (700-1500 units away)
		// ============================================
		NiPointer<Actor> riderForCharge;
		if (CALL_MEMBER_FN(horse, GetMountedBy)(riderForCharge) && riderForCharge)
		{
			if (IsHorseCharging(horse->formID))
			{
				if (UpdateChargeManeuver(horse, riderForCharge.get(), target, distanceToTarget, meleeRange))
				{
					targetAngle = angleToTarget;
					
					float currentAngle = horse->rot.z;
					float angleDiff = targetAngle - currentAngle;
					while (angleDiff > 3.14159f) angleDiff -= 6.28318f;
					while (angleDiff < -3.14159f) angleDiff += 6.28318f;
					
					const float ROTATION_SPEED = 0.15f;
					float newAngle = currentAngle + (angleDiff * ROTATION_SPEED);
					horse->rot.z = newAngle;
					
					return 4;
				}
			}
			else if (distanceToTarget >= 700.0f && distanceToTarget <= 1500.0f)
			{
				if (TryChargeManeuver(horse, riderForCharge.get(), target, distanceToTarget))
				{
					targetAngle = angleToTarget;
					
					float currentAngle = horse->rot.z;
					float angleDiff = targetAngle - currentAngle;
					while (angleDiff > 3.14159f) angleDiff -= 6.28318f;
					while (angleDiff < -3.14159f) angleDiff += 6.28318f;
					
					const float ROTATION_SPEED = 0.15f;
					float newAngle = currentAngle + (angleDiff * ROTATION_SPEED);
					horse->rot.z = newAngle;
					
					return 4;
				}
			}
			
			// ============================================
			// RAPID FIRE MANEUVER CHECK
			// ============================================
			if (IsInRapidFire(horse->formID))
			{
				if (UpdateRapidFireManeuver(horse, riderForCharge.get(), target))
				{
					targetAngle = angleToTarget;
					
					float currentAngle = horse->rot.z;
					float angleDiff = targetAngle - currentAngle;
					while (angleDiff > 3.14159f) angleDiff -= 6.28318f;
					while (angleDiff < -3.14159f) angleDiff += 6.28318f;
					
					const float ROTATION_SPEED = 0.15f;
					float newAngle = currentAngle + (angleDiff * ROTATION_SPEED);
					horse->rot.z = newAngle;
					
					return 5;
				}
			}
			else if (distanceToTarget > meleeRange && GetCombatElapsedTime() >= 20.0f)
			{
				if (TryRapidFireManeuver(horse, riderForCharge.get(), target, distanceToTarget, meleeRange))
				{
					targetAngle = angleToTarget;
					
					float currentAngle = horse->rot.z;
					float angleDiff = targetAngle - currentAngle;
					while (angleDiff > 3.14159f) angleDiff -= 6.28318f;
					while (angleDiff < -3.14159f) angleDiff += 6.28318f;
					
					const float ROTATION_SPEED = 0.15f;
					float newAngle = currentAngle + (angleDiff * ROTATION_SPEED);
					horse->rot.z = newAngle;
					
					return 5;
				}
			}
		}
		
		// ============================================
		// DEFAULT BEHAVIOR: Horse faces and follows target
		// ============================================
		
		if (distanceToTarget < meleeRange)
		{
			TryRearUpOnApproach(horse, target, distanceToTarget);
			
			if (targetIsMounted)
			{
				// MOUNTED VS MOUNTED: Adjacent Riding
				targetAngle = GetAdjacentRidingAngle(horse->formID, target->pos, horse->pos, target->rot.z);
				
				attackState = 1;
				
				float currentAngle = horse->rot.z;
				float angleDiff = targetAngle - currentAngle;
				while (angleDiff > 3.14159f) angleDiff -= 6.28318f;
				while (angleDiff < -3.14159f) angleDiff += 6.28318f;
				
				if (fabs(angleDiff) < 0.35f)
				{
					attackState = 2;
					
					float horseRightX = cos(horse->rot.z);
					float horseRightY = -sin(horse->rot.z);
					
					float toTargetX = target->pos.x - horse->pos.x;
					float toTargetY = target->pos.y - horse->pos.y;
					
					float dotRight = (toTargetX * horseRightX) + (toTargetY * horseRightY);
					const char* targetSide = (dotRight > 0) ? "RIGHT" : "LEFT";
					
					NiPointer<Actor> riderForAttack;
					if (CALL_MEMBER_FN(horse, GetMountedBy)(riderForAttack) && riderForAttack)
					{
						PlayMountedAttackAnimation(riderForAttack.get(), targetSide);
						
						if (IsRiderAttacking(riderForAttack.get()))
						{
							if (UpdateMountedAttackHitDetection(riderForAttack.get(), target))
							{
								_MESSAGE("DynamicPackages: Rider %08X hit confirmed!", riderForAttack->formID);
							}
						}
					}
					
					// Removed verbose position logs - only log on state change in CombatStyles
				}
			}
			else
			{
				// MOUNTED VS ON-FOOT: 90-Degree Turn
				targetAngle = Get90DegreeTurnAngle(horse->formID, angleToTarget);
				
				attackState = 1;
				
				float currentAngle = horse->rot.z;
				float angleDiff = targetAngle - currentAngle;
				while (angleDiff > 3.14159f) angleDiff -= 6.28318f;
				while (angleDiff < -3.14159f) angleDiff += 6.28318f;
				
				if (fabs(angleDiff) < 0.26f)
				{
					attackState = 2;
					
					float horseRightX = cos(horse->rot.z);
					float horseRightY = -sin(horse->rot.z);
					
					float toTargetX = target->pos.x - horse->pos.x;
					float toTargetY = target->pos.y - horse->pos.y;
					
					float dotRight = (toTargetX * horseRightX) + (toTargetY * horseRightY);
					const char* targetSide = (dotRight > 0) ? "RIGHT" : "LEFT";
					
					NiPointer<Actor> riderForAttack;
					if (CALL_MEMBER_FN(horse, GetMountedBy)(riderForAttack) && riderForAttack)
					{
						PlayMountedAttackAnimation(riderForAttack.get(), targetSide);
						
						if (IsRiderAttacking(riderForAttack.get()))
						{
							if (UpdateMountedAttackHitDetection(riderForAttack.get(), target))
							{
								_MESSAGE("DynamicPackages: Rider %08X hit confirmed!", riderForAttack->formID);
							}
						}
					}
					
					// Removed verbose attack position logs
				}
			}
		}
		else
		{
			NotifyHorseLeftMeleeRange(horse->formID);
			if (targetIsMounted)
			{
				NotifyHorseLeftAdjacentRange(horse->formID);
			}
			targetAngle = angleToTarget;
		}
		
		// ============================================
		// APPLY ROTATION
		// ============================================
		
		float currentAngle = horse->rot.z;
		float angleDiff = targetAngle - currentAngle;
		
		while (angleDiff > 3.14159f) angleDiff -= 6.28318f;
		while (angleDiff < -3.14159f) angleDiff += 6.28318f;
		
		const float rotationSpeed = 0.15f;
		float newAngle = currentAngle + (angleDiff * rotationSpeed);
		
		while (newAngle > 3.14159f) newAngle -= 6.28318f;
		while (newAngle < -3.14159f) newAngle += 6.28318f;
		
		horse->rot.z = newAngle;
		
		// ============================================
		// MELEE RIDER COLLISION AVOIDANCE
		// Check if another melee rider is too close and steer away
		// ============================================
		if (distanceToTarget < meleeRange * 2.0f)  // Only check when in combat range
		{
			UpdateMeleeRiderCollisionAvoidance(horse, target);
		}
		
		// ============================================
		// CREATE TRAVEL PACKAGE
		// ============================================
		
		if (distanceToTarget >= meleeRange)
		{
			TESPackage* package = CreatePackageByType(6);
			if (package)
			{
				package->packageFlags |= 6;
				
				PackageLocation packageLocation;
				PackageLocation_CTOR(&packageLocation);
				PackageLocation_SetNearReference(&packageLocation, target);
				TESPackage_SetPackageLocation(package, &packageLocation);
				
				PackageTarget packageTarget;
				PackageTarget_CTOR(&packageTarget);
				TESPackage_SetPackageTarget(package, &packageTarget);
				PackageTarget_ResetValueByTargetType((PackageTarget*)package->unk40, 0);
				PackageTarget_SetFromReference((PackageTarget*)package->unk40, target);
				
				TESPackage_sub_140439BE0(package, 0);
				
				if (TESPackage* currentPackage = process->unk18.package)
				{
					TESPackage_CopyFlagsFromOtherPackage(package, currentPackage);
				}
				
				get_vfunc<_Actor_PutCreatedPackage>(horse, 0xE1)(horse, package, true, 1);
			}
		}
		
		return attackState;
	}
}
