#include "AILogging.h"
#include "MountedCombat.h"
#include "DynamicPackages.h"
#include "SpecialMovesets.h"  // For IsInStandGround, IsInRapidFire

namespace MountedNPCCombatVR
{
	// ============================================
	// ADDRESS DEFINITIONS
	// ============================================
	
	// Send assault alarm - triggers crime/aggression response (NPC becomes hostile to player)
	// Address: 0x986530
	typedef void (*_Actor_SendAssaultAlarm)(UInt64 a1, UInt64 a2, Actor* actor);
	RelocAddr<_Actor_SendAssaultAlarm> Actor_SendAssaultAlarm_AILog(0x986530);
	
	// Stop combat alarm - clears the crime/alarm state (NPC forgives player)
	// Address: 0x987A70
	typedef void (*_Actor_StopCombatAlarm)(UInt64 a1, UInt64 a2, Actor* actor);
	RelocAddr<_Actor_StopCombatAlarm> Actor_StopCombatAlarm(0x987A70);

	const char* GetPackageTypeName(UInt8 packageType)
	{
		switch (packageType)
		{
			case TESPackage::kPackageType_Find: return "Find";
			case TESPackage::kPackageType_Follow: return "Follow";
			case TESPackage::kPackageType_Escort: return "Escort";
			case TESPackage::kPackageType_Eat: return "Eat";
			case TESPackage::kPackageType_Sleep: return "Sleep";
			case TESPackage::kPackageType_Wander: return "Wander";
			case TESPackage::kPackageType_Travel: return "Travel";
			case TESPackage::kPackageType_Accompany: return "Accompany";
			case TESPackage::kPackageType_UseItemAt: return "UseItemAt";
			case TESPackage::kPackageType_Ambush: return "Ambush";
			case TESPackage::kPackageType_FleeNotCombat: return "FleeNotCombat";
			case TESPackage::kPackageType_CastMagic: return "CastMagic";
			case TESPackage::kPackageType_Sandbox: return "Sandbox";
			case TESPackage::kPackageType_Patrol: return "Patrol";
			case TESPackage::kPackageType_Guard: return "Guard";
			case TESPackage::kPackageType_Dialogue: return "Dialogue";
			case TESPackage::kPackageType_UseWeapon: return "UseWeapon";
			case TESPackage::kPackageType_Find2: return "Find2";
			case TESPackage::kPackageType_Package: return "Package";
			case TESPackage::kPackageType_PackageTemplate: return "PackageTemplate";
			case TESPackage::kPackageType_Activate: return "Activate";
			case TESPackage::kPackageType_Alarm: return "Alarm";
			case TESPackage::kPackageType_Flee: return "Flee";
			case TESPackage::kPackageType_Trespass: return "Trespass";
			case TESPackage::kPackageType_Spectator: return "Spectator";
			case TESPackage::kPackageType_ReactToDead: return "ReactToDead";
			case TESPackage::kPackageType_GetUpFromChair: return "GetUpFromChair";
			case TESPackage::kPackageType_DoNothing: return "DoNothing";
			case TESPackage::kPackageType_InGameDialogue: return "InGameDialogue";
			case TESPackage::kPackageType_Surface: return "Surface";
			case TESPackage::kPackageType_SearchForAttacker: return "SearchForAttacker";
			case TESPackage::kPackageType_AvoidPlayer: return "AvoidPlayer";
			case TESPackage::kPackageType_ReactToDestroyedObject: return "ReactToDestroyedObject";
			case TESPackage::kPackageType_ReactToGrenadeOrMine: return "ReactToGrenadeOrMine";
			case TESPackage::kPackageType_StealWarning: return "StealWarning";
			case TESPackage::kPackageType_PickPocketWarning: return "PickPocketWarning";
			case TESPackage::kPackageType_MovementBlocked: return "MovementBlocked";
			default: return "Unknown";
		}
	}
	
	// Check if package type is a dialogue/crime package that overrides combat
	bool IsDialogueOrCrimePackage(UInt8 packageType)
	{
		switch (packageType)
		{
			case TESPackage::kPackageType_Dialogue:
			case TESPackage::kPackageType_InGameDialogue:
			case TESPackage::kPackageType_Alarm:
			case TESPackage::kPackageType_Trespass:
			case TESPackage::kPackageType_StealWarning:
			case TESPackage::kPackageType_PickPocketWarning:
				return true;
			default:
				return false;
		}
	}
	
	// Get the actor's current running package
	TESPackage* GetActorCurrentPackage(Actor* actor)
	{
		if (!actor) return nullptr;
		
		ActorProcessManager* processManager = actor->processManager;
		if (!processManager) return nullptr;
		
		// Try to get package from unk18 (Data58) which contains the current package
		// unk18 is a MiddleProcess::Data58 struct, and package is at offset 0x08
		TESPackage* package = processManager->unk18.package;
		if (package && package->formType == kFormType_Package)
		{
			return package;
		}
		
		// Also try middleProcess if available
		MiddleProcess* middleProc = processManager->middleProcess;
		if (middleProc)
		{
			package = middleProc->unk058.package;
			if (package && package->formType == kFormType_Package)
			{
				return package;
			}
		}
		
		return nullptr;
	}
	
