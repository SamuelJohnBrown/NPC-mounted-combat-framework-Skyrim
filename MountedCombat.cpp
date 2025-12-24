#include "MountedCombat.h"
#include "CombatStyles.h"
#include "FleeingBehavior.h"
#include "FactionData.h"
#include "WeaponDetection.h"
#include "NPCProtection.h"
#include "AILogging.h"
#include "TargetSelection.h"

namespace MountedNPCCombatVR
{
	// ============================================
	// Configuration
	// ============================================
	
	float g_updateInterval = 0.5f;  // Update every 500ms
	const int MAX_TRACKED_NPCS = 5;  // Maximum 5 NPCs tracked at once
	const float FLEE_SAFE_DISTANCE = 8192.0f;  // Distance at which fleeing NPCs feel safe (2 cells)
	
	// ============================================
	// Player Mounted Combat State
	// ============================================
	
	bool g_playerInMountedCombat = false;
	bool g_playerTriggeredMountedCombat = false;
	bool g_playerWasMountedWhenCombatStarted = false;
	bool g_playerInExterior = true;  // Assume exterior until checked
	bool g_playerIsDead = false;     // Track player death state
	static bool g_lastPlayerMountedCombatState = false;  // For detecting state changes
	static bool g_lastExteriorState = true;  // For detecting cell changes
	static bool g_lastPlayerDeadState = false;  // For detecting death state changes
	
	// ============================================
	// Combat Class Global Bools
	// ============================================
	
	bool g_guardInMountedCombat = false;
	bool g_soldierInMountedCombat = false;
	bool g_banditInMountedCombat = false;
	bool g_hunterInMountedCombat = false;
	bool g_mageInMountedCombat = false;
	bool g_civilianFleeing = false;
	
	// ============================================
	// Internal State
	// ============================================
	
	static MountedNPCData g_trackedNPCs[MAX_TRACKED_NPCS];
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
		
		g_systemInitialized = true;
		_MESSAGE("MountedCombat: System initialized (max %d NPCs tracked)", MAX_TRACKED_NPCS);
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
		g_systemInitialized = false;
		
