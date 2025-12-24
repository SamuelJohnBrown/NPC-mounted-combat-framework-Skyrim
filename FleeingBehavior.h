#pragma once

#include "MountedCombat.h"

namespace MountedNPCCombatVR
{
	// ============================================
	// Civilian Flee Behavior
	// ============================================
	// Handles all flee logic for passive/civilian NPCs
	// who are mounted and want to escape from threats.
	// ============================================

	namespace CivilianFlee
	{
		// ============================================
		// Configuration - Flee Distances
		// ============================================
		
		extern const float FLEE_TRIGGER_RANGE;      // Distance at which to start fleeing
		extern const float FLEE_SAFE_RANGE;         // Distance considered safe to stop
		extern const float FLEE_GALLOP_SPEED;   // Speed multiplier for galloping
		
		// ============================================
		// Core Functions
		// ============================================
		
		// Determine optimal flee state
		MountedCombatState DetermineState(Actor* actor, Actor* mount, Actor* threat);
		
		// Execute flee behavior - main entry point
		void ExecuteBehavior(MountedNPCData* npcData, Actor* actor, Actor* mount, Actor* threat);
		
		// ============================================
		// Flee Actions
		// ============================================
		
		// Execute full gallop flee away from threat
		void ExecuteGallopFlee(Actor* actor, Actor* mount, Actor* threat);
		
		// Execute slower retreat (looking back)
		void ExecuteRetreat(Actor* actor, Actor* mount, Actor* threat);
		
		// ============================================
		// Utility Functions
		// ============================================
		
		// Calculate direction away from threat
		NiPoint3 CalculateFleeDirection(Actor* actor, Actor* threat);
		
		// Check if NPC has reached safe distance
		bool IsAtSafeDistance(Actor* actor, Actor* threat);
		
		// Check if threat is still pursuing
		bool IsThreatPursuing(Actor* actor, Actor* threat);
		
		// Get nearest safe location/path
		NiPoint3 GetNearestSafePath(Actor* actor, Actor* threat);
	}
}