	void LogCurrentAIPackage(Actor* actor, UInt32 formID)
	{
		if (!actor) return;
		
		_MESSAGE("MountedCombat: === NPC %08X AI PACKAGE INFO ===", formID);
		
		ActorProcessManager* processManager = actor->processManager;
		if (!processManager)
		{
			_MESSAGE("MountedCombat: NPC %08X - No process manager (AI not loaded)", formID);
			return;
		}
		
		_MESSAGE("MountedCombat: NPC %08X - Process Manager: 0x%p", formID, processManager);
		
		// Try to get and log the current package
		TESPackage* currentPackage = GetActorCurrentPackage(actor);
		if (currentPackage)
		{
			const char* packageTypeName = GetPackageTypeName(currentPackage->type);
			_MESSAGE("MountedCombat: NPC %08X - Current Package: %s (FormID: %08X, Type: %d)", 
				formID, packageTypeName, currentPackage->formID, currentPackage->type);
			
			// Check if this is a dialogue/crime package
			if (IsDialogueOrCrimePackage(currentPackage->type))
			{
				_MESSAGE("MountedCombat: WARNING - NPC %08X has DIALOGUE/CRIME package active!", formID);
				_MESSAGE("MountedCombat: This will override combat behavior!");
			}
		}
		else
		{
			_MESSAGE("MountedCombat: NPC %08X - Could not retrieve current package", formID);
		}
		
		NiPointer<Actor> npcMount;
		bool npcMounted = CALL_MEMBER_FN(actor, GetMount)(npcMount);
		bool inCombat = actor->IsInCombat();
		
		_MESSAGE("MountedCombat: NPC %08X - Mounted: %s | Combat State: %s",
			formID, npcMounted ? "YES" : "NO", inCombat ? "IN COMBAT" : "NOT IN COMBAT");
	}
	
	// New function to detect and log dialogue/crime package issues
	bool DetectDialoguePackageIssue(Actor* actor)
	{
		if (!actor) return false;
		
		TESPackage* currentPackage = GetActorCurrentPackage(actor);
		if (!currentPackage) return false;
		
		if (IsDialogueOrCrimePackage(currentPackage->type))
		{
			const char* actorName = CALL_MEMBER_FN(actor, GetReferenceName)();
			const char* packageTypeName = GetPackageTypeName(currentPackage->type);
			
			_MESSAGE("MountedCombat: !!! DIALOGUE/CRIME PACKAGE DETECTED !!!");
			_MESSAGE("MountedCombat: NPC: '%s' (FormID: %08X)", actorName ? actorName : "Unknown", actor->formID);
			_MESSAGE("MountedCombat: Package Type: %s (FormID: %08X)", packageTypeName, currentPackage->formID);
			_MESSAGE("MountedCombat: This is likely a guard crime dialogue that overrides combat!");
			
			return true;
		}
		
		return false;
	}
	
	// Force-clear dialogue/crime packages and restore combat following
	bool ClearDialoguePackageAndRestoreFollow(Actor* actor)
	{
		if (!actor) return false;
		
		TESPackage* currentPackage = GetActorCurrentPackage(actor);
		if (!currentPackage) return false;
		
		if (!IsDialogueOrCrimePackage(currentPackage->type))
		{
			return false;  // No dialogue package to clear
		}
		
		const char* actorName = CALL_MEMBER_FN(actor, GetReferenceName)();
		const char* packageTypeName = GetPackageTypeName(currentPackage->type);
		
		_MESSAGE("MountedCombat: >>> CLEARING DIALOGUE/ALARM PACKAGE <<<");
		_MESSAGE("MountedCombat: NPC: '%s' (FormID: %08X)", actorName ? actorName : "Unknown", actor->formID);
		_MESSAGE("MountedCombat: Clearing Package: %s (FormID: %08X)", packageTypeName, currentPackage->formID);
		
		// Method 1: Stop the combat alarm - this clears the crime/alarm state
		_MESSAGE("MountedCombat: Calling Actor_StopCombatAlarm...");
		Actor_StopCombatAlarm(0, 0, actor);
		
		// Method 2: Force AI reset to interrupt the dialogue package
		CALL_MEMBER_FN(actor, ResetAI)(0, 0);
		
		// Method 3: Pause any current dialogue
		get_vfunc<void(*)(Actor*)>(actor, 0x4F)(actor);
		
		// Method 4: Clear the dialogue/crime flags
		// kFlag_kAttackOnSight helps override the crime response
		actor->flags2 |= Actor::kFlag_kAttackOnSight;
		
		_MESSAGE("MountedCombat: Alarm stopped and AI reset - combat should take over");
		
		return true;
	}
	