		_MESSAGE("MountedCombat: === SHUTDOWN COMPLETE ===");
	}
	
	void ResetAllMountedNPCs()
	{
		_MESSAGE("MountedCombat: Resetting all runtime state...");
		
		// Count how many NPCs we're clearing
		int clearedCount = 0;
		
		// Remove protection from all tracked NPCs before reset
		for (int i = 0; i < MAX_TRACKED_NPCS; i++)
		{
			if (g_trackedNPCs[i].isValid)
			{
				clearedCount++;
				TESForm* form = LookupFormByID(g_trackedNPCs[i].actorFormID);
				if (form)
				{
					Actor* actor = DYNAMIC_CAST(form, TESForm, Actor);
					if (actor)
					{
						RemoveMountedProtection(actor);
					}
				}
			}
			g_trackedNPCs[i].Reset();
		}
		
		if (clearedCount > 0)
		{
			_MESSAGE("MountedCombat: Cleared %d tracked NPCs", clearedCount);
		}
		
		// Clear any remaining protection tracking
		ClearAllMountedProtection();
		
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
		g_hunterInMountedCombat = false;
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
		g_hunterInMountedCombat = false;
		g_mageInMountedCombat = false;
		g_civilianFleeing = false;
		
		// Check each tracked NPC
		for (int i = 0; i < MAX_TRACKED_NPCS; i++)
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
				case MountedCombatClass::HunterRanged:
					g_hunterInMountedCombat = true;
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
		
		// Get or create tracking data for this NPC
		MountedNPCData* data = GetOrCreateNPCData(actor);
		if (!data)
		{
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
			
			const char* behaviorStr = (data->behavior == MountedBehaviorType::Aggressive) ? "AGGRESSIVE" : "PASSIVE";
			_MESSAGE("MountedCombat: NPC %08X behavior determined: %s", actor->formID, behaviorStr);
		}
		
		// Determine combat class based on faction
		if (data->combatClass == MountedCombatClass::None)
		{
			data->combatClass = DetermineCombatClass(actor);
			_MESSAGE("MountedCombat: NPC %08X combat class: %s", actor->formID, GetCombatClassName(data->combatClass));
		}
		
		// Track player's mount status when combat with mounted NPC starts
		OnPlayerTriggeredMountedCombat(actor);
		
		Actor* target = GetCombatTarget(actor);
		if (target)
		{
			data->targetFormID = target->formID;
		}
		
		// Set initial state based on combat class
		if (data->state == MountedCombatState::None)
		{
			// Record combat start time for weapon draw delay
			data->combatStartTime = GetCurrentGameTime();
			data->weaponDrawn = false;
			
			switch (data->combatClass)
			{
				case MountedCombatClass::CivilianFlee:
					data->state = MountedCombatState::Fleeing;
					break;
				case MountedCombatClass::HunterRanged:
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
		
		// Update combat class bools
		UpdateCombatClassBools();
	}
	
	void UpdateMountedCombat()
	{
		if (!g_systemInitialized)
		{
			return;
		}
		
		// Update the combat styles system (reinforcement of follow packages)
		UpdateCombatStylesSystem();
		
		// Update player mounted combat state
		UpdatePlayerMountedCombatState();
		
		// Update combat class bools
		UpdateCombatClassBools();
		
		float currentTime = GetCurrentGameTime();
		
		for (int i = 0; i < MAX_TRACKED_NPCS; i++)
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
				ClearNPCFollowPlayer(actor);
				data->Reset();
				continue;
			}
			
			// Check if still mounted
			NiPointer<Actor> mountPtr;
			bool stillMounted = CALL_MEMBER_FN(actor, GetMount)(mountPtr);
			if (!stillMounted || !mountPtr)
			{
				RemoveMountedProtection(actor);
				ClearNPCFollowPlayer(actor);
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
					
					// Re-apply the follow package to override the dialogue
					SetNPCFollowPlayer(actor);
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
				ClearNPCFollowPlayer(actor);
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
					
				case MountedCombatClass::HunterRanged:
					HunterCombat::ExecuteBehavior(data, actor, mountPtr, target);
					break;
					
				case MountedCombatClass::MageCaster:
					MageCombat::ExecuteBehavior(data, actor, mountPtr, target);
					break;
					
				case MountedCombatClass::CivilianFlee:
					CivilianFlee::ExecuteBehavior(data, actor, mountPtr, target);
					break;
					
				default:
					// Unknown class - do nothing, rely on vanilla AI
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
		for (int i = 0; i < MAX_TRACKED_NPCS; i++)
		{
			if (g_trackedNPCs[i].isValid && g_trackedNPCs[i].actorFormID == formID)
			{
				return &g_trackedNPCs[i];
			}
		}
		
		// Find empty slot
		for (int i = 0; i < MAX_TRACKED_NPCS; i++)
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
		for (int i = 0; i < MAX_TRACKED_NPCS; i++)
		{
			if (g_trackedNPCs[i].isValid && g_trackedNPCs[i].actorFormID == formID)
			{
				return &g_trackedNPCs[i];
			}
		}
		return nullptr;
	}
	
	void RemoveNPCFromTracking(UInt32 formID)
	{
		for (int i = 0; i < MAX_TRACKED_NPCS; i++)
		{
			if (g_trackedNPCs[i].isValid && g_trackedNPCs[i].actorFormID == formID)
			{
				// Remove mounted protection before resetting
				TESForm* form = LookupFormByID(formID);
				if (form)
				{
					Actor* actor = DYNAMIC_CAST(form, TESForm, Actor);
					if (actor)
					{
						RemoveMountedProtection(actor);
						
						// Clear follow mode if set
						ClearNPCFollowPlayer(actor);
					}
				}
				
				g_trackedNPCs[i].Reset();
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
		for (int i = 0; i < MAX_TRACKED_NPCS; i++)
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
		// Aggressive factions: Guards, Soldiers, Bandits, Hunters, Mages
		if (IsGuardFaction(actor) || IsSoldierFaction(actor) || 
			IsBanditFaction(actor) || IsHunterFaction(actor) || IsMageFaction(actor))
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
		
		if (!actor->IsInCombat())
		{
			return nullptr;
		}
		
		// Use the new TargetSelection system to get the actual combat target
		Actor* target = GetRiderCombatTarget(actor);
		
		if (target)
		{
			return target;
		}
		
		// Fallback: If no valid target found but in combat, 
		// check if player is nearby and hostile
		if (g_thePlayer && (*g_thePlayer))
		{
			Actor* player = *g_thePlayer;
			if (IsValidCombatTarget(actor, player))
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
				for (int i = 0; i < MAX_TRACKED_NPCS; i++)
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
				for (int i = 0; i < MAX_TRACKED_NPCS; i++)
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
		for (int i = 0; i < MAX_TRACKED_NPCS; i++)
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
}
