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
