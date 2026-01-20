#pragma once

#include "MountedCombat.h"

namespace MountedNPCCombatVR
{
	// ============================================
	// Reset CombatStyles Cache
	// Called on game load/reload to clear stale pointers
	// ============================================
	
	void ResetCombatStylesCache();
	
	// ============================================
	// Mount Tracking (cleanup only)
	// ============================================
	
	void ReleaseAllMountControl();
	
	// ============================================
	// Follow Target Behavior (Dynamic Package Injection)
	// ============================================
	// Note: Supports any Actor as target for NPC vs NPC combat
	// ============================================
	
	void UpdateFollowBehavior();
	void SetNPCFollowTarget(Actor* actor, Actor* target);  // Follow specific target - ALWAYS USE THIS
	void ClearNPCFollowTarget(Actor* actor);
	bool IsNPCFollowingTarget(Actor* actor);
	void ClearAllFollowingNPCs();
	
	// ============================================
	// System Update (call from main update loop)
	// ============================================
	
	void UpdateCombatStylesSystem();

	// ============================================
	// Weapon Draw/Sheathe
	// ============================================
	
	void SetWeaponDrawn(Actor* actor, bool draw);

	// ============================================
	// Attack Animation System
	// ============================================
	
	// Initialize attack animation forms from ESP
	bool InitAttackAnimations();
	
	// Play mounted attack animation on rider
	// targetSide: "LEFT" or "RIGHT"
	bool PlayMountedAttackAnimation(Actor* rider, const char* targetSide);
	
	// Attack state tracking
	enum class RiderAttackState
	{
		None = 0,
		WindingUp,      // Animation starting
		PreHit,     // Before hit frame
		Hitting,   // Hit frame active
		Recovery        // Attack winding down
	};
	
	RiderAttackState GetRiderAttackState(Actor* rider);
	bool IsRiderAttacking(Actor* rider);

	// ============================================
	// Mounted Attack Hit Detection
	// ============================================
	
	// Check if mounted attack should hit target during animation
	// Returns true if hit was detected and damage was applied
	bool UpdateMountedAttackHitDetection(Actor* rider, Actor* target);
	
	// Reset hit data when starting new attack
	void ResetHitData(UInt32 riderFormID);
	
	// Set whether the current attack is a power attack (adds +5 damage)
	void SetHitDataPowerAttack(UInt32 riderFormID, bool isPowerAttack);

	// ============================================
	// RANGED ROLE ASSIGNMENT SYSTEM
	// ============================================
	// When 3+ riders of the same faction are in battle:
	// - Leaders/Captains are always assigned ranged role
	// - Otherwise, the furthest rider from target gets ranged role
	// - Mages are EXCLUDED from this system (they have their own logic)
	// - Ranged role maintains distance and uses bow
	// - If target gets too close, switches to melee with full weapon switching
	// ============================================
	// NOTE: Distance/threshold values are INI configurable via config.h:
	// - DynamicRangedRoleIdealDistance (default 800)
	// - DynamicRangedRoleMeleeThreshold (default 350)
	// - DynamicRangedRoleReturnThreshold (default 500)
	// - DynamicRangedRoleModeSwitchCooldown (default 3.0)
	// - DynamicRangedRoleMinRiders (default 3)
	// ============================================
	
	enum class RangedRoleMode
	{
		None = 0,    // Not in ranged role
		Ranged,      // Maintaining distance, using bow
		Melee  // Close range, using melee weapons (still in ranged role but temp melee)
	};
	
	// Update ranged role assignments (call periodically)
	void UpdateRangedRoleAssignments();
	
	// Clear all ranged role assignments
	void ClearRangedRoleAssignments();
	
	// Clear ranged role for a specific rider
	void ClearRangedRoleForRider(UInt32 riderFormID);
	
	// Check if rider is in ranged role (either mode)
	bool IsInRangedRole(UInt32 riderFormID);
	
	// Get current ranged role mode for a rider
	RangedRoleMode GetRangedRoleMode(UInt32 riderFormID);
	
	// Check if rider is in ranged role's ranged mode (maintaining distance)
	bool IsInRangedRoleRangedMode(UInt32 riderFormID);
	
	// ============================================
	// PRE-ASSIGN RANGED ROLE FOR CAPTAINS/LEADERS
	// Call this when a Captain/Leader is first detected to ensure they get
	// the ranged follow package from the very start (not the default melee one)
	// ============================================
	bool PreAssignRangedRoleForCaptain(Actor* rider, Actor* mount, Actor* target);

	// ============================================
	// Combat Style Namespaces
	// ============================================
	
	namespace GuardCombat
	{
		MountedCombatState DetermineState(Actor* actor, Actor* mount, Actor* target, MountedWeaponInfo* weaponInfo);
		void ExecuteBehavior(MountedNPCData* npcData, Actor* actor, Actor* mount, Actor* target);
		bool ShouldUseRanged(Actor* actor, Actor* target, MountedWeaponInfo* weaponInfo);
	}
	
	namespace SoldierCombat
	{
		MountedCombatState DetermineState(Actor* actor, Actor* mount, Actor* target, MountedWeaponInfo* weaponInfo);
		void ExecuteBehavior(MountedNPCData* npcData, Actor* actor, Actor* mount, Actor* target);
		bool ShouldUseRanged(Actor* actor, Actor* target, MountedWeaponInfo* weaponInfo);
	}
	
	namespace BanditCombat
	{
		MountedCombatState DetermineState(Actor* actor, Actor* mount, Actor* target, MountedWeaponInfo* weaponInfo);
		void ExecuteBehavior(MountedNPCData* npcData, Actor* actor, Actor* mount, Actor* target);
		bool ShouldUseMelee(Actor* actor, Actor* target, MountedWeaponInfo* weaponInfo);
	}
	
	namespace MageCombat
	{
		MountedCombatState DetermineState(Actor* actor, Actor* mount, Actor* target, MountedWeaponInfo* weaponInfo);
		void ExecuteBehavior(MountedNPCData* npcData, Actor* actor, Actor* mount, Actor* target);
	}

	// ============================================
	// Attack Position Query
	// ============================================
	
	bool IsNPCInMeleeRange(Actor* actor);
	bool IsNPCInAttackPosition(Actor* actor);
	int GetFollowingNPCCount();
}
