#include "HorseMountScanner.h"
#include "MountedCombat.h"
#include "WeaponDetection.h"
#include "DynamicPackages.h"
#include "AILogging.h"
#include "Helper.h"
#include "config.h"
#include "FactionData.h"
#include "CompanionCombat.h"  // For IsCompanion
#include "skse64/GameReferences.h"
#include "skse64/GameRTTI.h"
#include "skse64/GameData.h"
#include "skse64/GameObjects.h"
#include "skse64/PluginAPI.h"
#include "skse64/PapyrusVM.h"
#include "skse64_common/BranchTrampoline.h"
#include "skse64_common/Relocation.h"
#include <cmath>
#include <chrono>

namespace MountedNPCCombatVR
{
	// ============================================
	// EXTERNAL TASK INTERFACE
	// ============================================
	extern SKSETaskInterface* g_task;
	
	// ============================================
	// NATIVE FUNCTION: TESObjectREFR::ActivateRef
	// Used to make NPCs mount horses
	// Papyrus signature: Activate(ObjectReference akActivator, bool abDefaultProcessingOnly = false)
	// ============================================
	
	// TESObjectREFR::ActivateRef native function
	// Parameters: this (horse), activator (NPC who wants to mount), unk1, unk2, count, defaultProcessingOnly
	typedef bool (*_TESObjectREFR_ActivateRef)(TESObjectREFR* thisRef, TESObjectREFR* activator, UInt8 unk1, TESBoundObject* unk2, SInt32 count, bool defaultProcessingOnly);
	RelocAddr<_TESObjectREFR_ActivateRef> TESObjectREFR_ActivateRef(0x2A8300);  // SKSEVR 1.4.15 - ID 19369
	
	// ============================================
	// CONFIGURATION
	// ============================================
	
	const float HORSE_SCAN_RANGE = 2000.0f;
	const float SCAN_UPDATE_INTERVAL = 3.0f;
	const float ACTIVATION_DELAY_SECONDS = 1.0f;
	const float COMBAT_CHECK_INTERVAL = 2.0f;
	const int MAX_DISMOUNTED_NPCS = 10;
	const int MAX_AVAILABLE_HORSES = 10;
	const int MAX_SCAN_ATTEMPTS = 25;
	const float MAX_SCAN_DISTANCE = 3000.0f;
	const float MOUNT_ACTIVATION_DISTANCE = 300.0f;  // NPC must be within this distance to mount
	const float MOUNT_ATTEMPT_COOLDOWN = 5.0f; // Seconds between mount attempts for same NPC
	const float POST_RAGDOLL_DELAY = 3.0f;           // Wait this long after ragdoll before allowing mount
	const float IGNORE_RANGE_DURATION = 15.0f;       // After mount attempt, ignore range checks for this long (NPC may get flung)
	const float POST_DISMOUNT_DELAY = 5.0f;     // Wait this long after dismount before trying to remount
	const float REMOUNT_STABLE_DELAY = 0.1f;    // Wait 100ms after mount before triggering aggro
	
	// ============================================
	// DISMOUNTED NPC TRACKING
	// Separate from MountedCombat - tracks NPCs who
	// have dismounted or were never mounted but are
	// in combat and aggressive
	// ============================================
	
	struct DismountedNPCEntry
	{
		UInt32 npcFormID;
		UInt32 lastKnownHorseFormID;  // 0 if never had a horse
		UInt32 targetHorseFormID;     // Horse we're trying to mount (for continuous teleport)
		float posX, posY, posZ;
		bool isValid;
		float lastMountAttemptTime;   // Cooldown tracking
		bool mountAttemptInProgress;  // Prevent double activation
		bool ignoreRangeCheck;    // Ignore distance checks after mount attempt (NPC may get flung)
		float ignoreRangeUntil;       // Time when range check becomes active again
		float dismountedTime;      // When the NPC was dismounted - delay remount attempts
		
		// Remount tracking
		bool mountActivationSucceeded; // True if ActivateRef returned SUCCESS - waiting for mount to complete
		bool remountedSuccessfully;   // True if NPC has remounted and we're waiting to trigger aggro
		float remountedTime;    // When the NPC successfully remounted
		bool aggroTriggered;     // True if we've already triggered aggro
		float lastTeleportTime;       // For continuous teleporting every second
		
		void Reset()
		{
			npcFormID = 0;
			lastKnownHorseFormID = 0;
			targetHorseFormID = 0;
			posX = posY = posZ = 0;
			isValid = false;
			lastMountAttemptTime = 0;
			mountAttemptInProgress = false;
			ignoreRangeCheck = false;
			ignoreRangeUntil = 0;
			dismountedTime = 0;
			mountActivationSucceeded = false;
			remountedSuccessfully = false;
			remountedTime = 0;
			aggroTriggered = false;
			lastTeleportTime = 0;
		}
	};
	
	struct AvailableHorseEntry
	{
		UInt32 horseFormID;
		float posX, posY, posZ;
		bool isValid;
		
		void Reset()
		{
			horseFormID = 0;
			posX = posY = posZ = 0;
			isValid = false;
		}
	};
	
	static DismountedNPCEntry g_dismountedNPCs[MAX_DISMOUNTED_NPCS];
	static AvailableHorseEntry g_availableHorses[MAX_AVAILABLE_HORSES];
	static int g_dismountedNPCCount = 0;
	static int g_availableHorseCount = 0;
	
	// ============================================
	// NATIVE FUNCTIONS FOR AGGRESSION
	// ============================================
	
	// SendAssaultAlarm - triggers crime/aggression response from NPC (same as SpecialDismount)
	typedef void (*_Actor_SendAssaultAlarm)(UInt64 a1, UInt64 a2, Actor* actor);
	RelocAddr<_Actor_SendAssaultAlarm> Actor_SendAssaultAlarm_HMS(0x986530);
	
	// ============================================
	// TRIGGER AGGRO ON REMOUNTED NPC
	// Uses the same method as SpecialDismount to trigger aggression
	// FOR COMPANIONS: Don't use assault alarm - directly set them to target player's target
	// ============================================
	