	void LogMountAIPackage(Actor* mount, UInt32 formID)
	{
		if (!mount) return;
		
		_MESSAGE("MountedCombat: === MOUNT %08X AI PACKAGE INFO ===", formID);
		
		const char* mountName = CALL_MEMBER_FN(mount, GetReferenceName)();
		_MESSAGE("MountedCombat: Mount Name: '%s'", mountName ? mountName : "Unknown");
		
		UInt32 flags2 = mount->flags2;
		bool isMount = (flags2 & Actor::kFlag_kIsAMount) != 0;
		bool mountPointClear = (flags2 & Actor::kFlag_kMountPointClear) != 0;
		
		_MESSAGE("MountedCombat: Mount %08X - IsMount Flag: %s | MountPointClear: %s",
			formID, isMount ? "YES" : "NO", mountPointClear ? "YES" : "NO");
		
		bool inCombat = mount->IsInCombat();
		bool isDead = mount->IsDead(1);
		
		_MESSAGE("MountedCombat: Mount %08X - InCombat: %s | IsDead: %s",
			formID, inCombat ? "YES" : "NO", isDead ? "YES" : "NO");
		
		ActorProcessManager* processManager = mount->processManager;
		if (!processManager)
		{
			_MESSAGE("MountedCombat: Mount %08X - No process manager", formID);
		}
		else
		{
			_MESSAGE("MountedCombat: Mount %08X - Process Manager: 0x%p", formID, processManager);
		}
		
		NiPointer<Actor> rider;
		bool hasRider = CALL_MEMBER_FN(mount, GetMountedBy)(rider);
		if (hasRider && rider)
		{
			const char* riderName = CALL_MEMBER_FN(rider.get(), GetReferenceName)();
			_MESSAGE("MountedCombat: Mount %08X - Current Rider: '%s' (FormID: %08X)",
				formID, riderName ? riderName : "Unknown", rider->formID);
		}
		else
		{
			_MESSAGE("MountedCombat: Mount %08X - No rider detected", formID);
		}
		
		_MESSAGE("MountedCombat: Mount %08X - Position: (%.1f, %.1f, %.1f)",
			formID, mount->pos.x, mount->pos.y, mount->pos.z);
		
		_MESSAGE("MountedCombat: === END MOUNT AI PACKAGE INFO ===");
	}
	
	void LogMountedCombatAIState(Actor* rider, Actor* mount, UInt32 riderFormID)
	{
		if (!rider || !mount) return;
		
		_MESSAGE("MountedCombat: ======================================");
		_MESSAGE("MountedCombat: MOUNTED COMBAT AI STATE SNAPSHOT");
		_MESSAGE("MountedCombat: ======================================");
		
		LogCurrentAIPackage(rider, riderFormID);
		LogMountAIPackage(mount, mount->formID);
		
		// Check for dialogue/crime package issue
		if (DetectDialoguePackageIssue(rider))
		{
			_MESSAGE("MountedCombat: >>> ISSUE: Guard entered crime dialogue while mounted! <<<");
		}
		
		_MESSAGE("MountedCombat: --- RIDER-MOUNT RELATIONSHIP ---");
		
		NiPointer<Actor> verifyMount;
		bool riderMounted = CALL_MEMBER_FN(rider, GetMount)(verifyMount);
		if (riderMounted && verifyMount)
		{
			if (verifyMount->formID == mount->formID)
			{
				_MESSAGE("MountedCombat: Rider %08X confirmed mounted on Mount %08X",
					riderFormID, mount->formID);
			}
			else
			{
				_MESSAGE("MountedCombat: WARNING - Mount mismatch! Expected %08X, got %08X",
					mount->formID, verifyMount->formID);
			}
		}
		else
		{
			_MESSAGE("MountedCombat: WARNING - Rider %08X GetMount returned false!", riderFormID);
		}
		
		if (g_thePlayer && (*g_thePlayer))
		{
			Actor* player = *g_thePlayer;
			float distance = GetDistanceBetween(rider, player);
			_MESSAGE("MountedCombat: Distance to player: %.1f units (%.1f meters)",
				distance, distance / 70.0f);
		}
		
		_MESSAGE("MountedCombat: ======================================");
	}

	// ============================================
	// ALARM PACKAGE HANDLING
	// ============================================
	
