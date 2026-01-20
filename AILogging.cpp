#include "AILogging.h"
#include "MountedCombat.h"
#include "DynamicPackages.h"
#include "SpecialMovesets.h" // For IsInStandGround, IsInRapidFire
#include <mutex>
#include <vector>
#include <thread>

namespace MountedNPCCombatVR
{
	// Threading support for queuing StopCombatAlarm from worker threads
	static DWORD g_mainThreadId =0;
	static std::mutex g_stopQueueMutex;
	static std::vector<UInt32> g_stopAlarmQueue;

	void SetAILoggingMainThreadId(DWORD id)
	{
		g_mainThreadId = id;
	}

	void ProcessPendingStopCombatAlarms()
	{
		// Only process on main thread
		if (g_mainThreadId ==0 || GetCurrentThreadId() != g_mainThreadId) return;

		std::vector<UInt32> pending;
		{
			std::lock_guard<std::mutex> lock(g_stopQueueMutex);
			if (g_stopAlarmQueue.empty()) return;
			pending.swap(g_stopAlarmQueue);
		}

		for (UInt32 formID : pending)
		{
			TESForm* form = LookupFormByID(formID);
			if (!form) continue;
			Actor* actor = DYNAMIC_CAST(form, TESForm, Actor);
			if (!actor) continue;
			// Call directly on main thread
			StopActorCombatAlarm(actor);
		}
	}

	// ============================================
	// ADDRESS DEFINITIONS
	// ============================================
	
	// No longer using crime/alarm handling addresses - logging only

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
	
	// Detect and log dialogue/crime package issues (LOGGING ONLY - no handling)
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
		
		// Check for dialogue/crime package issue (logging only)
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
	// Used by HorseMountScanner to stop combat so NPCs can remount
	// Also used by CombatStyles/MultiMountedCombat to disengage from distant targets
	// ============================================
	
	// Stop combat alarm - clears the crime/alarm state (NPC forgives player)
	// Address: 0x987A70 (Skyrim VR 1.4.15)
	// First two params are unused - just pass 0
	typedef void (*_Actor_StopCombatAlarm)(UInt64 a1, UInt64 a2, Actor* actor);
	RelocAddr<_Actor_StopCombatAlarm> Actor_StopCombatAlarm_Native(0x987A70);
	
	// ============================================
	// COOLDOWN SYSTEM FOR MULTIPLE DISENGAGEMENTS
	// Prevents CTD when multiple riders disengage in rapid succession
	// ============================================
	
	struct CombatAlarmCooldown
	{
		UInt32 actorFormID;
		float lastCallTime;
		bool isValid;
	};
	
	static const int MAX_ALARM_COOLDOWNS = 20;  // Increased for multi-rider scenarios
	static CombatAlarmCooldown g_alarmCooldowns[MAX_ALARM_COOLDOWNS];
	static int g_alarmCooldownCount = 0;
	static float g_lastGlobalAlarmCallTime = 0;
	static const float GLOBAL_ALARM_COOLDOWN = 1.5f;  // 1.5s between ANY alarm calls (increased for safety)
	static const float PER_ACTOR_COOLDOWN = 5.0f;   // 5 seconds per actor (increased for safety)
	
	// ============================================
	// DISENGAGE QUEUE SYSTEM
	// For multi-rider scenarios - queue disengagements to spread them out
	// ============================================
	
	struct QueuedDisengage
	{
		UInt32 actorFormID;
		float queueTime;
		bool processed;
		bool isValid;
	};
	
	static const int MAX_DISENGAGE_QUEUE = 10;
	static const float DISENGAGE_QUEUE_INTERVAL = 2.0f;  // Process one disengage every 2 seconds
	static QueuedDisengage g_disengageQueue[MAX_DISENGAGE_QUEUE];
	static int g_disengageQueueCount = 0;
	static float g_lastDisengageProcessTime = 0;
	
