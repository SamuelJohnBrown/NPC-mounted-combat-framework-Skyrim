#pragma once

#include "skse64/GameReferences.h"

namespace MountedNPCCombatVR
{
	// ============================================
	// NPC Protection System
	// ============================================
	// Makes mounted NPCs immune to stagger and bleedout
	// by setting flags and modifying actor values.
	// Also prevents NPCs from dismounting during combat.
	// ============================================
	
	// ============================================
	// Mounted Protection (Stagger/Bleedout)
	// ============================================
	
	void ApplyMountedProtection(Actor* actor);
	void RemoveMountedProtection(Actor* actor);
	bool HasMountedProtection(Actor* actor);
	void ClearAllMountedProtection();
	
	// Set actor mass directly (used for ragdoll recovery)
	void SetActorMass(Actor* actor, float mass);
	
	// ============================================
	// Temporary Stagger Allow System
	// ============================================
	// Temporarily removes stagger protection to allow
	// block stagger animations to play on mounted NPCs.
	// Protection is automatically restored after duration.
	
	// Temporarily allow stagger for an actor (removes mass protection)
	// Duration is in seconds (default 2.5s for stagger animation)
	void AllowTemporaryStagger(Actor* actor, float duration = 2.5f);
	
	// Check if actor currently has stagger allowed
	bool HasTemporaryStaggerAllowed(Actor* actor);
	
	// Update temporary stagger timers - call from main update loop
	void UpdateTemporaryStaggerTimers();
	
	// ============================================
	// NPC Dismount Prevention Hook
	// ============================================
	
	// Function signature for the game's Dismount function
	typedef int64_t(__fastcall* _Dismount)(Actor* actor);
	
	// Original dismount function pointer (set during hook setup)
	extern _Dismount OriginalDismount;

	// The hook function that intercepts ALL dismount calls
	int64_t __fastcall DismountHook(Actor* actor);

	// Sets up the dismount hook - call this during mod initialization
	void SetupDismountHook();
}