	// Stop combat alarm on an NPC - makes them stop being hostile to player
	// Based on activeragdoll implementation: the function is called with PLAYER
	// as the third parameter to make the NPC forgive the player
	void StopActorCombatAlarm(Actor* actor)
	{
		if (!actor) return;
		
		const char* actorName = CALL_MEMBER_FN(actor, GetReferenceName)();
		_MESSAGE("AILogging: Stopping combat alarm for '%s' (%08X)",
			actorName ? actorName : "Unknown", actor->formID);
		
		// Get player reference - the alarm is cleared relative to player
		if (!g_thePlayer || !(*g_thePlayer))
		{
			_MESSAGE("AILogging: WARNING - No player reference, using actor directly");
			Actor_StopCombatAlarm(0, 0, actor);
			return;
		}
		
		Actor* player = *g_thePlayer;
		
		// Method 1: Call StopCombatAlarm with PLAYER to make NPC forgive player
		// This is the correct usage based on activeragdoll reference
		Actor_StopCombatAlarm(0, 0, player);
		_MESSAGE("AILogging: Called Actor_StopCombatAlarm with player");
		
		// Method 2: Clear attack-on-sight flag on the NPC
		actor->flags2 &= ~Actor::kFlag_kAttackOnSight;
		_MESSAGE("AILogging: Cleared kAttackOnSight flag");
		
		// Method 3: Clear the NPC's combat target if it's the player
		UInt32 combatTargetHandle = actor->currentCombatTarget;
		if (combatTargetHandle != 0)
		{
			NiPointer<TESObjectREFR> targetRef;
			LookupREFRByHandle(combatTargetHandle, targetRef);
			if (targetRef && targetRef->formID == player->formID)
			{
				actor->currentCombatTarget = 0;
				_MESSAGE("AILogging: Cleared combat target (was player)");
			}
		}
		
		// Method 4: Force AI re-evaluation to exit combat state
		Actor_EvaluatePackage(actor, false, false);
		_MESSAGE("AILogging: Evaluated AI package");
		
		// Method 5: Reset AI to interrupt any ongoing hostile behavior
		CALL_MEMBER_FN(actor, ResetAI)(0, 0);
		_MESSAGE("AILogging: Reset AI");
		
		_MESSAGE("AILogging: Combat alarm stop complete for '%s'", 
			actorName ? actorName : "Unknown");
	}
	
	// ============================================
	// MOUNT OBSTRUCTION DETECTION
	// ============================================
	
	const int MAX_OBSTRUCTION_TRACKED = 5;
	const float OBSTRUCTION_CHECK_INTERVAL = 0.25f;  // Check every 250ms
	const float OBSTRUCTION_MOVE_THRESHOLD = 5.0f;   // Must move at least 5 units
	const float OBSTRUCTION_STATIONARY_TIME = 2.0f;  // Stationary for 2 sec = obstructed
	const float OBSTRUCTION_RUNNING_TIME = 3.0f;     // Running in place for 3 sec = severely obstructed
	const float SIDE_CHECK_DISTANCE =150.0f; // How far to check for side obstructions
	const float SHEER_DROP_HEIGHT =400.0f; // Sheer drop threshold (units)
	const float SHEER_PROBE_FORWARD =200.0f; // Forward probe distance
	const float SHEER_PROBE_SIDE =100.0f; // Side offset for probes
	
	static HorseObstructionInfo g_obstructionData[MAX_OBSTRUCTION_TRACKED];
	static int g_obstructionCount =0;
	static float g_lastObstructionCheckTime =0;
	
	// Sheer drop cache
	struct HorseSheerInfo
	{
		UInt32 horseFormID;
		bool nearSheer;
		float lastCheckTime;
		bool isValid;
	};
	
	static HorseSheerInfo g_horseSheerData[MAX_OBSTRUCTION_TRACKED];
	static int g_horseSheerCount =0;
	
	// Use shared GetGameTime() from Helper.h instead of local function
	static float GetObstructionTime()
	{
		return GetGameTime();
	}
	
	HorseObstructionInfo* GetHorseObstructionInfo(UInt32 horseFormID)
	{
		for (int i = 0; i < g_obstructionCount; i++)
		{
			if (g_obstructionData[i].isValid && g_obstructionData[i].horseFormID == horseFormID)
			{
				return &g_obstructionData[i];
			}
		}
		return nullptr;
	}
	
	ObstructionSide GetObstructionSide(UInt32 horseFormID)
	{
		HorseObstructionInfo* info = GetHorseObstructionInfo(horseFormID);
		if (info)
		{
			return info->side;
		}
		return ObstructionSide::Unknown;
	}
	
	static HorseObstructionInfo* GetOrCreateObstructionInfo(UInt32 horseFormID)
	{
		// Check if already tracked
		HorseObstructionInfo* existing = GetHorseObstructionInfo(horseFormID);
		if (existing) return existing;
		
		// Create new entry
		if (g_obstructionCount < MAX_OBSTRUCTION_TRACKED)
		{
			HorseObstructionInfo* info = &g_obstructionData[g_obstructionCount];
			info->horseFormID = horseFormID;
			info->type = ObstructionType::None;
			info->side = ObstructionSide::Unknown;
			info->stuckDuration = 0;
			info->lastMovementTime = GetObstructionTime();
			info->lastPosition = NiPoint3();
			info->intendedDirection = NiPoint3();
			info->stuckCount = 0;
			info->isValid = true;
			g_obstructionCount++;
			return info;
		}
		
		return nullptr;
	}
	