	// Add an actor to the disengage queue
	static bool QueueDisengage(UInt32 actorFormID)
	{
		// Check if already queued
		for (int i = 0; i < g_disengageQueueCount; i++)
		{
			if (g_disengageQueue[i].isValid && g_disengageQueue[i].actorFormID == actorFormID)
			{
				return true;  // Already queued
			}
		}
		
		// Add to queue
		if (g_disengageQueueCount < MAX_DISENGAGE_QUEUE)
		{
			g_disengageQueue[g_disengageQueueCount].actorFormID = actorFormID;
			g_disengageQueue[g_disengageQueueCount].queueTime = GetGameTime();
			g_disengageQueue[g_disengageQueueCount].processed = false;
			g_disengageQueue[g_disengageQueueCount].isValid = true;
			g_disengageQueueCount++;
			_MESSAGE("AILogging: Queued disengage for actor %08X (queue size: %d)", actorFormID, g_disengageQueueCount);
			return true;
		}
		
		return false;  // Queue full
	}
	
	// Check if an actor is in the disengage queue (pending or processing)
	static bool IsInDisengageQueue(UInt32 actorFormID)
	{
		for (int i = 0; i < g_disengageQueueCount; i++)
		{
			if (g_disengageQueue[i].isValid && g_disengageQueue[i].actorFormID == actorFormID)
			{
				return true;
			}
		}
		return false;
	}
	
	// Remove an actor from the disengage queue
	static void RemoveFromDisengageQueue(UInt32 actorFormID)
	{
		for (int i = 0; i < g_disengageQueueCount; i++)
		{
			if (g_disengageQueue[i].isValid && g_disengageQueue[i].actorFormID == actorFormID)
			{
				// Shift remaining entries
				for (int j = i; j < g_disengageQueueCount - 1; j++)
				{
					g_disengageQueue[j] = g_disengageQueue[j + 1];
				}
				g_disengageQueueCount--;
				return;
			}
		}
	}
	
	// Clear the entire disengage queue
	void ClearDisengageQueue()
	{
		for (int i = 0; i < MAX_DISENGAGE_QUEUE; i++)
		{
			g_disengageQueue[i].isValid = false;
		}
		g_disengageQueueCount = 0;
	}
	
	// Check if we can process a disengage now (rate limiting)
	static bool CanProcessDisengageNow()
	{
		float currentTime = GetGameTime();
		return (currentTime - g_lastDisengageProcessTime) >= DISENGAGE_QUEUE_INTERVAL;
	}
	
	// Mark that we just processed a disengage
	static void MarkDisengageProcessed()
	{
		g_lastDisengageProcessTime = GetGameTime();
	}
	
	static CombatAlarmCooldown* GetOrCreateAlarmCooldown(UInt32 actorFormID)
	{
		// Find existing
		for (int i = 0; i < g_alarmCooldownCount; i++)
		{
			if (g_alarmCooldowns[i].isValid && g_alarmCooldowns[i].actorFormID == actorFormID)
			{
				return &g_alarmCooldowns[i];
			}
		}
		
		// Create new
		if (g_alarmCooldownCount < MAX_ALARM_COOLDOWNS)
		{
			CombatAlarmCooldown* cd = &g_alarmCooldowns[g_alarmCooldownCount];
			cd->actorFormID = actorFormID;
			cd->lastCallTime = 0;
			cd->isValid = true;
			g_alarmCooldownCount++;
			return cd;
		}
		
		// Reuse oldest slot
		float oldestTime = 999999.0f;
		int oldestIdx = 0;
		for (int i = 0; i < MAX_ALARM_COOLDOWNS; i++)
		{
			if (g_alarmCooldowns[i].lastCallTime < oldestTime)
			{
				oldestTime = g_alarmCooldowns[i].lastCallTime;
				oldestIdx = i;
			}
		}
		
		g_alarmCooldowns[oldestIdx].actorFormID = actorFormID;
		g_alarmCooldowns[oldestIdx].lastCallTime = 0;
		g_alarmCooldowns[oldestIdx].isValid = true;
		return &g_alarmCooldowns[oldestIdx];
	}
	