	static void TriggerRemountAggro(Actor* npc)
	{
		if (!npc) return;
		if (!g_thePlayer || !(*g_thePlayer)) return;
		
		Actor* player = *g_thePlayer;
		
		const char* name = CALL_MEMBER_FN(npc, GetReferenceName)();
		_MESSAGE("HorseMountScanner: *** TRIGGERING REMOUNT AGGRO ***");
		_MESSAGE("HorseMountScanner:   NPC: '%s' (%08X)", name ? name : "Unknown", npc->formID);
		
		// ============================================
		// CHECK IF THIS IS A COMPANION
		// Companions should NOT be sent assault alarms - they should target the player's target
		// ============================================
		bool isCompanion = IsCompanion(npc);
		
		if (isCompanion)
		{
			_MESSAGE("HorseMountScanner:   COMPANION detected - redirecting to player's target");
			
			// Find the player's combat target
			Actor* playerTarget = nullptr;
			if (player->IsInCombat())
			{
				UInt32 playerCombatHandle = player->currentCombatTarget;
				if (playerCombatHandle != 0)
				{
					NiPointer<TESObjectREFR> playerTargetRef;
					LookupREFRByHandle(playerCombatHandle, playerTargetRef);
					if (playerTargetRef && playerTargetRef->formType == kFormType_Character)
					{
						Actor* potentialTarget = static_cast<Actor*>(playerTargetRef.get());
						if (potentialTarget && !potentialTarget->IsDead(1) && potentialTarget != player)
						{
							playerTarget = potentialTarget;
						}
					}
				}
			}
			
			if (playerTarget)
			{
				// Set companion to target the player's target
				UInt32 targetHandle = playerTarget->CreateRefHandle();
				if (targetHandle != 0 && targetHandle != *g_invalidRefHandle)
				{
					npc->currentCombatTarget = targetHandle;
				}
				
				// Set attack on sight flag
				npc->flags2 |= Actor::kFlag_kAttackOnSight;
				
				const char* targetName = CALL_MEMBER_FN(playerTarget, GetReferenceName)();
				_MESSAGE("HorseMountScanner:   COMPANION set to target '%s' (%08X)",
					targetName ? targetName : "Unknown", playerTarget->formID);
			}
			else
			{
				// No valid player target - just set attack flag, don't send assault alarm
				npc->flags2 |= Actor::kFlag_kAttackOnSight;
				_MESSAGE("HorseMountScanner:   COMPANION - no valid player target, just set attack flag");
			}
		}
		else
		{
			// Non-companion: Use assault alarm to trigger aggression against player
			_MESSAGE("HorseMountScanner:   Sending assault alarm...");
			Actor_SendAssaultAlarm_HMS(0, 0, npc);
			
			// Set attack on sight flag
			npc->flags2 |= Actor::kFlag_kAttackOnSight;
			_MESSAGE("HorseMountScanner:   Set kAttackOnSight flag");
		}
		
		// Check result
		bool isNowInCombat = npc->IsInCombat();
		_MESSAGE("HorseMountScanner:   Post-aggression: InCombat=%s", isNowInCombat ? "YES" : "NO");
	}
	
	// ============================================
	// SYSTEM STATE
	// ============================================
	
	static bool g_scannerInitialized = false;
	static bool g_scannerActive = false;
	static float g_lastScanTime = 0.0f;
	static int g_lastScanHorseCount = 0;
	static bool g_scannerReady = false;
	static std::chrono::steady_clock::time_point g_scannerActivationTime;
	static bool g_playerWasInCombat = false;
	static float g_lastCombatCheckTime = 0.0f;
	static int g_scanAttempts = 0;
	static bool g_scanDisabledForSession = false;
	
	// Forward declarations
	static void ScanForUnmountedAggressiveNPCs();
	static void TeleportNPCToHorse(Actor* npc, Actor* horse);
	
	// ============================================
	// CHECK IF ANY NPCs ARE WAITING FOR AGGRO TRIGGER
	// Returns true if any NPC is:
	// - Waiting for mount animation to complete (mountActivationSucceeded)
	// - Waiting for aggro delay after remount (remountedSuccessfully && !aggroTriggered)
	// ============================================
	
	static bool HasPendingAggroTriggers()
	{
		for (int i = 0; i < MAX_DISMOUNTED_NPCS; i++)
		{
			if (g_dismountedNPCs[i].isValid)
			{
				// Still waiting for mount to complete
				if (g_dismountedNPCs[i].mountActivationSucceeded && !g_dismountedNPCs[i].remountedSuccessfully)
				{
					return true;
				}
				// Mounted, waiting for aggro delay
				if (g_dismountedNPCs[i].remountedSuccessfully && !g_dismountedNPCs[i].aggroTriggered)
				{
					return true;
				}
			}
		}
		return false;
	}

	// ============================================
	// UTILITY FUNCTIONS
	// ============================================
	