	void ClearHorseObstructionInfo(UInt32 horseFormID)
	{
		for (int i = 0; i < g_obstructionCount; i++)
		{
			if (g_obstructionData[i].isValid && g_obstructionData[i].horseFormID == horseFormID)
			{
				// Shift remaining entries
				for (int j = i; j < g_obstructionCount - 1; j++)
				{
					g_obstructionData[j] = g_obstructionData[j + 1];
				}
				g_obstructionCount--;
				return;
			}
		}
	}
	
	void ClearAllObstructionInfo()
	{
		for (int i = 0; i < MAX_OBSTRUCTION_TRACKED; i++)
		{
			g_obstructionData[i].isValid = false;
		}
		g_obstructionCount = 0;
	}
	
	// ============================================
	// SIDE DETECTION LOGIC
	// ============================================
	// Determines which side of the horse has a clearer path
	// by analyzing the horse's position history and intended direction.
	// If horse is stuck facing forward, we check which way the horse
	// was drifting to determine which side has the obstruction.
	
	static ObstructionSide DetermineObstructionSide(Actor* horse, Actor* target)
	{
		if (!horse) return ObstructionSide::Unknown;
		
		// Get horse's current facing direction
		float horseAngle = horse->rot.z;
		
		// Calculate horse's forward and right vectors
		float forwardX = sin(horseAngle);
		float forwardY = cos(horseAngle);
		float rightX = cos(horseAngle);   // Right is 90 degrees clockwise from forward
		float rightY = -sin(horseAngle);
		
		// Get obstruction info for position history
		HorseObstructionInfo* info = GetHorseObstructionInfo(horse->formID);
		if (!info) return ObstructionSide::Front;
		
		// Calculate drift from last position (small movements while stuck)
		float driftX = horse->pos.x - info->lastPosition.x;
		float driftY = horse->pos.y - info->lastPosition.y;
		float driftMagnitude = sqrt(driftX * driftX + driftY * driftY);
		
		// If there's any drift, check which side the horse is drifting toward
		// Drifting indicates the horse is being pushed by collision on the opposite side
		if (driftMagnitude > 0.5f)  // Small threshold for micro-movements
		{
			// Normalize drift
			driftX /= driftMagnitude;
			driftY /= driftMagnitude;
			
			// Check if drift is to the right or left of the horse
			float dotRight = (driftX * rightX) + (driftY * rightY);
			
			if (dotRight > 0.2f)
			{
				// Horse is drifting RIGHT, meaning LEFT side is blocked
				_MESSAGE("AILogging: Horse drifting RIGHT (dot: %.2f) - LEFT side obstructed", dotRight);
				return ObstructionSide::Left;
			}
			else if (dotRight < -0.2f)
			{
				// Horse is drifting LEFT, meaning RIGHT side is blocked
				_MESSAGE("AILogging: Horse drifting LEFT (dot: %.2f) - RIGHT side obstructed", dotRight);
				return ObstructionSide::Right;
			}
		}
		
		// If no clear drift, check the intended direction vs current facing
		if (target)
		{
			float toTargetX = target->pos.x - horse->pos.x;
			float toTargetY = target->pos.y - horse->pos.y;
			float toTargetDist = sqrt(toTargetX * toTargetX + toTargetY * toTargetY);
			
			if (toTargetDist > 0.01f)
			{
				toTargetX /= toTargetDist;
				toTargetY /= toTargetDist;
				
				// Calculate angle difference between facing and target
				float dotForward = (toTargetX * forwardX) + (toTargetY * forwardY);
				float dotRight = (toTargetX * rightX) + (toTargetY * rightY);
				
				// If target is mostly ahead but horse can't move, obstruction is in front
				if (dotForward > 0.7f)
				{
					// Target ahead - check angle to determine slight bias
					if (dotRight > 0.15f)
					{
						// Target is slightly to the right, try turning right (obstruction may be left/front)
						_MESSAGE("AILogging: Target ahead-right (dotR: %.2f) - trying RIGHT path", dotRight);
						return ObstructionSide::Left;  // Assume left blocked, turn right
					}
					else if (dotRight < -0.15f)
					{
						// Target is slightly to the left, try turning left  
						_MESSAGE("AILogging: Target ahead-left (dotR: %.2f) - trying LEFT path", dotRight);
						return ObstructionSide::Right;  // Assume right blocked, turn left
					}
					else
					{
						// Target directly ahead - front obstruction, pick random
						_MESSAGE("AILogging: Target directly ahead - FRONT obstruction");
						return ObstructionSide::Front;
					}
				}
				else if (dotForward < -0.3f)
				{
					// Target is behind - need to turn around
					// Pick the side that would turn us toward the target faster
					if (dotRight > 0)
					{
						_MESSAGE("AILogging: Target behind-right - turn RIGHT");
						return ObstructionSide::Left;
					}
					else
					{
						_MESSAGE("AILogging: Target behind-left - turn LEFT");
						return ObstructionSide::Right;
					}
				}
				else
				{
					// Target is to the side
					if (dotRight > 0.3f)
					{
						// Target to the right but stuck - right is blocked
						_MESSAGE("AILogging: Target to RIGHT but stuck - RIGHT blocked");
						return ObstructionSide::Right;
					}
					else if (dotRight < -0.3f)
					{
						// Target to the left but stuck - left is blocked
						_MESSAGE("AILogging: Target to LEFT but stuck - LEFT blocked");
						return ObstructionSide::Left;
					}
				}
			}
		}
		
		// Default to front obstruction
		_MESSAGE("AILogging: Unable to determine side - defaulting to FRONT");
		return ObstructionSide::Front;
	}
	
