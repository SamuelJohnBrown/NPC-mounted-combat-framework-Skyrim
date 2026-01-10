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

namespace MountedNPCCombatVR
{
	// ============================================
	// Configuration
	// ============================================
	
	float g_updateInterval = 0.5f;  // Update every 500ms
	const int MAX_TRACKED_NPCS_ARRAY = 10;  // Maximum array size (hardcoded for memory safety)
	// Actual runtime limit is MaxTrackedMountedNPCs from config (1-10)
	const float FLEE_SAFE_DISTANCE = 4500.0f;  // Distance at which fleeing NPCs feel safe (just over 1 cell)
	const float ALLY_ALERT_RANGE = 2000.0f;    // Range to alert allies when attacked
	
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
		
		// Initialize companion combat system
		InitCompanionCombat();
		
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
		
		// Reset companion combat tracking
		ResetCompanionCombat();
		
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
		
		// Log compact summary
		_MESSAGE("MountedCombat: Detected '%s' (%08X) on horse %08X - Class: %s", 
			actorName ? actorName : "Unknown", actor->formID, mount->formID,
			GetCombatClassName(data->combatClass));
		
		// ============================================
		// PRE-ASSIGN CAPTAIN TO RANGED ROLE
		// ============================================
		if (actorName && strstr(actorName, "Captain") != nullptr)
		{
			if (!HasBowInInventory(actor))
			{
				GiveDefaultBow(actor);
			}
			EquipArrows(actor);
			EquipBestBow(actor);
			actor->DrawSheatheWeapon(true);
			
			_MESSAGE("MountedCombat: Captain '%s' pre-assigned to RANGED", actorName);
			
			Actor* target = nullptr;
			if (g_thePlayer && (*g_thePlayer))
			{
				target = *g_thePlayer;
			}
			
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
			// For guards/soldiers without a target, default to player if player is in combat nearby
			if (data->combatClass == MountedCombatClass::GuardMelee ||
				data->combatClass == MountedCombatClass::SoldierMelee)
			{
				if (g_thePlayer && (*g_thePlayer))
				{
					Actor* player = *g_thePlayer;
					if (player->IsInCombat())
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
		
		// Update mounted companion combat (player teammates on horseback)
		UpdateMountedCompanionCombat();
		
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
			// CHECK FOR DIALOGUE/CRIME PACKAGE OVERRIDE
			// This detects when a guard enters crime dialogue
			// and clears it to restore combat behavior
			// ============================================
			if (DetectDialoguePackageIssue(actor))
			{
				const char* actorName = CALL_MEMBER_FN(actor, GetReferenceName)();
				_MESSAGE("MountedCombat: NPC '%s' (%08X) has dialogue package - CLEARING IT!", 
					actorName ? actorName : "Unknown", data->actorFormID);
				
				// Clear the dialogue package and restore follow behavior
				if (ClearDialoguePackageAndRestoreFollow(actor))
				{
					_MESSAGE("MountedCombat: Dialogue package cleared - re-applying follow package");
					
					// Re-apply the follow package using the stored target
					if (data->targetFormID != 0 && data->targetFormID != 0x14)  // Has target and not player
					{
						TESForm* targetForm = LookupFormByID(data->targetFormID);
						if (targetForm && targetForm->formType == kFormType_Character)
						{
							Actor* storedTarget = static_cast<Actor*>(targetForm);
							if (!storedTarget->IsDead(1))
							{
								SetNPCFollowTarget(actor, storedTarget);
							}
						}
					}
					else
					{
						_MESSAGE("MountedCombat: No valid target stored - skipping follow package re-apply");
					}
				}
				
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
						
						// Clear follow mode on the rider
						ClearNPCFollowTarget(actor);
					}
				}
				
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
							
							// Re-evaluate the horse's AI packages so it returns to normal behavior
							Actor_EvaluatePackage(mount, false, false);
						}
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
			// FOURTH: For guards with NO hostiles found, check if player is in combat
			// If player is attacking allies, guards should join in
			// ============================================
			if (g_thePlayer && (*g_thePlayer))
			{
				Actor* player = *g_thePlayer;
				if (player->IsInCombat())
				{
					float distance = GetDistanceBetween(actor, player);
					if (distance < ALLY_ALERT_RANGE)
					{
						// Player is in combat nearby - they're a valid target
						data->targetFormID = player->formID;
						_MESSAGE("GetCombatTarget: Guard %08X targeting player (in combat nearby, %.0f units)", 
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
				
				// Reset all state
				g_playerInMountedCombat = false;
				g_playerTriggeredMountedCombat = false;
				g_playerWasMountedWhenCombatStarted = false;
				g_playerInExterior = true;  // Assume exterior until checked
				g_playerIsDead = false;     // Assume alive until checked
				g_lastPlayerMountedCombatState = false;
				g_lastExteriorState = true;
				g_lastPlayerDeadState = false;
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
	// Scans for hostile NPCs from the FactionData list
	// within range and initiates combat if found.
	// Called periodically to check for nearby threats.
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
}
