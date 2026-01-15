#include "MountedCombat.h"
#include "CombatStyles.h"
#include "FleeingBehavior.h"
#include "FactionData.h"
#include "WeaponDetection.h"
#include "NPCProtection.h"
#include "AILogging.h"
#include "ArrowSystem.h"
#include "MultiMountedCombat.h"
#include "DynamicPackages.h"
#include "HorseMountScanner.h"
#include "CompanionCombat.h"
#include "SpecialMovesets.h"
#include "MagicCastingSystem.h"
#include "skse64/GameRTTI.h"
#include "skse64/GameData.h"
#include "skse64/GameObjects.h"
#include "skse64_common/Utilities.h"
#include <cmath>
#include <ctime>

namespace MountedNPCCombatVR
{
	// ============================================
	// Configuration
	// ============================================
	
	float g_updateInterval = 0.5f;  // Update every 500ms
	const int MAX_TRACKED_NPCS_ARRAY = 10;  // Maximum array size (hardcoded for memory safety)
	// Actual runtime limit is MaxTrackedMountedNPCs from config (1-10)
	const float FLEE_SAFE_DISTANCE = 2001.0f;  // Distance at which fleeing NPCs feel safe (just over 1 cell)
	const float ALLY_ALERT_RANGE = 700.0f;    // Range to alert allies when attacked
	
	// ============================================
	// Horse Animation Configuration (from SingleMountedCombat)
	// ============================================
	
	// Horse sprint animation FormIDs from Skyrim.esm
	const UInt32 HORSE_SPRINT_START_FORMID = 0x0004408B;
	const UInt32 HORSE_SPRINT_STOP_FORMID = 0x0004408C;
	const UInt32 HORSE_REAR_UP_FORMID = 0x000DCD7C;  // Horse rear up animation from Skyrim.esm
	
	// Horse jump animation from MountedNPCCombat.esp
	const UInt32 HORSE_JUMP_BASE_FORMID = 0x0008E6;
	const char* JUMP_ESP_NAME = "MountedNPCCombat.esp";
	
	// Horse jump cooldown
	const float HORSE_JUMP_COOLDOWN = 4.0f;  // Only attempt jump every 4 seconds
	
	// Horse sprint cooldown
	const float HORSE_SPRINT_COOLDOWN = 3.0f;  // Only attempt sprint every 3 seconds
	const float HORSE_SPRINT_DURATION = 5.0f;  // Sprint lasts 5 seconds before needing refresh
	
	// ============================================
	// Player Mounted Combat State
	// ============================================
	
	bool g_playerInMountedCombat = false;
	bool g_playerTriggeredMountedCombat = false;
	bool g_playerWasMountedWhenCombatStarted = false;
	bool g_playerInExterior = true;  // Assume exterior until checked
	bool g_playerIsDead = false;   // Track player death state
	static bool g_lastPlayerMountedCombatState = false;  // For detecting state changes
	static bool g_lastExteriorState = true;  // For detecting cell changes
	static bool g_lastPlayerDeadState = false;  // For detecting death state changes
	
	// ============================================
	// Combat Class Global Bools
	// ============================================
	
	bool g_guardInMountedCombat = false;
	bool g_soldierInMountedCombat = false;
	bool g_banditInMountedCombat = false;
	bool g_mageInMountedCombat = false;
	bool g_civilianFleeing = false;
	
	// ============================================
	// Internal State
	// ============================================
	
	static MountedNPCData g_trackedNPCs[MAX_TRACKED_NPCS_ARRAY];
	static bool g_systemInitialized = false;
	
	// ============================================
	// DISENGAGE COOLDOWN TRACKING
	// Prevents NPCs from immediately re-engaging after distance disengage
	// ============================================
	
	const float DISENGAGE_COOLDOWN = 3.5f;  // 3.5 seconds before NPC can re-engage
	const int MAX_DISENGAGED_NPCS = 10;
	
	struct DisengagedNPCEntry
	{
		UInt32 npcFormID;
		float disengageTime;
		bool isValid;
		
		void Reset()
		{
			npcFormID = 0;
			disengageTime = 0;
			isValid = false;
		}
	};
	
	static DisengagedNPCEntry g_disengagedNPCs[MAX_DISENGAGED_NPCS];
	
	// Check if an NPC is on disengage cooldown
	bool IsNPCOnDisengageCooldown(UInt32 npcFormID)
	{
		float currentTime = GetCurrentGameTime();
		
		for (int i = 0; i < MAX_DISENGAGED_NPCS; i++)
		{
			if (g_disengagedNPCs[i].isValid && g_disengagedNPCs[i].npcFormID == npcFormID)
			{
				float elapsed = currentTime - g_disengagedNPCs[i].disengageTime;
				if (elapsed < DISENGAGE_COOLDOWN)
				{
					return true;  // Still on cooldown
				}
				else
				{
					// Cooldown expired - remove from list
					g_disengagedNPCs[i].Reset();
					return false;
				}
			}
		}
		return false;  // Not in list
	}
	
	// Add an NPC to the disengage cooldown list
	void AddNPCToDisengageCooldown(UInt32 npcFormID)
	{
		// Check if already in list
		for (int i = 0; i < MAX_DISENGAGED_NPCS; i++)
		{
			if (g_disengagedNPCs[i].isValid && g_disengagedNPCs[i].npcFormID == npcFormID)
			{
				// Update time
				g_disengagedNPCs[i].disengageTime = GetCurrentGameTime();
				return;
			}
		}
		
		// Find empty slot
		for (int i = 0; i < MAX_DISENGAGED_NPCS; i++)
		{
			if (!g_disengagedNPCs[i].isValid)
			{
				g_disengagedNPCs[i].npcFormID = npcFormID;
				g_disengagedNPCs[i].disengageTime = GetCurrentGameTime();
				g_disengagedNPCs[i].isValid = true;
				_MESSAGE("MountedCombat: Added NPC %08X to disengage cooldown (%.0f seconds)", npcFormID, DISENGAGE_COOLDOWN);
				return;
			}
		}
		
		// List full - overwrite oldest entry
		float oldestTime = GetCurrentGameTime();
		int oldestIdx = 0;
		for (int i = 0; i < MAX_DISENGAGED_NPCS; i++)
		{
			if (g_disengagedNPCs[i].disengageTime < oldestTime)
			{
				oldestTime = g_disengagedNPCs[i].disengageTime;
				oldestIdx = i;
			}
		}
		g_disengagedNPCs[oldestIdx].npcFormID = npcFormID;
		g_disengagedNPCs[oldestIdx].disengageTime = GetCurrentGameTime();
		g_disengagedNPCs[oldestIdx].isValid = true;
	}
	
	// Clear all disengage cooldowns
	void ClearAllDisengageCooldowns()
	{
		for (int i = 0; i < MAX_DISENGAGED_NPCS; i++)
		{
			g_disengagedNPCs[i].Reset();
		}
	}

	// ============================================
	// Horse Animation State (from SingleMountedCombat)
	// ============================================
	
	static bool g_singleCombatInitialized = false;
	static float g_combatStartTime = 0.0f;
	
	// Cached sprint idles
	static TESIdleForm* g_horseSprintStart = nullptr;
	static TESIdleForm* g_horseSprintStop = nullptr;
	static TESIdleForm* g_horseRearUp = nullptr;
	static bool g_sprintIdlesInitialized = false;
	
	// Cached horse jump idle
	static TESIdleForm* g_horseJump = nullptr;
	static bool g_jumpIdleInitialized = false;
	
	// Horse sprint state tracking
	struct HorseSprintData
	{
		UInt32 horseFormID;
		float lastSprintStartTime;  // When sprint was last started
		bool isSprinting;           // Currently sprinting
		bool isValid;
	};
	
	static HorseSprintData g_horseSprintData[10];
	static int g_horseSprintCount = 0;
	
	// Horse jump cooldown tracking
	struct HorseJumpData
	{
		UInt32 horseFormID;
		float lastJumpTime;
		bool isValid;
	};
	
	static HorseJumpData g_horseJumpData[5];
	static int g_horseJumpCount = 0;

	// ============================================
	// Core Functions
	// ============================================
	
	void InitMountedCombatSystem()
	{
		_MESSAGE("MountedCombat: === INITIALIZING SYSTEM ===");
		_MESSAGE("MountedCombat: Previous g_systemInitialized state: %s", g_systemInitialized ? "TRUE" : "FALSE");
		
		// Always reinitialize - don't skip based on g_systemInitialized
		// This ensures the system works correctly after loading saves/new games
		
		// Clear all tracking data
		ResetAllMountedNPCs();
		
		// Initialize weapon state system
		InitWeaponStateSystem();
		
		// Initialize companion combat system
		InitCompanionCombat();
		
		// Initialize tactical flee system
		InitTacticalFlee();
		
		// Initialize civilian flee system
		InitCivilianFlee();
		
		g_systemInitialized = true;
		_MESSAGE("MountedCombat: System initialized (max %d NPCs tracked, config limit: %d)", 
			MAX_TRACKED_NPCS_ARRAY, MaxTrackedMountedNPCs);
		_MESSAGE("MountedCombat: === INITIALIZATION COMPLETE ===");
	}
	
