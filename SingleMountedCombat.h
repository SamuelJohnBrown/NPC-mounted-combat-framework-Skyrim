#pragma once

#include "skse64/GameReferences.h"
#include "skse64/NiTypes.h"

namespace MountedNPCCombatVR
{
	// ============================================
	// Initialization
	// ============================================
	
	void InitSingleMountedCombat();
	void NotifyCombatStarted();
	
	// ============================================
	// Animation Event Helper
	// ============================================
	
	// Send an animation event to an actor's animation graph
	// Returns true if the event was accepted
	bool SendHorseAnimationEvent(Actor* actor, const char* eventName);
	
	// ============================================
	// Horse Sprint Control
	// ============================================
	
	// Start/stop horse sprint animation
	void StartHorseSprint(Actor* horse);
	void StopHorseSprint(Actor* horse);
	
	// ============================================
	// Horse Rear Up Animation
	// ============================================
	
	// Play the rear up animation on a horse (no checks, just plays)
	// Returns true if animation was triggered successfully
	bool PlayHorseRearUpAnimation(Actor* horse);
	
	// ============================================
	// Bow Attack System
	// ============================================
	
	// Play bow draw animation on a rider
	// Returns true if animation was triggered successfully
	bool PlayBowDrawAnimation(Actor* rider);
	
	// Play bow release animation on a rider and fire arrow at target
	// target: the actor to fire the arrow at (can be nullptr for animation only)
	// Returns true if animation was triggered successfully
	bool PlayBowReleaseAnimation(Actor* rider, Actor* target = nullptr);
	
	// Update bow attack state for a rider
	// allowAttack: if false, only tracks equip time but doesn't start attacks
	// target: the combat target to fire arrows at (required for actual firing)
	// Returns true if bow attack is in progress
	bool UpdateBowAttack(Actor* rider, bool allowAttack, Actor* target = nullptr);
	
	// Reset bow attack state (call when bow is unequipped)
	void ResetBowAttackState(UInt32 riderFormID);
	
	// Fire an arrow projectile from shooter to target
	// Returns true if projectile was launched successfully
	bool FireArrowAtTarget(Actor* shooter, Actor* target);
}