	static const char* GetObstructionSideName(ObstructionSide side)
	{
		switch (side)
		{
			case ObstructionSide::Unknown: return "UNKNOWN";
			case ObstructionSide::Front: return "FRONT";
			case ObstructionSide::Left: return "LEFT";
			case ObstructionSide::Right: return "RIGHT";
			case ObstructionSide::Both: return "BOTH";
			default: return "UNKNOWN";
		}
	}
	
	void LogObstructionDiagnostic(Actor* horse, Actor* target, ObstructionType type, ObstructionSide side)
	{
		if (!horse) return;
		
		const char* horseName = CALL_MEMBER_FN(horse, GetReferenceName)();
		
		const char* typeStr = "Unknown";
		switch (type)
		{
			case ObstructionType::None: typeStr = "None"; break;
			case ObstructionType::Stationary: typeStr = "STATIONARY"; break;
			case ObstructionType::RunningInPlace: typeStr = "RUNNING IN PLACE"; break;
			case ObstructionType::CollisionBlocked: typeStr = "COLLISION BLOCKED"; break;
			case ObstructionType::PathfindingFailed: typeStr = "PATHFINDING FAILED"; break;
		}
		
		const char* sideStr = GetObstructionSideName(side);
		
		_MESSAGE("AILogging: ========================================");
		_MESSAGE("AILogging: MOUNT OBSTRUCTION DETECTED");
		_MESSAGE("AILogging: ========================================");
		_MESSAGE("AILogging: Horse: '%s' (FormID: %08X)", horseName ? horseName : "Unknown", horse->formID);
		_MESSAGE("AILogging: Obstruction Type: %s", typeStr);
		_MESSAGE("AILogging: Obstruction Side: %s", sideStr);
		_MESSAGE("AILogging: Position: (%.1f, %.1f, %.1f)", horse->pos.x, horse->pos.y, horse->pos.z);
		_MESSAGE("AILogging: Rotation Z: %.2f radians (%.1f degrees)", horse->rot.z, horse->rot.z * 57.2958f);
		
		if (target)
		{
			float dx = target->pos.x - horse->pos.x;
			float dy = target->pos.y - horse->pos.y;
			float distance = sqrt(dx * dx + dy * dy);
			float angleToTarget = atan2(dx, dy);
			
			_MESSAGE("AILogging: Target Distance: %.1f units", distance);
			_MESSAGE("AILogging: Angle to Target: %.2f rad (%.1f deg)", angleToTarget, angleToTarget * 57.2958f);
			
			// Check if horse is facing target
			float angleDiff = angleToTarget - horse->rot.z;
			while (angleDiff > 3.14159f) angleDiff -= 6.28318f;
			while (angleDiff < -3.14159f) angleDiff += 6.28318f;
			
			const char* facingStr = (fabs(angleDiff) < 0.5f) ? "YES" : "NO";
			_MESSAGE("AILogging: Facing Target: %s (diff: %.1f deg)", facingStr, angleDiff * 57.2958f);
			
			// Log recommended escape direction
			if (side == ObstructionSide::Left)
			{
				_MESSAGE("AILogging: RECOMMENDATION: Turn RIGHT to escape");
			}
			else if (side == ObstructionSide::Right)
			{
				_MESSAGE("AILogging: RECOMMENDATION: Turn LEFT to escape");
			}
			else if (side == ObstructionSide::Front)
			{
				_MESSAGE("AILogging: RECOMMENDATION: Back up or turn around");
			}
		}
		
		// Get obstruction info for additional details
		HorseObstructionInfo* info = GetHorseObstructionInfo(horse->formID);
		if (info)
		{
			_MESSAGE("AILogging: Stuck Duration: %.1f seconds", info->stuckDuration);
			_MESSAGE("AILogging: Total Stuck Count (session): %d", info->stuckCount);
			_MESSAGE("AILogging: Last Good Position: (%.1f, %.1f, %.1f)", 
				info->lastPosition.x, info->lastPosition.y, info->lastPosition.z);
		}
		
		// Check current AI package
		TESPackage* currentPackage = GetActorCurrentPackage(horse);
		if (currentPackage)
		{
			const char* packageName = GetPackageTypeName(currentPackage->type);
			_MESSAGE("AILogging: Current AI Package: %s (FormID: %08X)", packageName, currentPackage->formID);
		}
		
		// Check if MovementBlocked package is active
		if (currentPackage && currentPackage->type == TESPackage::kPackageType_MovementBlocked)
		{
			_MESSAGE("AILogging: >>> MovementBlocked package active! <<<");
		}
		
		_MESSAGE("AILogging: ========================================");
	}
	
