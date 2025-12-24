#include "MultiMountedCombat.h"
#include "CombatStyles.h"
#include "WeaponDetection.h"
#include "DynamicPackages.h"
#include "skse64/GameRTTI.h"
#include "skse64/GameData.h"
#include <cmath>
#include <cstdlib>
#include <ctime>

namespace MountedNPCCombatVR
{
	// ============================================
	// CONFIGURATION
	// ============================================
	
	const float FORMATION_RADIUS = 400.0f;         // Distance from player for formation
	const float ATTACK_COORDINATION_DELAY = 2.0f;    // Seconds between coordinated attacks
	const float REGROUP_DISTANCE = 600.0f;  // Distance to regroup after attack
	
	// ============================================
	// SYSTEM STATE
	// ============================================
	
	static bool g_multiCombatInitialized = false;
	static FormationType g_currentFormation = FormationType::None;
	static MultiCombatState g_globalState = MultiCombatState::None;
	static float g_lastCoordinatedAttackTime = 0;
	
	// Rider tracking
	static MultiRiderData g_multiRiders[5];
	static int g_multiRiderCount = 0;
	
	// ============================================
	// UTILITY FUNCTIONS
	// ============================================
	
	static float GetGameTimeSeconds()
	{
		static auto startTime = clock();
		return (float)(clock() - startTime) / CLOCKS_PER_SEC;
	}
	
	// ============================================
	// INITIALIZATION
	// ============================================
	
	void InitMultiMountedCombat()
	{
		if (g_multiCombatInitialized) return;
		
		_MESSAGE("MultiMountedCombat: Initializing...");
		
		// Clear rider data
		for (int i = 0; i < 5; i++)
		{
			g_multiRiders[i].isValid = false;
		}
		g_multiRiderCount = 0;
		g_currentFormation = FormationType::None;
		g_globalState = MultiCombatState::None;
		
		g_multiCombatInitialized = true;
		_MESSAGE("MultiMountedCombat: Initialized successfully");
	}
	
	// ============================================
	// RIDER MANAGEMENT
	// ============================================
	
	void RegisterRider(Actor* horse, Actor* rider)
	{
		if (!horse || !rider) return;
		
		// Check if already registered
		for (int i = 0; i < g_multiRiderCount; i++)
		{
			if (g_multiRiders[i].isValid && g_multiRiders[i].horseFormID == horse->formID)
			{
				return;  // Already registered
			}
		}
		
		// Add new rider
		if (g_multiRiderCount < 5)
		{
			MultiRiderData* data = &g_multiRiders[g_multiRiderCount];
			data->horseFormID = horse->formID;
			data->riderFormID = rider->formID;
			data->assignedPosition = g_multiRiderCount;
			data->angleOffset = 0;
			data->state = MultiCombatState::None;
			data->stateStartTime = GetGameTimeSeconds();
			data->isValid = true;
			g_multiRiderCount++;
			
			_MESSAGE("MultiMountedCombat: Registered rider %08X on horse %08X (total: %d)",
				rider->formID, horse->formID, g_multiRiderCount);
			
			// Reassign formation positions
			AssignFormationPositions();
		}
	}
	
	void UnregisterRider(UInt32 horseFormID)
	{
		for (int i = 0; i < g_multiRiderCount; i++)
		{
			if (g_multiRiders[i].isValid && g_multiRiders[i].horseFormID == horseFormID)
			{
				_MESSAGE("MultiMountedCombat: Unregistering horse %08X", horseFormID);
				
				// Shift remaining entries
				for (int j = i; j < g_multiRiderCount - 1; j++)
				{
					g_multiRiders[j] = g_multiRiders[j + 1];
				}
				g_multiRiderCount--;
				
				// Reassign formation positions
				AssignFormationPositions();
				return;
			}
		}
	}
	
	void ClearAllMultiRiders()
	{
		_MESSAGE("MultiMountedCombat: Clearing all riders");
		for (int i = 0; i < 5; i++)
		{
			g_multiRiders[i].isValid = false;
		}
		g_multiRiderCount = 0;
		g_currentFormation = FormationType::None;
		g_globalState = MultiCombatState::None;
	}
	
	int GetActiveMultiRiderCount()
	{
		return g_multiRiderCount;
	}
	
	// ============================================
	// FORMATION FUNCTIONS
	// ============================================
	
	FormationType DetermineFormation(int riderCount)
	{
		if (riderCount <= 1) return FormationType::None;
		if (riderCount == 2) return FormationType::Flank;
		if (riderCount == 3) return FormationType::Surround;
		return FormationType::Staggered;  // 4+ riders
	}
	
