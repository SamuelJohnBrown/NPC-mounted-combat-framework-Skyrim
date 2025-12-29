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
	float GetCombatElapsedTime();  // Returns seconds since combat started
	
	// Reset cached forms on game load (prevents stale pointers)
	void ResetSingleMountedCombatCache();
	
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
}