	static float GetGameTime()
	{
		static auto startTime = std::chrono::steady_clock::now();
		auto now = std::chrono::steady_clock::now();
		auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - startTime);
		return duration.count() / 1000.0f;
	}
	
	static float CalculateDistance3D(float x1, float y1, float z1, float x2, float y2, float z2)
	{
		float dx = x1 - x2;
		float dy = y1 - y2;
		float dz = z1 - z2;
		return sqrt(dx * dx + dy * dy + dz * dz);
	}
	
	static float CalculateDistance3D(Actor* a, Actor* b)
	{
		if (!a || !b) return 99999.0f;
		return CalculateDistance3D(a->pos.x, a->pos.y, a->pos.z, b->pos.x, b->pos.y, b->pos.z);
	}
	
	static float CalculateDistanceToPlayer(Actor* actor)
	{
		if (!actor || !g_thePlayer || !(*g_thePlayer)) return 99999.0f;
		Actor* player = *g_thePlayer;
		return CalculateDistance3D(actor, player);
	}
	
	static bool IsHorseRace(Actor* actor)
	{
		if (!actor) return false;
		TESRace* race = actor->race;
		if (race)
		{
			const char* raceName = race->fullName.name.data;
			if (raceName)
			{
				if (strstr(raceName, "Horse") != nullptr || strstr(raceName, "horse") != nullptr)
					return true;
			}
		}
		return false;
	}
	
	static bool IsActorMounted(Actor* actor)
	{
		if (!actor) return false;
		NiPointer<Actor> mount;
		return CALL_MEMBER_FN(actor, GetMount)(mount) && mount;
	}
	
	static bool IsHorseRidden(Actor* horse)
	{
		if (!horse) return false;
		NiPointer<Actor> rider;
		bool hasRider = CALL_MEMBER_FN(horse, GetMountedBy)(rider);
		return hasRider && rider;
	}
	
	static bool IsOutdoorCell()
	{
		if (!g_thePlayer || !(*g_thePlayer)) return false;
		Actor* player = *g_thePlayer;
		TESObjectCELL* cell = player->parentCell;
		if (!cell) return false;
		TESWorldSpace* worldspace = cell->unk120;
		return worldspace != nullptr;
	}
	
	static bool IsHumanoidNPC(Actor* actor)
	{
		if (!actor) return false;
		if (!g_thePlayer || !(*g_thePlayer)) return false;
		if (actor == *g_thePlayer) return false;
		if (actor->IsPlayerRef()) return false;
		
		// Check race - exclude animals/creatures
		TESRace* race = actor->race;
		if (!race) return false;
		
		const char* raceName = race->fullName.name.data;
		if (!raceName) return false;
		
		// Exclude horse/animal races
		if (strstr(raceName, "Horse") != nullptr) return false;
		if (strstr(raceName, "horse") != nullptr) return false;
		if (strstr(raceName, "Wolf") != nullptr) return false;
		if (strstr(raceName, "Bear") != nullptr) return false;
		if (strstr(raceName, "Sabre") != nullptr) return false;
		if (strstr(raceName, "Spider") != nullptr) return false;
		if (strstr(raceName, "Skeever") != nullptr) return false;
		if (strstr(raceName, "Dragon") != nullptr) return false;
		if (strstr(raceName, "Troll") != nullptr) return false;
		if (strstr(raceName, "Giant") != nullptr) return false;
		if (strstr(raceName, "Mammoth") != nullptr) return false;
		if (strstr(raceName, "Mudcrab") != nullptr) return false;
		if (strstr(raceName, "Chaurus") != nullptr) return false;
		if (strstr(raceName, "Frostbite") != nullptr) return false;
		
		return true;
	}
	
	// ============================================
	// DISMOUNTED NPC REGISTRATION
	// Call this when an NPC dismounts or is found
	// unmounted in combat
	// ============================================
	
	void RegisterDismountedNPC(UInt32 npcFormID, UInt32 horseFormID)
	{
		if (npcFormID == 0) return;
		
		float currentTime = GetGameTime();
		
		// Check if already registered
		for (int i = 0; i < MAX_DISMOUNTED_NPCS; i++)
		{
			if (g_dismountedNPCs[i].isValid && g_dismountedNPCs[i].npcFormID == npcFormID)
			{
				// Update horse if we have one
				if (horseFormID != 0)
				{
					g_dismountedNPCs[i].lastKnownHorseFormID = horseFormID;
				}
				// Update dismounted time on re-registration (they dismounted again)
				g_dismountedNPCs[i].dismountedTime = currentTime;
				return;
			}
		}
		
		// Find empty slot
		for (int i = 0; i < MAX_DISMOUNTED_NPCS; i++)
		{
			if (!g_dismountedNPCs[i].isValid)
			{
				g_dismountedNPCs[i].npcFormID = npcFormID;
				g_dismountedNPCs[i].lastKnownHorseFormID = horseFormID;
				g_dismountedNPCs[i].isValid = true;
				g_dismountedNPCs[i].dismountedTime = currentTime;  // Set dismount time
				g_dismountedNPCCount++;
				
				TESForm* form = LookupFormByID(npcFormID);
				if (form)
				{
					Actor* actor = DYNAMIC_CAST(form, TESForm, Actor);
					if (actor)
					{
						const char* name = CALL_MEMBER_FN(actor, GetReferenceName)();
						_MESSAGE("HorseMountScanner: Registered dismounted NPC '%s' (%08X) with horse %08X - will attempt remount in %.0f seconds",
							name ? name : "Unknown", npcFormID, horseFormID, POST_DISMOUNT_DELAY);
					}
				}
				return;
			}
		}
		
		_MESSAGE("HorseMountScanner: WARNING - Cannot register dismounted NPC %08X - array full!", npcFormID);
	}
	
	void RegisterAvailableHorse(UInt32 horseFormID)
	{
		if (horseFormID == 0) return;
		
		// Check if already registered
		for (int i = 0; i < MAX_AVAILABLE_HORSES; i++)
		{
			if (g_availableHorses[i].isValid && g_availableHorses[i].horseFormID == horseFormID)
			{
				return;
			}
		}
		
		// Find empty slot
		for (int i = 0; i < MAX_AVAILABLE_HORSES; i++)
		{
			if (!g_availableHorses[i].isValid)
			{
				g_availableHorses[i].horseFormID = horseFormID;
				g_availableHorses[i].isValid = true;
				g_availableHorseCount++;
				
				TESForm* form = LookupFormByID(horseFormID);
				if (form)
				{
					Actor* actor = DYNAMIC_CAST(form, TESForm, Actor);
					if (actor)
					{
						const char* name = CALL_MEMBER_FN(actor, GetReferenceName)();
						_MESSAGE("HorseMountScanner: Registered available horse '%s' (%08X)",
							name ? name : "Unknown", horseFormID);
					}
				}
				return;
			}
		}
	}
	
	void UnregisterDismountedNPC(UInt32 npcFormID)
	{
		for (int i = 0; i < MAX_DISMOUNTED_NPCS; i++)
		{
			if (g_dismountedNPCs[i].isValid && g_dismountedNPCs[i].npcFormID == npcFormID)
			{
				g_dismountedNPCs[i].Reset();
				g_dismountedNPCCount--;
				return;
			}
		}
	}
	
	void UnregisterAvailableHorse(UInt32 horseFormID)
	{
		for (int i = 0; i < MAX_AVAILABLE_HORSES; i++)
		{
			if (g_availableHorses[i].isValid && g_availableHorses[i].horseFormID == horseFormID)
			{
				g_availableHorses[i].Reset();
				g_availableHorseCount--;
				return;
			}
		}
	}
	
	void ClearAllDismountedTracking()
	{
		for (int i = 0; i < MAX_DISMOUNTED_NPCS; i++)
		{
			g_dismountedNPCs[i].Reset();
		}
		for (int i = 0; i < MAX_AVAILABLE_HORSES; i++)
		{
			g_availableHorses[i].Reset();
		}
		g_dismountedNPCCount = 0;
		g_availableHorseCount = 0;
	}
	
	// ============================================
	// CALLED FROM MOUNTEDCOMBAT WHEN NPC DISMOUNTS
	// ============================================
	
	void OnNPCDismounted(UInt32 npcFormID, UInt32 horseFormID)
	{
		// Skip if remounting is disabled
		if (!EnableRemounting)
		{
			_MESSAGE("HorseMountScanner: NPC %08X dismounted but remounting is disabled", npcFormID);
			return;
		}
		
		_MESSAGE("HorseMountScanner: NPC %08X dismounted from horse %08X", npcFormID, horseFormID);
		RegisterDismountedNPC(npcFormID, horseFormID);
		if (horseFormID != 0)
		{
			RegisterAvailableHorse(horseFormID);
		}
	}
	
	// ============================================
	// SCAN FOR AGGRESSIVE UNMOUNTED NPCs NEAR PLAYER
	// Scans cell for ANY NPC in combat who is unmounted
	// Returns up to MAX_DISMOUNTED_NPCS (5) closest NPCs
	// ============================================
	
	struct TempNPCEntry
	{
		UInt32 formID;
		float distanceToPlayer;
	};
	
	static void ScanCellForUnmountedCombatNPCs()
	{
		if (!g_thePlayer || !(*g_thePlayer)) return;
		
		Actor* player = *g_thePlayer;
		if (!player->IsInCombat()) return;
		
		TESObjectCELL* cell = player->parentCell;
		if (!cell) return;
		
		float currentTime = GetGameTime();
		
		// First, update ignore range flags for existing entries
		for (int i = 0; i < MAX_DISMOUNTED_NPCS; i++)
		{
			if (g_dismountedNPCs[i].isValid && g_dismountedNPCs[i].ignoreRangeCheck)
			{
				if (currentTime >= g_dismountedNPCs[i].ignoreRangeUntil)
				{
					g_dismountedNPCs[i].ignoreRangeCheck = false;
					_MESSAGE("HorseMountScanner: Range check re-enabled for NPC %08X", g_dismountedNPCs[i].npcFormID);
				}
			}
		}
		
		// Temporary array to collect all candidates
		TempNPCEntry candidates[50];
		int candidateCount = 0;
		
		// Iterate through all actors in the cell
		for (UInt32 i = 0; i < cell->objectList.count && candidateCount < 50; i++)
		{
			TESObjectREFR* ref = nullptr;
			cell->objectList.GetNthItem(i, ref);
			
			if (!ref) continue;
			if (ref->formType != kFormType_Character) continue;
			
			Actor* actor = static_cast<Actor*>(ref);
			
			// Skip player
			if (actor->IsPlayerRef()) continue;
			if (actor == player) continue;
			
			// Skip dead
			if (actor->IsDead(1)) continue;
			
			// Skip if not in combat
			if (!actor->IsInCombat()) continue;
			
			// Skip if mounted - we only want unmounted NPCs
			if (IsActorMounted(actor)) continue;
			
			// Skip if not humanoid (horses, wolves, etc.)
			if (!IsHumanoidNPC(actor)) continue;
			
			// Check distance to player (but allow if already registered with ignoreRangeCheck)
			float dist = CalculateDistanceToPlayer(actor);
			bool alreadyRegistered = false;
			bool hasIgnoreRange = false;
			
			for (int j = 0; j < MAX_DISMOUNTED_NPCS; j++)
			{
				if (g_dismountedNPCs[j].isValid && g_dismountedNPCs[j].npcFormID == actor->formID)
				{
					alreadyRegistered = true;
					hasIgnoreRange = g_dismountedNPCs[j].ignoreRangeCheck;
					break;
				}
			}
			
			// Skip if out of range AND not already tracked with ignore flag
			if (dist > MAX_SCAN_DISTANCE && !hasIgnoreRange)
			{
				continue;
			}
			
			if (!alreadyRegistered)
			{
				candidates[candidateCount].formID = actor->formID;
				candidates[candidateCount].distanceToPlayer = dist;
				candidateCount++;
			}
		}
		
		// Sort candidates by distance (simple bubble sort for small array)
		for (int i = 0; i < candidateCount - 1; i++)
		{
			for (int j = 0; j < candidateCount - i - 1; j++)
			{
				if (candidates[j].distanceToPlayer > candidates[j + 1].distanceToPlayer)
				{
					TempNPCEntry temp = candidates[j];
					candidates[j] = candidates[j + 1];
					candidates[j + 1] = temp;
				}
			}
		}
		
		// Register only the closest NPCs (up to available slots)
		int slotsAvailable = MAX_DISMOUNTED_NPCS - g_dismountedNPCCount;
		int toRegister = (candidateCount < slotsAvailable) ? candidateCount : slotsAvailable;
		
		for (int i = 0; i < toRegister; i++)
		{
			RegisterDismountedNPC(candidates[i].formID, 0);
		}
	}
	
	// ============================================
	// SCAN FOR AVAILABLE HORSES IN CELL
	// Finds any riderless horses near the player
	// ============================================
	
	static void ScanCellForAvailableHorses()
	{
		if (!g_thePlayer || !(*g_thePlayer)) return;
		
		Actor* player = *g_thePlayer;
		TESObjectCELL* cell = player->parentCell;
		if (!cell) return;
		
		// Iterate through all actors in the cell
		for (UInt32 i = 0; i < cell->objectList.count; i++)
		{
			TESObjectREFR* ref = nullptr;
			cell->objectList.GetNthItem(i, ref);
			
			if (!ref) continue;
			if (ref->formType != kFormType_Character) continue;
			
			Actor* actor = static_cast<Actor*>(ref);
			
			// Skip dead
			if (actor->IsDead(1)) continue;
			
			// Check if this is a horse
			if (!IsHorseRace(actor)) continue;
			
			// Skip if horse has a rider
			if (IsHorseRidden(actor)) continue;
			
			// Check distance to player
			float dist = CalculateDistanceToPlayer(actor);
			if (dist > MAX_SCAN_DISTANCE) continue;
			
			// Check if already registered
			bool alreadyRegistered = false;
			for (int j = 0; j < MAX_AVAILABLE_HORSES; j++)
			{
				if (g_availableHorses[j].isValid && g_availableHorses[j].horseFormID == actor->formID)
				{
					alreadyRegistered = true;
					break;
				}
			}
			
			if (!alreadyRegistered)
			{
				RegisterAvailableHorse(actor->formID);
			}
		}
	}

	static void ScanForUnmountedAggressiveNPCs()
	{
		if (!g_thePlayer || !(*g_thePlayer)) return;
		
		float currentTime = GetGameTime();
		
		// ============================================
		// FIRST: Check for NPCs that have successfully remounted
		// and trigger aggro after 2 second delay
		// ============================================
		for (int i = 0; i < MAX_DISMOUNTED_NPCS; i++)
		{
			if (!g_dismountedNPCs[i].isValid) continue;
			
			// Check if this NPC has remounted and we need to trigger aggro
			if (g_dismountedNPCs[i].remountedSuccessfully && !g_dismountedNPCs[i].aggroTriggered)
			{
				float timeSinceRemount = currentTime - g_dismountedNPCs[i].remountedTime;
				
				if (timeSinceRemount >= REMOUNT_STABLE_DELAY)
				{
					// Verify NPC is still mounted before triggering aggro
					TESForm* form = LookupFormByID(g_dismountedNPCs[i].npcFormID);
					if (form)
					{
						Actor* npc = DYNAMIC_CAST(form, TESForm, Actor);
						if (npc && IsActorMounted(npc) && !npc->IsDead(1))
						{
							_MESSAGE("HorseMountScanner:   Time since remount: %.1f seconds", timeSinceRemount);
							
							// Trigger aggro
							TriggerRemountAggro(npc);
							g_dismountedNPCs[i].aggroTriggered = true;
							
							// Remove from tracking - they're back in combat now
							_MESSAGE("HorseMountScanner: NPC %08X remount complete - removing from tracking", g_dismountedNPCs[i].npcFormID);
							g_dismountedNPCs[i].Reset();
							g_dismountedNPCCount--;
							continue;
						}
					}
				}
			}
			
			// ============================================
			// CONTINUOUS TELEPORT: If mount activation succeeded but NPC hasn't mounted yet,
			// keep teleporting them to the horse every second
			// ============================================
			if (g_dismountedNPCs[i].mountActivationSucceeded && 
			    !g_dismountedNPCs[i].remountedSuccessfully &&
			    g_dismountedNPCs[i].targetHorseFormID != 0)
			{
				float timeSinceLastTeleport = currentTime - g_dismountedNPCs[i].lastTeleportTime;
				
				if (timeSinceLastTeleport >= 1.0f)  // Teleport every 1 second
				{
					TESForm* npcForm = LookupFormByID(g_dismountedNPCs[i].npcFormID);
					TESForm* horseForm = LookupFormByID(g_dismountedNPCs[i].targetHorseFormID);
					
					if (npcForm && horseForm)
					{
						Actor* npc = DYNAMIC_CAST(npcForm, TESForm, Actor);
						Actor* horse = DYNAMIC_CAST(horseForm, TESForm, Actor);
						
						if (npc && horse && !npc->IsDead(1) && !horse->IsDead(1))
						{
							// Check if NPC is now mounted
							if (IsActorMounted(npc))
							{
								// They mounted! Stop teleporting
								const char* name = CALL_MEMBER_FN(npc, GetReferenceName)();
								_MESSAGE("HorseMountScanner: *** NPC '%s' (%08X) REMOUNTED SUCCESSFULLY! ***", 
									name ? name : "Unknown", g_dismountedNPCs[i].npcFormID);
								_MESSAGE("HorseMountScanner: Will trigger aggro in %.1f seconds", REMOUNT_STABLE_DELAY);
								
								g_dismountedNPCs[i].remountedSuccessfully = true;
								g_dismountedNPCs[i].remountedTime = currentTime;
								g_dismountedNPCs[i].aggroTriggered = false;
								g_dismountedNPCs[i].targetHorseFormID = 0;  // Stop teleporting
								continue;
							}
							
							// Not mounted yet - teleport again
							TeleportNPCToHorse(npc, horse);
							g_dismountedNPCs[i].lastTeleportTime = currentTime;
							_MESSAGE("HorseMountScanner: Continuous teleport - NPC %08X to horse %08X", 
								g_dismountedNPCs[i].npcFormID, g_dismountedNPCs[i].targetHorseFormID);
						}
					}
				}
			}
		}
		
		// ============================================
		// SECOND: Scan cell for ANY unmounted NPCs in combat
		// This catches NPCs that were never mounted
		// ============================================
		ScanCellForUnmountedCombatNPCs();
		
		// ============================================
		// THIRD: Scan cell for ANY available horses
		// This catches horses that may not be in our tracking
		// ============================================
		ScanCellForAvailableHorses();
		
		// ============================================
		// FOURTH: Update existing tracked NPCs
		// ============================================
		for (int i = 0; i < MAX_DISMOUNTED_NPCS; i++)
		{
			if (!g_dismountedNPCs[i].isValid) continue;
			
			// Skip NPCs waiting for aggro trigger
			if (g_dismountedNPCs[i].remountedSuccessfully) continue;
			
			// Skip NPCs in continuous teleport mode (handled above)
			if (g_dismountedNPCs[i].mountActivationSucceeded && g_dismountedNPCs[i].targetHorseFormID != 0) continue;
			
			TESForm* form = LookupFormByID(g_dismountedNPCs[i].npcFormID);
			if (!form) 
			{
				g_dismountedNPCs[i].Reset();
				g_dismountedNPCCount--;
				continue;
			}
			
			Actor* npc = DYNAMIC_CAST(form, TESForm, Actor);
			if (!npc)
			{
				g_dismountedNPCs[i].Reset();
				g_dismountedNPCCount--;
				continue;
			}
			
			// Check if NPC is still valid for tracking
			if (npc->IsDead(1))
			{
				_MESSAGE("HorseMountScanner: NPC %08X died - removing from tracking", g_dismountedNPCs[i].npcFormID);
				g_dismountedNPCs[i].Reset();
				g_dismountedNPCCount--;
				continue;
			}
			
			// Check if NPC remounted - mark for aggro trigger!
			if (IsActorMounted(npc))
			{
				const char* name = CALL_MEMBER_FN(npc, GetReferenceName)();
				_MESSAGE("HorseMountScanner: *** NPC '%s' (%08X) REMOUNTED SUCCESSFULLY! ***", 
					name ? name : "Unknown", g_dismountedNPCs[i].npcFormID);
				_MESSAGE("HorseMountScanner: Will trigger aggro in %.1f seconds", REMOUNT_STABLE_DELAY);
				
				g_dismountedNPCs[i].remountedSuccessfully = true;
				g_dismountedNPCs[i].remountedTime = currentTime;
				g_dismountedNPCs[i].aggroTriggered = false;
				continue;
			}
			
			// Skip combat/distance checks if ignoring range (NPC may be flying around after mount attempt)
			if (g_dismountedNPCs[i].ignoreRangeCheck)
			{
				// Check if ignore time expired
				if (currentTime >= g_dismountedNPCs[i].ignoreRangeUntil)
				{
					g_dismountedNPCs[i].ignoreRangeCheck = false;
					_MESSAGE("HorseMountScanner: Range check re-enabled for NPC %08X", g_dismountedNPCs[i].npcFormID);
				}
				else
				{
					// Still update position
					g_dismountedNPCs[i].posX = npc->pos.x;
					g_dismountedNPCs[i].posY = npc->pos.y;
					g_dismountedNPCs[i].posZ = npc->pos.z;
					continue;  // Skip all other checks - keep tracking until timer expires
				}
			}
			
			// Check if still in combat
			if (!npc->IsInCombat())
			{
				_MESSAGE("HorseMountScanner: NPC %08X no longer in combat - removing from tracking", g_dismountedNPCs[i].npcFormID);
				g_dismountedNPCs[i].Reset();
				g_dismountedNPCCount--;
				continue;
			}
			
			// Update position
			g_dismountedNPCs[i].posX = npc->pos.x;
			g_dismountedNPCs[i].posY = npc->pos.y;
			g_dismountedNPCs[i].posZ = npc->pos.z;
		}
		
		// ============================================
		// FIFTH: Update available horses
		// ============================================
		for (int i = 0; i < MAX_AVAILABLE_HORSES; i++)
		{
			if (!g_availableHorses[i].isValid) continue;
			
			TESForm* form = LookupFormByID(g_availableHorses[i].horseFormID);
			if (!form)
			{
				g_availableHorses[i].Reset();
				g_availableHorseCount--;
				continue;
			}
			
			Actor* horse = DYNAMIC_CAST(form, TESForm, Actor);
			if (!horse)
			{
				g_availableHorses[i].Reset();
				g_availableHorseCount--;
				continue;
			}
			
			if (horse->IsDead(1))
			{
				_MESSAGE("HorseMountScanner: Horse %08X died - removing from tracking", g_availableHorses[i].horseFormID);
				g_availableHorses[i].Reset();
				g_availableHorseCount--;
				continue;
			}
			
			// Check if horse got a new rider
			if (IsHorseRidden(horse))
			{
				_MESSAGE("HorseMountScanner: Horse %08X now has rider - removing from available", g_availableHorses[i].horseFormID);
				g_availableHorses[i].Reset();
				g_availableHorseCount--;
				continue;
			}
			
			// Update position
			g_availableHorses[i].posX = horse->pos.x;
			g_availableHorses[i].posY = horse->pos.y;
			g_availableHorses[i].posZ = horse->pos.z;
		}
	}

	// ============================================
	// CHECK IF ACTOR IS IN RAGDOLL STATE
	// ============================================
	
	static bool IsActorInRagdoll(Actor* actor)
	{
		if (!actor) return false;
		
		// Check knockdown state
		// actorState flags08 bits 14-15 contain GetKnockState
		// 0 = normal, 1 = knocked down, 2 = getting up, 3 = queued
		UInt32 knockState = (actor->actorState.flags08 >> 14) & 3;
		if (knockState != 0)
		{
			return true;
		}
		
		return false;
	}
	
	// ============================================
	// TELEPORT NPC TO HORSE
	// Used for initial teleport and continuous teleport while waiting for mount
	// ============================================
	
	static void TeleportNPCToHorse(Actor* npc, Actor* horse)
	{
		if (!npc || !horse) return;
		
		// Calculate position near horse (offset by ~75 units behind horse based on horse's facing)
		float horseAngleZ = horse->rot.z;  // Radians
		float offsetDist = 75.0f;
		float offsetX = offsetDist * sin(horseAngleZ);
		float offsetY = offsetDist * cos(horseAngleZ);
		
		// Directly set NPC position
		npc->pos.x = horse->pos.x - offsetX;
		npc->pos.y = horse->pos.y - offsetY;
		npc->pos.z = horse->pos.z;
		
		// Set NPC rotation to face same direction as horse
		npc->rot.z = horseAngleZ;
	}
	
	// ============================================
	// ATTEMPT TO MOUNT HORSE
	// Makes an NPC activate a horse to mount it
	// Returns true if activation was successful
	// ============================================
	
	static bool AttemptMountHorse(Actor* npc, Actor* horse, int npcSlotIndex)
	{
		if (!npc || !horse) return false;
		
		float currentTime = GetGameTime();
		
		// Check cooldown and dismount delay
		if (npcSlotIndex >= 0 && npcSlotIndex < MAX_DISMOUNTED_NPCS)
		{
			DismountedNPCEntry& entry = g_dismountedNPCs[npcSlotIndex];
			
			// Check if mount attempt is already in progress
			if (entry.mountAttemptInProgress)
			{
				_MESSAGE("HorseMountScanner: Mount already in progress for NPC %08X - skipping", npc->formID);
				return false;
			}
			
			// Check post-dismount delay
			float timeSinceDismount = currentTime - entry.dismountedTime;
			if (timeSinceDismount < POST_DISMOUNT_DELAY)
			{
				_MESSAGE("HorseMountScanner: Post-dismount delay for NPC %08X (%.1f seconds remaining)", 
					npc->formID, POST_DISMOUNT_DELAY - timeSinceDismount);
				return false;
			}
			
			// Check mount attempt cooldown
			float timeSinceLastAttempt = currentTime - entry.lastMountAttemptTime;
			if (entry.lastMountAttemptTime > 0 && timeSinceLastAttempt < MOUNT_ATTEMPT_COOLDOWN)
			{
				_MESSAGE("HorseMountScanner: Mount cooldown for NPC %08X (%.1f seconds remaining)", 
					npc->formID, MOUNT_ATTEMPT_COOLDOWN - timeSinceLastAttempt);
				return false;
			}
		}
		
		// Check if NPC is in ragdoll
		if (IsActorInRagdoll(npc))
		{
			_MESSAGE("HorseMountScanner: NPC %08X is in ragdoll state - waiting for recovery", npc->formID);
			return false;
		}
		
		// Verify NPC is not already mounted
		if (IsActorMounted(npc))
		{
			_MESSAGE("HorseMountScanner: NPC %08X already mounted - skipping", npc->formID);
			return false;
		}
		
		// Verify horse doesn't have a rider
		if (IsHorseRidden(horse))
		{
			_MESSAGE("HorseMountScanner: Horse %08X already has rider - skipping", horse->formID);
			return false;
		}
		
		// Verify both are alive
		if (npc->IsDead(1) || horse->IsDead(1))
		{
			_MESSAGE("HorseMountScanner: NPC or horse is dead - skipping");
			return false;
		}
		
		// Verify both have 3D loaded
		if (!npc->loadedState || !horse->loadedState)
		{
			_MESSAGE("HorseMountScanner: NPC or horse 3D not loaded - skipping");
			return false;
		}
		
		// Mark mount attempt in progress and set ignore range flag
		if (npcSlotIndex >= 0 && npcSlotIndex < MAX_DISMOUNTED_NPCS)
		{
			g_dismountedNPCs[npcSlotIndex].mountAttemptInProgress = true;
			g_dismountedNPCs[npcSlotIndex].lastMountAttemptTime = currentTime;
			g_dismountedNPCs[npcSlotIndex].ignoreRangeCheck = true;
			g_dismountedNPCs[npcSlotIndex].ignoreRangeUntil = currentTime + IGNORE_RANGE_DURATION;
			g_dismountedNPCs[npcSlotIndex].targetHorseFormID = horse->formID;  // Store target horse for continuous teleport
			g_dismountedNPCs[npcSlotIndex].lastTeleportTime = currentTime;
		}
		
		const char* npcName = CALL_MEMBER_FN(npc, GetReferenceName)();
		const char* horseName = CALL_MEMBER_FN(horse, GetReferenceName)();
		
		_MESSAGE("HorseMountScanner: *** ATTEMPTING MOUNT ***");
		_MESSAGE("HorseMountScanner:   NPC: '%s' (%08X)", npcName ? npcName : "Unknown", npc->formID);
		_MESSAGE("HorseMountScanner:   Horse: '%s' (%08X)", horseName ? horseName : "Horse", horse->formID);
		
		// ============================================
		// STEP 1: Stop combat alarm - removes NPC from combat temporarily
		// This is CRITICAL - NPC must not be in combat to mount
		// ============================================
		_MESSAGE("HorseMountScanner:   Stopping combat alarm...");
		StopActorCombatAlarm(npc);
		
		// ============================================
		// STEP 2: Sheathe weapon before mounting
		// ============================================
		if (IsWeaponDrawn(npc))
		{
			_MESSAGE("HorseMountScanner:   Sheathing weapon...");
			npc->DrawSheatheWeapon(false);
		}
		
		// ============================================
		// STEP 3: Teleport NPC directly to horse position
		// ============================================
		_MESSAGE("HorseMountScanner:   Teleporting NPC to horse...");
		TeleportNPCToHorse(npc, horse);
		_MESSAGE("HorseMountScanner:   Teleported to (%.0f, %.0f, %.0f)", npc->pos.x, npc->pos.y, npc->pos.z);
		
		// ============================================
		// STEP 4: Call ActivateRef on the horse with FORCE
		// NOTE: We do NOT call Actor_EvaluatePackage - don't want to disrupt the mounting process
		// ============================================
		_MESSAGE("HorseMountScanner:   Activating horse (FORCE)..."); 
		bool result = TESObjectREFR_ActivateRef(horse, npc, 0, nullptr, 1, true);
		
		_MESSAGE("HorseMountScanner:   Activate result: %s", result ? "SUCCESS" : "FAILED");
		
		// Clear in-progress flag (even on failure, we want cooldown to apply)
		if (npcSlotIndex >= 0 && npcSlotIndex < MAX_DISMOUNTED_NPCS)
		{
			g_dismountedNPCs[npcSlotIndex].mountAttemptInProgress = false;
			
			// If activation succeeded, mark as waiting for remount confirmation
			if (result)
			{
				g_dismountedNPCs[npcSlotIndex].mountActivationSucceeded = true;
				_MESSAGE("HorseMountScanner:   Marked for remount confirmation - will keep teleporting until mounted");
			}
			else
			{
				// Failed - clear target horse
				g_dismountedNPCs[npcSlotIndex].targetHorseFormID = 0;
			}
		}
		
		return result;
	}
	
	// ============================================
	// CHECK AND TRIGGER MOUNTING FOR CLOSE NPCs
	// Called during scan to mount NPCs within range
	// ============================================
	
	static void CheckAndTriggerMounting()
	{
		// For each unmounted NPC, check if they're within range of a horse
		for (int ni = 0; ni < MAX_DISMOUNTED_NPCS; ni++)
		{
			if (!g_dismountedNPCs[ni].isValid) continue;
			
			// Skip if mount attempt already in progress or succeeded
			if (g_dismountedNPCs[ni].mountAttemptInProgress) continue;
			if (g_dismountedNPCs[ni].mountActivationSucceeded) continue;
			if (g_dismountedNPCs[ni].remountedSuccessfully) continue;
			
			TESForm* npcForm = LookupFormByID(g_dismountedNPCs[ni].npcFormID);
			if (!npcForm) continue;
			
			Actor* npc = DYNAMIC_CAST(npcForm, TESForm, Actor);
			if (!npc) continue;
			
			// Skip if already mounted (double-check)
			if (IsActorMounted(npc)) continue;
			
			// Skip if in ragdoll
			if (IsActorInRagdoll(npc)) continue;
		
			// Find the nearest available horse
			float nearestDist = 99999.0f;
			int nearestHorseIdx = -1;
			Actor* nearestHorse = nullptr;
			
			for (int hi = 0; hi < MAX_AVAILABLE_HORSES; hi++)
			{
				if (!g_availableHorses[hi].isValid) continue;
				
				TESForm* horseForm = LookupFormByID(g_availableHorses[hi].horseFormID);
				if (!horseForm) continue;
				
				Actor* horse = DYNAMIC_CAST(horseForm, TESForm, Actor);
				if (!horse) continue;
				
				// Skip if horse now has rider
				if (IsHorseRidden(horse)) continue;
				
				float dist = CalculateDistance3D(npc, horse);
				if (dist < nearestDist)
				{
					nearestDist = dist;
					nearestHorseIdx = hi;
					nearestHorse = horse;
				}
			}
			
			// If we found a horse within range, attempt to mount
			if (nearestHorse && nearestDist <= MOUNT_ACTIVATION_DISTANCE)
			{
				const char* npcName = CALL_MEMBER_FN(npc, GetReferenceName)();
				_MESSAGE("HorseMountScanner: NPC '%s' within %.0f units of horse - triggering mount!",
					npcName ? npcName : "Unknown", nearestDist);
				
				if (AttemptMountHorse(npc, nearestHorse, ni))
				{
					_MESSAGE("HorseMountScanner: Mount triggered for NPC %08X", g_dismountedNPCs[ni].npcFormID);
				}
			}
		}
	}
	
	// ============================================
	// MAIN SCAN - LOG UNMOUNTED NPCs + AVAILABLE HORSES
	// ============================================
	
	static void PerformHorseScan()
	{
		if (!g_thePlayer || !(*g_thePlayer)) return;
		if (g_scanDisabledForSession) return;
		
		if (g_scanAttempts >= MAX_SCAN_ATTEMPTS)
		{
			g_scanDisabledForSession = true;
			_MESSAGE("HorseMountScanner: Max attempts (%d) reached - disabled until combat ends", MAX_SCAN_ATTEMPTS);
			return;
		}
		
		// Update tracking data
		ScanForUnmountedAggressiveNPCs();
		
		// Check if any NPCs are close enough to mount
		CheckAndTriggerMounting();
		
		// Count valid entries
		int npcCount = 0;
		int horseCount = 0;
		
		for (int i = 0; i < MAX_DISMOUNTED_NPCS; i++)
		{
			if (g_dismountedNPCs[i].isValid) npcCount++;
		}
		for (int i = 0; i < MAX_AVAILABLE_HORSES; i++)
		{
			if (g_availableHorses[i].isValid) horseCount++;
		}
		
		g_lastScanHorseCount = horseCount;
		g_scanAttempts++;
		
		// Only log if we have something to report AND at reasonable intervals
		if (npcCount == 0 && horseCount == 0)
		{
			// Don't log "No unmounted NPCs" every scan - too verbose
			return;
		}
		
		// Only log detailed scan info every 5th scan to reduce spam
		if (g_scanAttempts % 5 != 1)
		{
			return;  // Skip detailed logging for most scans
		}
		
		_MESSAGE("HorseMountScanner: ========== SCAN %d/%d ==========", g_scanAttempts, MAX_SCAN_ATTEMPTS);
		_MESSAGE("HorseMountScanner: %d unmounted NPCs, %d available horses", npcCount, horseCount);
		
		// Build arrays for distance calculation
		struct NPCInfo { int idx; const char* name; UInt32 formID; float x, y, z; };
		struct HorseInfo { int idx; const char* name; UInt32 formID; float x, y, z; };
		
		NPCInfo npcs[MAX_DISMOUNTED_NPCS];
		HorseInfo horses[MAX_AVAILABLE_HORSES];
		int npcIdx = 0;
		int horseIdx = 0;
		
		// Collect NPC info
		for (int i = 0; i < MAX_DISMOUNTED_NPCS; i++)
		{
			if (!g_dismountedNPCs[i].isValid) continue;
			
			TESForm* form = LookupFormByID(g_dismountedNPCs[i].npcFormID);
			const char* name = "Unknown";
			if (form)
			{
				Actor* actor = DYNAMIC_CAST(form, TESForm, Actor);
				if (actor) name = CALL_MEMBER_FN(actor, GetReferenceName)();
			}
			
			npcs[npcIdx].idx = i;
			npcs[npcIdx].name = name;
			npcs[npcIdx].formID = g_dismountedNPCs[i].npcFormID;
			npcs[npcIdx].x = g_dismountedNPCs[i].posX;
			npcs[npcIdx].y = g_dismountedNPCs[i].posY;
			npcs[npcIdx].z = g_dismountedNPCs[i].posZ;
			npcIdx++;
		}
		
		// Collect Horse info
		for (int i = 0; i < MAX_AVAILABLE_HORSES; i++)
		{
			if (!g_availableHorses[i].isValid) continue;
			
			TESForm* form = LookupFormByID(g_availableHorses[i].horseFormID);
			const char* name = "Unknown";
			if (form)
			{
				Actor* actor = DYNAMIC_CAST(form, TESForm, Actor);
				if (actor) name = CALL_MEMBER_FN(actor, GetReferenceName)();
			}
			
			horses[horseIdx].idx = i;
			horses[horseIdx].name = name;
			horses[horseIdx].formID = g_availableHorses[i].horseFormID;
			horses[horseIdx].x = g_availableHorses[i].posX;
			horses[horseIdx].y = g_availableHorses[i].posY;
			horses[horseIdx].z = g_availableHorses[i].posZ;
			horseIdx++;
		}
		
		// Log each NPC with their nearest horse
		for (int ni = 0; ni < npcIdx; ni++)
		{
			float nearestDist = 99999.0f;
			int nearestHorse = -1;
			
			// Find nearest horse
			for (int hi = 0; hi < horseIdx; hi++)
			{
				float dist = CalculateDistance3D(
					npcs[ni].x, npcs[ni].y, npcs[ni].z,
					horses[hi].x, horses[hi].y, horses[hi].z);
				
				if (dist < nearestDist)
				{
					nearestDist = dist;
					nearestHorse = hi;
				}
			}
			
			// Log NPC with nearest horse info
			if (nearestHorse >= 0)
			{
				_MESSAGE("  [NPC] '%s' (%08X) pos(%.0f,%.0f,%.0f) -> nearest horse '%s' at %.0f units",
					npcs[ni].name ? npcs[ni].name : "Unknown",
					npcs[ni].formID,
					npcs[ni].x, npcs[ni].y, npcs[ni].z,
					horses[nearestHorse].name ? horses[nearestHorse].name : "Horse",
					nearestDist);
			}
			else
			{
				_MESSAGE("  [NPC] '%s' (%08X) pos(%.0f,%.0f,%.0f) -> NO HORSES AVAILABLE",
					npcs[ni].name ? npcs[ni].name : "Unknown",
					npcs[ni].formID,
					npcs[ni].x, npcs[ni].y, npcs[ni].z);
			}
		}
		
		// Log available horses
		if (horseIdx > 0)
		{
			_MESSAGE("  Available horses:");
			for (int hi = 0; hi < horseIdx; hi++)
			{
				_MESSAGE("    [HORSE] '%s' (%08X) pos(%.0f,%.0f,%.0f)",
					horses[hi].name ? horses[hi].name : "Horse",
					horses[hi].formID,
					horses[hi].x, horses[hi].y, horses[hi].z);
			}
		}
		
		_MESSAGE("HorseMountScanner: ========== END ==========");
	}

	// ============================================
	// PUBLIC API
	// ============================================

	void InitHorseMountScanner()
	{
		if (g_scannerInitialized) return;
		_MESSAGE("HorseMountScanner: Initializing...");
		g_scannerActive = false;
		g_scannerReady = false;
		g_lastScanTime = 0.0f;
		g_lastScanHorseCount = 0;
		g_playerWasInCombat = false;
		g_lastCombatCheckTime = 0.0f;
		g_scanAttempts = 0;
		g_scanDisabledForSession = false;
		ClearAllDismountedTracking();
		g_scannerInitialized = true;
		_MESSAGE("HorseMountScanner: Initialized");
	}

	void ShutdownHorseMountScanner()
	{
		if (!g_scannerInitialized) return;
		g_scannerActive = false;
		g_scannerReady = false;
		ClearAllDismountedTracking();
		g_scannerInitialized = false;
	}

	void StopHorseMountScanner()
	{
		g_scannerActive = false;
		g_scannerReady = false;
		g_playerWasInCombat = false;
		ClearAllDismountedTracking();
	}

	void ResetHorseMountScanner()
	{
		_MESSAGE("HorseMountScanner: Resetting for game load...");
		g_scannerActive = false;
		g_scannerReady = false;
		g_lastScanTime = 0.0f;
		g_lastScanHorseCount = 0;
		g_playerWasInCombat = false;
		g_lastCombatCheckTime = 0.0f;
		g_scanAttempts = 0;
		g_scanDisabledForSession = false;
		ClearAllDismountedTracking();
		g_scannerActivationTime = std::chrono::steady_clock::now() + std::chrono::milliseconds((int)(ACTIVATION_DELAY_SECONDS * 1000));
		_MESSAGE("HorseMountScanner: Will activate in %.0f seconds", ACTIVATION_DELAY_SECONDS);
	}

	bool UpdateHorseMountScanner()
	{
		if (!g_scannerInitialized) return false;
		
		// Check if remounting is enabled in config
		if (!EnableRemounting)
		{
			// If scanner was active, deactivate it
			if (g_scannerActive)
			{
				_MESSAGE("HorseMountScanner: Remounting disabled in config - deactivating");
				g_scannerActive = false;
				g_playerWasInCombat = false;
				ClearAllDismountedTracking();
			}
			return false;
		}
		
		if (!g_scannerReady)
		{
			auto now = std::chrono::steady_clock::now();
			if (now < g_scannerActivationTime) return false;
			g_scannerReady = true;
			_MESSAGE("HorseMountScanner: Now ready");
		}
		
		if (!g_thePlayer || !(*g_thePlayer)) return false;
		Actor* player = *g_thePlayer;
		if (!player->parentCell) return false;
		
		float currentTime = GetGameTime();
		if ((currentTime - g_lastCombatCheckTime) < COMBAT_CHECK_INTERVAL) return g_scannerActive;
		g_lastCombatCheckTime = currentTime;
		
		if (!IsOutdoorCell())
		{
			if (g_scannerActive)
			{
				_MESSAGE("HorseMountScanner: Interior - deactivated");
				g_scannerActive = false;
				g_playerWasInCombat = false;
				g_scanAttempts = 0;
				g_scanDisabledForSession = false;
				ClearAllDismountedTracking();
			}
			return false;
		}
		
		bool playerInCombat = player->IsInCombat();
		bool hasPendingAggro = HasPendingAggroTriggers();
		
		if (playerInCombat && !g_playerWasInCombat)
		{
			_MESSAGE("HorseMountScanner: *** COMBAT START - ACTIVATED ***");
			g_scannerActive = true;
			g_lastScanTime = 0.0f;
			g_scanAttempts = 0;
			g_scanDisabledForSession = false;
			PerformHorseScan();
		}
		else if (!playerInCombat && g_playerWasInCombat)
		{
			// Combat ended - but DON'T deactivate if we have pending aggro triggers!
			if (hasPendingAggro)
			{
				_MESSAGE("HorseMountScanner: Combat ended but has pending aggro triggers - staying active");
				// Process pending aggro triggers
				ScanForUnmountedAggressiveNPCs();
			}
			else
			{
				_MESSAGE("HorseMountScanner: Combat ended - deactivated");
				g_scannerActive = false;
				g_scanAttempts = 0;
				g_scanDisabledForSession = false;
				ClearAllDismountedTracking();
			}
		}
		else if (g_scannerActive && playerInCombat)
		{
			if ((currentTime - g_lastScanTime) >= SCAN_UPDATE_INTERVAL)
			{
				g_lastScanTime = currentTime;
				PerformHorseScan();
			}
		}
		else if (!playerInCombat && hasPendingAggro)
		{
			// Player not in combat, but we have pending aggro - keep processing
			_MESSAGE("HorseMountScanner: Processing pending aggro triggers (player not in combat)");
			ScanForUnmountedAggressiveNPCs();
			
			// If no more pending, fully deactivate
			if (!HasPendingAggroTriggers())
			{
				_MESSAGE("HorseMountScanner: All aggro triggers processed - deactivating");
				g_scannerActive = false;
				g_scanAttempts = 0;
				g_scanDisabledForSession = false;
				ClearAllDismountedTracking();
			}
		}
		
		g_playerWasInCombat = playerInCombat;
		return g_scannerActive || hasPendingAggro;
	}

	bool IsScannerActive() { return g_scannerActive; }
	int GetLastScanHorseCount() { return g_lastScanHorseCount; }
	void InstallCombatStateHook() { _MESSAGE("HorseMountScanner: Poll-based (no hooks)"); }
}

