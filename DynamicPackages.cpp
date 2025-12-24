#include "DynamicPackages.h"
#include "CombatStyles.h"
#include "WeaponDetection.h"
#include "SingleMountedCombat.h"
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
		bool isValid;
	};
	
	static HorseMovementData g_horseMovement[5];
	static int g_horseMovementCount = 0;
	
	const float STUCK_THRESHOLD_DISTANCE = 10.0f;   // Must move at least 10 units
	const float STUCK_TIMEOUT = 5.0f;      // If no movement for 5 seconds, reset
	const float STUCK_CHECK_INTERVAL = 0.5f;        // Check every 500ms
	
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
	bool CheckHorseStuck(Actor* horse, float distanceToPlayer, float meleeRange)
	{
		if (!horse) return false;
		
		HorseMovementData* data = GetOrCreateMovementData(horse->formID);
		if (!data) return false;
		
		float currentTime = GetMovementTime();
		
		// Rate limit checks
		if ((currentTime - data->stuckCheckTime) < STUCK_CHECK_INTERVAL)
		{
			return false;
		}
		data->stuckCheckTime = currentTime;
		
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
		if (distanceToPlayer < meleeRange + 50.0f)
		{
			data->lastMoveTime = currentTime;  // Reset timer when in attack position
			return false;
		}
		
		// Check if stuck for too long
		if (data->lastMoveTime > 0 && (currentTime - data->lastMoveTime) > STUCK_TIMEOUT)
		{
			_MESSAGE("DynamicPackages: Horse %08X STUCK for %.1f seconds - forcing reset!", 
				horse->formID, currentTime - data->lastMoveTime);
			
			// Reset the timer to avoid spam
			data->lastMoveTime = currentTime;
			data->lastPosition = horse->pos;
			
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
		
		_MESSAGE("DynamicPackages: Resetting horse %08X to default follow behavior", horse->formID);
		
		// Clear turn direction so it picks fresh on next melee approach
		ClearHorseTurnDirection(horse->formID);
		
		// Clear any keep offset
		Actor_ClearKeepOffsetFromActor(horse);
		
		// Force AI re-evaluation
		Actor_EvaluatePackage(horse, false, false);
		
		// Re-apply follow behavior
		ForceHorseCombatWithPlayer(horse);
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

	bool ForceHorseCombatWithPlayer(Actor* horse);

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

		// Force the HORSE TO FOLLOW PLAYER
		NiPointer<Actor> mount;
		if (CALL_MEMBER_FN(actor, GetMount)(mount) && mount)
		{
			ForceHorseCombatWithPlayer(mount.get());
			int attackState = InjectTravelPackageToHorse(mount.get(), target);
			if (outAttackState) *outAttackState = attackState;
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
	// SET NPC KEEP OFFSET FROM PLAYER
	// ============================================

	bool SetNPCKeepOffsetFromPlayer(Actor* actor, float catchUpRadius, float followRadius)
	{
		if (!actor || !g_thePlayer || !(*g_thePlayer))
		{
			return false;
		}

		Actor* player = *g_thePlayer;
		UInt32 playerHandle = player->CreateRefHandle();

		if (playerHandle == 0 || playerHandle == *g_invalidRefHandle)
		{
			return false;
		}

		NiPoint3 offset;
		offset.x = 100.0f;
		offset.y = -150.0f;
		offset.z = 0;

		NiPoint3 offsetAngle;
		offsetAngle.x = 0;
		offsetAngle.y = 0;
		offsetAngle.z = 0;

		Actor_KeepOffsetFromActor(actor, playerHandle, offset, offsetAngle, catchUpRadius, followRadius);
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
	// FORCE HORSE INTO COMBAT WITH PLAYER
	// ============================================

	bool ForceHorseCombatWithPlayer(Actor* horse)
	{
		if (!horse || !g_thePlayer || !(*g_thePlayer))
		{
			return false;
		}

		Actor* player = *g_thePlayer;

		UInt32 playerHandle = player->CreateRefHandle();
		if (playerHandle != 0 && playerHandle != *g_invalidRefHandle)
		{
			horse->currentCombatTarget = playerHandle;
		}

		horse->flags2 |= Actor::kFlag_kAttackOnSight;

		NiPoint3 offset;
		offset.x = 200.0f;
		offset.y = -300.0f;
		offset.z = 0;

		NiPoint3 offsetAngle;
		offsetAngle.x = 0;
		offsetAngle.y = 0;
		offsetAngle.z = 0;

		Actor_KeepOffsetFromActor(horse, playerHandle, offset, offsetAngle, 1000.0f, 300.0f);
		Actor_EvaluatePackage(horse, false, false);

		return true;
	}

	// ============================================
	// INJECT TRAVEL PACKAGE TO HORSE
	// Main movement logic - delegates to Single/Multi combat as needed
	// Returns: 0 = traveling, 1 = melee range, 2 = attack position, 3 = special maneuver
	// ============================================

	int InjectTravelPackageToHorse(Actor* horse, Actor* target)
	{
		if (!horse || !target)
		{
			return 0;
		}
		
		ActorProcessManager* process = horse->processManager;
		if (!process || !process->middleProcess)
		{
			return 0;
		}
		
		// ============================================
		// CHECK IF RIDER IS IN RAPID FIRE MODE
		// If so, horse ROTATES to face target but does NOT move
		// Faster/smoother rotation while drawing bow
		// ============================================
		
		NiPointer<Actor> riderCheck;
		if (CALL_MEMBER_FN(horse, GetMountedBy)(riderCheck) && riderCheck)
		{
			if (IsInStationaryRapidFire(riderCheck->formID))
			{
				// Horse ROTATES to face target
				float dx = target->pos.x - horse->pos.x;
				float dy = target->pos.y - horse->pos.y;
				float angleToTarget = atan2(dx, dy);
				
				float currentAngle = horse->rot.z;
				float angleDiff = angleToTarget - currentAngle;
				while (angleDiff > 3.14159f) angleDiff -= 6.28318f;
				while (angleDiff < -3.14159f) angleDiff += 6.28318f;
				
				if (fabs(angleDiff) > 0.03f)  // Tighter threshold for smoother stop
				{
					// Faster rotation while drawing (0.25), normal speed otherwise (0.15)
					bool isDrawing = IsRapidFireDrawing(riderCheck->formID);
					float rotationSpeed = isDrawing ? 0.25f : 0.15f;
					
					float newAngle = currentAngle + (angleDiff * rotationSpeed);
					while (newAngle > 3.14159f) newAngle -= 6.28318f;
					while (newAngle < -3.14159f) newAngle += 6.28318f;
					horse->rot.z = newAngle;
				}
				
				// Return - no travel package, horse stays stationary but rotates
				return 4;
			}
		}
		
		// Calculate distance to player
		float dx = target->pos.x - horse->pos.x;
		float dy = target->pos.y - horse->pos.y;
		float distanceToPlayer = sqrt(dx * dx + dy * dy);
		
		float angleToPlayer = atan2(dx, dy);
		float targetAngle = angleToPlayer;  // Default: face player
		
		// Check if player is mounted - adjust melee range
		float meleeRange = 150.0f;
		
		if (g_thePlayer && (*g_thePlayer))
		{
			Actor* player = *g_thePlayer;
			NiPointer<Actor> playerMount;
			if (CALL_MEMBER_FN(player, GetMount)(playerMount) && playerMount)
			{
				meleeRange = 300.0f;
			}
		}
		
		// ============================================
		// FAILSAFE: If horse is very close and facing player, ensure sprint is stopped
		// This catches any edge cases where sprint might bleed through
		// ============================================
		const float BREATHING_DISTANCE = 200.0f;
		if (distanceToPlayer < BREATHING_DISTANCE)
		{
			// Check if horse is roughly facing the player
			float currentAngle = horse->rot.z;
			float angleDiff = angleToPlayer - currentAngle;
			while (angleDiff > 3.14159f) angleDiff -= 6.28318f;
			while (angleDiff < -3.14159f) angleDiff += 6.28318f;
			
			// If within ~45 degrees of facing player and very close, force sprint stop
			if (fabs(angleDiff) < 0.785f)  // ~45 degrees
			{
				// Call sprint stop as failsafe - this is safe to call even if not sprinting
				StopHorseSprint(horse);
			}
		}
		
		// ============================================
		// FAILSAFE: CHECK IF HORSE IS STUCK
		// ============================================
		
		// Check for obstruction and log details
		ObstructionType obstruction = CheckAndLogHorseObstruction(horse, target, distanceToPlayer);
		
		// If obstructed, try horse jump to escape (has4 second cooldown)
		if (obstruction == ObstructionType::Stationary ||
			obstruction == ObstructionType::RunningInPlace || 
			obstruction == ObstructionType::CollisionBlocked)
		{
			// Check for sheer drop first - if near sheer, log and avoid jump/turn maneuvers
			if (CheckAndLogSheerDrop(horse))
			{
				_MESSAGE("DynamicPackages: Horse %08X near SHEER DROP - avoiding jump/turn maneuvers", horse->formID);
				// don't attempt jump or trot here
			}
			else
			{
				// First try to jump over the obstruction
				if (TryHorseJumpToEscape(horse))
				{
					_MESSAGE("DynamicPackages: Horse %08X attempting jump to escape obstruction", horse->formID);
				}
				else
				{
					// Jump on cooldown or failed - try trot turn to avoid obstruction
					// This uses the detected side to turn AWAY from the blocked direction
					if (TryHorseTrotTurnFromObstruction(horse))
					{
						_MESSAGE("DynamicPackages: Horse %08X attempting trot turn to avoid obstruction", horse->formID);
					}
				}
			}
		}
		
		// Legacy stuck check (backup)
		if (CheckHorseStuck(horse, distanceToPlayer, meleeRange))
		{
			ResetHorseToDefaultBehavior(horse, target);
			// Continue with normal behavior after reset
		}
		
		// ============================================
		// CHECK FOR MULTI MOUNTED COMBAT MANEUVERS
		// Multi mounted combat (2+) - coordinated movement
		// ============================================
		
		int mountedRiderCount = GetFollowingNPCCount();
		
		if (mountedRiderCount >= 2)
		{
			// Multi mounted combat - coordinated movement
			if (UpdateMultiMountedCombat(horse, target, distanceToPlayer, meleeRange))
			{
				return 3;  // Multi-combat handling movement
			}
		}
		
		// ============================================
		// WEAPON SWITCHING
		// Close range (< 230): Use melee weapon (if available, no daggers)
		// Far range (> 230): Use bow if available
		// If no valid melee weapon, use bow at all ranges
		// ============================================
		
		const float WEAPON_SWITCH_DISTANCE = 230.0f;
		
		NiPointer<Actor> rider;
		if (CALL_MEMBER_FN(horse, GetMountedBy)(rider) && rider)
		{
			bool hasMelee = HasMeleeWeaponInInventory(rider.get());
			bool hasBow = HasBowInInventory(rider.get());
			
			if (distanceToPlayer <= WEAPON_SWITCH_DISTANCE)
			{
				// Close range - prefer melee if available
				if (hasMelee)
				{
					if (IsBowEquipped(rider.get()))
					{
						// Reset bow attack state when switching to melee
						ResetBowAttackState(rider->formID);
						EquipBestMeleeWeapon(rider.get());
						rider->DrawSheatheWeapon(true);
					}
				}
				else if (hasBow && !IsBowEquipped(rider.get()))
				{
					// No melee available, use bow even at close range
					EquipBestBow(rider.get());
					rider->DrawSheatheWeapon(true);
				}
			}
			else
			{
				// Far range - prefer bow if available
				if (hasBow)
				{
					if (IsMeleeEquipped(rider.get()))
					{
						EquipBestBow(rider.get());
						rider->DrawSheatheWeapon(true);
					}
				}
			}
			
			// ============================================
			// BOW ATTACK HANDLING (includes rapid fire check)
			// ============================================
			if (IsBowEquipped(rider.get()) && distanceToPlayer > WEAPON_SWITCH_DISTANCE)
			{
				// Check for rapid fire first - it takes priority over normal bow attacks
				if (TryStationaryRapidFire(rider.get(), horse, target, distanceToPlayer))
				{
					// Rapid fire just triggered!
					// COMPLETELY STOP THE HORSE - clear ALL movement
					Actor_ClearKeepOffsetFromActor(horse);
					ClearInjectedPackages(horse);
					Actor_EvaluatePackage(horse, false, false);
					
					_MESSAGE("DynamicPackages: RAPID FIRE - Horse %08X COMPLETELY STOPPED", horse->formID);
					
					// Return immediately - don't create any travel packages
					return 4;
				}
				
				// Normal bow attack (only if not in rapid fire)
				// Pass target so arrows actually fire at something
				if (!IsInStationaryRapidFire(rider->formID))
				{
					UpdateBowAttack(rider.get(), true, target);
				}
			}
		}
		
		int attackState = 0;
		
		// ============================================
		// DEFAULT BEHAVIOR: Horse faces and follows player
		// ============================================
		
		if (distanceToPlayer < meleeRange)
		{
			// ============================================
			// HORSE REAR UP CHECK (7% chance when horse facing player head-on)
			// This happens just before the 90-degree turn
			// ============================================
			TryRearUpOnApproach(horse, target, distanceToPlayer);
			
			// In melee range - turn 90 degrees to present rider's weapon side
			bool turnClockwise = GetHorseTurnDirectionClockwise(horse->formID);
			
			if (turnClockwise)
			{
				targetAngle = angleToPlayer + 1.5708f;  // +90 degrees
			}
			else
			{
				targetAngle = angleToPlayer - 1.5708f;  // -90 degrees
			}
			
			attackState = 1;
			
			float currentAngle = horse->rot.z;
			float angleDiff = targetAngle - currentAngle;
			while (angleDiff > 3.14159f) angleDiff -= 6.28318f;
			while (angleDiff < -3.14159f) angleDiff += 6.28318f;
			
			if (fabs(angleDiff) < 0.26f)
			{
				attackState = 2;
				
				// ============================================
				// ATTACK POSITION - DETERMINE PLAYER SIDE
				// ============================================
				
				float horseForwardX = sin(horse->rot.z);
				float horseForwardY = cos(horse->rot.z);
				float horseRightX = cos(horse->rot.z);
				float horseRightY = -sin(horse->rot.z);
				
				float toPlayerX = target->pos.x - horse->pos.x;
				float toPlayerY = target->pos.y - horse->pos.y;
				
				float dotRight = (toPlayerX * horseRightX) + (toPlayerY * horseRightY);
				const char* playerSide = (dotRight > 0) ? "RIGHT" : "LEFT";
				
				// Get the rider and try to play attack animation
				NiPointer<Actor> riderForAttack;
				if (CALL_MEMBER_FN(horse, GetMountedBy)(riderForAttack) && riderForAttack)
				{
					if (PlayMountedAttackAnimation(riderForAttack.get(), playerSide))
					{
						static float lastAttackLogTime = 0;
						float currentTime = (float)clock() / CLOCKS_PER_SEC;
						if ((currentTime - lastAttackLogTime) > 2.0f)
						{
							_MESSAGE("DynamicPackages: Rider %08X attacking player on %s side",
								riderForAttack->formID, playerSide);
							lastAttackLogTime = currentTime;
						}
					}
					
					if (IsRiderAttacking(riderForAttack.get()))
					{
						if (UpdateMountedAttackHitDetection(riderForAttack.get(), target))
						{
							_MESSAGE("DynamicPackages: Rider %08X melee hit confirmed on player!", 
								riderForAttack->formID);
						}
					}
				}
				
				// Log position (rate limited)
				static UInt32 lastLoggedHorse = 0;
				static float lastLogTime = 0;
				float currentTime = (float)clock() / CLOCKS_PER_SEC;
				
				if (horse->formID != lastLoggedHorse || (currentTime - lastLogTime) > 1.0f)
				{
					_MESSAGE("DynamicPackages: Horse %08X in ATTACK POSITION - Player on %s side (dot: %.2f, turnDir: %s)",
						horse->formID, playerSide, dotRight, turnClockwise ? "clockwise" : "counter-clockwise");
					lastLoggedHorse = horse->formID;
					lastLogTime = currentTime;
				}
			}
		}
		else
		{
			// Not in melee range - face player directly and follow
			NotifyHorseLeftMeleeRange(horse->formID);
			targetAngle = angleToPlayer;
		}
		
		// ============================================
		// APPLY ROTATION - Always face target direction
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
		// CREATE TRAVEL PACKAGE - Move toward player when not in melee
		// ============================================
		
		if (distanceToPlayer >= meleeRange)
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