	void ShutdownMountedCombatSystem()
	{
		_MESSAGE("MountedCombat: === SHUTTING DOWN SYSTEM ===");
		
		if (!g_systemInitialized)
		{
			_MESSAGE("MountedCombat: System was not initialized, nothing to shut down");
			return;
		}
		
		ResetAllMountedNPCs();
		
		// Shutdown companion combat system
		ShutdownCompanionCombat();
		
		// Shutdown tactical flee system
		ShutdownTacticalFlee();
		
		// Shutdown civilian flee system
		ShutdownCivilianFlee();
		
		g_systemInitialized = false;
		
		_MESSAGE("MountedCombat: === SHUTDOWN COMPLETE ===");
	}
	
	void ResetAllMountedNPCs()
	{
		_MESSAGE("MountedCombat: Resetting all runtime state...");
		
		// Count how many NPCs we're clearing
		int clearedCount = 0;
		
		// Remove protection from all tracked NPCs before reset
		// AND clear horse movement packages
		for (int i = 0; i < MAX_TRACKED_NPCS_ARRAY; i++)
		{
			if (g_trackedNPCs[i].isValid)
			{
				clearedCount++;
				
				// Clear rider protection and follow target
				TESForm* form = LookupFormByID(g_trackedNPCs[i].actorFormID);
				if (form)
				{
					Actor* actor = DYNAMIC_CAST(form, TESForm, Actor);
					if (actor)
					{
						RemoveMountedProtection(actor);
						ClearNPCFollowTarget(actor);
					}
				}
				
				// Clear horse movement packages
				if (g_trackedNPCs[i].mountFormID != 0)
				{
					TESForm* mountForm = LookupFormByID(g_trackedNPCs[i].mountFormID);
					if (mountForm)
					{
						Actor* mount = DYNAMIC_CAST(mountForm, TESForm, Actor);
						if (mount)
						{
							Actor_ClearKeepOffsetFromActor(mount);
							Actor_EvaluatePackage(mount, false, false);
						}
					}
				}
			}
			g_trackedNPCs[i].Reset();
		}
		
		if (clearedCount > 0)
		{
			_MESSAGE("MountedCombat: Cleared %d tracked NPCs (including horse packages)", clearedCount);
		}
		
		// Clear any remaining protection tracking
		ClearAllMountedProtection();
		
		// Clear all disengage cooldowns
		ClearAllDisengageCooldowns();
		
		// Reset weapon state system
		ResetWeaponStateSystem();
		
		// Reset companion combat tracking
		ResetCompanionCombat();
		
		// Reset tactical flee system
		ResetTacticalFlee();
		
		// Reset civilian flee system
		ResetCivilianFlee();
		
		// ============================================
		// RESET MAGIC CASTING SYSTEM
		// Clear all mage spell casting state
		// ============================================
		ResetMagicCastingSystem();
		
		// Reset player mounted combat state
		g_playerInMountedCombat = false;
		g_playerTriggeredMountedCombat = false;
		g_playerWasMountedWhenCombatStarted = false;
		g_playerInExterior = true;  // Assume exterior until checked
		g_playerIsDead = false;     // Assume alive until checked
		g_lastPlayerMountedCombatState = false;
		g_lastExteriorState = true;
		g_lastPlayerDeadState = false;
		
		// Reset combat class bools
		g_guardInMountedCombat = false;
		g_soldierInMountedCombat = false;
		g_banditInMountedCombat = false;
		g_mageInMountedCombat = false;
		g_civilianFleeing = false;
		
		// Mark system as needing re-initialization
		g_systemInitialized = false;
		
		_MESSAGE("MountedCombat: All runtime state reset");
	}
	
	void UpdateCombatClassBools()
	{
		// Reset all bools
		g_guardInMountedCombat = false;
		g_soldierInMountedCombat = false;
		g_banditInMountedCombat = false;
		g_mageInMountedCombat = false;
		g_civilianFleeing = false;
		
		// Check each tracked NPC
		for (int i = 0; i < MAX_TRACKED_NPCS_ARRAY; i++)
		{
			if (!g_trackedNPCs[i].isValid)
			{
				continue;
			}
			
			switch (g_trackedNPCs[i].combatClass)
			{
				case MountedCombatClass::GuardMelee:
					g_guardInMountedCombat = true;
					break;
				case MountedCombatClass::SoldierMelee:
					g_soldierInMountedCombat = true;
					break;
				case MountedCombatClass::BanditRanged:
					g_banditInMountedCombat = true;
					break;
				case MountedCombatClass::MageCaster:
					g_mageInMountedCombat = true;
					break;
				case MountedCombatClass::CivilianFlee:
					g_civilianFleeing = true;
					break;
				default:
					break;
			}
		}
	}
	
	void OnDismountBlocked(Actor* actor, Actor* mount)
	{
		if (!g_systemInitialized)
		{
			return;
		}
		
		if (!actor || !mount)
		{
			return;
		}
		
		// ============================================
		// CHECK DISENGAGE COOLDOWN
		// If this NPC recently disengaged due to distance, don't re-engage
		// ============================================
		if (IsNPCOnDisengageCooldown(actor->formID))
		{
			return;  // Skip - NPC is on cooldown from recent disengage
		}
		
		const char* actorName = CALL_MEMBER_FN(actor, GetReferenceName)();
		
		// Get or create tracking data for this NPC
		MountedNPCData* data = GetOrCreateNPCData(actor);
		if (!data)
		{
			_MESSAGE("MountedCombat: ERROR - Could not create tracking data for %08X (max NPCs reached?)", actor->formID);
			return;
		}
		
		// Apply mounted protection (stagger/bleedout immunity)
		ApplyMountedProtection(actor);
		
		// Update mount info
		data->mountFormID = mount->formID;
		
		// Determine behavior type (fight or flee) based on faction
		if (data->behavior == MountedBehaviorType::Unknown)
		{
			data->behavior = DetermineBehaviorType(actor);
		}
		
		// Determine combat class based on faction
		if (data->combatClass == MountedCombatClass::None)
		{
			data->combatClass = DetermineCombatClass(actor);
		}
		
		// Rate limit logging - only log first detection per NPC
		static UInt32 lastDetectedNPC = 0;
		if (lastDetectedNPC != actor->formID)
		{
			lastDetectedNPC = actor->formID;
			_MESSAGE("MountedCombat: Detected '%s' (%08X) on horse %08X - Class: %s", 
				actorName ? actorName : "Unknown", actor->formID, mount->formID,
				GetCombatClassName(data->combatClass));
		}

		// ============================================
		// PRE-ASSIGN CAPTAIN OR LEADER TO RANGED ROLE
		// Use centralized weapon system
		// ============================================
		if (actorName && (strstr(actorName, "Captain") != nullptr || strstr(actorName, "Leader") != nullptr))
		{
			if (!HasBowInInventory(actor))
			{
				GiveDefaultBow(actor);
			}
			
			// Use centralized weapon system to switch to bow
			RequestWeaponSwitch(actor, WeaponRequest::Bow);
			
			_MESSAGE("MountedCombat: Captain '%s' pre-assigned to RANGED", actorName);
			
			// Get the captain's actual combat target (not always player!)
			Actor* target = GetCombatTarget(actor);
			
			if (target)
			{
				RegisterMultiRider(actor, mount, target);
				Actor_ClearKeepOffsetFromActor(mount);
			}
		}
		
		// ============================================
		// PRE-ASSIGN MAGES TO USE WARSTAFF
		// Mages get the warstaff from MountedNPCCombat.esp
		// This is their primary weapon for mounted combat
		// ============================================
		if (data->combatClass == MountedCombatClass::MageCaster)
		{
			// Give warstaff if they don't have one
			if (!HasStaffInInventory(actor))
			{
				GiveWarstaff(actor);
				_MESSAGE("MountedCombat: Gave Warstaff to Mage '%s'", actorName ? actorName : "Unknown");
			}
			
			// Use centralized weapon system to switch to staff
			RequestWeaponSwitch(actor, WeaponRequest::Staff);
			
			_MESSAGE("MountedCombat: Mage '%s' pre-assigned to STAFF combat", actorName ? actorName : "Unknown");
			
			// Get the mage's actual combat target (not always player!)
			Actor* target = GetCombatTarget(actor);
			
			if (target)
			{
				RegisterMultiRider(actor, mount, target);
				Actor_ClearKeepOffsetFromActor(mount);
			}
		}

		// Track player's mount status when combat with mounted NPC starts
		OnPlayerTriggeredMountedCombat(actor);
		
		// Get combat target
		Actor* target = GetCombatTarget(actor);
		
		if (target)
		{
			data->targetFormID = target->formID;
		}
		else
		{
			// For guards/soldiers without a target, check if they should be hostile to player
			// Only target player if NPC is actually hostile to them (e.g., player committed crime)
			if (data->combatClass == MountedCombatClass::GuardMelee ||
				data->combatClass == MountedCombatClass::SoldierMelee)
			{
				if (g_thePlayer && (*g_thePlayer))
				{
					Actor* player = *g_thePlayer;
					
					// Only target player if NPC is hostile to them
					bool isHostileToPlayer = IsActorHostileToActor(actor, player);
					
					if (isHostileToPlayer && player->IsInCombat())
					{
						float distance = GetDistanceBetween(actor, player);
						if (distance < ALLY_ALERT_RANGE)
						{
							data->targetFormID = player->formID;
							target = player;
							
							UInt32 playerHandle = player->CreateRefHandle();
							if (playerHandle != 0 && playerHandle != *g_invalidRefHandle)
							{
								actor->currentCombatTarget = playerHandle;
							}
							actor->flags2 |= Actor::kFlag_kAttackOnSight;
							
							const char* actorName = CALL_MEMBER_FN(actor, GetReferenceName)();
							_MESSAGE("MountedCombat: Guard/Soldier '%s' targeting player (is hostile)", 
								actorName ? actorName : "Unknown");
						}
					}
				}
			}
		}
		
		// Alert nearby allies if we have a target and are a guard/soldier
		if (target && (data->combatClass == MountedCombatClass::GuardMelee ||
			    data->combatClass == MountedCombatClass::SoldierMelee))
		{
			AlertNearbyMountedAllies(actor, target);
		}
		
		// Set initial state based on combat class
		if (data->state == MountedCombatState::None)
		{
			data->combatStartTime = GetCurrentGameTime();
			data->weaponDrawn = false;
			
			switch (data->combatClass)
			{
				case MountedCombatClass::CivilianFlee:
					data->state = MountedCombatState::Fleeing;
					break;
				case MountedCombatClass::BanditRanged:
				case MountedCombatClass::MageCaster:
					data->state = MountedCombatState::RangedAttack;
					break;
				default:
					data->state = MountedCombatState::Engaging;
					break;
			}
			data->stateStartTime = GetCurrentGameTime();
		}
		
		data->lastUpdateTime = GetCurrentGameTime();
		UpdateCombatClassBools();
	}
	