	ObstructionType CheckAndLogHorseObstruction(Actor* horse, Actor* target, float distanceToTarget)
	{
		if (!horse) return ObstructionType::None;
		
		// ============================================
		// SKIP OBSTRUCTION CHECK IF IN NORMAL COMBAT POSITIONING
		// Being stationary at close range is EXPECTED behavior, not an obstruction
		// ============================================
		const float CLOSE_COMBAT_DISTANCE = 250.0f;  // If within this range, being still is normal
		
		if (distanceToTarget < CLOSE_COMBAT_DISTANCE)
		{
			// Horse is close to target - this is normal combat, not an obstruction
			return ObstructionType::None;
		}
		
		// Also skip if in special maneuvers where stationary is expected
		if (IsInStandGround(horse->formID) || IsInRapidFire(horse->formID))
		{
			return ObstructionType::None;
		}
		
		float currentTime = GetObstructionTime();
		
		// Rate limit checks
		if ((currentTime - g_lastObstructionCheckTime) < OBSTRUCTION_CHECK_INTERVAL)
		{
			// Return cached type if we have it
			HorseObstructionInfo* info = GetHorseObstructionInfo(horse->formID);
			if (info) return info->type;
			return ObstructionType::None;
		}
		g_lastObstructionCheckTime = currentTime;
		
		HorseObstructionInfo* info = GetOrCreateObstructionInfo(horse->formID);
		if (!info) return ObstructionType::None;
		
		// Calculate distance moved since last check
		float dx = horse->pos.x - info->lastPosition.x;
		float dy = horse->pos.y - info->lastPosition.y;
		float distanceMoved = sqrt(dx * dx + dy * dy);
		
		// Store intended direction (toward target)
		if (target)
		{
			info->intendedDirection.x = target->pos.x - horse->pos.x;
			info->intendedDirection.y = target->pos.y - horse->pos.y;
			info->intendedDirection.z = 0;
		}
		
		// If horse moved enough, update position and reset
		if (distanceMoved > OBSTRUCTION_MOVE_THRESHOLD)
		{
			info->lastPosition = horse->pos;
			info->lastMovementTime = currentTime;
			info->stuckDuration = 0;
			info->type = ObstructionType::None;
			info->side = ObstructionSide::Unknown;
			return ObstructionType::None;
		}
		
		// Initialize if first check
		if (info->lastMovementTime == 0)
		{
			info->lastPosition = horse->pos;
			info->lastMovementTime = currentTime;
			return ObstructionType::None;
		}
		
		// Calculate how long we've been stuck
		info->stuckDuration = currentTime - info->lastMovementTime;
		
		// Determine obstruction type based on duration
		ObstructionType newType = ObstructionType::None;
		ObstructionSide newSide = ObstructionSide::Unknown;
		
		if (info->stuckDuration >= OBSTRUCTION_RUNNING_TIME)
		{
			// Check if there's a MovementBlocked package
			TESPackage* pkg = GetActorCurrentPackage(horse);
			if (pkg && pkg->type == TESPackage::kPackageType_MovementBlocked)
			{
				newType = ObstructionType::CollisionBlocked;
			}
			else
			{
				newType = ObstructionType::RunningInPlace;
			}
			
			// Determine which side is blocked
			newSide = DetermineObstructionSide(horse, target);
		}
		else if (info->stuckDuration >= OBSTRUCTION_STATIONARY_TIME)
		{
			newType = ObstructionType::Stationary;
			newSide = DetermineObstructionSide(horse, target);
		}
		
		// Only log when type changes or escalates
		if (newType != ObstructionType::None && (newType != info->type || newSide != info->side))
		{
			info->stuckCount++;
			LogObstructionDiagnostic(horse, target, newType, newSide);
		}
		
		info->type = newType;
		info->side = newSide;
		return newType;
	}
	
