#pragma once

#include "skse64/GameReferences.h"
#include "skse64/NiNodes.h"

namespace MountedNPCCombatVR
{
	// ============================================
	// MULTI MOUNTED COMBAT (2+ Riders vs Target)
	// ============================================
	// Handles coordinated combat movement for multiple mounted riders
	// - Assigns combat roles (Melee vs Ranged) based on distance
	// - Ranged riders maintain distance and use bows
	// - Melee riders close in for attacks
	// ============================================
	
	// ============================================
	// Combat Role Types
	// ============================================
	
	enum class MultiCombatRole
	{
		None = 0,
		Melee,        // Uses melee weapon, follows target closely
		Ranged,       // Uses bow, maintains distance (Captain only)
		FullCombat    // Has both weapons, dynamic switching based on distance (promoted melee)
	};
	
	// ============================================
	// Multi-Rider Tracking Data
	// ============================================
	
	struct MultiRiderData
	{
		UInt32 riderFormID;
		UInt32 horseFormID;
		UInt32 targetFormID;
		MultiCombatRole role;
		float distanceToTarget;
		float lastRoleCheckTime;
		float lastRangedSetupTime;  // Rate limit ranged follow setup
		bool hasBow;
		bool isValid;
		
		MultiRiderData() :
			riderFormID(0), horseFormID(0), targetFormID(0),
			role(MultiCombatRole::None), distanceToTarget(0.0f),
			lastRoleCheckTime(0.0f), lastRangedSetupTime(0.0f),
			hasBow(false), isValid(false)
		{}
		
		void Reset()
		{
			riderFormID = 0;
			horseFormID = 0;
			targetFormID = 0;
			role = MultiCombatRole::None;
			distanceToTarget = 0.0f;
			lastRoleCheckTime = 0.0f;
			lastRangedSetupTime = 0.0f;
			hasBow = false;
			isValid = false;
		}
	};
	
	// ============================================
	// Configuration
	// ============================================
	
	extern const float RANGED_ROLE_MIN_DISTANCE;    // 400 units - minimum distance to be assigned ranged role
	extern const float RANGED_ROLE_IDEAL_DISTANCE;  // 600 units - ideal distance for ranged combat
	extern const float ROLE_CHECK_INTERVAL;         // How often to re-evaluate roles
	extern const int MAX_MULTI_RIDERS;              // Maximum tracked riders
	
	// ============================================
	// Core Functions
	// ============================================
	
	// Initialize multi mounted combat system
	void InitMultiMountedCombat();
	
	// Shutdown and clear all data
	void ShutdownMultiMountedCombat();
	
	// Clear all multi-rider tracking
	void ClearAllMultiRiders();
	
	// ============================================
	// Rider Management
	// ============================================
	
	// Register a rider for multi-combat tracking
	// Returns the assigned role
	MultiCombatRole RegisterMultiRider(Actor* rider, Actor* horse, Actor* target);
	
	// Unregister a rider from multi-combat tracking
	void UnregisterMultiRider(UInt32 riderFormID);
	
	// Get rider data by rider FormID
	MultiRiderData* GetMultiRiderData(UInt32 riderFormID);
	
	// Get rider data by horse FormID
	MultiRiderData* GetMultiRiderDataByHorse(UInt32 horseFormID);
	
	// ============================================
	// Role Assignment
	// ============================================
	
	// Determine the best role for a rider based on:
	// - Distance to target
	// - Whether they have a bow
	// - How many other melee/ranged riders exist
	MultiCombatRole DetermineOptimalRole(Actor* rider, Actor* target, float distanceToTarget);
	
	// Force a specific role on a rider
	void SetRiderRole(UInt32 riderFormID, MultiCombatRole role);
	
	// Get the current role for a rider
	MultiCombatRole GetRiderRole(UInt32 riderFormID);
	
	// ============================================
	// Role-Specific Behavior
	// ============================================
	
	// Execute ranged role behavior:
	// - Maintain distance from target
	// - Always face target
	// - Keep bow equipped and drawn
	// - Fire arrows when in range
	void ExecuteRangedRoleBehavior(MultiRiderData* data, Actor* rider, Actor* horse, Actor* target);
	
	// Execute melee role behavior:
	// - Close distance to target
	// - Use standard melee follow package
	void ExecuteMeleeRoleBehavior(MultiRiderData* data, Actor* rider, Actor* horse, Actor* target);
	
	// Execute full combat role behavior:
	// - Dynamic weapon switching based on distance
	// - Melee when close, ranged when far
	// - Uses charge maneuvers, rapid fire, etc.
	void ExecuteFullCombatRoleBehavior(MultiRiderData* data, Actor* rider, Actor* horse, Actor* target);
	
	// Promote a melee rider to full combat capability
	// - Gives them a bow
	// - Enables dynamic weapon switching
	void PromoteMeleeRiderToFullCombat(MultiRiderData* data, Actor* rider, Actor* horse);
	
	// ============================================
	// Update Functions
	// ============================================
	
	// Main update - called from DynamicPackages
	// Updates all tracked riders and their roles
	void UpdateMultiMountedCombat();
	
	// Update a single rider's behavior based on their role
	// Returns: attack state (0=traveling, 1=melee range, 2=attack position, 3=ranged)
	int UpdateMultiRiderBehavior(Actor* horse, Actor* target);
	
	// ============================================
	// Query Functions
	// ============================================
	
	// Get count of active multi-riders
	int GetActiveMultiRiderCount();
	
	// Get count of riders in melee role
	int GetMeleeRiderCount();
	
	// Get count of riders in ranged role
	int GetRangedRiderCount();
	
	// Check if a rider is in ranged role
	bool IsRiderInRangedRole(UInt32 riderFormID);
	
	// Check if a horse's rider is in ranged role
	bool IsHorseRiderInRangedRole(UInt32 horseFormID);
}