	void AssignFormationPositions()
	{
		if (g_multiRiderCount <= 1)
		{
			g_currentFormation = FormationType::None;
			return;
		}
		
		g_currentFormation = DetermineFormation(g_multiRiderCount);
		
		// Assign angle offsets based on formation
		float angleStep = 6.28318f / (float)g_multiRiderCount;  // 2*PI / count
		
		for (int i = 0; i < g_multiRiderCount; i++)
		{
			if (g_multiRiders[i].isValid)
			{
				g_multiRiders[i].assignedPosition = i;
				g_multiRiders[i].angleOffset = angleStep * (float)i;
				
				_MESSAGE("MultiMountedCombat: Rider %d assigned angle %.2f rad",
					i, g_multiRiders[i].angleOffset);
			}
		}
		
		_MESSAGE("MultiMountedCombat: Formation set to %d with %d riders",
			(int)g_currentFormation, g_multiRiderCount);
	}
	
	NiPoint3 GetFormationPosition(Actor* player, int positionIndex, int totalRiders, FormationType formation)
	{
		if (!player) return NiPoint3();
		
		NiPoint3 position;
		float angleStep = 6.28318f / (float)totalRiders;
		float angle = angleStep * (float)positionIndex;
		
		switch (formation)
		{
			case FormationType::Flank:
				// Two riders on opposite sides
				angle = (positionIndex == 0) ? 1.5708f : -1.5708f;  // +/- 90 degrees
				break;
				
			case FormationType::Surround:
				// Evenly spaced around player
				// Already calculated above
				break;
				
			case FormationType::Staggered:
				// Alternating close/far positions
				if (positionIndex % 2 == 1)
				{
					// Odd positions are further out
					position.x = player->pos.x + (FORMATION_RADIUS * 1.5f * sin(angle));
					position.y = player->pos.y + (FORMATION_RADIUS * 1.5f * cos(angle));
					position.z = player->pos.z;
					return position;
				}
				break;
				
			default:
				break;
		}
		
		position.x = player->pos.x + (FORMATION_RADIUS * sin(angle));
		position.y = player->pos.y + (FORMATION_RADIUS * cos(angle));
		position.z = player->pos.z;
		
		return position;
	}
	
	// ============================================
	// COORDINATION FUNCTIONS
	// ============================================
	
	bool ShouldCoordinateAttack()
	{
		float currentTime = GetGameTimeSeconds();
		return (currentTime - g_lastCoordinatedAttackTime) >= ATTACK_COORDINATION_DELAY;
	}
	
	void StartCoordinatedAttack()
	{
		_MESSAGE("MultiMountedCombat: Starting coordinated attack with %d riders", g_multiRiderCount);
		
		g_globalState = MultiCombatState::Attacking;
		g_lastCoordinatedAttackTime = GetGameTimeSeconds();
		
		for (int i = 0; i < g_multiRiderCount; i++)
		{
			if (g_multiRiders[i].isValid)
			{
				g_multiRiders[i].state = MultiCombatState::Attacking;
				g_multiRiders[i].stateStartTime = GetGameTimeSeconds();
			}
		}
	}
	
	void UpdateCoordinatedMovement()
	{
		// TODO: Implement coordinated movement logic
		// This will handle synchronized attacks, formation keeping, etc.
	}
	
	// ============================================
	// MAIN UPDATE FUNCTION
	// ============================================
	
	bool UpdateMultiMountedCombat(Actor* horse, Actor* target, float distanceToPlayer, float meleeRange)
	{
		if (!horse || !target) return false;
		
		// Only works with 2+ riders
		int mountedRiderCount = GetFollowingNPCCount();
		if (mountedRiderCount < 2) return false;
		
		// Initialize if needed
		if (!g_multiCombatInitialized)
		{
			InitMultiMountedCombat();
		}
		
		// Find this horse's data
		MultiRiderData* riderData = nullptr;
		for (int i = 0; i < g_multiRiderCount; i++)
		{
			if (g_multiRiders[i].isValid && g_multiRiders[i].horseFormID == horse->formID)
			{
				riderData = &g_multiRiders[i];
				break;
			}
		}
		
		// Register if not found
		if (!riderData)
		{
			NiPointer<Actor> rider;
			if (CALL_MEMBER_FN(horse, GetMountedBy)(rider) && rider)
			{
				RegisterRider(horse, rider.get());
				return false;  // Let normal behavior handle this frame
			}
			return false;
		}
		
		// TODO: Implement multi-rider combat logic
		// For now, return false to use default behavior
		// This is a placeholder for future implementation
		
		return false;
	}
}
