#pragma once

#include "skse64/GameReferences.h"

namespace MountedNPCCombatVR
{
	// ============================================
	// HORSE STABILIZATION SYSTEM
	// ============================================
	// Prevents horses from falling through the ground on cell/game load
	// by stabilizing their position after physics initialization
	
	// Initialize the stabilization system
	void InitHorseStabilization();
	
	// Called after game load to stabilize all horses in loaded cells
	void StabilizeAllHorses();
	
	// Check for cell change and trigger stabilization if needed
	// Call this from update loop - very cheap when no cell change
	void CheckCellChangeForStabilization();
	
	// Stabilize a specific horse's position
	// Returns true if stabilization was applied
	bool StabilizeHorse(Actor* horse);
	
	// Check if an actor is a horse
	bool IsHorse(Actor* actor);
	
	// Register a horse for delayed stabilization (called on cell load)
	void RegisterHorseForStabilization(Actor* horse);
	
	// Process pending stabilizations (call from update loop)
	void ProcessPendingStabilizations();
	
	// Clear all pending stabilizations
	void ClearPendingStabilizations();
	
	// Enable/disable the stabilization system
	void SetStabilizationEnabled(bool enabled);
	bool IsStabilizationEnabled();
}