	void UpdateMountedCombat()
	{
		if (!g_systemInitialized)
		{
			return;
		}
		
		// ============================================
		// CRITICAL: EARLY EXIT IF PLAYER IS DEAD
		// This is the PRIMARY protection against CTD when player dies
		// All subsystems should also have their own checks but this is first line
		// ============================================
		if (g_thePlayer && (*g_thePlayer) && (*g_thePlayer)->IsDead(1))
		{
			// Player is dead - skip all processing
			// The death handler in UpdatePlayerMountedCombatState will clean up
			return;
		}
		
		// Update delayed arrow fires (200ms delay between animation and arrow spawn)
		UpdateDelayedArrowFires();
		
		// Update the combat styles system (reinforcement of follow packages)
		UpdateCombatStylesSystem();
		
		// Update multi-mounted combat (ranged role behaviors, etc.)
		UpdateMultiMountedCombat();
		
		// Update temporary stagger timers (restore protection after block stagger)
		UpdateTemporaryStaggerTimers();
		
		// Update player mounted combat state
		UpdatePlayerMountedCombatState();
		
		// Update combat class bools
		UpdateCombatClassBools();
		
		// Scan for hostile targets (guards/soldiers will engage hostiles within range)
		ScanForHostileTargets();
		
		// Update horse mount scanner (independent system for tracking horses near combat NPCs)
		UpdateHorseMountScanner();
		
		// Update NPC mounting detection scanner (logs NPCs mounting horses within 2000 units)
		UpdateNPCMountingScanner();
		
		// Update mounted companion combat (player teammates on horseback)
		UpdateMountedCompanionCombat();
		
		// Update tactical flee system (low health riders may temporarily retreat)
		UpdateTacticalFlee();
		
		// Update civilian flee system (civilians flee from threats)
		UpdateCivilianFlee();
		
		float currentTime = GetCurrentGameTime();
		
		for (int i = 0; i < MAX_TRACKED_NPCS_ARRAY; i++)
		{
			MountedNPCData* data = &g_trackedNPCs[i];
			
			if (!data->isValid)
			{
				continue;
			}
			
			// Check update interval
			if ((currentTime - data->lastUpdateTime) < g_updateInterval)
			{
				continue;
			}
			
			// Look up the actor
			TESForm* form = LookupFormByID(data->actorFormID);
			if (!form)
			{
				data->Reset();
				continue;
			}
			
			Actor* actor = DYNAMIC_CAST(form, TESForm, Actor);
			if (!actor)
			{
				data->Reset();
				continue;
			}
			
			// CRITICAL: Check if NPC died - remove protection IMMEDIATELY
			// This prevents the high mass from affecting ragdoll physics
			if (actor->IsDead(1))
			{
				_MESSAGE("MountedCombat: NPC %08X DIED - removing protection immediately", data->actorFormID);
				RemoveMountedProtection(actor);
				ClearNPCFollowTarget(actor);
				data->Reset();
				continue;
			}
			
			// Check if still mounted
			NiPointer<Actor> mountPtr;
			bool stillMounted = CALL_MEMBER_FN(actor, GetMount)(mountPtr);
			if (!stillMounted || !mountPtr)
			{
				// NPC dismounted - notify scanner before clearing tracking
				OnNPCDismounted(data->actorFormID, data->mountFormID);
				
				RemoveMountedProtection(actor);
				ClearNPCFollowTarget(actor);
				data->Reset();
				continue;
			}
			
			// ============================================
			// SKIP IF RIDER IS CURRENTLY FLEEING
			// Tactical flee system handles their behavior
			// ============================================
			if (IsRiderFleeing(data->actorFormID))
			{
				data->lastUpdateTime = currentTime;
				continue;
			}
			
			// ============================================
			// CHECK FOR DIALOGUE/CRIME PACKAGE OVERRIDE
			// This detects and logs when a guard enters crime dialogue
			// We no longer try to clear it - just log for debugging
			// ============================================
			if (DetectDialoguePackageIssue(actor))
			{
				// Log the full AI state for debugging
				LogMountedCombatAIState(actor, mountPtr.get(), data->actorFormID);
			}
			
			// Check if still in combat
			if (!actor->IsInCombat())
			{
				if (data->weaponDrawn)
				{
					SetWeaponDrawn(actor, false);
					_MESSAGE("MountedCombat: NPC %08X - combat ended, sheathing weapon", data->actorFormID);
				}
				
				RemoveMountedProtection(actor);
				ClearNPCFollowTarget(actor);
				data->Reset();
				continue;
			}
			
			// Get current target/threat
			Actor* target = GetCombatTarget(actor);
			if (target)
			{
				data->targetFormID = target->formID;
				
				// ============================================
				// CHECK FOR TACTICAL FLEE
				// Only check if we have a valid target to flee from
				// ============================================
				if (CheckAndTriggerTacticalFlee(actor, mountPtr.get(), target))
				{
					// Flee was triggered - skip normal combat behavior this frame
					data->lastUpdateTime = currentTime;
					continue;
				}
			}
			
			// Update weapon info periodically
			data->weaponInfo = GetWeaponInfo(actor);
			
			// ============================================
			// ROUTE TO COMBAT STYLES
			// All combat logic is handled in CombatStyles.cpp
			// MountedCombat.cpp only tracks and routes
			// ============================================
			
			switch (data->combatClass)
			{
				case MountedCombatClass::GuardMelee:
					GuardCombat::ExecuteBehavior(data, actor, mountPtr, target);
					break;
					
				case MountedCombatClass::SoldierMelee:
					SoldierCombat::ExecuteBehavior(data, actor, mountPtr, target);
					break;
					
				case MountedCombatClass::BanditRanged:
					BanditCombat::ExecuteBehavior(data, actor, mountPtr, target);
					break;
					
				case MountedCombatClass::MageCaster:
					MageCombat::ExecuteBehavior(data, actor, mountPtr, target);
					break;
					
				case MountedCombatClass::CivilianFlee:
					// TODO: CivilianFlee behavior not yet implemented
					// CivilianFlee::ExecuteBehavior(data, actor, mountPtr, target);
					break;
				
				case MountedCombatClass::Other:
					// Unknown faction - use Guard melee behavior (aggressive)
					GuardCombat::ExecuteBehavior(data, actor, mountPtr, target);
					break;
					
				default:
					// None class - do nothing, rely on vanilla AI
					break;
			}
			
			data->lastUpdateTime = currentTime;
		}
	}

	// ============================================
	// NPC Tracking
	// ============================================
	
