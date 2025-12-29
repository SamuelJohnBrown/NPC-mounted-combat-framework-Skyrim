#include "CombatRemount.h"
#include "FactionData.h"
#include "MountedCombat.h"
#include "skse64/GameRTTI.h"

namespace MountedNPCCombatVR
{
	// ============================================
	// COMBAT REMOUNT SYSTEM
	// ============================================
	// Handles remounting logic for dismounted NPCs in combat.
	// Applies to all faction types when in combat state.
	// ============================================

	// ============================================
	// Configuration
	// ============================================
	
	const float REMOUNT_MAX_DISTANCE = 1500.0f;  // Max distance NPC will travel to remount
	const float REMOUNT_HORSE_SEARCH_RADIUS = 2000.0f;  // Radius to search for riderless horses
	const float REMOUNT_DELAY_AFTER_DISMOUNT = 2.0f;    // 2 second delay before attempting remount
	const float REMOUNT_ATTEMPT_INTERVAL = 1.0f;      // Try every 1 second
	const float REMOUNT_TIMEOUT = 30.0f;                // Give up after 30 seconds
	
	// ============================================
	// System State
	// ============================================
	
	static bool g_remountSystemInitialized = false;
	
	const int MAX_REMOUNT_QUEUE = 5;
	
	struct RemountData
	{
		UInt32 npcFormID;
		UInt32 previousHorseFormID;
		float dismountTime;
		float lastAttemptTime;
		bool isValid;
		
		void Reset()
		{
			npcFormID = 0;
			previousHorseFormID = 0;
			dismountTime = 0;
			lastAttemptTime = 0;
			isValid = false;
		}
	};
	
	static RemountData g_remountQueue[MAX_REMOUNT_QUEUE];
	static int g_remountQueueCount = 0;
	
	// ============================================
	// Utility - Get Current Time
	// ============================================
	
	static float GetRemountTime()
	{
		static auto startTime = clock();
		return (float)(clock() - startTime) / CLOCKS_PER_SEC;
	}
	
	// ============================================
	// Initialization
	// ============================================
	
	void InitCombatRemountSystem()
	{
		if (g_remountSystemInitialized) return;
		
		_MESSAGE("CombatRemount: Initializing combat remount system...");
		
		for (int i = 0; i < MAX_REMOUNT_QUEUE; i++)
		{
			g_remountQueue[i].Reset();
		}
		g_remountQueueCount = 0;
		
		g_remountSystemInitialized = true;
		_MESSAGE("CombatRemount: System initialized (max queue: %d)", MAX_REMOUNT_QUEUE);
	}
	
	void ShutdownCombatRemountSystem()
	{
		if (!g_remountSystemInitialized) return;
		
		_MESSAGE("CombatRemount: Shutting down...");
		ClearAllRemountAttempts();
		g_remountSystemInitialized = false;
	}
	
	void ResetCombatRemountSystem()
	{
		_MESSAGE("CombatRemount: Resetting all state...");
		ClearAllRemountAttempts();
		g_remountSystemInitialized = false;
	}
	
	// ============================================
	// Core Functions
	// ============================================
	
	void OnCombatDismount(Actor* npc, Actor* previousHorse)
	{
		if (!npc) return;
		if (!g_remountSystemInitialized) return;
		
		// Check if NPC is in combat
		if (!npc->IsInCombat())
		{
			return;  // Not in combat, don't track for remount
		}
		
		// Check if NPC is a valid faction member we care about
		bool isValidFaction = IsGuardFaction(npc) || 
		     IsSoldierFaction(npc) || 
		     IsBanditFaction(npc) || 
		       IsHunterFaction(npc) || 
		             IsMageFaction(npc) ||
		    IsCivilianFaction(npc);
		
		if (!isValidFaction)
		{
			return;  // Not a faction we handle
		}
		
		const char* npcName = CALL_MEMBER_FN(npc, GetReferenceName)();
		_MESSAGE("CombatRemount: NPC '%s' (%08X) dismounted during combat - registering for remount",
			npcName ? npcName : "Unknown", npc->formID);
		
		// Check if already in queue
		for (int i = 0; i < MAX_REMOUNT_QUEUE; i++)
		{
			if (g_remountQueue[i].isValid && g_remountQueue[i].npcFormID == npc->formID)
			{
				_MESSAGE("CombatRemount: NPC already in remount queue");
				return;
			}
		}
		
		// Find empty slot
		for (int i = 0; i < MAX_REMOUNT_QUEUE; i++)
		{
			if (!g_remountQueue[i].isValid)
			{
				g_remountQueue[i].npcFormID = npc->formID;
				g_remountQueue[i].previousHorseFormID = previousHorse ? previousHorse->formID : 0;
				g_remountQueue[i].dismountTime = GetRemountTime();
				g_remountQueue[i].lastAttemptTime = 0;
				g_remountQueue[i].isValid = true;
				g_remountQueueCount++;
				
				_MESSAGE("CombatRemount: Added to queue (slot %d, queue size: %d)", i, g_remountQueueCount);
				return;
			}
		}
		
		_MESSAGE("CombatRemount: WARNING - Remount queue full, cannot track NPC");
	}
	