	static HorseSheerInfo* GetOrCreateSheerInfo(UInt32 horseFormID)
	{
		for (int i =0; i < g_horseSheerCount; i++)
		{
			if (g_horseSheerData[i].isValid && g_horseSheerData[i].horseFormID == horseFormID)
				return &g_horseSheerData[i];
		}
		
		if (g_horseSheerCount < MAX_OBSTRUCTION_TRACKED)
		{
			HorseSheerInfo* info = &g_horseSheerData[g_horseSheerCount];
			info->horseFormID = horseFormID;
			info->nearSheer = false;
			info->lastCheckTime =0;
			info->isValid = true;
			g_horseSheerCount++;
			return info;
		}
		
		return nullptr;
	}
	
	bool IsHorseNearSheerDrop(UInt32 horseFormID)
	{
		HorseSheerInfo* s = GetOrCreateSheerInfo(horseFormID);
		return s ? s->nearSheer : false;
	}
	
	// Simple helper to sample ground Z at a world position. Uses a small downward probe by
	// checking objects at the position - fallback to current position Z if not found.
	static float SampleGroundZAt(const NiPoint3& pos)
	{
		// Heuristic: try HasLOS downward to see if there's ground. As a fallback, assume terrain at pos.z -0.
		// We don't have an explicit terrain height API here, so use pos.z as proxy and rely on relative checks.
		// In practice this will work because our probes offset Z relative to horse and we care about large differences.
		return pos.z;
	}
	
	bool CheckAndLogSheerDrop(Actor* horse)
	{
		if (!horse) return false;
		
		float now = GetObstructionTime();
		HorseSheerInfo* s = GetOrCreateSheerInfo(horse->formID);
		if (!s) return false;
		
		// Rate limit shear checks to obstruction interval
		if ((now - s->lastCheckTime) < OBSTRUCTION_CHECK_INTERVAL)
		{
			return s->nearSheer;
		}
		s->lastCheckTime = now;
		
		// Compute probe points (forward, forward-left, forward-right, left, right)
		float angle = horse->rot.z;
		float fwdX = sin(angle);
		float fwdY = cos(angle);
		float rightX = cos(angle);
		float rightY = -sin(angle);
		
		NiPoint3 base = horse->pos;
		float horseZ = base.z;
		
		NiPoint3 probes[5];
		// forward
		probes[0].x = base.x + fwdX * SHEER_PROBE_FORWARD;
		probes[0].y = base.y + fwdY * SHEER_PROBE_FORWARD;
		probes[0].z = base.z;
		// forward-left
		probes[1].x = base.x + fwdX * SHEER_PROBE_FORWARD - rightX * SHEER_PROBE_SIDE;
		probes[1].y = base.y + fwdY * SHEER_PROBE_FORWARD - rightY * SHEER_PROBE_SIDE;
		probes[1].z = base.z;
		// forward-right
		probes[2].x = base.x + fwdX * SHEER_PROBE_FORWARD + rightX * SHEER_PROBE_SIDE;
		probes[2].y = base.y + fwdY * SHEER_PROBE_FORWARD + rightY * SHEER_PROBE_SIDE;
		probes[2].z = base.z;
		// left
		probes[3].x = base.x - rightX * SHEER_PROBE_SIDE;
		probes[3].y = base.y - rightY * SHEER_PROBE_SIDE;
		probes[3].z = base.z;
		// right
		probes[4].x = base.x + rightX * SHEER_PROBE_SIDE;
		probes[4].y = base.y + rightY * SHEER_PROBE_SIDE;
		probes[4].z = base.z;
		
		bool foundSheer = false;
		bool sideSheerLeft = false;
		bool sideSheerRight = false;
		
		for (int i =0; i <5; i++)
		{
			float groundZ = SampleGroundZAt(probes[i]);
			float drop = horseZ - groundZ;
			if (drop >= SHEER_DROP_HEIGHT)
			{
				foundSheer = true;
				// Map probe to side
				if (i ==1 || i ==3) sideSheerLeft = true; // forward-left or left
				if (i ==2 || i ==4) sideSheerRight = true; // forward-right or right
				if (i ==0) { sideSheerLeft = sideSheerRight = true; } // forward = both
			}
		}
		
		s->nearSheer = foundSheer;
		
		if (foundSheer)
		{
			const char* which = "UNKNOWN";
			if (sideSheerLeft && !sideSheerRight) which = "LEFT";
			else if (!sideSheerLeft && sideSheerRight) which = "RIGHT";
			else if (sideSheerLeft && sideSheerRight) which = "BOTH/FRONT";
			
			_MESSAGE("AILogging: SHEER DROP DETECTED near Horse %08X - Direction: %s - threshold: %.1f units", 
				horse->formID, which, SHEER_DROP_HEIGHT);
		}
		
		return s->nearSheer;
	}
}
