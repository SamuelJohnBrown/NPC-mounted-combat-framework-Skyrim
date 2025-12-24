#include "FleeingBehavior.h"

namespace MountedNPCCombatVR
{
	namespace CivilianFlee
	{
		// ============================================
		// Configuration - Flee Distances
		// ============================================
		
		const float FLEE_TRIGGER_RANGE = 2048.0f;   // Start fleeing if threat this close
		const float FLEE_SAFE_RANGE = 4096.0f;      // Safe distance to stop fleeing (1 cell)
		const float FLEE_GALLOP_SPEED = 1.5f;       // Speed multiplier for galloping
		
		// ============================================
		// Core Functions
		// ============================================
		
		MountedCombatState DetermineState(Actor* actor, Actor* mount, Actor* threat)
		{
			if (!actor || !mount)
			{
				return MountedCombatState::None;
			}
			
			// No threat - stop fleeing
			if (!threat)
			{
				return MountedCombatState::None;
			}
			
			// Check distance to threat
			if (IsAtSafeDistance(actor, threat))
			{
				return MountedCombatState::None;  // Safe, stop fleeing
			}
			
			return MountedCombatState::Fleeing;
		}
		
		void ExecuteBehavior(MountedNPCData* npcData, Actor* actor, Actor* mount, Actor* threat)
		{
			if (!npcData || !actor || !mount)
			{
				return;
			}
			
			// Determine optimal state
			MountedCombatState newState = DetermineState(actor, mount, threat);
			
			// State transition
			if (newState != npcData->state)
			{
				npcData->state = newState;
				npcData->stateStartTime = GetCurrentGameTime();
				
				if (newState == MountedCombatState::None)
				{
					_MESSAGE("CivilianFlee: NPC %08X reached safety, stopping flee", npcData->actorFormID);
				}
				else if (newState == MountedCombatState::Fleeing)
				{
					_MESSAGE("CivilianFlee: NPC %08X starting to flee", npcData->actorFormID);
				}
			}
			
			// Execute flee
			if (npcData->state == MountedCombatState::Fleeing)
			{
				ExecuteGallopFlee(actor, mount, threat);
			}
		}
		
		// ============================================
		// Flee Actions
		// ============================================
		
		void ExecuteGallopFlee(Actor* actor, Actor* mount, Actor* threat)
		{
			if (!actor || !mount || !threat)
			{
				return;
			}
			
			// Calculate flee direction
			NiPoint3 fleeDir = CalculateFleeDirection(actor, threat);
			
			// TODO: Implement actual flee movement
			// For now, rely on vanilla flee AI
			// 
			// Future implementation:
			// 1. Set mount to sprint/gallop state
			// 2. Set movement direction away from threat
			// 3. Possibly use AIPackage or direct movement commands
			// 
			// Possible approaches:
			// - Use Actor::SetPosition to move (jerky)
			// - Use MovementController to set destination
			// - Create and apply a flee AI package
			// - Hook into movement system
		}
		
		void ExecuteRetreat(Actor* actor, Actor* mount, Actor* threat)
		{
			if (!actor || !mount || !threat)
			{
				return;
			}
			
			// TODO: Implement slower retreat behavior
			// - Move away from threat at walking pace
			// - Periodically look back at threat
			// - Used when threat is not actively pursuing
		}
		
		// ============================================
		// Utility Functions
		// ============================================
		
		NiPoint3 CalculateFleeDirection(Actor* actor, Actor* threat)
		{
			NiPoint3 fleeDir;
			fleeDir.x = 0;
			fleeDir.y = 0;
			fleeDir.z = 0;
			
			if (!actor || !threat)
			{
				return fleeDir;
			}
			
			// Direction away from threat (opposite of threat direction)
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
		
		bool IsAtSafeDistance(Actor* actor, Actor* threat)
		{
			if (!actor || !threat)
			{
				return true;  // No threat = safe
			}
			
			float distance = GetDistanceBetween(actor, threat);
			return distance >= FLEE_SAFE_RANGE;
		}
		
		bool IsThreatPursuing(Actor* actor, Actor* threat)
		{
			if (!actor || !threat)
			{
				return false;
			}
			
			// Check if threat is in combat and targeting us
			if (!threat->IsInCombat())
			{
				return false;
			}
			
			// TODO: Check if threat's combat target is this actor
			// For now, assume if threat is in combat and close, they're pursuing
			float distance = GetDistanceBetween(actor, threat);
			return distance < FLEE_TRIGGER_RANGE;
		}
		
		NiPoint3 GetNearestSafePath(Actor* actor, Actor* threat)
		{
			// TODO: Implement pathfinding to find safe route
			// For now, just return flee direction
			NiPoint3 fleeDir = CalculateFleeDirection(actor, threat);
			
			// Project a point in the flee direction
			NiPoint3 safePath;
			safePath.x = actor->pos.x + (fleeDir.x * FLEE_SAFE_RANGE);
			safePath.y = actor->pos.y + (fleeDir.y * FLEE_SAFE_RANGE);
			safePath.z = actor->pos.z;
			
			return safePath;
		}
	}
}