	void UpdateCombatRemounts()
	{
		if (!g_remountSystemInitialized) return;
		if (g_remountQueueCount == 0) return;
		
		float currentTime = GetRemountTime();
		
		for (int i = 0; i < MAX_REMOUNT_QUEUE; i++)
		{
			RemountData* data = &g_remountQueue[i];
			if (!data->isValid) continue;
			
			// Look up NPC
			TESForm* npcForm = LookupFormByID(data->npcFormID);
			if (!npcForm)
			{
				data->Reset();
				g_remountQueueCount--;
				continue;
			}
			
			Actor* npc = DYNAMIC_CAST(npcForm, TESForm, Actor);
			if (!npc)
			{
				data->Reset();
				g_remountQueueCount--;
				continue;
			}
			
			// Check if NPC died
			if (npc->IsDead(1))
			{
				const char* npcName = CALL_MEMBER_FN(npc, GetReferenceName)();
				_MESSAGE("CombatRemount: NPC '%s' died - removing from queue",
					npcName ? npcName : "Unknown");
				data->Reset();
				g_remountQueueCount--;
				continue;
			}
			
			// Check if NPC already remounted (by other means)
			NiPointer<Actor> currentMount;
			if (CALL_MEMBER_FN(npc, GetMount)(currentMount) && currentMount)
			{
				const char* npcName = CALL_MEMBER_FN(npc, GetReferenceName)();
				_MESSAGE("CombatRemount: NPC '%s' already remounted - removing from queue",
					npcName ? npcName : "Unknown");
				data->Reset();
				g_remountQueueCount--;
				continue;
			}
			
			// Check if NPC exited combat
			if (!npc->IsInCombat())
			{
				const char* npcName = CALL_MEMBER_FN(npc, GetReferenceName)();
				_MESSAGE("CombatRemount: NPC '%s' exited combat - removing from queue",
					npcName ? npcName : "Unknown");
				data->Reset();
				g_remountQueueCount--;
				continue;
			}
			
			// Check for timeout
			float timeSinceDismount = currentTime - data->dismountTime;
			if (timeSinceDismount > REMOUNT_TIMEOUT)
			{
				const char* npcName = CALL_MEMBER_FN(npc, GetReferenceName)();
				_MESSAGE("CombatRemount: NPC '%s' remount TIMEOUT (%.1f seconds) - removing from queue",
					npcName ? npcName : "Unknown", timeSinceDismount);
				data->Reset();
				g_remountQueueCount--;
				continue;
			}
			
			// Check delay after dismount
			if (timeSinceDismount < REMOUNT_DELAY_AFTER_DISMOUNT)
			{
				continue;  // Still waiting for delay
			}
			
			// Check attempt interval
			float timeSinceLastAttempt = currentTime - data->lastAttemptTime;
			if (data->lastAttemptTime > 0 && timeSinceLastAttempt < REMOUNT_ATTEMPT_INTERVAL)
			{
				continue;  // Not time to try again yet
			}
			
			data->lastAttemptTime = currentTime;
			
			// ============================================
			// ATTEMPT REMOUNT
			// ============================================
			
			// TODO: Implement remount logic
			// 1. Find nearest riderless horse
			// 2. Move NPC toward horse
			// 3. Trigger mount action
			
			// For now, just log that we would attempt
			const char* npcName = CALL_MEMBER_FN(npc, GetReferenceName)();
			_MESSAGE("CombatRemount: Would attempt remount for '%s' (time since dismount: %.1f)",
				npcName ? npcName : "Unknown", timeSinceDismount);
		}
	}
	
	// ============================================
	// Query Functions
	// ============================================
	
	bool IsNPCWaitingToRemount(UInt32 npcFormID)
	{
		for (int i = 0; i < MAX_REMOUNT_QUEUE; i++)
		{
			if (g_remountQueue[i].isValid && g_remountQueue[i].npcFormID == npcFormID)
			{
				return true;
			}
		}
		return false;
	}
	
	int GetRemountQueueCount()
	{
		return g_remountQueueCount;
	}
	
	// ============================================
	// Utility Functions
	// ============================================
	
	Actor* FindNearestRiderlessHorse(Actor* npc, float searchRadius)
	{
		// TODO: Implement horse search
		// 1. Get NPC's cell
		// 2. Iterate through actors in cell
		// 3. Find horses without riders
		// 4. Return nearest within searchRadius
		
		return nullptr;
	}
	
	bool IsHorseAvailableForMount(Actor* horse)
	{
		if (!horse) return false;
		
		// Check if horse is alive
		if (horse->IsDead(1)) return false;
		
		// Check if horse already has a rider
		NiPointer<Actor> rider;
		if (CALL_MEMBER_FN(horse, GetMountedBy)(rider) && rider)
		{
			return false;  // Already has rider
		}
		
		// TODO: Add more checks
		// - Is horse hostile?
		// - Is horse owned by enemy faction?
		// - Is horse fleeing?
		
		return true;
	}
	
	bool AttemptRemount(Actor* npc, Actor* horse)
	{
		if (!npc || !horse) return false;
		
		// TODO: Implement mount action
		// 1. Check distance to horse
		// 2. If close enough, trigger mount
		// 3. If not close enough, move NPC toward horse
		
		return false;
	}
	
	void CancelRemountAttempt(UInt32 npcFormID)
	{
		for (int i = 0; i < MAX_REMOUNT_QUEUE; i++)
		{
			if (g_remountQueue[i].isValid && g_remountQueue[i].npcFormID == npcFormID)
			{
				_MESSAGE("CombatRemount: Cancelling remount attempt for %08X", npcFormID);
				g_remountQueue[i].Reset();
				g_remountQueueCount--;
				return;
			}
		}
	}
	
	void ClearAllRemountAttempts()
	{
		_MESSAGE("CombatRemount: Clearing all %d pending remount attempts", g_remountQueueCount);
		
		for (int i = 0; i < MAX_REMOUNT_QUEUE; i++)
		{
			g_remountQueue[i].Reset();
		}
		g_remountQueueCount = 0;
	}
}