	MountedNPCData* GetOrCreateNPCData(Actor* actor)
	{
		if (!actor)
		{
			return nullptr;
		}
		
		UInt32 formID = actor->formID;
		
		// First, check if already tracked
		for (int i = 0; i < MAX_TRACKED_NPCS_ARRAY; i++)
		{
			if (g_trackedNPCs[i].isValid && g_trackedNPCs[i].actorFormID == formID)
			{
				return &g_trackedNPCs[i];
			}
		}
		
		// Find empty slot (limited by MaxTrackedMountedNPCs config)
		for (int i = 0; i < MaxTrackedMountedNPCs && i < MAX_TRACKED_NPCS_ARRAY; i++)
		{
			if (!g_trackedNPCs[i].isValid)
			{
				g_trackedNPCs[i].Reset();
				g_trackedNPCs[i].actorFormID = formID;
				g_trackedNPCs[i].isValid = true;
				return &g_trackedNPCs[i];
			}
		}
		
		return nullptr;
	}
	
	MountedNPCData* GetNPCData(UInt32 formID)
	{
		for (int i = 0; i < MAX_TRACKED_NPCS_ARRAY; i++)
		{
			if (g_trackedNPCs[i].isValid && g_trackedNPCs[i].actorFormID == formID)
			{
				return &g_trackedNPCs[i];
			}
		}
		return nullptr;
	}
	
	MountedNPCData* GetNPCDataByIndex(int index)
	{
		if (index < 0 || index >= MAX_TRACKED_NPCS_ARRAY)
		{
			return nullptr;
		}
		return &g_trackedNPCs[index];
	}
	
	void RemoveNPCFromTracking(UInt32 formID)
	{
		for (int i = 0; i < MAX_TRACKED_NPCS_ARRAY; i++)
		{
			if (g_trackedNPCs[i].isValid && g_trackedNPCs[i].actorFormID == formID)
			{
				// Get the mount FormID BEFORE resetting (we need it to clear the horse)
				UInt32 mountFormID = g_trackedNPCs[i].mountFormID;
				
				// Remove mounted protection and clear rider's follow target
				TESForm* form = LookupFormByID(formID);
				if (form)
				{
					Actor* actor = DYNAMIC_CAST(form, TESForm, Actor);
					if (actor)
					{
						RemoveMountedProtection(actor);
						
						// Clear follow mode on the rider (also clears mage spell state)
						ClearNPCFollowTarget(actor);
					}
				}
				
				// ============================================
				// CLEAR COMBAT STATES FOR THIS NPC
				// Belt-and-suspenders: also clear directly in case
				// ClearNPCFollowTarget didn't find the actor in its list
				// ============================================
				ResetBowAttackState(formID);
				ResetRapidFireBowAttack(formID);
				ResetMageSpellState(formID);
				ClearWeaponStateData(formID);
				
				// ============================================
				// CRITICAL: Clear the HORSE's movement packages too!
				// The horse may have KeepOffsetFromActor set which
				// makes it follow the player even after rider dismounts.
				// ============================================
				if (mountFormID != 0)
				{
					TESForm* mountForm = LookupFormByID(mountFormID);
					if (mountForm)
					{
						Actor* mount = DYNAMIC_CAST(mountForm, TESForm, Actor);
						if (mount)
						{
							const char* mountName = CALL_MEMBER_FN(mount, GetReferenceName)();
							_MESSAGE("MountedCombat: Clearing movement packages from horse '%s' (%08X)",
								mountName ? mountName : "Horse", mountFormID);
							
							// Clear KeepOffsetFromActor on the horse
							Actor_ClearKeepOffsetFromActor(mount);
							
							// Clear special movesets (charge, stand ground, rapid fire, etc.)
							ClearAllMovesetData(mountFormID);
							
							// Re-evaluate the horse's AI packages so it returns to normal behavior
							Actor_EvaluatePackage(mount, false, false);
						}
					}
					else
					{
						// Horse form not found but still clear moveset data by ID
						ClearAllMovesetData(mountFormID);
					}
				}
				
				g_trackedNPCs[i].Reset();
				
				_MESSAGE("MountedCombat: Removed NPC %08X from tracking (mount %08X also cleared)", formID, mountFormID);
				return;
			}
		}
	}
	
	bool IsNPCTracked(UInt32 formID)
	{
		return GetNPCData(formID) != nullptr;
	}
	
	int GetTrackedNPCCount()
	{
		int count = 0;
		for (int i = 0; i < MAX_TRACKED_NPCS_ARRAY; i++)
		{
			if (g_trackedNPCs[i].isValid)
			{
				count++;
			}
		}
		return count;
	}

	// ============================================
	// Faction / Behavior Determination
	// ============================================

	MountedBehaviorType DetermineBehaviorType(Actor* actor)
	{
		if (!actor)
		{
			return MountedBehaviorType::Passive; // Default to flee if unknown
		}
		
		// Use the faction checks from FactionData.cpp
		// Aggressive factions: Guards, Soldiers, Bandits, Mages
		if (IsGuardFaction(actor) || IsSoldierFaction(actor) || 
			IsBanditFaction(actor) || IsMageFaction(actor))
		{
			return MountedBehaviorType::Aggressive;
		}
		
		// Passive factions: Civilians
		if (IsCivilianFaction(actor))
		{
			return MountedBehaviorType::Passive;
		}
		
		// Default: Check if NPC has weapons - armed NPCs are more likely to fight
		MountedWeaponInfo weaponInfo = GetWeaponInfo(actor);
		if (weaponInfo.hasWeaponEquipped || weaponInfo.hasWeaponSheathed)
		{
			// Armed but unknown faction - default to aggressive
			return MountedBehaviorType::Aggressive;
		}
		
		// Unarmed unknown faction - flee
		return MountedBehaviorType::Passive;
	}
	
	// ============================================
	// Combat Behavior (Aggressive NPCs)
	// ============================================

	MountedCombatState DetermineAggressiveState(Actor* actor, Actor* mount, Actor* target, MountedWeaponInfo* weaponInfo)
	{
		if (!actor || !mount || !target || !weaponInfo)
		{
			return MountedCombatState::None;
		}
		
		float distance = GetDistanceBetween(actor, target);
		float attackRange = weaponInfo->weaponReach > 0 ? weaponInfo->weaponReach : 256.0f;
		
		// Adjust ranges based on weapon type
		if (weaponInfo->isBow)
		{
			// Ranged weapon - can attack from far, prefer medium distance
			if (distance <= 512.0f)
			{
				return MountedCombatState::Circling;  // Too close for bow, circle
			}
			else if (distance <= 2048.0f)
			{
				return MountedCombatState::Attacking;  // Good bow range
			}
			else
			{
				return MountedCombatState::Engaging;  // Close distance
			}
		}
		else
		{
			// Melee weapon
			if (distance <= attackRange + 64.0f)  // Add some buffer
			{
				return MountedCombatState::Attacking;
			}
			else if (distance <= 512.0f)
			{
				return MountedCombatState::Charging;  // Close enough to charge
			}
			else if (distance <= 1024.0f)
			{
				if (IsPathClear(mount, target))
				{
					return MountedCombatState::Charging;
				}
				else
				{
					return MountedCombatState::Engaging;
				}
			}
			else
			{
				return MountedCombatState::Engaging;
			}
		}
	}
	
	void ExecuteAggressiveBehavior(MountedNPCData* npcData, Actor* actor, Actor* mount, Actor* target)
	{
		if (!npcData || !actor || !mount)
		{
			return;
		}
		
		// Determine optimal state
		MountedCombatState newState = DetermineAggressiveState(actor, mount, target, &npcData->weaponInfo);
		
		// State transition
		if (newState != npcData->state && newState != MountedCombatState::None)
		{
			npcData->state = newState;
			npcData->stateStartTime = GetCurrentGameTime();
		}
		
		// State tracking is done - vanilla AI + quest package handles actual movement
		// No need for ExecuteEngaging/ExecuteCharging - the quest follow package does this
	}
	
	void ExecuteAttacking(Actor* actor, Actor* mount, Actor* target, MountedWeaponInfo* weaponInfo)
	{
		// Vanilla AI handles attacks
	}
	
	void ExecuteCircling(Actor* actor, Actor* mount, Actor* target)
	{
		// TODO: Command mount to circle around target
		// This is useful for bow users or repositioning
		// For now, rely on default AI
	}

	// ============================================
	// Flee Behavior (Passive NPCs)
	// ============================================

	MountedCombatState DeterminePassiveState(Actor* actor, Actor* mount, Actor* threat)
	{
		if (!actor || !mount)
		{
			return MountedCombatState::None;
		}
		
		if (!threat)
		{
			// No threat - can stop fleeing
			return MountedCombatState::None;
		}
		
		float distance = GetDistanceBetween(actor, threat);
		
		if (distance >= FLEE_SAFE_DISTANCE)
		{
			// Safe distance reached - stop fleeing
			return MountedCombatState::None;
		}
		else
		{
			// Still too close - keep fleeing
			return MountedCombatState::Fleeing;
		}
	}
	
