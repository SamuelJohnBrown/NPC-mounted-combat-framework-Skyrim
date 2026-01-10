#pragma once

#include "skse64/GameReferences.h"

namespace MountedNPCCombatVR
{
	// ============================================
	// HORSE MOUNT SCANNER
	// Tracks dismounted NPCs in combat and available horses
	// for potential remount AI.
	// 
	// Poll-based system - called from UpdateMountedCombat()
	// ============================================
	
	// Initialize the scanner system (call on game load)
	void InitHorseMountScanner();
	
	// Shutdown the scanner system
	void ShutdownHorseMountScanner();
	
	// Stop the scanner (call before game load)
	void StopHorseMountScanner();
	
	// Reset scanner state (call after game load completes)
	void ResetHorseMountScanner();
	
	// Main update function - called from UpdateMountedCombat()
	// Returns true if scanner is active
	bool UpdateHorseMountScanner();
	
	// Check if scanner is currently active (player in outdoor combat)
	bool IsScannerActive();
	
	// Get the last scan results count
	int GetLastScanHorseCount();
	
	// ============================================
	// DISMOUNTED NPC TRACKING
	// ============================================
	
	// Called when an NPC dismounts (from MountedCombat or SpecialDismount)
	// Registers both the NPC and their horse for tracking
	void OnNPCDismounted(UInt32 npcFormID, UInt32 horseFormID);
	
	// Register an unmounted aggressive NPC for tracking
	void RegisterDismountedNPC(UInt32 npcFormID, UInt32 horseFormID);
	
	// Register an available (riderless) horse for tracking
	void RegisterAvailableHorse(UInt32 horseFormID);
	
	// Remove NPC from tracking (remounted, died, left combat)
	void UnregisterDismountedNPC(UInt32 npcFormID);
	
	// Remove horse from tracking (got rider, died)
	void UnregisterAvailableHorse(UInt32 horseFormID);
	
	// Clear all tracking data
	void ClearAllDismountedTracking();
	
	// Legacy - no longer needed (poll-based now)
	void InstallCombatStateHook();
}
