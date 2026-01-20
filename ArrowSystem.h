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
	// Delayed Arrow Fire System
	// Handles 200ms delay between release animation and actual arrow
	// ============================================
	
	// Schedule an arrow to fire after 200ms delay
	void ScheduleDelayedArrowFire(Actor* shooter, Actor* target);
	
	// Update delayed arrow fires - call every frame
	void UpdateDelayedArrowFires();
	
	// Clear all pending delayed arrow fires
	void ClearDelayedArrowFires();
	
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
	
	// Check if rider has bow drawn and ready to fire (in Drawing or Holding state)
	// Used to decide whether to fire before switching weapons
	bool IsBowDrawnAndReady(UInt32 riderFormID);
	
	// Force release the drawn bow and fire at target
	// Returns true if release was successful
	bool ForceReleaseBowAtTarget(Actor* rider, Actor* target);
	
	// ============================================
	// Rapid Fire Bow Attack System
	// Special fast bow attack cycle for rapid fire maneuver
	// Fires 4 quick shots during the 5 second stationary period
	// For archers: Uses bow animations + arrow spell
	// For mages: Casts Ice Spike spells (no bow animations)
	// ============================================
	
	// Start a rapid fire bow attack sequence (4 quick shots)
	// Call once when rapid fire maneuver begins
	// isMage: if true, casts Ice Spike spells instead of using bow
	void StartRapidFireBowAttack(UInt32 riderFormID, bool isMage = false);
	
	// Update rapid fire bow attack state
	// Returns true if rapid fire attack is still in progress
	// Returns false when all 4 shots have been fired
	bool UpdateRapidFireBowAttack(Actor* rider, Actor* target);
	
	// Reset rapid fire bow attack state (call when rapid fire ends or is aborted)
	void ResetRapidFireBowAttack(UInt32 riderFormID);
	
	// Check if rapid fire bow attack is active
	bool IsRapidFireBowAttackActive(UInt32 riderFormID);
	
	// Check if mage rapid fire is active (for bypassing normal spell cooldowns)
	bool IsMageRapidFireActive(UInt32 riderFormID);
}