	void ExecutePassiveBehavior(MountedNPCData* npcData, Actor* actor, Actor* mount, Actor* threat)
	{
		if (!npcData || !actor || !mount)
		{
			return;
		}
		
		// Determine flee state
		MountedCombatState newState = DeterminePassiveState(actor, mount, threat);
		
		// State transition
		if (newState != npcData->state)
		{
			npcData->state = newState;
			npcData->stateStartTime = GetCurrentGameTime();
			
			if (newState == MountedCombatState::None)
			{
				_MESSAGE("MountedCombat: NPC %08X reached safe distance, stopping flee", npcData->actorFormID);
			}
		}
		
		// Execute flee
		if (npcData->state == MountedCombatState::Fleeing)
		{
			ExecuteFleeing(actor, mount, threat);
		}
	}
	
	void ExecuteFleeing(Actor* actor, Actor* mount, Actor* threat)
	{
		// TODO: Command mount to gallop away from threat
		// For now, rely on default flee AI
		
		// Future implementation:
		// 1. Calculate direction away from threat
		// 2. Set mount to sprint/gallop
		// 3. Move in opposite direction
	}

	// ============================================
	// Utility Functions
	// ============================================

	Actor* GetCombatTarget(Actor* actor)
	{
		if (!actor)
		{
			return nullptr;
		}
		
		// ============================================
		// FIRST: Check if we have a stored target in tracking data
		// This is set by EngageHostileTarget() or OnDismountBlocked()
		// ============================================
		MountedNPCData* data = GetNPCData(actor->formID);
		if (data && data->targetFormID != 0)
		{
			// Guards/Soldiers should only target the player if player is genuinely hostile
			if (data->targetFormID == 0x14 && 
				(data->combatClass == MountedCombatClass::GuardMelee || 
				 data->combatClass == MountedCombatClass::SoldierMelee))
			{
				// Check if player is actually hostile to this guard (attacked them, has bounty, etc.)
				if (g_thePlayer && (*g_thePlayer))
				{
					Actor* player = *g_thePlayer;
					
					// Check if the guard's actual game combat target is the player
					// This means the game itself decided the player is hostile
					UInt32 combatTargetHandle = actor->currentCombatTarget;
					if (combatTargetHandle != 0)
					{
						NiPointer<TESObjectREFR> targetRef;
						LookupREFRByHandle(combatTargetHandle, targetRef);
						if (targetRef && targetRef->formID == 0x14)
						{
							// Game says player is the target - player must have attacked
							return player;
						}
					}
					
					// Also check if player is in combat - if so, they probably attacked someone
					if (player->IsInCombat())
					{
						// Player is in combat - they're a valid target for guards
						return player;
					}
				}
				
				// Player not genuinely hostile - clear and look for real hostiles
				data->targetFormID = 0;
			}
			else
			{
				TESForm* targetForm = LookupFormByID(data->targetFormID);
				if (targetForm && targetForm->formType == kFormType_Character)
				{
					Actor* storedTarget = static_cast<Actor*>(targetForm);
					
					// Verify target is still valid (alive)
					if (!storedTarget->IsDead(1))
					{
						return storedTarget;
					}
					else
					{
						// Target died - clear it so we can find a new one
						data->targetFormID = 0;
					}
				}
			}
		}
		
		// ============================================
		// SECOND: Check the actor's actual combat target from the game
		// ============================================
		UInt32 combatTargetHandle = actor->currentCombatTarget;
		if (combatTargetHandle != 0)
		{
			NiPointer<TESObjectREFR> targetRef;
			LookupREFRByHandle(combatTargetHandle, targetRef);
			if (targetRef)
			{
				Actor* combatTarget = DYNAMIC_CAST(targetRef.get(), TESObjectREFR, Actor);
				if (combatTarget && !combatTarget->IsDead(1))
				{
					return combatTarget;
				}
			}
		}
		
		// ============================================
		// THIRD: For Guards/Soldiers, scan for nearest hostile from our list
		// This ensures they target bandits/etc, not random NPCs
		// ============================================
		const float HOSTILE_SCAN_RANGE = 1400.0f;
		
		if (data && (data->combatClass == MountedCombatClass::GuardMelee || 
		      data->combatClass == MountedCombatClass::SoldierMelee))
		{
			Actor* hostile = FindNearestHostileTarget(actor, HOSTILE_SCAN_RANGE);
			if (hostile)
			{
				// Store this as the new target
				data->targetFormID = hostile->formID;
				_MESSAGE("GetCombatTarget: Guard %08X acquired new hostile target %08X", actor->formID, hostile->formID);
				return hostile;
			}
			
			// ============================================
			// FOURTH: For guards with NO hostiles found, check if player is hostile
			// Only target player if guard is actually hostile to them
			// ============================================
			if (g_thePlayer && (*g_thePlayer))
			{
				Actor* player = *g_thePlayer;
				
				// Only target player if guard is hostile to them
				bool isHostileToPlayer = IsActorHostileToActor(actor, player);
				
				if (isHostileToPlayer && player->IsInCombat())
				{
					float distance = GetDistanceBetween(actor, player);
					if (distance < ALLY_ALERT_RANGE)
					{
						// Player is hostile and in combat nearby
						data->targetFormID = player->formID;
						_MESSAGE("GetCombatTarget: Guard %08X targeting player (hostile, in combat nearby, %.0f units)", 
							actor->formID, distance);
						return player;
					}
				}
			}
			
			// No valid target found
			return nullptr;
		}
		
		// ============================================
		// LAST: For bandits and other hostile classes, player is a valid target
		// ============================================
		if (g_thePlayer && (*g_thePlayer))
		{
			Actor* player = *g_thePlayer;
			float distance = GetDistanceBetween(actor, player);
			
			// If player is close, they may be the target
			if (distance < 4096.0f)
			{
				return player;
			}
		}
		
		return nullptr;
	}
	
	float GetDistanceBetween(Actor* a, Actor* b)
	{
		if (!a || !b)
		{
			return 999999.0f;
		}
		
		NiPoint3 posA = a->pos;
		NiPoint3 posB = b->pos;
		
		float dx = posA.x - posB.x;
		float dy = posA.y - posB.y;
		float dz = posA.z - posB.z;
		
		return sqrt(dx*dx + dy*dy + dz*dz);
	}
	
	bool CanAttackTarget(Actor* actor, Actor* target, MountedWeaponInfo* weaponInfo)
	{
		if (!actor || !target || !weaponInfo)
		{
			return false;
		}
		
		float distance = GetDistanceBetween(actor, target);
		float reach = weaponInfo->weaponReach > 0 ? weaponInfo->weaponReach : 256.0f;
		
		return distance <= reach;
	}
	
	bool IsPathClear(Actor* mount, Actor* target)
	{
		// TODO: Implement actual pathfinding/raycast check
		// For now, assume path is clear
		return true;
	}
	
