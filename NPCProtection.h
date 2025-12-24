#pragma once

#include "skse64/GameReferences.h"

namespace MountedNPCCombatVR
{
	// ============================================
	// NPC Protection System
	// ============================================
	// Makes mounted NPCs immune to stagger and bleedout
	// by setting flags and modifying actor values.
	// ============================================
	
	void ApplyMountedProtection(Actor* actor);
	void RemoveMountedProtection(Actor* actor);
	bool HasMountedProtection(Actor* actor);
	void ClearAllMountedProtection();
}
