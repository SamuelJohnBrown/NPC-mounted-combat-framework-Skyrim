#pragma once

#include "skse64/GameReferences.h"
#include "skse64/NiTypes.h"

namespace MountedNPCCombatVR
{
	// ============================================
	// Arrow System Initialization
	// ============================================
	
	// Initialize the arrow system (call once at mod startup)
	void InitArrowSystem();
	
	// Reset cached forms on game load (prevents stale pointers)
	void ResetArrowSystemCache();
	
	// Reset all arrow system state (call on game load/reload)
	void ResetArrowSystem();
	
	// ============================================
	// Projectile Hook Control
	// ============================================
	
	// Install the projectile redirect hook
	void InstallProjectileHook();
	
	// Clear all pending projectile aims (call when combat ends)
	void ClearPendingProjectileAims();
	
	// Enable/disable projectile hook processing (for safe cleanup)
	void SetProjectileHookEnabled(bool enabled);
	
	// Register a shooter's next projectile for redirection
	void RegisterProjectileForRedirect(UInt32 shooterFormID, UInt32 targetFormID, const NiPoint3& targetAimPos);
	
	// ============================================
	// Arrow Spell Firing
	// ============================================
	
	// Fire arrow spell at target
	// shooter: the mounted NPC firing the arrow
	// target: the target to aim at
	// Returns true if spell cast was queued successfully
	bool FireArrowSpellAtTarget(Actor* shooter, Actor* target);
	
	// ============================================
	// Bow Attack System
	// ============================================
	
	// Play bow draw animation on a rider
	// Returns true if animation was triggered successfully
	bool PlayBowDrawAnimation(Actor* rider);
	
	// Play bow release animation on a rider and fire arrow spell at target
	// target: the actor to aim at (required for arrow firing)
	// Returns true if animation was triggered successfully
	bool PlayBowReleaseAnimation(Actor* rider, Actor* target = nullptr);
	
	// Update bow attack state for a rider
	// allowAttack: if false, only tracks equip time but doesn't start attacks
	// target: the combat target (required for arrow firing)
	// Returns true if bow attack is in progress
	bool UpdateBowAttack(Actor* rider, bool allowAttack, Actor* target = nullptr);
	
	// Reset bow attack state (call when bow is unequipped)
	void ResetBowAttackState(UInt32 riderFormID);
	
	// ============================================
	// Rapid Fire Bow Attack System
	// Special fast bow attack cycle for rapid fire maneuver
	// Fires 4 quick shots during the 5 second stationary period
	// ============================================
	
	// Start a rapid fire bow attack sequence (4 quick shots)
	// Call once when rapid fire maneuver begins
	void StartRapidFireBowAttack(UInt32 riderFormID);
	
	// Update rapid fire bow attack state
	// Returns true if rapid fire attack is still in progress
	// Returns false when all 4 shots have been fired
	bool UpdateRapidFireBowAttack(Actor* rider, Actor* target);
	
	// Reset rapid fire bow attack state (call when rapid fire ends or is aborted)
	void ResetRapidFireBowAttack(UInt32 riderFormID);
	
	// Check if rapid fire bow attack is active
	bool IsRapidFireBowAttackActive(UInt32 riderFormID);
}