	float GetCurrentGameTime()
	{
		static auto startTime = std::chrono::steady_clock::now();
		auto now = std::chrono::steady_clock::now();
		auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - startTime);
		return duration.count() / 1000.0f;
	}
	
	NiPoint3 GetFleeDirection(Actor* actor, Actor* threat)
	{
		NiPoint3 fleeDir;
		fleeDir.x = 0;
		fleeDir.y = 0;
		fleeDir.z = 0;
		
		if (!actor || !threat)
		{
			return fleeDir;
		}
		
		// Calculate direction away from threat
		fleeDir.x = actor->pos.x - threat->pos.x;
		fleeDir.y = actor->pos.y - threat->pos.y;
		fleeDir.z = 0;  // Keep on ground plane
		
		// Normalize
		float length = sqrt(fleeDir.x * fleeDir.x + fleeDir.y * fleeDir.y);
		if (length > 0)
		{
			fleeDir.x /= length;
			fleeDir.y /= length;
		}
		
		return fleeDir;
	}
	
	// ============================================
	// Player Mounted Combat State
	// ============================================

	bool IsPlayerMounted()
	{
		if (!g_thePlayer || !(*g_thePlayer))
		{
			return false;
		}
		
		Actor* player = *g_thePlayer;
		
		NiPointer<Actor> mount;
		bool isMounted = CALL_MEMBER_FN(player, GetMount)(mount);
		
		return isMounted && mount;
	}
	
	bool IsPlayerInCombat()
	{
		if (!g_thePlayer || !(*g_thePlayer))
		{
			return false;
		}
		
		Actor* player = *g_thePlayer;
		return player->IsInCombat();
	}
	
	bool IsPlayerInExteriorCell()
	{
		if (!g_thePlayer || !(*g_thePlayer))
		{
			return false;
		}
		
		Actor* player = *g_thePlayer;
		
		// Get the player's current cell
		TESObjectCELL* cell = player->parentCell;
		if (!cell)
		{
			return false;
		}
		
		// Check if the cell is an exterior cell
		// Exterior cells have a worldspace, interior cells do not
		// We can check via unk120 which is the TESWorldSpace*
		TESWorldSpace* worldspace = cell->unk120;
		
		// If worldspace is not null, this is an exterior cell
		return worldspace != nullptr;
	}
	
	bool IsPlayerDead()
	{
		if (!g_thePlayer || !(*g_thePlayer))
		{
			return true;  // No player = treat as dead (disable logic)
		}
		
		Actor* player = *g_thePlayer;
		
		// IsDead takes a parameter (1 for actors)
		return player->IsDead(1);
	}
	
	void UpdatePlayerMountedCombatState()
	{
		// First, check if player is dead - disable ALL mounted combat logic
		g_playerIsDead = IsPlayerDead();
		
		if (g_playerIsDead != g_lastPlayerDeadState)
		{
			if (g_playerIsDead)
			{
				_MESSAGE("MountedCombat: Player DIED - disabling ALL mounted combat logic");
				
				// Immediately reset everything
				for (int i = 0; i < MAX_TRACKED_NPCS_ARRAY; i++)
				{
					if (g_trackedNPCs[i].isValid)
					{
						TESForm* form = LookupFormByID(g_trackedNPCs[i].actorFormID);
						if (form)
						{
							Actor* actor = DYNAMIC_CAST(form, TESForm, Actor);
							if (actor)
							{
								RemoveMountedProtection(actor);
							}
						}
						
						g_trackedNPCs[i].Reset();
					}
				}
				
				ClearAllMountedProtection();
				
				// ============================================
				// CRITICAL: Clear all multi-rider tracking on player death
				// This resets ranged roles, horse state, weapon states, etc.
				// ============================================
				ClearAllMultiRiders();
				
				// ============================================
				// CRITICAL: Reset all special moveset
				// ============================================
				ResetAllSpecialMovesets();
				
				// ============================================
				// CRITICAL: Reset companion combat tracking on player death
				// This clears all mounted companion tracking and packages
				// ============================================
				ResetCompanionCombat();
				
				// ============================================
				// CRITICAL: Reset tactical and civilian flee on player death
				// ============================================
				ResetTacticalFlee();
				ResetCivilianFlee();
				
				// ============================================
				// CRITICAL: Reset weapon state tracking on player death
				// Prevents stale FormIDs from causing crashes
				// ============================================
				ResetWeaponStateSystem();
				
				// ============================================
				// CRITICAL: Reset arrow system on player death
				// Clears bow attack data and pending projectiles
				// ============================================
				ResetArrowSystem();
				
				// ============================================
				// CRITICAL: Reset magic casting system on player death
				// Clears spell casting state and delayed casts
				// ============================================
				ResetMagicCastingSystem();
				
				// ============================================
				// CRITICAL: Reset dynamic package state on player death
				// Clears horse movement tracking and follow packages
				// ============================================
				ResetDynamicPackageState();
				
				// Reset combat state but NOT the dead tracking!
				g_playerInMountedCombat = false;
				g_playerTriggeredMountedCombat = false;
				g_playerWasMountedWhenCombatStarted = false;
				g_playerInExterior = true;  // Assume exterior until checked
				// DON'T reset g_playerIsDead or g_lastPlayerDeadState here!
			}
			else
			{
				_MESSAGE("MountedCombat: Player ALIVE - mounted combat logic enabled");
			}
			g_lastPlayerDeadState = g_playerIsDead;
		}
		
		// If player is dead, don't process anything else
		if (g_playerIsDead)
		{
			g_playerInMountedCombat = false;
			return;
		}
		
		// Update exterior cell status
		g_playerInExterior = IsPlayerInExteriorCell();
		
		// Log cell transition
		if (g_playerInExterior != g_lastExteriorState)
		{
			if (g_playerInExterior)
			{
				_MESSAGE("MountedCombat: Player entered EXTERIOR cell - mounted combat ENABLED");
			}
			else
			{
				_MESSAGE("MountedCombat: Player entered INTERIOR cell - mounted combat DISABLED");
				
				// Clear all tracked NPCs when entering interior
				for (int i = 0; i < MAX_TRACKED_NPCS_ARRAY; i++)
				{
					if (g_trackedNPCs[i].isValid)
					{
						g_trackedNPCs[i].Reset();
					}
				}
				
				// Reset flee systems on cell change
				ResetTacticalFlee();
				ResetCivilianFlee();
				
				// Reset combat state bools
				g_playerInMountedCombat = false;
				g_playerTriggeredMountedCombat = false;
				g_playerWasMountedWhenCombatStarted = false;
				UpdateCombatClassBools();
			}
			g_lastExteriorState = g_playerInExterior;
		}
		
		// If in interior, don't process mounted combat
		if (!g_playerInExterior)
		{
			g_playerInMountedCombat = false;
			return;
		}
		
		// Check if player is mounted AND in combat with aggressive mounted NPCs
		bool playerMounted = IsPlayerMounted();
		bool playerInCombat = IsPlayerInCombat();
		bool hasAggressiveMountedNPCs = false;
		
		// Check if any tracked NPCs are aggressive (fighting the player)
		for (int i = 0; i < MAX_TRACKED_NPCS_ARRAY; i++)
		{
			if (g_trackedNPCs[i].isValid && 
				g_trackedNPCs[i].behavior == MountedBehaviorType::Aggressive)
			{
				hasAggressiveMountedNPCs = true;
				break;
			}
		}
		
		// Player is in mounted combat if:
		// 1. Player is mounted
		// 2. Player is in combat
		// 3. There are aggressive mounted NPCs fighting them
		g_playerInMountedCombat = playerMounted && playerInCombat && hasAggressiveMountedNPCs;
		
		// Reset triggered combat flag if no more tracked NPCs
		if (!hasAggressiveMountedNPCs && GetTrackedNPCCount() == 0)
		{
			if (g_playerTriggeredMountedCombat)
			{
				_MESSAGE("MountedCombat: Player mounted combat ENDED - no more mounted NPC threats");
				g_playerTriggeredMountedCombat = false;
				g_playerWasMountedWhenCombatStarted = false;
			}
		}
		
		// Log state changes
		if (g_playerInMountedCombat != g_lastPlayerMountedCombatState)
		{
			if (g_playerInMountedCombat)
			{
				_MESSAGE("MountedCombat: Player ENTERED mounted combat");
			}
			else
			{
				_MESSAGE("MountedCombat: Player EXITED mounted combat");
			}
			g_lastPlayerMountedCombatState = g_playerInMountedCombat;
		}
	}
	
	void OnPlayerTriggeredMountedCombat(Actor* mountedNPC)
	{
		if (!mountedNPC)
		{
			return;
		}
		
		// Check if this is a new combat engagement
		if (!g_playerTriggeredMountedCombat)
		{
			// First mounted NPC engagement - capture player's mount status
			g_playerTriggeredMountedCombat = true;
			g_playerWasMountedWhenCombatStarted = IsPlayerMounted();
			
			const char* npcName = CALL_MEMBER_FN(mountedNPC, GetReferenceName)();
			
			if (g_playerWasMountedWhenCombatStarted)
			{
				_MESSAGE("MountedCombat: Player (MOUNTED) triggered combat with mounted NPC '%s' (FormID: %08X)",
					npcName ? npcName : "Unknown", mountedNPC->formID);
			}
			else
			{
				_MESSAGE("MountedCombat: Player (ON FOOT) triggered combat with mounted NPC '%s' (FormID: %08X)",
					npcName ? npcName : "Unknown", mountedNPC->formID);
			}
		}
	}
	
	// ============================================
	// HOSTILE TARGET DETECTION & ENGAGEMENT
	// ============================================

	// ============================================
	// HOSTILE DETECTION CONFIGURATION
	// Now loaded from config - see HostileDetectionRange and HostileScanInterval
	// ============================================

	static float g_lastHostileScanTime = 0;

	// ============================================
	// ALERT NEARBY ALLIES WHEN ATTACKED
	// When a mounted guard is attacked, alert other nearby
	// mounted guards/soldiers to join combat against the attacker
	// ============================================
	void AlertNearbyMountedAllies(Actor* attackedNPC, Actor* attacker)
	{
		if (!attackedNPC || !attacker) return;
		if (attacker->IsDead(1)) return;
		
		MountedCombatClass attackedClass = DetermineCombatClass(attackedNPC);
		
		// Only guards and soldiers alert allies
		if (attackedClass != MountedCombatClass::GuardMelee &&
			attackedClass != MountedCombatClass::SoldierMelee)
		{
			return;
		}
		
		TESObjectCELL* cell = attackedNPC->parentCell;
		if (!cell) return;
		
		int alliesAlerted = 0;
		
		// Scan for nearby mounted NPCs
		for (UInt32 i = 0; i < cell->objectList.count; i++)
		{
			TESObjectREFR* ref = nullptr;
			cell->objectList.GetNthItem(i, ref);
			
			if (!ref) continue;
			
			Actor* potentialAlly = DYNAMIC_CAST(ref, TESObjectREFR, Actor);
			if (!potentialAlly) continue;
			
			// Skip self, attacker, player, dead
			if (potentialAlly->formID == attackedNPC->formID) continue;
			if (potentialAlly->formID == attacker->formID) continue;
			if (g_thePlayer && (*g_thePlayer) && potentialAlly == (*g_thePlayer)) continue;
			if (potentialAlly->IsDead(1)) continue;
			
			// Check if mounted
			NiPointer<Actor> mount;
			bool isMounted = CALL_MEMBER_FN(potentialAlly, GetMount)(mount);
			if (!isMounted || !mount) continue;
			
			// Check distance
			float distance = GetDistanceBetween(attackedNPC, potentialAlly);
			if (distance > ALLY_ALERT_RANGE) continue;
			
			// Check if ally (same type: guard or soldier)
			MountedCombatClass allyClass = DetermineCombatClass(potentialAlly);
			if (allyClass != MountedCombatClass::GuardMelee &&
				allyClass != MountedCombatClass::SoldierMelee)
			{
				continue;
			}
			
			// Check if already tracked with this target
			MountedNPCData* existingData = GetNPCData(potentialAlly->formID);
			if (existingData && existingData->isValid)
			{
				if (existingData->targetFormID == attacker->formID) continue;
				
				// Update target
				existingData->targetFormID = attacker->formID;
				SetNPCFollowTarget(potentialAlly, attacker);
				alliesAlerted++;
				continue;
			}
			
			// New ally - set up tracking
			MountedNPCData* data = GetOrCreateNPCData(potentialAlly);
			if (!data) continue;
			
			data->mountFormID = mount->formID;
			data->targetFormID = attacker->formID;
			data->combatClass = allyClass;
			data->behavior = MountedBehaviorType::Aggressive;
			data->state = MountedCombatState::Engaging;
			data->stateStartTime = GetCurrentGameTime();
			data->combatStartTime = GetCurrentGameTime();
			data->weaponDrawn = false;
			
			ApplyMountedProtection(potentialAlly);
			
			potentialAlly->flags2 |= Actor::kFlag_kAttackOnSight;
			
			UInt32 attackerHandle = attacker->CreateRefHandle();
			if (attackerHandle != 0 && attackerHandle != *g_invalidRefHandle)
			{
				potentialAlly->currentCombatTarget = attackerHandle;
			}
			
			SetNPCFollowTarget(potentialAlly, attacker);
			alliesAlerted++;
		}
		
		if (alliesAlerted > 0)
		{
			_MESSAGE("MountedCombat: Alerted %d nearby allies to attack %08X", alliesAlerted, attacker->formID);
		}
	}
	
	// Find the nearest hostile NPC within range of a mounted guard/soldier
	Actor* FindNearestHostileTarget(Actor* rider, float maxRange)
	{
		if (!rider) return nullptr;
		
		TESObjectCELL* cell = rider->parentCell;
		if (!cell) return nullptr;
		
		Actor* nearestHostile = nullptr;
		float nearestDistance = maxRange + 1.0f;
		
		// Iterate through references in the cell
		for (UInt32 i = 0; i < cell->objectList.count; i++)
		{
			TESObjectREFR* ref = nullptr;
			cell->objectList.GetNthItem(i, ref);
			
			if (!ref) continue;
			
			Actor* potentialTarget = DYNAMIC_CAST(ref, TESObjectREFR, Actor);
			if (!potentialTarget) continue;
			
			// Skip self
			if (potentialTarget->formID == rider->formID) continue;
			
			// Skip dead actors
			if (potentialTarget->IsDead(1)) continue;
			
			// Skip player (handled separately)
			if (g_thePlayer && (*g_thePlayer) && potentialTarget->formID == (*g_thePlayer)->formID) continue;
			
			// ============================================
			// COMPANION HANDLING
			// Only skip companions who are NOT hostile to this guard
			// If a companion attacks a guard, the guard CAN target them
			// ============================================
			if (IsCompanion(potentialTarget))
			{
				// Check if companion is hostile to this guard (attacked them, etc.)
				// Use the game's IsHostileToActor check
				bool companionIsHostile = IsActorHostileToActor(potentialTarget, rider);
				
				if (!companionIsHostile)
				{
					// Companion is friendly - skip them
					continue;
				}
				// else: Companion IS hostile - allow targeting
			}
			
			// Check if this actor is hostile (from our FactionData lists)
			// OR if it's a hostile companion (already passed the check above)
			bool isKnownHostile = IsHostileNPC(potentialTarget);
			bool isHostileCompanion = IsCompanion(potentialTarget) && IsActorHostileToActor(potentialTarget, rider);
			
			if (!isKnownHostile && !isHostileCompanion) continue;
			
			// Calculate distance
			float distance = GetDistanceBetween(rider, potentialTarget);
			
			// Check if within range and closer than current nearest
			if (distance <= maxRange && distance < nearestDistance)
			{
				nearestHostile = potentialTarget;
				nearestDistance = distance;
			}
		}
		
		return nearestHostile;
	}
	
	// Force a mounted NPC into combat with a target
	bool EngageHostileTarget(Actor* rider, Actor* target)
	{
		if (!rider || !target) return false;
		
		// Get or create tracking data
		MountedNPCData* data = GetOrCreateNPCData(rider);
		if (!data)
		{
			return false;
		}
		
		// Set target
		data->targetFormID = target->formID;
		
		// Determine combat class if not set
		if (data->combatClass == MountedCombatClass::None)
		{
			data->combatClass = DetermineCombatClass(rider);
		}
		
		// Set behavior to aggressive
		data->behavior = MountedBehaviorType::Aggressive;
		
		// Apply mounted protection
		ApplyMountedProtection(rider);
		
		// Set rider to be attack-on-sight
		rider->flags2 |= Actor::kFlag_kAttackOnSight;
		
		// Set combat target handle on rider
		UInt32 targetHandle = target->CreateRefHandle();
		if (targetHandle != 0 && targetHandle != *g_invalidRefHandle)
		{
			rider->currentCombatTarget = targetHandle;
		}
		
		// Draw weapon (with slight delay for realism)
		data->combatStartTime = GetCurrentGameTime();
		data->weaponDrawn = false;
		
		// Set initial state
		data->state = MountedCombatState::Engaging;
		data->stateStartTime = GetCurrentGameTime();
		
		// Inject follow package
		SetNPCFollowTarget(rider, target);
		
		const char* riderName = CALL_MEMBER_FN(rider, GetReferenceName)();
		const char* hostileType = GetHostileTypeName(target);
		_MESSAGE("MountedCombat: %08X '%s' engaging %08X (%s)", 
			rider->formID, riderName ? riderName : "Unknown", target->formID, hostileType);
		
		UpdateCombatClassBools();
		
		return true;
	}
	
	// Scan all tracked mounted NPCs (guards/soldiers) for nearby hostile targets
	void ScanForHostileTargets()
	{
		float currentTime = GetCurrentGameTime();
		
		// Only scan periodically (using config value)
		if ((currentTime - g_lastHostileScanTime) < HostileScanInterval)
		{
			return;
		}
		g_lastHostileScanTime = currentTime;
		
		// Skip if player is dead or in interior
		if (g_playerIsDead || !g_playerInExterior)
		{
			return;
		}
		
		// Scan all tracked mounted NPCs
		for (int i = 0; i < MAX_TRACKED_NPCS_ARRAY; i++)
		{
			MountedNPCData* data = &g_trackedNPCs[i];
			
			if (!data->isValid) continue;
			
			// Only guards and soldiers scan for hostiles
			if (data->combatClass != MountedCombatClass::GuardMelee &&
				data->combatClass != MountedCombatClass::SoldierMelee)
			{
				continue;
			}
			
			TESForm* riderForm = LookupFormByID(data->actorFormID);
			if (!riderForm) continue;
			
			Actor* rider = DYNAMIC_CAST(riderForm, TESForm, Actor);
			if (!rider) continue;
			
			// ============================================
			// CHECK RIDER'S ACTUAL COMBAT TARGET FROM GAME
			// If the game has set a combat target, RESPECT IT
			// Don't override game AI target selection
			// ============================================
			UInt32 combatTargetHandle = rider->currentCombatTarget;
			if (combatTargetHandle != 0)
			{
				NiPointer<TESObjectREFR> targetRef;
				LookupREFRByHandle(combatTargetHandle, targetRef);
				if (targetRef && targetRef->formType == kFormType_Character)
				{
					Actor* actualTarget = static_cast<Actor*>(targetRef.get());
					if (actualTarget && !actualTarget->IsDead(1))
					{
						// Rider has a valid combat target from the game - update our tracking
						if (data->targetFormID != actualTarget->formID)
						{
							data->targetFormID = actualTarget->formID;
						}
						// Rider already has a target - skip scanning for new ones
						continue;
					}
				}
			}
			
			// ============================================
			// CHECK IF STORED TARGET IS STILL VALID
			// ============================================
			bool needsNewTarget = false;
			
			if (data->targetFormID == 0)
			{
				needsNewTarget = true;
			}
			else if (data->targetFormID == 0x14)
			{
				// Targeting player - check if player is genuinely hostile
				bool playerIsGenuinelyHostile = false;
				if (combatTargetHandle != 0)
				{
					NiPointer<TESObjectREFR> targetRef;
					LookupREFRByHandle(combatTargetHandle, targetRef);
					if (targetRef && targetRef->formID == 0x14)
					{
						playerIsGenuinelyHostile = true;
					}
				}
				
				if (!playerIsGenuinelyHostile)
				{
					needsNewTarget = true;
				}
				else
				{
					continue;  // Player is hostile, keep targeting them
				}
			}
			else
			{
				// Verify non-player target is still valid and alive
				TESForm* targetForm = LookupFormByID(data->targetFormID);
				if (targetForm && targetForm->formType == kFormType_Character)
				{
					Actor* currentTarget = static_cast<Actor*>(targetForm);
					if (currentTarget->IsDead(1))
					{
						needsNewTarget = true;
						data->targetFormID = 0;
					}
					// else: target is alive - continue with current target, no need for new one
				}
				else
				{
					needsNewTarget = true;
					data->targetFormID = 0;
				}
			}
			
			if (!needsNewTarget) continue;
			
			// Find nearest hostile (using config value)
			Actor* hostile = FindNearestHostileTarget(rider, HostileDetectionRange);
			if (hostile)
			{
				// Clear any existing follow behavior first if switching targets
				if (rider->IsInCombat())
				{
					ClearNPCFollowTarget(rider);
				}
				
				EngageHostileTarget(rider, hostile);
			}
			// NOTE: Removed the "stop combat" logic here
			// If rider has no hostile target but is still in combat (e.g., vs companion),
			// let the game handle it naturally - don't force stop combat
		}
	}
	
	// ============================================
	// HORSE ANIMATION FUNCTIONS (from SingleMountedCombat)
	// ============================================
	
	// Use shared GetGameTime() from Helper.h instead of local function
	static float GetGameTimeSeconds()
	{
		return GetGameTime();
	}
	
	// ============================================
	// HORSE SPRINT TRACKING
	// ============================================
	
	static HorseSprintData* GetOrCreateSprintData(UInt32 horseFormID)
	{
		// Find existing
		for (int i = 0; i < g_horseSprintCount; i++)
		{
			if (g_horseSprintData[i].isValid && g_horseSprintData[i].horseFormID == horseFormID)
			{
				return &g_horseSprintData[i];
			}
		}
		
		// Create new
		if (g_horseSprintCount < 10)
		{
			HorseSprintData* data = &g_horseSprintData[g_horseSprintCount];
			data->horseFormID = horseFormID;
			data->lastSprintStartTime = -HORSE_SPRINT_COOLDOWN;  // Allow immediate first sprint
			data->isSprinting = false;
			data->isValid = true;
			g_horseSprintCount++;
			return data;
		}
		
		return nullptr;
	}
	
	bool IsHorseSprinting(Actor* horse)
	{
		if (!horse) return false;
		
		HorseSprintData* data = GetOrCreateSprintData(horse->formID);
		if (!data) return false;
		
		// Check if sprint has expired (after duration, consider it no longer sprinting)
		float currentTime = GetGameTimeSeconds();
		if (data->isSprinting && (currentTime - data->lastSprintStartTime) > HORSE_SPRINT_DURATION)
		{
			data->isSprinting = false;
		}
		
		return data->isSprinting;
	}
	
	// ============================================
	// SPRINT ANIMATION FUNCTIONS
	// ============================================

	static void InitSprintIdles()
	{
		if (g_sprintIdlesInitialized) return;
		
		// Silently load horse animations - only log errors
		TESForm* sprintStartForm = LookupFormByID(HORSE_SPRINT_START_FORMID);
		if (sprintStartForm)
		{
			g_horseSprintStart = DYNAMIC_CAST(sprintStartForm, TESForm, TESIdleForm);
		}
		else
		{
			_MESSAGE("MountedCombat: ERROR - Could not find HORSE_SPRINT_START");
			}
		
		TESForm* sprintStopForm = LookupFormByID(HORSE_SPRINT_STOP_FORMID);
		if (sprintStopForm)
		{
			g_horseSprintStop = DYNAMIC_CAST(sprintStopForm, TESForm, TESIdleForm);
		}
		else
		{
			_MESSAGE("MountedCombat: ERROR - Could not find HORSE_SPRINT_STOP");
		}
		
		TESForm* rearUpForm = LookupFormByID(HORSE_REAR_UP_FORMID);
		if (rearUpForm)
		{
			g_horseRearUp = DYNAMIC_CAST(rearUpForm, TESForm, TESIdleForm);
		}
		else
		{
			_MESSAGE("MountedCombat: ERROR - Could not find HORSE_REAR_UP");
		}
		
		g_sprintIdlesInitialized = true;
	}

	bool SendHorseAnimationEvent(Actor* horse, const char* eventName)
	{
		if (!horse) return false;
		
		BSFixedString event(eventName);
		
		typedef bool (*_IAnimationGraphManagerHolder_NotifyAnimationGraph)(IAnimationGraphManagerHolder* _this, const BSFixedString& eventName);
		return get_vfunc<_IAnimationGraphManagerHolder_NotifyAnimationGraph>(&horse->animGraphHolder, 0x1)(&horse->animGraphHolder, event);
	}
	
	void StartHorseSprint(Actor* horse)
	{
		if (!horse)
		{
			return;
		}
		
		HorseSprintData* sprintData = GetOrCreateSprintData(horse->formID);
		if (!sprintData) return;
		
		float currentTime = GetGameTimeSeconds();
		
		// Check if already sprinting
	 if (sprintData->isSprinting)
		{
			// Check if sprint is still valid (within duration)
			if ((currentTime - sprintData->lastSprintStartTime) < HORSE_SPRINT_DURATION)
			{
				// Still sprinting - don't spam the animation
				return;
			}
			// Sprint expired - allow refresh
		}
		
		// Check cooldown - prevent rapid sprint spam
		float timeSinceLastSprint = currentTime - sprintData->lastSprintStartTime;
		if (timeSinceLastSprint < HORSE_SPRINT_COOLDOWN)
		{
			// On cooldown - don't spam
			return;
		}
		
		InitSprintIdles();
		
		if (g_horseSprintStart)
		{
			const char* eventName = g_horseSprintStart->animationEvent.c_str();
			if (eventName && strlen(eventName) > 0)
			{
				if (SendHorseAnimationEvent(horse, eventName))
				{
					sprintData->isSprinting = true;
					sprintData->lastSprintStartTime = currentTime;
					_MESSAGE("MountedCombat: Horse %08X sprint STARTED", horse->formID);
				}
			}
		}
	}
	
	void StopHorseSprint(Actor* horse)
	{
		if (!horse) return;
		
		HorseSprintData* sprintData = GetOrCreateSprintData(horse->formID);
		if (sprintData)
		{
			sprintData->isSprinting = false;
		}
		
		InitSprintIdles();
		
		if (g_horseSprintStop)
		{
			const char* eventName = g_horseSprintStop->animationEvent.c_str();
			if (eventName && strlen(eventName) > 0)
			{
				SendHorseAnimationEvent(horse, eventName);
			}
		}
	}
	
	// ============================================
	// HORSE REAR UP ANIMATION
	// ============================================

	bool PlayHorseRearUpAnimation(Actor* horse)
	{
		if (!horse) return false;
		
		InitSprintIdles();
		
		if (g_horseRearUp)
		{
			const char* eventName = g_horseRearUp->animationEvent.c_str();
			if (eventName && strlen(eventName) > 0)
			{
				if (SendHorseAnimationEvent(horse, eventName))
				{
					return true;
				}
			}
		}
		
		return false;
	}
	
	// ============================================
	// RESET CACHED FORMS
	// ============================================

	void ResetSingleMountedCombatCache()
	{
		_MESSAGE("MountedCombat: Resetting cached horse animation forms...");
		
		g_horseSprintStart = nullptr;
		g_horseSprintStop = nullptr;
		g_horseRearUp = nullptr;
		g_sprintIdlesInitialized = false;
		
		g_horseJump = nullptr;
		g_jumpIdleInitialized = false;
		
		ResetArrowSystemCache();
		
		// Reset sprint tracking
		g_horseSprintCount = 0;
		for (int i = 0; i < 10; i++)
		{
			g_horseSprintData[i].isValid = false;
			g_horseSprintData[i].horseFormID = 0;
			g_horseSprintData[i].isSprinting = false;
		}
		
		g_horseJumpCount = 0;
		for (int i = 0; i < 5; i++)
		{
			g_horseJumpData[i].isValid = false;
			g_horseJumpData[i].horseFormID = 0;
		}
		
		g_singleCombatInitialized = false;
	}
	
	// ============================================
	// COMBAT TIME TRACKING
	// ============================================

	void InitSingleMountedCombat()
	{
		if (g_singleCombatInitialized) return;
		
		g_combatStartTime = GetGameTimeSeconds();
		InitSprintIdles();
		InitArrowSystem();
		g_singleCombatInitialized = true;
	}
	
	void NotifyCombatStarted()
	{
		g_combatStartTime = GetGameTimeSeconds();
	}
	
	float GetCombatElapsedTime()
	{
		return GetGameTimeSeconds() - g_combatStartTime;
	}
}
