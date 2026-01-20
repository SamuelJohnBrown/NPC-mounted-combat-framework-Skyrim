#include "DynamicPackages.h"
#include "CombatStyles.h"
#include "WeaponDetection.h"
#include "MountedCombat.h"
#include "ArrowSystem.h"
#include "SpecialMovesets.h"
#include "FleeingBehavior.h"
#include "MagicCastingSystem.h"
#include "AILogging.h"
#include "config.h"  // For DynamicRangedRole settings
#include "skse64/GameRTTI.h"
#include "skse64/GameData.h"
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <mutex>

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
	const float STUCK_CHECK_INTERVAL = 0.5f;      // Check every 500ms
	const float RESET_COOLDOWN = 10.0f;    // Don't reset same horse more than once per 10 seconds	
	
	// ============================================
	// RANGED FOLLOW STATE TRACKING
	// ============================================
	// Tracks whether a ranged NPC is in ranged or melee follow mode
	// Switches to melee when target gets within RANGED_TO_MELEE_DISTANCE
	// Switches back to ranged when target exceeds MELEE_TO_RANGED_DISTANCE
	// THREAD SAFE: Uses mutex for multi-rider scenarios
	
	const float RANGED_TO_MELEE_DISTANCE = 340.0f;    // Switch to melee follow when closer than this
	const float MELEE_TO_RANGED_DISTANCE = 500.0f;    // Switch back to ranged when further than this
	const float RANGED_SWITCH_COOLDOWN = 2.0f;  // Minimum time between switches to prevent spam
	
	struct RangedFollowStateData
	{
		UInt32 actorFormID;
		bool isInRangedMode;        // true = maintaining distance, false = melee follow
		float lastSwitchTime;       // Time of last mode switch
		bool isValid;
	};
	
	static const int MAX_RANGED_FOLLOW_TRACKED = 10;
	static RangedFollowStateData g_rangedFollowState[MAX_RANGED_FOLLOW_TRACKED];
	static int g_rangedFollowStateCount = 0;
	static std::mutex g_rangedFollowMutex;  // Thread safety for multi-rider scenarios
	
	// Get or create ranged follow state data for an actor
	// NOTE: Caller must hold g_rangedFollowMutex lock
	static RangedFollowStateData* GetOrCreateRangedFollowState_Unlocked(UInt32 actorFormID)
	{
		// Check if already tracked
		for (int i = 0; i < g_rangedFollowStateCount; i++)
		{
			if (g_rangedFollowState[i].isValid && g_rangedFollowState[i].actorFormID == actorFormID)
			{
				return &g_rangedFollowState[i];
			}
		}
		
		// Create new entry
		if (g_rangedFollowStateCount < MAX_RANGED_FOLLOW_TRACKED)
		{
			RangedFollowStateData* data = &g_rangedFollowState[g_rangedFollowStateCount];
			data->actorFormID = actorFormID;
			data->isInRangedMode = true;  // Start in ranged mode
			data->lastSwitchTime = -RANGED_SWITCH_COOLDOWN;  // Allow immediate first switch
			data->isValid = true;
			g_rangedFollowStateCount++;
			return data;
		}
		
		return nullptr;
	}
	
	// Clear ranged follow state for an actor
	// THREAD SAFE: Uses mutex lock
	void ClearRangedFollowState(UInt32 actorFormID)
	{
		std::lock_guard<std::mutex> lock(g_rangedFollowMutex);
		
		for (int i = 0; i < g_rangedFollowStateCount; i++)
		{
			if (g_rangedFollowState[i].isValid && g_rangedFollowState[i].actorFormID == actorFormID)
			{
				// Shift remaining entries
				for (int j = i; j < g_rangedFollowStateCount - 1; j++)
				{
					g_rangedFollowState[j] = g_rangedFollowState[j + 1];
				}
				g_rangedFollowStateCount--;
				return;
			}
		}
	}
	
	// Reset all ranged follow state (call on game load)
	// THREAD SAFE: Uses mutex lock
	void ResetAllRangedFollowState()
	{
		std::lock_guard<std::mutex> lock(g_rangedFollowMutex);
		
		for (int i = 0; i < MAX_RANGED_FOLLOW_TRACKED; i++)
		{
			g_rangedFollowState[i].isValid = false;
		}
		g_rangedFollowStateCount = 0;
	}
	
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
		
		float currentTime = GetGameTime();
		
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
		
		// Verify horse has valid loaded state (prevents MovementPathManager CTD)
		if (!horse->loadedState)
		{
			_MESSAGE("DynamicPackages: Cannot reset horse %08X - no loaded state", horse->formID);
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
	
	// Centralized weapon switching for ALL riders
	bool UpdateRiderWeaponForDistance(Actor* rider, float distanceToTarget, bool targetIsMounted = false);
	
	// Check if obstruction is caused by an NPC (not terrain/geometry)
	static bool IsObstructionCausedByNPC(Actor* horse, Actor* target);

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
		
		// ============================================
		// CRITICAL: Verify both actors have valid 3D and loaded state
		// This prevents MovementPathManager CTD when actors are in invalid state
		// ============================================
		if (!actor->loadedState || !actor->GetNiNode())
		{
			return false;
		}
		
		if (!target->loadedState || !target->GetNiNode())
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
		// CRITICAL: Verify target has valid processManager
		// ============================================
		if (!target->processManager)
		{
			return false;
		}
		
		// ============================================
		// CHECK COMBAT CLASS FOR SPECIAL BEHAVIOR
		// ============================================
		MountedCombatClass combatClass = DetermineCombatClass(actor);
		
		// Get the actor's mount
		NiPointer<Actor> mount;
		if (CALL_MEMBER_FN(actor, GetMount)(mount) && mount)
		{
			// ============================================
			// CRITICAL: Verify mount has valid state before calling ForceHorseCombatWithTarget
			// ============================================
			if (mount->loadedState && mount->GetNiNode() && mount->processManager)
			{
				// ============================================
				// SKIP FOLLOW PACKAGE DURING SPECIAL MANEUVERS
				// Rapid fire, charge, and stand ground have their own movement control
				// Injecting follow packages during these causes CTD in MovementPathManager
				// ============================================
				if (IsInRapidFire(mount->formID))
				{
					// Rapid fire active - skip follow package, let rapid fire handle movement
					int attackState = InjectTravelPackageToHorse(mount.get(), target);
					if (outAttackState) *outAttackState = attackState;
					return true;
				}
				
				if (IsHorseCharging(mount->formID))
				{
					// Charge active - skip follow package, let charge handle movement
					int attackState = InjectTravelPackageToHorse(mount.get(), target);
					if (outAttackState) *outAttackState = attackState;
					return true;
				}
				
				if (IsInStandGround(mount->formID))
				{
					// Stand ground active - skip follow package
					int attackState = InjectTravelPackageToHorse(mount.get(), target);
					if (outAttackState) *outAttackState = attackState;
					return true;
				}
				
				// ============================================
				// MAGE CLASS - SPECIAL HANDLING
				// Mages use different follow packages based on combat mode:
				// - SPELL MODE: Maintain MageRoleIdealDistance, stand ground if closer
				// - MELEE MODE: Use standard melee follow (close in on target)
				// ============================================
				if (combatClass == MountedCombatClass::MageCaster)
				{
					// Ensure mage has staff equipped (one-time setup)
					if (!IsStaffEquipped(actor))
					{
						RequestWeaponSwitch(actor, WeaponRequest::Staff);
						_MESSAGE("InjectFollowPackage: MAGE %08X - equipping staff", actor->formID);
					}
					
					// Calculate distance to target
					float dx = target->pos.x - mount->pos.x;
					float dy = target->pos.y - mount->pos.y;
					float distToTarget = sqrt(dx * dx + dy * dy);
					
					// Update combat mode (handles buffer zone and cooldown)
					MageCombatMode combatMode = UpdateMageCombatMode(actor->formID, distToTarget);
					
					if (combatMode == MageCombatMode::Spell)
					{
						// SPELL MODE: Maintain distance
						// Only call ForceHorseCombatWithTarget if mage is TOO FAR
						// When within range, InjectTravelPackageToHorse handles everything
						if (distToTarget > MageRoleIdealDistance)
						{
							ForceHorseCombatWithTarget(mount.get(), target);
						}
						else
						{
							// Within range - do nothing, let horse stay where it is
						}
					}
					else
					{
						// MELEE MODE: Close in on target like a normal melee fighter
						ForceHorseCombatWithTarget(mount.get(), target);
					}
					
					// Process travel package - mage stance handled there
					int attackState = InjectTravelPackageToHorse(mount.get(), target);
					if (outAttackState) *outAttackState = attackState;
					return true;
				}
				
				// ============================================
				// RANGED ROLE - SPECIAL HANDLING
				// Uses EXACT same follow logic as mages - ALWAYS maintains distance
				// Ranged role riders NEVER chase with melee, they stand ground
				// NOW USES WEAPON SWITCHING STATE MACHINE FOR BOW/MELEE
				// ============================================
				if (IsInRangedRole(actor->formID))
				{
					// Calculate distance to target
					float dx = target->pos.x - mount->pos.x;
					float dy = target->pos.y - mount->pos.y;
					float distToTarget = sqrt(dx * dx + dy * dy);
					
					// ============================================
					// CHECK IF TARGET IS MOUNTED FOR WEAPON SWITCHING
					// ============================================
					bool targetIsMountedForRanged = false;
					NiPointer<Actor> targetMountCheck;
					if (CALL_MEMBER_FN(target, GetMount)(targetMountCheck) && targetMountCheck)
					{
						targetIsMountedForRanged = true;
					}
					
					// ============================================
					// WEAPON SWITCHING FOR RANGED ROLE - CRITICAL!
					// Ranged role uses the same distance-based weapon switching
					// as all other riders via UpdateRiderWeaponForDistance
					// This handles bow at range, melee when target gets close
					// - Bow when distance > WeaponSwitchDistance (default 250)
					// - Melee when distance <= WeaponSwitchDistance
					// ============================================
					UpdateRiderWeaponForDistance(actor, distToTarget, targetIsMountedForRanged);
					
					// RANGED ROLE: ALWAYS maintain distance (like mages in spell mode)
					// Only call ForceHorseCombatWithTarget if too far
					// When within range, just skip - let horse stay where it is
					if (distToTarget > DynamicRangedRoleIdealDistance)
					{
						ForceHorseCombatWithTarget(mount.get(), target);
					}
					// Within range - do nothing, let horse stay where it is
					// Rider will use bow at any range, or melee if target gets very close
					
					// Process travel package
					int attackState = InjectTravelPackageToHorse(mount.get(), target);
					if (outAttackState) *outAttackState = attackState;
					return true;
				}
				
				// All other classes use standard close-range follow
				ForceHorseCombatWithTarget(mount.get(), target);
				
				int attackState = InjectTravelPackageToHorse(mount.get(), target);
				if (outAttackState) *outAttackState = attackState;
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
	// SET NPC RANGED FOLLOW (DynamicRangedRoleIdealDistance units from target)
	// ============================================
	// Similar to regular follow but maintains greater distance
	// Used for ranged combat positioning (archers/bows in dynamic ranged role)
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

		// Offset using DynamicRangedRole config value (default 800 units away from target)
		NiPoint3 offset;
		offset.x = 0;
		offset.y = -DynamicRangedRoleIdealDistance;  // Use dynamic ranged role config value
		offset.z = 0;

		NiPoint3 offsetAngle;
		offsetAngle.x = 0;
		offsetAngle.y = 0;
		offsetAngle.z = 0;

		// catchUpRadius slightly larger than ideal, followRadius = ideal distance
		float catchUp = DynamicRangedRoleIdealDistance + 200.0f;
		Actor_KeepOffsetFromActor(actor, targetHandle, offset, offsetAngle, catchUp, DynamicRangedRoleIdealDistance);
		
		_MESSAGE("DynamicPackages: Set RANGED follow for actor %08X (%.0f units from target %08X)", 
			actor->formID, DynamicRangedRoleIdealDistance, target->formID);
		return true;
	}
	
	// ============================================
	// UPDATE RANGED FOLLOW STATE (Distance-based mode switching)
	// ============================================
	// Call this periodically for ranged NPCs to check if they should switch
	// between ranged follow (maintaining distance) and melee follow (close combat).
	// - Switches to MELEE when target is within RANGED_TO_MELEE_DISTANCE (340 units)
	// - Switches back to RANGED when target exceeds MELEE_TO_RANGED_DISTANCE (500 units)
	// - Has cooldown to prevent rapid switching spam
	// Returns: true if a switch occurred, false otherwise
	// THREAD SAFE: Uses mutex lock for state access
	
	bool UpdateRangedFollowState(Actor* actor, Actor* target)
	{
		if (!actor || !target)
		{
			return false;
		}
		
		// Calculate current distance to target (no lock needed - just reading actor positions)
		float dx = target->pos.x - actor->pos.x;
		float dy = target->pos.y - actor->pos.y;
		float distanceToTarget = sqrt(dx * dx + dy * dy);
		
		// Get current time for cooldown check
		float currentTime = GetGameTime();
		
		// Variables to capture state under lock
		bool isInRangedMode = true;
		float lastSwitchTime = 0.0f;
		bool dataFound = false;
		
		// Lock to read current state
		{
			std::lock_guard<std::mutex> lock(g_rangedFollowMutex);
			RangedFollowStateData* data = GetOrCreateRangedFollowState_Unlocked(actor->formID);
			if (data)
			{
				isInRangedMode = data->isInRangedMode;
				lastSwitchTime = data->lastSwitchTime;
				dataFound = true;
			}
		}
		
		if (!dataFound)
		{
			return false;
		}
		
		float timeSinceLastSwitch = currentTime - lastSwitchTime;
		
		// Check cooldown - prevent spam switching
		if (timeSinceLastSwitch < RANGED_SWITCH_COOLDOWN)
		{
			return false;// Still on cooldown
		}
		
		bool switchOccurred = false;
		bool newRangedMode = isInRangedMode;
		
		// Currently in RANGED mode - check if should switch to MELEE
		if (isInRangedMode)
		{
			if (distanceToTarget < RANGED_TO_MELEE_DISTANCE)
			{
				// Target is too close - switch to melee follow
				// Get actor's mount to apply the follow package
				NiPointer<Actor> mount;
				if (CALL_MEMBER_FN(actor, GetMount)(mount) && mount)
				{
					if (mount->loadedState && mount->GetNiNode() && mount->processManager)
					{
						// Switch to standard melee follow (no lock - game call)
						ForceHorseCombatWithTarget(mount.get(), target);
						
						newRangedMode = false;
						switchOccurred = true;
						
						_MESSAGE("DynamicPackages: Ranged actor %08X switched to MELEE follow (distance: %.0f < %.0f)",
							actor->formID, distanceToTarget, RANGED_TO_MELEE_DISTANCE);
					}
				}
			}
		}
		// Currently in MELEE mode - check if should switch back to RANGED
		else
		{
			if (distanceToTarget > MELEE_TO_RANGED_DISTANCE)
			{
				// Target is far enough - switch back to ranged follow
				NiPointer<Actor> mount;
				if (CALL_MEMBER_FN(actor, GetMount)(mount) && mount)
				{
					if (mount->loadedState && mount->GetNiNode() && mount->processManager)
					{
						// Switch back to ranged follow (no lock - game call)
						SetNPCRangedFollowFromTarget(mount.get(), target);
						
						newRangedMode = true;
					 switchOccurred = true;
						
						_MESSAGE("DynamicPackages: Ranged actor %08X switched back to RANGED follow (distance: %.0f > %.0f)",
							actor->formID, distanceToTarget, MELEE_TO_RANGED_DISTANCE);
					}
				}
			}
		}
		
		// If a switch occurred, update state under lock
		if (switchOccurred)
		{
			std::lock_guard<std::mutex> lock(g_rangedFollowMutex);
			RangedFollowStateData* data = GetOrCreateRangedFollowState_Unlocked(actor->formID);
			if (data)
			{
				data->isInRangedMode = newRangedMode;
				data->lastSwitchTime = currentTime;
			}
		}
		
		return switchOccurred;
	}
	
	// Check if actor is currently in ranged follow mode
	// THREAD SAFE: Uses mutex lock
	bool IsInRangedFollowMode(UInt32 actorFormID)
	{
		std::lock_guard<std::mutex> lock(g_rangedFollowMutex);
		
		for (int i = 0; i < g_rangedFollowStateCount; i++)
		{
			if (g_rangedFollowState[i].isValid && g_rangedFollowState[i].actorFormID == actorFormID)
			{
				return g_rangedFollowState[i].isInRangedMode;
			}
		}
		return true;  // Default to ranged mode if not tracked
	}
	
	// ============================================
	// SET NPC MAGE FOLLOW (MageRoleIdealDistance units from target)
	// ============================================
	// Similar to ranged follow but maintains closer distance for mages
	// Used for mage/staff combat positioning
	// - Faces target when stationary
	// - Faces target when traveling toward them
	// - Faces travel direction when moving away (no backwards walking)
	
	bool SetNPCMageFollowFromTarget(Actor* actor, Actor* target)
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

		// Offset using config value (default 500 units away from target)
		NiPoint3 offset;
		offset.x = 0;
		offset.y = -MageRoleIdealDistance;  // Use config value (closer than archers)
		offset.z = 0;

		NiPoint3 offsetAngle;
		offsetAngle.x = 0;
		offsetAngle.y = 0;
		offsetAngle.z = 0;

		// catchUpRadius slightly larger than ideal, followRadius = ideal distance
		float catchUp = MageRoleIdealDistance + 150.0f;
		Actor_KeepOffsetFromActor(actor, targetHandle, offset, offsetAngle, catchUp, MageRoleIdealDistance);
		
		_MESSAGE("DynamicPackages: Set MAGE follow for actor %08X (%.0f units from target %08X)", 
			actor->formID, MageRoleIdealDistance, target->formID);
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
	// FORCE HORSE INTO COMBAT WITH TARGET (COMPANION VERSION)
	// Uses CompanionMeleeRange from config for tighter engagement
	// ============================================

	bool ForceCompanionHorseCombatWithTarget(Actor* horse, Actor* target)
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
		
		// Verify horse has valid loaded state (prevents MovementPathManager CTD)
		if (!horse->loadedState)
		{
			return false;
		}
		
		// ============================================
		// CRITICAL: Additional safety check for MovementController
		// The crash happens when the MovementPathManagerArbiter is invalid
		// Check that the horse's 3D is fully loaded before manipulating movement
		// ============================================
		if (!horse->GetNiNode())
		{
			return false;
		}
		
		// ============================================
		// CRITICAL: Check target has valid 3D as well
		// Invalid target reference can cause MovementAgentActorAvoider CTD
		// ============================================
		if (!target->GetNiNode())
		{
			return false;
		}
		
		// ============================================
		// CRITICAL: Verify target's loadedState
		// ============================================
		if (!target->loadedState)
		{
			return false;
		}
		
		// ============================================
		// CRITICAL: VERIFY TARGET HAS VALID PROCESSMANAGER
		// ============================================
		if (!target->processManager)
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

		// Companion melee follow - use config value for tighter engagement
		NiPoint3 offset;
		offset.x = 100.0f;   // Slight offset to approach from side
		offset.y = -CompanionMeleeRange;  // Use config melee range
		offset.z = 0;

		NiPoint3 offsetAngle;
		offsetAngle.x = 0;
		offsetAngle.y = 0;
		offsetAngle.z = 0;

		// catchUpRadius slightly larger than melee range, followRadius = melee range
		float catchUp = CompanionMeleeRange + 100.0f;
		Actor_KeepOffsetFromActor(horse, targetHandle, offset, offsetAngle, catchUp, CompanionMeleeRange);
		Actor_EvaluatePackage(horse, false, false);
		
		_MESSAGE("DynamicPackages: Companion horse %08X set to melee range %.0f from target %08X", 
			horse->formID, CompanionMeleeRange, target->formID);

		return true;
	}

	// ============================================
	// FORCE HORSE INTO COMBAT WITH TARGET
	// ============================================

	bool ForceHorseCombatWithTarget(Actor* horse, Actor* target)
	{
		// ============================================
		// SAFETY: Validate actors before any processing
		// ============================================
		if (!horse || !target)
		{
			return false;
		}
		
		// ============================================
		// CRITICAL: VALIDATE FORMIDS
		// Ensure both actors have valid FormIDs before proceeding
		// ============================================
		if (horse->formID == 0 || horse->formID == 0xFFFFFFFF)
		{
			_MESSAGE("ForceHorseCombatWithTarget: Invalid horse formID - skipping");
			return false;
		}
		
		if (target->formID == 0 || target->formID == 0xFFFFFFFF)
		{
			_MESSAGE("ForceHorseCombatWithTarget: Invalid target formID - skipping");
			return false;
		}
		
		// ============================================
		// CRITICAL: VERIFY FORMIDS BY LOOKUP
		// Ensure the FormIDs are still valid in the game
		// ============================================
		TESForm* horseForm = LookupFormByID(horse->formID);
		TESForm* targetForm = LookupFormByID(target->formID);
		
		if (!horseForm || horseForm != (TESForm*)horse)
		{
			_MESSAGE("ForceHorseCombatWithTarget: Horse %08X form mismatch - skipping", horse->formID);
			return false;
		}
		
		if (!targetForm || targetForm != (TESForm*)target)
		{
			_MESSAGE("ForceHorseCombatWithTarget: Target %08X form mismatch - skipping", target->formID);
			return false;
		}
		
		
		// ============================================
		// CRITICAL: CHECK DISENGAGE COOLDOWN
		// Don't inject follow package for actors that are disengaging
		// This prevents the BGSProcedureFollowExecState CTD
		// ============================================
		// Check if the rider (who owns the horse) is on disengage cooldown
		NiPointer<Actor> rider;
		bool hasRider = CALL_MEMBER_FN(horse, GetMountedBy)(rider);
		if (hasRider && rider)
		{
			if (IsNPCOnDisengageCooldown(rider->formID))
			{
				_MESSAGE("ForceHorseCombatWithTarget: Rider %08X on disengage cooldown - skipping follow injection", rider->formID);
				return false;
			}
		}
		
		// Verify horse has required components for movement
		if (!horse->processManager || !horse->processManager->middleProcess)
		{
			return false;
		}
		
		// Verify horse has valid loaded state (prevents MovementPathManager CTD)
		if (!horse->loadedState)
		{
			return false;
		}
		
		// ============================================
		// CRITICAL: Additional safety check for MovementController
		// The crash happens when the MovementPathManagerArbiter is invalid
		// Check that the horse's 3D is fully loaded before manipulating movement
		// ============================================
		if (!horse->GetNiNode())
		{
			return false;
		}
		
		// ============================================
		// CRITICAL: Check target has valid 3D as well
		// Invalid target reference can cause MovementAgentActorAvoider CTD
		// ============================================
		if (!target->GetNiNode())
		{
			return false;
		}
		
		// ============================================
		// CRITICAL: Verify target's loadedState
		// ============================================
		if (!target->loadedState)
		{
			return false;
		}
		
		// ============================================
		// CRITICAL: VERIFY TARGET HAS VALID PROCESSMANAGER
		// ============================================
		if (!target->processManager)
		{
			return false;
		}
		
		// ============================================
		// CRITICAL: Check distance BEFORE creating handle
		// If target is extremely far (> 8000 units), don't attempt follow
		// This prevents CTD when MovementPathManager can't handle distant targets
		// ============================================
		float dx = target->pos.x - horse->pos.x;
		float dy = target->pos.y - horse->pos.y;
		float distanceToTarget = sqrt(dx * dx + dy * dy);
		
		const float MAX_FOLLOW_DISTANCE = 4100.0f;  // Don't attempt pathfinding beyond this
		if (distanceToTarget > MAX_FOLLOW_DISTANCE)
		{
			_MESSAGE("ForceHorseCombatWithTarget: Target %08X too far (%.0f > %.0f) - skipping follow",
				target->formID, distanceToTarget, MAX_FOLLOW_DISTANCE);
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
		// CHECK DISTANCE - SKIP FOLLOW OFFSET IF WITHIN 150 UNITS
		// Within close range, we let the 90-degree turn system handle positioning
		// The offset follow can cause the horse to walk into the target
		// DO NOT call Actor_ClearKeepOffsetFromActor - it causes CTD!
		// ============================================
		const float STOP_OFFSET_DISTANCE = 150.0f;
		if (distanceToTarget < STOP_OFFSET_DISTANCE)
		{
			// Within close range - just return true without setting any offset
			// The 90-degree turn system handles positioning at this range
			return true;
		}

		// ============================================
		// DETERMINE FOLLOW DISTANCE BASED ON RIDER'S COMBAT CLASS
		// - MageCaster: Uses MageRoleIdealDistance (default 550) - maintains distance, stands ground if closer
		// - All others: Standard melee follow (300 units)
		// ============================================
		float followDistance = 300.0f;  // Default melee follow distance
		float offsetX = 200.0f;   // Default side offset
		float catchUpRadius = 1000.0f;  // Default catch-up radius
		
		// Check rider's combat class if we have a rider
		if (hasRider && rider)
		{
			MountedCombatClass combatClass = DetermineCombatClass(rider.get());
			
			if (combatClass == MountedCombatClass::MageCaster)
			{
				// Mages maintain MageRoleIdealDistance from target
				// They stand ground if target gets closer (handled elsewhere)
				followDistance = MageRoleIdealDistance;
				offsetX = 0.0f;  // No side offset for mages - they want direct line of sight
				catchUpRadius = MageRoleIdealDistance + 200.0f;
				
				// Only log once per mage (use static to track)
				static UInt32 lastLoggedMage = 0;
				if (lastLoggedMage != rider->formID)
				{
					lastLoggedMage = rider->formID;
					_MESSAGE("ForceHorseCombatWithTarget: MAGE rider %08X - using follow distance %.0f", 
						rider->formID, followDistance);
				}
			}
			// ============================================
			// RANGED ROLE - ALWAYS uses distant follow like mages
			// Ranged role riders NEVER use melee follow - they maintain distance
			// ============================================
			else if (IsInRangedRole(rider->formID))
			{
				// ALWAYS use ranged distance - no melee mode check
				followDistance = DynamicRangedRoleIdealDistance;
				offsetX = 0.0f;  // No side offset - direct line of sight for bow
				catchUpRadius = DynamicRangedRoleIdealDistance + 200.0f;
				
				static UInt32 lastLoggedRanged = 0;
				if (lastLoggedRanged != rider->formID)
				{
					lastLoggedRanged = rider->formID;
					_MESSAGE("ForceHorseCombatWithTarget: RANGED ROLE rider %08X - using follow distance %.0f", 
						rider->formID, followDistance);
				}
			}
		}

		// Standard melee follow - close in on target
		NiPoint3 offset;
		offset.x = offsetX;
		offset.y = -followDistance;
		offset.z = 0;

		NiPoint3 offsetAngle;
		offsetAngle.x = 0;
		offsetAngle.y = 0;
		offsetAngle.z = 0;

		Actor_KeepOffsetFromActor(horse, targetHandle, offset, offsetAngle, catchUpRadius, followDistance);
		Actor_EvaluatePackage(horse, false, false);

		return true;
	}
	
	// ============================================
	// TRAVEL PACKAGE INJECTION FOR HORSE
	// Core pathfinding loop that processes horse movement
	// Returns: 0 = traveling, 1 = in melee range, 2 = in attack position, 
	// 3 = ranged combat, 4 = charge active, 5 = rapid fire, 6 = ranged role, 7 = stand ground
	// ============================================
	
	// Rate limiting to prevent duplicate processing per frame
	struct HorseProcessingTracker
	{
		UInt32 horseFormID;
		float lastProcessTime;
		bool isValid;
	};
	
	static HorseProcessingTracker g_horseProcessing[10];
	static int g_horseProcessingCount = 0;
	
	static bool ShouldSkipDuplicateProcessing(UInt32 horseFormID)
	{
		float currentTime = GetGameTime();
		const float MIN_PROCESS_INTERVAL = 0.016f;  // ~60fps - skip if processed within this frame
		
		for (int i = 0; i < g_horseProcessingCount; i++)
		{
			if (g_horseProcessing[i].isValid && g_horseProcessing[i].horseFormID == horseFormID)
			{
				if ((currentTime - g_horseProcessing[i].lastProcessTime) < MIN_PROCESS_INTERVAL)
				{
					return true;  // Already processed this frame - skip
				}
				g_horseProcessing[i].lastProcessTime = currentTime;
				return false;
			}
		}
		
		// New horse - add to tracking
		if (g_horseProcessingCount < 10)
		{
			g_horseProcessing[g_horseProcessingCount].horseFormID = horseFormID;
			g_horseProcessing[g_horseProcessingCount].lastProcessTime = currentTime;
			g_horseProcessing[g_horseProcessingCount].isValid = true;
			g_horseProcessingCount++;
		}
		
		return false;
	}

	int InjectTravelPackageToHorse(Actor* horse, Actor* target)
	{
		// ============================================
		// CRITICAL: VALIDATE ALL ACTORS BEFORE PROCESSING
		// Prevents CTD in MovementAgentActorAvoider from null/invalid actors
		// ============================================
		if (!horse || !target)
		{
			return 0;
		}
		
		// ============================================
		// CHECK IF RIDER IS FLEEING - SKIP ALL PROCESSING
		// Fleeing riders are controlled by tactical flee system
		// Do NOT apply any movement, rotation, or packages!
		// ============================================
		if (IsHorseRiderFleeing(horse->formID))
		{
			return 0;  // Skip all processing - flee system handles this horse
		}
		
		// ============================================
		// CHECK IF CIVILIAN - PROCESS AS FLEE ONLY
		// Civilians get NO combat logic - only flee package
		// ============================================
		NiPointer<Actor> riderForCivilianCheck;
		if (CALL_MEMBER_FN(horse, GetMountedBy)(riderForCivilianCheck) && riderForCivilianCheck)
		{
			if (ProcessCivilianMountedNPC(riderForCivilianCheck.get(), horse, target))
			{
				return 0;  // Civilian is fleeing - skip all combat logic
			}
		}
		
		// Validate horse is still valid and has required state
		if (horse->formID == 0 || horse->IsDead(1))
		{
			return 0;
		}
		
		// CRITICAL: Check for valid loaded state - prevents MovementAgentActorAvoider crash
		if (!horse->loadedState || !horse->processManager)
		{
			return 0;
		}
		
		// Validate target is still valid
		if (target->formID == 0 || target->IsDead(1))
		{
			return 0;
		}
		
		// CRITICAL: Check target loaded state too
		if (!target->loadedState)
		{
			return 0;
		}
		
		// ============================================
		// CLOSE RANGE MELEE ASSAULT - HIGHEST PRIORITY!
		// This MUST be checked FIRST before any other logic.
		// When target is within 145 units, FORCE ATTACKS regardless
		// of angle, weapon state, or any other conditions!
		// ============================================
		float dx_assault = target->pos.x - horse->pos.x;
		float dy_assault = target->pos.y - horse->pos.y;
		float distanceForAssault = sqrt(dx_assault * dx_assault + dy_assault * dy_assault);
		
		NiPointer<Actor> riderForAssault;
		if (CALL_MEMBER_FN(horse, GetMountedBy)(riderForAssault) && riderForAssault)
		{
			// Try to activate close range melee assault
			if (TryCloseRangeMeleeAssault(horse, riderForAssault.get(), target))
			{
				// Assault is active - FORCE attacks every interval
				// But DO NOT override rotation - let normal 90-degree turn handle it
				UpdateCloseRangeMeleeAssault(horse, riderForAssault.get(), target);
				
				// DON'T return here - let the rest of the function handle rotation normally!
				// The assault just guarantees attacks happen, it doesn't change movement
			}
		}
		
		// ============================================
		// RATE LIMIT: Skip if already processed this frame
		// Prevents duplicate rotation/movement when called from multiple places
		// EXCEPTION: Rapid fire horses MUST be processed every frame!
		// ============================================
		if (!IsInRapidFire(horse->formID) && ShouldSkipDuplicateProcessing(horse->formID))
		{
			return 0;  // Already processed this frame
		}
		
		// ============================================
		// ADDITIONAL SAFETY: VERIFY HORSE HAS VALID LOADED STATE
		// THIS PREVENTS CTD IN MOVEMENTPATHMANAGER WHEN HORSE
		// IS IN AN INVALID/TRANSITIONAL STATE
		// ============================================

		// Check for valid loaded state - prevents MovementAgentActorAvoider crash
		if (!horse->loadedState)
		{
			return 0;
		}
		
		// Calculate distance to target
		float dx = target->pos.x - horse->pos.x;
		float dy = target->pos.y - horse->pos.y;
		float distanceToTarget = sqrt(dx * dx + dy * dy);
		
		float angleToTarget = atan2(dx, dy);
		float targetAngle = angleToTarget;
		
		// ============================================
		// DETERMINE MELEE RANGE BASED ON TARGET TYPE
		// - Player on foot: MeleeRangeOnFoot (195 default)
		// - NPC on foot: MeleeRangeOnFootNPC (230 default) - larger because both moving
		// - Mounted target: MeleeRangeMounted (300 default)
		// ============================================
		float meleeRange = MeleeRangeOnFoot;  // Default for player
		
		// Check if target is mounted
		NiPointer<Actor> targetMount;
		bool targetIsMountedCheck = CALL_MEMBER_FN(target, GetMount)(targetMount) && targetMount;
		
		// ============================================
		// MOUNTED VS MOUNTED COMBAT DETECTION
		// No verbose logging - just set melee range
		// ============================================
		if (targetIsMountedCheck)
		{
			meleeRange = MeleeRangeMounted;
		}

		// ============================================
		// CHECK IF HORSE IS IN RAPID FIRE MODE (EARLY EXIT)
		// If so, horse ROTATES to face target but does NOT move
		// This prevents any travel packages from being injected
		// ============================================
		if (IsInRapidFire(horse->formID))
		{
			// Get rider for rapid fire update
			NiPointer<Actor> riderForRapidFire;
			if (CALL_MEMBER_FN(horse, GetMountedBy)(riderForRapidFire) && riderForRapidFire)
			{
				// Update the rapid fire maneuver (handles bow firing)
				UpdateRapidFireManeuver(horse, riderForRapidFire.get(), target);
			}
			
			// ============================================
			// FORCE HORSE TO STOP EVERY FRAME
			// Clear all movement packages and offset tracking
			// ============================================
			Actor_ClearKeepOffsetFromActor(horse);
			ClearInjectedPackages(horse);
			StopHorseSprint(horse);
			
			// Horse ROTATES to face target but stays stationary
			float currentAngle = horse->rot.z;
			float angleDiff = angleToTarget - currentAngle;
			while (angleDiff > 3.14159f) angleDiff -= 6.28318f;
			while (angleDiff < -3.14159f) angleDiff += 6.28318f;
			
			if (fabs(angleDiff) > 0.03f)  // Tighter threshold for smoother stop
			{
				float newAngle = currentAngle + (angleDiff * HorseRotationSpeed);  // Use config value
				while (newAngle > 3.14159f) newAngle -= 6.28318f;
				while (newAngle < -3.14159f) newAngle += 6.28318f;
				horse->rot.z = newAngle;
			}
			
			// Return 5 - horse stays stationery but rotates (rapid fire mode)
			return 5;
		}
		
		// ============================================
		// CHECK IF HORSE IS IN STAND GROUND MODE (EARLY EXIT)
		// If so, horse does 90-degree turn ONCE then LOCKS rotation completely
		// NO MORE ROTATION until stand ground ends!
		// THIS MUST BE CHECKED BEFORE ALL OTHER CODE PATHS TO PREVENT JITTER
		// ============================================
		if (IsInStandGround(horse->formID))
		{
			// Update the stand ground maneuver
			if (!UpdateStandGroundManeuver(horse, target))
			{
				// Stand ground ended - continue to normal behavior below
				// Don't return here - let the rest of the function handle normal behavior
			}
			else
			{
				// ============================================
				// FORCE HORSE TO STOP EVERY FRAME
				// Clear all movement packages and offset tracking
				// ============================================
				Actor_ClearKeepOffsetFromActor(horse);
				ClearInjectedPackages(horse);
				StopHorseSprint(horse);
				
				// ============================================
				// CHECK IF ROTATION IS ALREADY LOCKED
				// If locked, just maintain the exact locked angle - NO ROTATION!
				// ============================================
				if (IsStandGroundRotationLocked(horse->formID))
				{
					// Rotation is locked - set horse to EXACT locked angle
					float lockedAngle = GetStandGroundLockedAngle(horse->formID);
					horse->rot.z = lockedAngle;  // Force exact angle every frame
					
					// Trigger attacks based on current position
					float horseRightX = cos(lockedAngle);
					float horseRightY = -sin(lockedAngle);
					
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
							UpdateMountedAttackHitDetection(riderForAttack.get(), target);
						}
					}
					
					// CRITICAL: Return immediately to skip ALL OTHER rotation code
					return 7;  // Stand ground with locked rotation
				}
				
				// ============================================
				// ROTATION NOT YET LOCKED - Still doing initial 90-degree turn
				// ============================================
				float targetAngle;
				bool noRotation = IsStandGroundNoRotation(horse->formID);
				
				if (noRotation)
				{
					// NO ROTATION mode - immediately lock to current facing
					LockStandGroundRotation(horse->formID, horse->rot.z);
					// CRITICAL: Return immediately to skip ALL OTHER rotation code
					return 7;
				}
				
				// ============================================
				// CRITICAL FIX FOR JITTER:
				// Use the stored target angle from when stand ground started!
				// Do NOT recalculate based on current angleToTarget!
				// The target may have moved, but we want to turn to where they WERE
				// ============================================
				targetAngle = GetStandGroundTarget90DegreeAngle(horse->formID, angleToTarget);
				
				float currentAngle = horse->rot.z;
				float angleDiff = targetAngle - currentAngle;
				while (angleDiff > 3.14159f) angleDiff -= 6.28318f;
				while (angleDiff < -3.14159f) angleDiff += 6.28318f;
				
				// Check if 90-degree turn is complete (within threshold)
				const float TURN_COMPLETE_THRESHOLD = 0.15f;  // ~8.6 degrees
				if (fabs(angleDiff) < TURN_COMPLETE_THRESHOLD)
				{
					// Turn complete! Lock the rotation at this angle
					LockStandGroundRotation(horse->formID, currentAngle);
					// CRITICAL: Return immediately to skip ALL OTHER rotation code
					return 7;
				}
				
				// Still turning - apply rotation toward target angle
				float newAngle = currentAngle + (angleDiff * HorseRotationSpeed);
				while (newAngle > 3.14159f) newAngle -= 6.28318f;
				while (newAngle < -3.14159f) newAngle += 6.28318f;
				horse->rot.z = newAngle;
				
				// CRITICAL: Return immediately to skip ALL OTHER rotation code
				return 7;  // Horse standing ground, still turning
			}
		}
		
		// ============================================
		// MAGE COMBAT STANCE - EARLY EXIT
		// Mages in SPELL mode maintain distance and STOP when within MageRoleIdealDistance
		// Mages in MELEE mode use normal melee combat (handled below)
		// Also handles mage tactical retreat (25% chance every 15 seconds)
		// ============================================
		NiPointer<Actor> riderForMageCheck;
		if (CALL_MEMBER_FN(horse, GetMountedBy)(riderForMageCheck) && riderForMageCheck)
		{
			MountedCombatClass riderClass = DetermineCombatClass(riderForMageCheck.get());
			
			if (riderClass == MountedCombatClass::MageCaster)
			{
				// Check for mage tactical retreat (25% chance every 15 seconds)
				if (CheckAndTriggerMageRetreat(riderForMageCheck.get(), horse, target, distanceToTarget))
				{
					// Mage is retreating - skip all other processing
					return 9;  // Mage retreating state
				}
				
				// Check current combat mode (updated earlier in this function)
				MageCombatMode combatMode = UpdateMageCombatMode(riderForMageCheck->formID, distanceToTarget);
				
				if (combatMode == MageCombatMode::Spell)
				{
					// SPELL MODE: Maintain distance, stop when within ideal range
					if (distanceToTarget <= MageRoleIdealDistance)
					{
						// Within ideal distance - STOP movement only
						StopHorseSprint(horse);
						
						// DO NOT clear follow package - let it handle rotation naturally
						// DO NOT apply any rotation here
						
						// EARLY RETURN - skip all other code paths
						return 8;  // Mage stance (spell mode)
					}
				}
				// MELEE MODE: Fall through to normal melee combat logic below
			}
			
			// ============================================
			// RANGED ROLE - EARLY EXIT (SAME AS MAGE SPELL MODE)
			// Ranged role riders maintain distance and NEVER chase with melee
			// They stand ground at DynamicRangedRoleIdealDistance
			// BUT they still use distance-based weapon switching!
			// ============================================
			if (IsInRangedRole(riderForMageCheck->formID))
			{
				// ============================================
				// WEAPON SWITCHING FOR RANGED ROLE - CRITICAL!
				// Ranged role riders switch between bow and melee based on distance
				// - Bow when distanceToTarget > WeaponSwitchDistance
				// - Melee when distanceToTarget <= WeaponSwitchDistance
				// This MUST happen before any early returns!
				// ============================================
				UpdateRiderWeaponForDistance(riderForMageCheck.get(), distanceToTarget, targetIsMountedCheck);
				
				// If bow is equipped and at range, fire it
				// Use WeaponSwitchDistance as the threshold for firing (not ideal distance)
				if (IsBowEquipped(riderForMageCheck.get()) && distanceToTarget > WeaponSwitchDistance)
				{
					UpdateBowAttack(riderForMageCheck.get(), true, target);
				}
				
				// Ranged role ALWAYS maintains distance - no melee mode
				if (distanceToTarget <= DynamicRangedRoleIdealDistance)
				{
					// Within ideal distance - STOP movement
					StopHorseSprint(horse);
					
					// If target is very close (within melee range), allow melee attacks
					// but DON'T chase - just attack from current position
					if (distanceToTarget < meleeRange)
					{
						// Determine attack side
						float horseRightX = cos(horse->rot.z);
						float horseRightY = -sin(horse->rot.z);
						float toTargetX = target->pos.x - horse->pos.x;
						float toTargetY = target->pos.y - horse->pos.y;
						float dotRight = (toTargetX * horseRightX) + (toTargetY * horseRightY);
						const char* targetSide = (dotRight > 0) ? "RIGHT" : "LEFT";
						
						// Play melee attack if melee weapon equipped
						if (IsMeleeEquipped(riderForMageCheck.get()))
						{
							PlayMountedAttackAnimation(riderForMageCheck.get(), targetSide);
							if (IsRiderAttacking(riderForMageCheck.get()))
							{
								UpdateMountedAttackHitDetection(riderForMageCheck.get(), target);
							}
						}
					}
					
					// EARLY RETURN - skip all other code paths
					// This prevents the travel package from being created
					return 6;  // Ranged role stance
				}
				// If too far, fall through to allow normal approach
				// But ForceHorseCombatWithTarget already set the ranged follow distance
			}
		}
		
		// ============================================
		// GET RIDER FOR WEAPON SWITCHING AND ATTACKS
		// ============================================

		NiPointer<Actor> rider;
		if (CALL_MEMBER_FN(horse, GetMountedBy)(rider) && rider)
		{
			// All riders use standard melee/ranged behavior now
			// No special ranged role handling
		}
		
		// ============================================
		// FAILSAFE: Sprint stop AND 90-degree turn at breathing distance
		// This ensures ALL melee riders (including companions/captains) 
		// never try to walk directly into the target
		// SKIP if already in stand ground - that's handled above
		// ============================================
		const float BREATHING_DISTANCE = 200.0f;
		if (distanceToTarget < BREATHING_DISTANCE)
		{
			// ALWAYS stop sprint at breathing distance
			StopHorseSprint(horse);
			
			// Stand ground horses were already handled above and returned early
			// This code path should only run for non-stand-ground horses
			
			// Apply 90-degree turn failsafe
			// FORCE 90-degree turn at breathing distance as failsafe
			// This prevents the horse from walking into the target
			float targetAngle90 = Get90DegreeTurnAngle(horse->formID, angleToTarget);
			float currentAngle = horse->rot.z;
			float angleDiff = targetAngle90 - currentAngle;
			while (angleDiff > 3.14159f) angleDiff -= 6.28318f;
			while (angleDiff < -3.14159f) angleDiff += 6.28318f;
			
			// Apply rotation toward 90-degree angle
			float newAngle = currentAngle + (angleDiff * HorseRotationSpeed);
			while (newAngle > 3.14159f) newAngle -= 6.28318f;
			while (newAngle < -3.14159f) newAngle += 6.28318f;
			horse->rot.z = newAngle;
			
			// Check if in attack position and trigger attack
			bool targetIsPlayer = (g_thePlayer && (*g_thePlayer) && target == (*g_thePlayer));
			float attackAngleThreshold = targetIsPlayer ? AttackAnglePlayer : AttackAngleNPC;
			
			// ============================================
			// CLOSE RANGE ATTACK GUARANTEE
			// When target is very close (within CloseRangeAttackDistance),
			// ALWAYS allow attacks regardless of angle. This fixes the issue
			// where attacks stop working when the target gets too close.
			// ============================================
			bool closeRangeOverride = (distanceToTarget < CloseRangeAttackDistance);
			
			// ============================================
			// CLOSE RANGE MELEE ASSAULT - EMERGENCY CLOSE COMBAT
			// When target is within CloseRangeMeleeAssaultDistance (145 units),
			// trigger rapid attacks every 1 second. This guarantees hits
			// when the target gets very close to the rider.
			// ============================================
			NiPointer<Actor> riderForAssault;
			if (CALL_MEMBER_FN(horse, GetMountedBy)(riderForAssault) && riderForAssault)
			{
				// Try to activate or continue close range melee assault
				if (TryCloseRangeMeleeAssault(horse, riderForAssault.get(), target))
				{
					// Assault is active - update it (triggers attacks every 1 second)
					UpdateCloseRangeMeleeAssault(horse, riderForAssault.get(), target);
				}
			}
			
			if (closeRangeOverride || fabs(angleDiff) < attackAngleThreshold)
			{
				// In attack position (or close enough to override) - determine side and attack
				float horseRightX = cos(horse->rot.z);
				float horseRightY = -sin(horse->rot.z);
				
				float toTargetX = target->pos.x - horse->pos.x;
				float toTargetY = target->pos.y - horse->pos.y;
				
				float dotRight = (toTargetX * horseRightX) + (toTargetY * horseRightY);
				const char* targetSide = (dotRight > 0) ? "RIGHT" : "LEFT";
				
				NiPointer<Actor> riderForAttack;
				if (CALL_MEMBER_FN(horse, GetMountedBy)(riderForAttack) && riderForAttack)
				{
					// ============================================
					// MAGES USE MELEE ATTACKS WHEN IN MELEE MODE
					// Mode is determined by UpdateMageCombatMode with buffer zone
					// ============================================
					MountedCombatClass attackerClass = DetermineCombatClass(riderForAttack.get());
					bool mageInMeleeMode = (attackerClass == MountedCombatClass::MageCaster && IsMageInMeleeMode(riderForAttack->formID));
					
					if (attackerClass != MountedCombatClass::MageCaster || mageInMeleeMode)
					{
						PlayMountedAttackAnimation(riderForAttack.get(), targetSide);
						
						if (IsRiderAttacking(riderForAttack.get()))
						{
							UpdateMountedAttackHitDetection(riderForAttack.get(), target);
						}
					}
				}
				
				// CRITICAL: Return immediately to skip ALL OTHER rotation code
				return 7;  // Stand ground with locked rotation
			}
			
			return 1;  // In melee range, turning
		}
		
		// ============================================
		// FAILSAFE: Check if horse is stuck (skip during special movesets)
		// Don't trigger jump/avoidance when horse is supposed to be stationary
		// ============================================

		// Skip stuck check if horse is in stand ground - let that system handle it
		if (!IsInStandGround(horse->formID))
		{
			if (!IsInRapidFire(horse->formID) && 
				!IsInStandGround(horse->formID) &&
				!IsHorseCharging(horse->formID))
			{
				ObstructionType obstruction = CheckAndLogHorseObstruction(horse, target, distanceToTarget);
				
				if (obstruction == ObstructionType::Stationary ||
					obstruction == ObstructionType::RunningInPlace || 
					obstruction == ObstructionType::CollisionBlocked)
				{
					// ============================================
					// CHECK IF OBSTRUCTION IS CAUSED BY AN NPC
					// If blocked by an NPC (enemy, creature, etc.), don't jump/avoid
					// Just let normal combat handle it - the NPC will move or die
					// ============================================
					if (IsObstructionCausedByNPC(horse, target))
					{
						// Obstruction is an NPC - skip jump/avoidance maneuvers
						// The combat system will handle engaging or maneuvering around them
					}
					else if (CheckAndLogSheerDrop(horse))
					{
						// Near sheer drop - avoid maneuvers (log only once via AILogging)
					}
					else
					{
						// ============================================
						// ELEVATED TARGET / COMBAT DISMOUNT SYSTEM REMOVED
						// NPCs will stay mounted and use jump for obstruction escape
						// ============================================
						
						if (TryHorseJumpToEscape(horse))
						{
							// Jump triggered - log only
							_MESSAGE("DynamicPackages: Horse %08X jumped to escape obstruction", horse->formID);
						}
					}
				}
				
				if (CheckHorseStuck(horse, distanceToTarget, meleeRange))
				{
					ResetHorseToDefaultBehavior(horse, target);
				}
			}
		}
		
		// ============================================
		// CHECK FOR MULTI MOUNTED COMBAT MANEUVER
		// ============================================
		
		int mountedRiderCount = GetFollowingNPCCount();
		
		// ============================================
		// WEAPON SWITCHING - ALL RIDERS USE CENTRALIZED SYSTEM
		// SINGLE/MULTI, MELEE/RANGED - EVERYONE USES UPDATERIDERWEAPONFORDISTANCE
		// EXCEPTION: MAGES NEVER SWITCH WEAPONS - THEY KEEP STAFF EQUIPPED
		// ============================================
		// Re-get rider reference
		if (CALL_MEMBER_FN(horse, GetMountedBy)(rider) && rider)
		{
			// Check if rider is a mage - mages NEVER switch weapons or use melee/bow
			MountedCombatClass riderCombatClass = DetermineCombatClass(rider.get());
			bool isMage = (riderCombatClass == MountedCombatClass::MageCaster);
			
			// Skip weapon switching during special maneuvers OR if rider is a mage
			if (!IsHorseCharging(horse->formID) && !IsInRapidFire(horse->formID) && !isMage)
			{
				// ALL riders use the same distance-based weapon switching
				// Pass targetIsMountedCheck to use appropriate switch distance
				UpdateRiderWeaponForDistance(rider.get(), distanceToTarget, targetIsMountedCheck);

				// If bow is equipped and at range, fire it
				if (IsBowEquipped(rider.get()) && distanceToTarget > WeaponSwitchDistance)
				{
					UpdateBowAttack(rider.get(), true, target);
				}
			}
			
			// ============================================
			// MAGE SPELL CASTING - Fire and Forget spells
			// Mages cast spells at targets up to SpellRangeMax (2000 units)
			// This is called every frame for mages to handle charge/cast/cooldown
			// Also updates mage combat mode (melee vs spell)
			// ============================================
			if (isMage)
			{
				// Update mage combat mode (handles buffer zone and cooldown)
				MageCombatMode combatMode = UpdateMageCombatMode(rider->formID, distanceToTarget);
				
				// Only cast spells when in Spell mode
				if (combatMode == MageCombatMode::Spell)
				{
					UpdateMageSpellCasting(rider.get(), target, distanceToTarget);
				}
			}
		}
		
		int attackState = 0;

		// ============================================
		// CHECK IF TARGET IS MOUNTED
		// (Already checked above for melee range - reuse the result)
		// ============================================

		bool targetIsMounted = targetIsMountedCheck;
		
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
					
					float newAngle = currentAngle + (angleDiff * HorseRotationSpeed);  // Use config value
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
					
					float newAngle = currentAngle + (angleDiff * HorseRotationSpeed);  // Use config value
					horse->rot.z = newAngle;
					
					return 4;
				}
			}
			
			// ============================================
			// RAPID FIRE MANEUVER CHECK
			// ============================================
			// Note: If already in rapid fire, we returned early above (line ~680)
			// This is only for TRIGGERING new rapid fire
			if (distanceToTarget > meleeRange && GetCombatElapsedTime() >= 20.0f)
			{
				if (TryRapidFireManeuver(horse, riderForCharge.get(), target, distanceToTarget, meleeRange))
				{
					// Rapid fire just triggered - stop horse movement immediately
					Actor_ClearKeepOffsetFromActor(horse);
					ClearInjectedPackages(horse);
		Actor_EvaluatePackage(horse, false, false);

					_MESSAGE("DynamicPackages: RAPID FIRE TRIGGERED - Horse %08X movement STOPPED (rotation continues)", horse->formID);
					
				 targetAngle = angleToTarget;
					
					float currentAngle = horse->rot.z;
					float angleDiff = targetAngle - currentAngle;
					while (angleDiff > 3.14159f) angleDiff -= 6.28318f;
					while (angleDiff < -3.14159f) angleDiff += 6.28318f;
					
					float newAngle = currentAngle + (angleDiff * HorseRotationSpeed);  // Use config value
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
			
			// ============================================
			// CLOSE RANGE MELEE ASSAULT - EMERGENCY CLOSE COMBAT
			// When target is within CloseRangeMeleeAssaultDistance (145 units),
			// trigger rapid attacks every 1 second. This guarantees hits
			// when the target gets very close to the rider.
			// ============================================
			NiPointer<Actor> riderForAssault;
			if (CALL_MEMBER_FN(horse, GetMountedBy)(riderForAssault) && riderForAssault)
			{
				// Try to activate or continue close range melee assault
				if (TryCloseRangeMeleeAssault(horse, riderForAssault.get(), target))
				{
					// Assault is active - update it (triggers attacks every 1 second)
					UpdateCloseRangeMeleeAssault(horse, riderForAssault.get(), target);
				}
			}
			
			// ============================================
			// TRY STAND GROUND MANEUVER (vs non-player NPCs only)
			// 25% chance when within 260 units of a mobile NPC target
			// ============================================
			bool targetIsPlayer = (g_thePlayer && (*g_thePlayer) && target == (*g_thePlayer));
			if (!targetIsPlayer && !IsInStandGround(horse->formID))
			{
				NiPointer<Actor> riderForStand;
				if (CALL_MEMBER_FN(horse, GetMountedBy)(riderForStand) && riderForStand)
				{
					TryStandGroundManeuver(horse, riderForStand.get(), target, distanceToTarget);
				}
			}
			
			// ============================================
			// TRY PLAYER AGGRO SWITCH (vs non-player NPCs only)
		 // 15% chance every 20 seconds when player is within 1500 units
			// Switches target to player and triggers a charge!
			// ============================================
			if (!targetIsPlayer && !IsHorseCharging(horse->formID) && !IsInRapidFire(horse->formID))
			{
				NiPointer<Actor> riderForAggro;
				if (CALL_MEMBER_FN(horse, GetMountedBy)(riderForAggro) && riderForAggro)
				{
					if (TryPlayerAggroSwitch(horse, riderForAggro.get(), target))
					{
						// Successfully switched to player - charge maneuver started
						// Return charging state
						return 4;
					}
				}
			}
			
			if (targetIsMounted)
			{
				// ============================================
				// MOUNTED VS MOUNTED COMBAT (within melee range)
				// ============================================
				
				// ============================================
				// CRITICAL: CHECK IF STAND GROUND IS ACTIVE
				// If horse is in stand ground, the early exit should have returned already.
				// But as a failsafe, check again and return immediately to prevent jitter.
				// ============================================
				if (IsInStandGround(horse->formID))
				{
					// Horse is in stand ground - this should have been handled by early exit
					// Return immediately to prevent any additional rotation
					return 5;
				}
				
				// ============================================
				// RATE LIMIT ROTATION UPDATES FOR MOUNTED VS MOUNTED
				// Only update rotation every 75ms to prevent jitter and CTD
				// ============================================
				static UInt32 lastRotationUpdateHorse = 0;
				static float lastRotationUpdateTime = 0;
				float currentTime = GetGameTime();
				
				// Only process rotation updates every 0.075 seconds (~13 times per second max)
				bool shouldUpdateRotation = true;
				if (lastRotationUpdateHorse == horse->formID)
				{
					if ((currentTime - lastRotationUpdateTime) < 0.075f)
					{
						shouldUpdateRotation = false;
					}
				}
				
				if (shouldUpdateRotation)
				{
					lastRotationUpdateHorse = horse->formID;
					lastRotationUpdateTime = currentTime;
				}
				
				// ============================================
				// MOUNTED VS MOUNTED: Face target directly
				// No stand ground for mounted vs mounted - just approach and attack
				// ============================================
				targetAngle = angleToTarget;
				
				attackState = 1;
				
				float currentAngle = horse->rot.z;
				float angleDiff = targetAngle - currentAngle;
				while (angleDiff > 3.14159f) angleDiff -= 6.28318f;
				while (angleDiff < -3.14159f) angleDiff += 6.28318f;
				
				// Use config value for attack angle threshold (mounted vs mounted)
				if (fabs(angleDiff) < AttackAngleMounted)
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
						// ============================================
						// MAGES USE MELEE ATTACKS WHEN IN MELEE MODE
						// Mode is determined by UpdateMageCombatMode with buffer zone
						// ============================================
						MountedCombatClass attackerClass = DetermineCombatClass(riderForAttack.get());
						bool mageInMeleeMode = (attackerClass == MountedCombatClass::MageCaster && IsMageInMeleeMode(riderForAttack->formID));
						
						if (attackerClass != MountedCombatClass::MageCaster || mageInMeleeMode)
						{
							PlayMountedAttackAnimation(riderForAttack.get(), targetSide);
						
							if (IsRiderAttacking(riderForAttack.get()))
							{
								UpdateMountedAttackHitDetection(riderForAttack.get(), target);
							}
						}
					}
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
				
				// Use config value for attack angle threshold
				// More lenient for NPCs (AttackAngleNPC) vs Player (AttackAnglePlayer)
				float attackAngleThreshold = targetIsPlayer ? AttackAnglePlayer : AttackAngleNPC;
				
				if (fabs(angleDiff) < attackAngleThreshold)
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
						// ============================================
						// MAGES USE MELEE ATTACKS WHEN IN MELEE MODE
						// Mode is determined by UpdateMageCombatMode with buffer zone
						// ============================================
						MountedCombatClass attackerClass = DetermineCombatClass(riderForAttack.get());
						bool mageInMeleeMode = (attackerClass == MountedCombatClass::MageCaster && IsMageInMeleeMode(riderForAttack->formID));
						
						if (attackerClass != MountedCombatClass::MageCaster || mageInMeleeMode)
						{
							PlayMountedAttackAnimation(riderForAttack.get(), targetSide);
						
							if (IsRiderAttacking(riderForAttack.get()))
							{
								UpdateMountedAttackHitDetection(riderForAttack.get(), target);
							}
						}
					}
				}
			}
		}
		else
		{
			NotifyHorseLeftMeleeRange(horse->formID);
			NotifyHorseLeftMobileTargetRange(horse->formID);
			// Adjacent riding notification removed - system no longer in use
			
			// ============================================
			// APPROACHING TARGET - Use interception for mobile NPCs
			// ============================================
			if (IsTargetMobileNPC(target, horse->formID))
			{
				// Target is a moving NPC (not player) - use interception angle
				// This prevents head-on collisions and circling
				targetAngle = GetMobileTargetInterceptionAngle(horse->formID, horse, target);
			}
			else
			{
				// Target is player or stationary - approach directly
				targetAngle = angleToTarget;
			}
		}
		
		// ============================================
		// APPLY ROTATION
		// ============================================

				// CRITICAL: Skip ALL rotation if in stand ground!
		// Stand ground horses should NOT have any rotation applied here
		if (IsInStandGround(horse->formID))
		{
			// Horse is in stand ground - skip rotation entirely
			// The stand ground code already handled rotation (or locked it)
			// Just update collision avoidance and create travel package if needed
			
			// Don't create travel package for stand ground horses
			return attackState;
		
			}
		
		float currentAngle = horse->rot.z;
		float angleDiff = targetAngle - currentAngle;
		
		while (angleDiff > 3.14159f) angleDiff -= 6.28318f;
		while (angleDiff < -3.14159f) angleDiff += 6.28318f;
		
		float newAngle = currentAngle + (angleDiff * HorseRotationSpeed);  // Use config value
		
		while (newAngle > 3.14159f) newAngle -= 6.28318f;
		while (newAngle < -3.14159f) newAngle += 6.28318f;
		
		horse->rot.z = newAngle;
		
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
				
				// Use horse->processManager directly (validated earlier in function)
				ActorProcessManager* process = horse->processManager;
				if (process && process->unk18.package)
				{
					TESPackage_CopyFlagsFromOtherPackage(package, process->unk18.package);
				}
				
				get_vfunc<_Actor_PutCreatedPackage>(horse, 0xE1)(horse, package, true, 1);
			}
		}
		
		return attackState;
	}
	
	// ============================================
	// CENTRALIZED WEAPON SWITCH SYSTEM
	// ALL riders use this - single/multi, melee/ranged captains
	// NOW USES WeaponDetection's state machine
	// ============================================

	// DEPRECATED: Old weapon switch data - now handled by WeaponDetection
	// Keeping ClearWeaponSwitchData for backward compatibility
	
	bool UpdateRiderWeaponForDistance(Actor* rider, float distanceToTarget, bool targetIsMounted)
	{
		if (!rider) return false;
		
		// Use the centralized weapon state machine from WeaponDetection
		return RequestWeaponForDistance(rider, distanceToTarget, targetIsMounted);
	}
	
	void ClearWeaponSwitchData(UInt32 actorFormID)
	{
		// Forward to centralized system
		ClearWeaponStateData(actorFormID);
	}
	
	void ClearAllWeaponSwitchData()
	{
		// Forward to centralized system
		ResetWeaponStateSystem();
	}
	
	// ============================================
	// RESET ALL DYNAMIC PACKAGE STATE
	// Called on game load/reload to clear stale pointers
	// ============================================

	void ResetDynamicPackageState()
	{
		_MESSAGE("DynamicPackages: === RESETTING ALL STATE ===");
		
		// Clear weapon switch tracking
		ClearAllWeaponSwitchData();
		
		// Clear horse movement tracking
		for (int i = 0; i < 5; i++)
		{
			g_horseMovement[i].isValid = false;
			g_horseMovement[i].horseFormID = 0;
		}
		g_horseMovementCount = 0;
		
		// Clear horse processing tracking
		for (int i = 0; i < 10; i++)
		{
			g_horseProcessing[i].isValid = false;
			g_horseProcessing[i].horseFormID = 0;
		}
		g_horseProcessingCount = 0;
		
		// Clear ranged follow state tracking
		ResetAllRangedFollowState();
		
		// Reset initialization flag so system can re-init
		g_dynamicPackageSystemInitialized = false;
		
		_MESSAGE("DynamicPackages: State reset complete");
	}
	
	
	// ============================================
	// CHECK IF OBSTRUCTION IS CAUSED BY NPC
	// ============================================
	// Returns true if the horse is likely blocked by an NPC (enemy, creature, etc.)
	// In this case we should NOT trigger jump/avoidance - just let combat handle it
	
	static bool IsObstructionCausedByNPC(Actor* horse, Actor* target)
	{
		if (!horse) return false;
		
		// Get horse's current cell
		TESObjectCELL* cell = horse->parentCell;
		if (!cell) return false;
		
		const float NPC_OBSTRUCTION_RANGE = 300.0f;  // Check within 300 units
		
		// Check all actors in cell
		for (UInt32 i = 0; i < cell->objectList.count; i++)
		{
			TESObjectREFR* ref = nullptr;
			cell->objectList.GetNthItem(i, ref);
			
			if (!ref) continue;
			if (ref->formType != kFormType_Character) continue;
			
			Actor* actor = static_cast<Actor*>(ref);
			
			// Skip the horse itself
			if (actor->formID == horse->formID) continue;
			
			// Skip the rider
			NiPointer<Actor> rider;
			if (CALL_MEMBER_FN(horse, GetMountedBy)(rider) && rider)
			{
				if (actor->formID == rider->formID) continue;
			}
			
			// Skip the current combat target (we WANT to engage them)
			if (target && actor->formID == target->formID) continue;
			
			// Skip dead actors
			if (actor->IsDead(1)) continue;
			
			// Calculate distance to this actor
			float dx = actor->pos.x - horse->pos.x;
			float dy = actor->pos.y - horse->pos.y;
			float distance = sqrt(dx * dx + dy * dy);
			
			// Check if this actor is close enough to be causing the obstruction
			if (distance < NPC_OBSTRUCTION_RANGE)
			{
				// Check if actor is in FRONT of the horse (the direction we're trying to go)
				float horseAngle = horse->rot.z;
				float angleToActor = atan2(dx, dy);
				float angleDiff = angleToActor - horseAngle;
				
				// Normalize
				while (angleDiff > 3.14159f) angleDiff -= 6.28318f;
				while (angleDiff < -3.14159f) angleDiff += 6.28318f;
				
				// If actor is within ~90 degrees of where we're facing, they're likely blocking us
				if (fabs(angleDiff) < 1.57f)  // 90 degrees
				{
					const char* actorName = CALL_MEMBER_FN(actor, GetReferenceName)();
					_MESSAGE("DynamicPackages: Obstruction is NPC '%s' (%08X) at distance %.0f - skipping jump/avoidance",
						actorName ? actorName : "Unknown", actor->formID, distance);
					return true;
				}
			}
		}
		
		return false;
	}
}

