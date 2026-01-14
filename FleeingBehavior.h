#pragma once

#include "skse64/GameReferences.h"

namespace MountedNPCCombatVR
{
	// ============================================
	// TACTICAL FLEE SYSTEM
	// ============================================
	// Mounted riders (excluding companions, captains, leaders, mages)
	// have a 15% chance every 5 seconds to tactically retreat
	// when below 30% health.
	// 
	// Flee lasts 4-10 seconds (randomized), then they return to combat.
	// Only ONE rider can flee at a time to prevent mass retreats.
	// ============================================
	
	// Initialize tactical flee system
	void InitTacticalFlee();
	
	// Shutdown tactical flee system
	void ShutdownTacticalFlee();
	
	// Reset all tactical flee state (on game load)
	void ResetTacticalFlee();
	
	// Start tactical flee for a rider
	// Returns true if flee was started, false if not eligible or another rider is fleeing
	bool StartTacticalFlee(Actor* rider, Actor* horse, Actor* target);
	
	// Stop tactical flee and return to combat
	void StopTacticalFlee(UInt32 riderFormID);
	
	// Check if rider should start fleeing (based on health, cooldown, and chance)
	// Returns true if flee was triggered
	bool CheckAndTriggerTacticalFlee(Actor* rider, Actor* horse, Actor* target);
	
	// Update tactical flee state - call every frame from main update loop
	void UpdateTacticalFlee();
	
	// Query functions
	bool IsRiderFleeing(UInt32 riderFormID);
	bool IsAnyRiderFleeing();
	UInt32 GetFleeingRiderFormID();
	float GetFleeTimeRemaining(UInt32 riderFormID);
	
	// Check if a horse's rider is fleeing
	bool IsHorseRiderFleeing(UInt32 horseFormID);

	// ============================================
	// Civilian Flee Behavior (Legacy Placeholder)
	// ============================================

	namespace CivilianFlee
	{
		// Initialize flee behavior system
		void InitFleeingBehavior();
		
		// Shutdown flee behavior system
		void ShutdownFleeingBehavior();
	}
}
