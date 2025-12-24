#pragma once

#include "skse64/GameReferences.h"
#include "skse64/NiNodes.h"

namespace MountedNPCCombatVR
{
	// ============================================
	// MULTI MOUNTED COMBAT (2+ Riders vs Player)
	// Handles coordinated combat movement for multiple mounted riders
	// ============================================
	
	// ============================================
	// FORMATION TYPES
	// ============================================
	
	enum class FormationType
	{
		None = 0,
		Surround,          // Riders spread around player
		Flank,             // Riders attack from sides
		LineCharge,        // Riders charge in a line
		Staggered          // Riders attack in waves
	};
	
	// ============================================
	// MULTI-RIDER STATE
	// ============================================
	
	enum class MultiCombatState
	{
		None = 0,
		Positioning,       // Moving to formation positions
		Attacking,         // Coordinated attack run
		Regrouping,        // Reforming after attack
		Circling     // Circling player waiting for opening
	};
	
	struct MultiRiderData
	{
		UInt32 horseFormID;
		UInt32 riderFormID;
		int assignedPosition;      // Position in formation (0, 1, 2, etc.)
		float angleOffset;         // Angle offset from player
		MultiCombatState state;
		float stateStartTime;
		bool isValid;
	};
	
	// ============================================
	// CONFIGURATION
	// ============================================
	
	extern const float FORMATION_RADIUS;
	extern const float ATTACK_COORDINATION_DELAY;
	extern const float REGROUP_DISTANCE;
	
	// ============================================
	// INITIALIZATION
	// ============================================
	
	void InitMultiMountedCombat();
	
	// ============================================
	// FORMATION FUNCTIONS
	// ============================================
	
	FormationType DetermineFormation(int riderCount);
	NiPoint3 GetFormationPosition(Actor* player, int positionIndex, int totalRiders, FormationType formation);
	void AssignFormationPositions();
	
	// ============================================
	// COORDINATION FUNCTIONS
	// ============================================
	
	bool ShouldCoordinateAttack();
	void StartCoordinatedAttack();
	void UpdateCoordinatedMovement();
	
	// ============================================
	// MAIN UPDATE FUNCTION
	// Called when 2+ mounted riders are in combat
	// Returns: true if multi-combat is handling movement
	// ============================================
	
	bool UpdateMultiMountedCombat(Actor* horse, Actor* target, float distanceToPlayer, float meleeRange);
	
	// ============================================
	// RIDER MANAGEMENT
	// ============================================
	
	void RegisterRider(Actor* horse, Actor* rider);
	void UnregisterRider(UInt32 horseFormID);
	void ClearAllMultiRiders();
	int GetActiveMultiRiderCount();
}
