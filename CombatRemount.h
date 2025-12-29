#pragma once

#include "skse64/GameReferences.h"

namespace MountedNPCCombatVR
{
	// ============================================
	// COMBAT REMOUNT SYSTEM
	// ============================================
	// Handles remounting logic for NPCs who are dismounted
	// during combat. Applies to all faction members and classes
	// (Guards, Soldiers, Bandits, Hunters, Mages, Civilians)
	// when they are in a combat state.
	// ============================================

	// ============================================
	// Configuration
	// ============================================
	
	// Distance thresholds
	extern const float REMOUNT_MAX_DISTANCE;        // Max distance NPC will travel to remount
	extern const float REMOUNT_HORSE_SEARCH_RADIUS; // Radius to search for riderless horses
	
	// Timing
	extern const float REMOUNT_DELAY_AFTER_DISMOUNT; // Delay before attempting remount
	extern const float REMOUNT_ATTEMPT_INTERVAL;     // Time between remount attempts
	extern const float REMOUNT_TIMEOUT; // Give up after this long
	
	// ============================================
	// Initialization
	// ============================================
	
	void InitCombatRemountSystem();
	void ShutdownCombatRemountSystem();
	void ResetCombatRemountSystem();
	
	// ============================================
	// Core Functions
	// ============================================
	
	// Called when an NPC is dismounted during combat
	// Registers them for potential remounting
	void OnCombatDismount(Actor* npc, Actor* previousHorse);
	
	// Main update function - call from game loop
	// Checks all registered NPCs and attempts remounts
	void UpdateCombatRemounts();
	
	// ============================================
	// Query Functions
	// ============================================
	
	// Check if an NPC is registered for remounting
	bool IsNPCWaitingToRemount(UInt32 npcFormID);
	
	// Get the number of NPCs waiting to remount
	int GetRemountQueueCount();
	
	// ============================================
	// Utility Functions
	// ============================================
	
	// Find nearest riderless horse within search radius
	Actor* FindNearestRiderlessHorse(Actor* npc, float searchRadius);
	
	// Check if a horse is available for mounting
	bool IsHorseAvailableForMount(Actor* horse);
	
	// Attempt to mount the NPC on the specified horse
	bool AttemptRemount(Actor* npc, Actor* horse);
	
	// Cancel remount attempt for an NPC
	void CancelRemountAttempt(UInt32 npcFormID);
	
	// Clear all pending remount attempts
	void ClearAllRemountAttempts();
}