	static bool IsAlarmOnCooldown(UInt32 actorFormID)
	{
		float currentTime = GetGameTime();
		
		// Check global cooldown - prevents ANY alarm calls too close together
		if ((currentTime - g_lastGlobalAlarmCallTime) < GLOBAL_ALARM_COOLDOWN)
		{
			return true;
		}
		
		// Check per-actor cooldown
		for (int i = 0; i < g_alarmCooldownCount; i++)
		{
			if (g_alarmCooldowns[i].isValid && g_alarmCooldowns[i].actorFormID == actorFormID)
			{
				if ((currentTime - g_alarmCooldowns[i].lastCallTime) < PER_ACTOR_COOLDOWN)
				{
					return true;
				}
			}
		}
		
		return false;
	}
	
	static void RecordAlarmCall(UInt32 actorFormID)
	{
		float currentTime = GetGameTime();
		g_lastGlobalAlarmCallTime = currentTime;
		
		CombatAlarmCooldown* cd = GetOrCreateAlarmCooldown(actorFormID);
		if (cd)
		{
			cd->lastCallTime = currentTime;
		}
	}
	
	void ClearAlarmCooldowns()
	{
		for (int i = 0; i < MAX_ALARM_COOLDOWNS; i++)
		{
			g_alarmCooldowns[i].isValid = false;
		}
		g_alarmCooldownCount = 0;
		g_lastGlobalAlarmCallTime = 0;
	}
	
	// ============================================
	// DISENGAGE QUEUE SYSTEM (legacy)
	// For compatibility with older code - should be deprecated
	// ============================================
	
	// Add an actor to the disengage queue (legacy version)
	static bool QueueDisengageLegacy(UInt32 actorFormID)
	{
		// Check if already queued
		for (int i = 0; i < g_disengageQueueCount; i++)
		{
			if (g_disengageQueue[i].isValid && g_disengageQueue[i].actorFormID == actorFormID)
			{
				return true;  // Already queued
			}
		}
		
		// Add to queue
		if (g_disengageQueueCount < MAX_DISENGAGE_QUEUE)
		{
			g_disengageQueue[g_disengageQueueCount].actorFormID = actorFormID;
			g_disengageQueue[g_disengageQueueCount].queueTime = GetGameTime();
			g_disengageQueue[g_disengageQueueCount].processed = false;
			g_disengageQueue[g_disengageQueueCount].isValid = true;
			g_disengageQueueCount++;
			return true;
		}
		
		return false;  // Queue full
	}
	
	// ============================================
	// STOP COMBAT ALARM HANDLER
	// Called by HorseMountScanner when a horse is detected
	// Also called by CombatStyles/MultiMountedCombat to disengage NPCs
	// ============================================
	
