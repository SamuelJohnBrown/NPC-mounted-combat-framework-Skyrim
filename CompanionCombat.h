#pragma once

#include "skse64/GameReferences.h"
#include "skse64/NiTypes.h"

namespace MountedNPCCombatVR
{
	// ============================================
	// MOUNTED COMPANION COMBAT SYSTEM
	// ============================================
	// Detects and manages mounted player teammates/companions
	// to ensure they get full combat behavior and target
	// hostiles aggressive to the player.
	// ============================================

	// ============================================
	// INITIALIZATION
	// ============================================
	
	void InitCompanionCombat();
	void ShutdownCompanionCombat();
	void ResetCompanionCombat();
	
	// ============================================
	// COMPANION DETECTION
	// ============================================
	
	// Check if an actor is a player teammate using game engine flag
	// Uses Actor::flags1 & kFlags_IsPlayerTeammate
	bool IsPlayerTeammate(Actor* actor);
	
	// Check if an actor should be treated as a companion
	// Uses both vanilla teammate flag AND configurable name list
	bool IsCompanion(Actor* actor);
	
	// Check if an actor is a mounted companion (teammate on horseback)
	bool IsMountedCompanion(Actor* actor);
	
	// Get the companion's current mount (if any)
	Actor* GetCompanionMount(Actor* companion);
	
	// ============================================
	// COMPANION TRACKING
	// ============================================
	
	// Maximum tracked companions
	const int MAX_TRACKED_COMPANIONS = 5;
	
	// Companion tracking data
	struct MountedCompanionData
	{
		UInt32 companionFormID;
		UInt32 mountFormID;
		UInt32 targetFormID;
		float lastUpdateTime;
		float combatStartTime;
		bool weaponDrawn;
		bool isValid;
		
		void Reset()
		{
			companionFormID = 0;
			mountFormID = 0;
			targetFormID = 0;
			lastUpdateTime = 0;
			combatStartTime = 0;
			weaponDrawn = false;
			isValid = false;
		}
	};
	
	// Register a mounted companion for tracking
	MountedCompanionData* RegisterMountedCompanion(Actor* companion, Actor* mount);
	
	// Unregister a companion (dismounted, died, etc.)
	void UnregisterMountedCompanion(UInt32 companionFormID);
	
	// Get tracking data for a companion
	MountedCompanionData* GetCompanionData(UInt32 companionFormID);
	
	// Get count of currently tracked mounted companions
	int GetMountedCompanionCount();
	
	// ============================================
	// COMPANION TARGET VALIDATION (Friendly Fire Prevention)
	// ============================================
	
	// Check if an actor is a valid combat target for a companion
	// Returns FALSE if target is: player, other companion, companion mount, or dead
	// NOTE: This is for companion-to-companion friendly fire prevention only.
	// Guards CAN still target companions if they become hostile.
	bool IsValidCompanionTarget(Actor* companion, Actor* potentialTarget);
	
	// ============================================
	// COMPANION COMBAT UPDATE
	// ============================================
	
	// Main update function - call from UpdateMountedCombat()
	// Monitors companion state (death, dismount) for cleanup
	void UpdateMountedCompanionCombat();
	
	// ============================================
	// LOGGING
	// ============================================
	
	// Log companion detection info
	void LogCompanionDetection(Actor* companion, Actor* mount);
	void LogCompanionCombatState(Actor* companion, const char* state);
	
	// Log all spells available to a companion
	void LogCompanionSpells(Actor* companion);
}
