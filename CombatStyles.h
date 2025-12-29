#pragma once

#include "MountedCombat.h"

namespace MountedNPCCombatVR
{
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
	
	namespace HunterCombat
	{
		MountedCombatState DetermineState(Actor* actor, Actor* mount, Actor* target, MountedWeaponInfo* weaponInfo);
		void ExecuteBehavior(MountedNPCData* npcData, Actor* actor, Actor* mount, Actor* target);
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