	// ============================================
	// SAFE FORM VALIDATION HELPER
	// Uses SEH to safely validate actor pointers
	// ============================================
	static bool SafeValidateActor(Actor* actor, UInt32& outFormID)
	{
		outFormID = 0;
		__try
		{
			if (!actor) return false;
			if (actor->formID == 0 || actor->formID == 0xFFFFFFFF) return false;
			if (actor->formType != kFormType_Character) return false;
			outFormID = actor->formID;
			return true;
		}
		__except(EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}
	
	static bool SafeCheckActorLoaded(Actor* actor)
	{
		__try
		{
			if (!actor) return false;
			if (!actor->loadedState) return false;
			if (!actor->GetNiNode()) return false;
			if (!actor->processManager) return false;
			return true;
		}
		__except(EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}
	
	static bool SafeCheckActorDead(Actor* actor)
	{
		__try
		{
			return actor && actor->IsDead(1);
		}
		__except(EXCEPTION_EXECUTE_HANDLER)
		{
			return true;  // Assume dead if we can't check
		}
	}
	
	static bool SafeCallStopCombatAlarm(Actor* actor)
	{
		__try
		{
			Actor_StopCombatAlarm_Native(0, 0, actor);
			return true;
		}
		__except(EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}
	
	static bool SafeCheckInCombat(Actor* actor)
	{
		__try
		{
			return actor && actor->IsInCombat();
		}
		__except(EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}
	
	static const char* SafeGetActorName(Actor* actor)
	{
		__try
		{
			return actor ? CALL_MEMBER_FN(actor, GetReferenceName)() : "Unknown";
		}
		__except(EXCEPTION_EXECUTE_HANDLER)
		{
			return "Unknown";
		}
	}

	void StopActorCombatAlarm(Actor* actor)
	{
		// ============================================
		// CRITICAL: THREAD SAFETY CHECK
		// Only call from main thread - queue if called from other threads
		// ============================================
		if (g_mainThreadId != 0 && GetCurrentThreadId() != g_mainThreadId)
		{
			_MESSAGE("StopActorCombatAlarm: Called from non-main thread - queueing");
			UInt32 formID = 0;
			if (SafeValidateActor(actor, formID) && formID != 0)
			{
				std::lock_guard<std::mutex> lock(g_stopQueueMutex);
				g_stopAlarmQueue.push_back(formID);
			}
			return;
		}
		
		// ============================================
		// CRITICAL VALIDATION - Prevent CTD from invalid actors
		// ============================================
		UInt32 actorFormID = 0;
		if (!SafeValidateActor(actor, actorFormID))
		{
			_MESSAGE("StopActorCombatAlarm: Invalid actor - skipping");
			return;
		}
		
		// ============================================
		// VALIDATE FORM ID BY LOOKING IT UP
		// This ensures the FormID is still valid in the game
		// ============================================
		TESForm* verifyForm = LookupFormByID(actorFormID);
		if (!verifyForm)
		{
			_MESSAGE("StopActorCombatAlarm: Actor %08X form lookup failed - skipping", actorFormID);
			RemoveFromDisengageQueue(actorFormID);
			return;
		}
		
		if (verifyForm != (TESForm*)actor)
		{
			_MESSAGE("StopActorCombatAlarm: Actor %08X form mismatch (stale pointer?) - skipping", actorFormID);
			RemoveFromDisengageQueue(actorFormID);
			return;
		}
		
		// ============================================
		// CHECK IF ALREADY IN DISENGAGE QUEUE
		// If so, don't process now - let the queue handle it
		// ============================================
		if (IsInDisengageQueue(actorFormID))
		{
			_MESSAGE("StopActorCombatAlarm: Actor %08X already in disengage queue - skipping duplicate", actorFormID);
			return;
		}
		
		// ============================================
		// CHECK COOLDOWNS - Prevent rapid-fire calls
		// This is CRITICAL for multiple rider disengagement!
		// ============================================
		if (IsAlarmOnCooldown(actorFormID))
		{
			_MESSAGE("StopActorCombatAlarm: Actor %08X on COOLDOWN - queueing for later", actorFormID);
			QueueDisengage(actorFormID);
			return;
		}
		
		// ============================================
		// CHECK GLOBAL RATE LIMIT FOR MULTI-RIDER SCENARIOS
		// If we just processed a disengage, queue this one
		// ============================================
		if (!CanProcessDisengageNow())
		{
			_MESSAGE("StopActorCombatAlarm: Global rate limit - queueing actor %08X", actorFormID);
			QueueDisengage(actorFormID);
			return;
		}
		
		// Check if actor is still valid and loaded
		if (!SafeCheckActorLoaded(actor))
		{
			_MESSAGE("StopActorCombatAlarm: Actor %08X not fully loaded - skipping", actorFormID);
			RemoveFromDisengageQueue(actorFormID);
			return;
		}
		
		// Check if actor is dead
		if (SafeCheckActorDead(actor))
		{
			_MESSAGE("StopActorCombatAlarm: Actor %08X is dead - skipping", actorFormID);
			RemoveFromDisengageQueue(actorFormID);
			return;
		}
		
		// ============================================
		// SKIP NON-HUMANOID ACTORS (creatures, animals)
		// Only process humanoid NPCs to avoid corrupting AI for other actors
		// ============================================
		TESRace* race = actor->race;
		if (race)
		{
			const char* raceName = race->fullName.name.data;
			if (raceName)
			{
				// Skip creatures/animals that might have different AI structures
				if (strstr(raceName, "Wisp") != nullptr ||
					strstr(raceName, "Wolf") != nullptr ||
					strstr(raceName, "Bear") != nullptr ||
					strstr(raceName, "Spider") != nullptr ||
					strstr(raceName, "Dragon") != nullptr ||
					strstr(raceName, "Troll") != nullptr ||
					strstr(raceName, "Giant") != nullptr ||
					strstr(raceName, "Atronach") != nullptr ||
					strstr(raceName, "Draugr") != nullptr ||
					strstr(raceName, "Skeleton") != nullptr ||
					strstr(raceName, "Horse") != nullptr)
				{
					_MESSAGE("StopActorCombatAlarm: Actor %08X is non-humanoid (%s) - skipping", 
						actorFormID, raceName);
					RemoveFromDisengageQueue(actorFormID);
					return;
				}
			}
		}
		
		const char* actorName = SafeGetActorName(actor);
		
		_MESSAGE("StopActorCombatAlarm: Stopping combat for '%s' (%08X)", 
			actorName ? actorName : "Unknown", actorFormID);
		
		// ============================================
		// RECORD THIS CALL FOR COOLDOWN TRACKING
		// Must be done BEFORE the native call
		// ============================================
		RecordAlarmCall(actorFormID);
		MarkDisengageProcessed();
		RemoveFromDisengageQueue(actorFormID);
		
		// ============================================
		// CALL THE NATIVE GAME FUNCTION
		// Wrapped in SEH helper for safety
		// ============================================
		if (!SafeCallStopCombatAlarm(actor))
		{
			_MESSAGE("StopActorCombatAlarm: EXCEPTION in native call for '%s' (%08X) - survived", 
				actorName ? actorName : "Unknown", actorFormID);
			return;
		}
		
		// Log final state
		bool stillInCombat = SafeCheckInCombat(actor);
		_MESSAGE("StopActorCombatAlarm: '%s' combat state after: %s", 
			actorName ? actorName : "Unknown", stillInCombat ? "STILL IN COMBAT" : "NOT IN COMBAT");
	}
	
	// ============================================
	// PROCESS QUEUED DISENGAGES
	// Call this periodically from the main update loop
	// ============================================
	void ProcessQueuedDisengages()
	{
		if (g_disengageQueueCount == 0) return;
		if (!CanProcessDisengageNow()) return;
		
		// Find the oldest queued disengage
		float oldestTime = 999999.0f;
		int oldestIdx = -1;
		
		for (int i = 0; i < g_disengageQueueCount; i++)
		{
			if (g_disengageQueue[i].isValid && !g_disengageQueue[i].processed)
			{
				if (g_disengageQueue[i].queueTime < oldestTime)
				{
					oldestTime = g_disengageQueue[i].queueTime;
					oldestIdx = i;
				}
			}
		}
		
		if (oldestIdx < 0) return;
		
		UInt32 actorFormID = g_disengageQueue[oldestIdx].actorFormID;
		
		// Look up the actor
		TESForm* form = LookupFormByID(actorFormID);
		if (!form)
		{
			_MESSAGE("ProcessQueuedDisengages: Actor %08X form not found - removing from queue", actorFormID);
			RemoveFromDisengageQueue(actorFormID);
			return;
		}
		
		Actor* actor = DYNAMIC_CAST(form, TESForm, Actor);
		if (!actor)
		{
			_MESSAGE("ProcessQueuedDisengages: Actor %08X cast failed - removing from queue", actorFormID);
			RemoveFromDisengageQueue(actorFormID);
			return;
		}
		
		_MESSAGE("ProcessQueuedDisengages: Processing queued disengage for actor %08X", actorFormID);
		
		// Mark as processed first to prevent re-queueing
		g_disengageQueue[oldestIdx].processed = true;
		
		// Call StopActorCombatAlarm - it will remove from queue when done
		StopActorCombatAlarm(actor);
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
