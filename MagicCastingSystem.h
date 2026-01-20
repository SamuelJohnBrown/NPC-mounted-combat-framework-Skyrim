#pragma once

#include "skse64/GameReferences.h"
#include "skse64/NiTypes.h"

namespace MountedNPCCombatVR
{
	// ============================================
	// MAGIC CASTING SYSTEM
	// ============================================
	// Handles spell casting for mounted mage NPCs.
	// 
	// FIRE AND FORGET SPELLS (Range 300-1950 units):
	// - Firebolt, Fireball, Ice Spike
	// - Charge time: 2.5-3.5 seconds (configurable)
	// - Cooldown: 3.0 seconds between casts
	// 
	// CLOSE RANGE (?299 units):
	// - Mages use melee combat with their staff
	// - No weapon switching, keeps staff equipped
	// 
	// BUFFER ZONE:
	// - Switch to melee: ?299 units
	// - Switch back to spell: >400 units AND 3 second cooldown
	// - Prevents rapid toggling between modes
	// 
	// Uses projectile hooks to redirect spell projectiles
	// toward the target for accurate aiming from horseback.
	// ============================================
	
	// ============================================
	// System Initialization
	// ============================================
	
	// Initialize the magic casting system (call once at mod startup)
	void InitMagicCastingSystem();
	
	// Shutdown the magic casting system
	void ShutdownMagicCastingSystem();
	
	// Reset cached spell forms on game load (prevents stale pointers)
	void ResetMagicCastingSystemCache();
	
	// Reset all magic casting state (call on game load/reload)
	void ResetMagicCastingSystem();
	
	// ============================================
	// Mage Combat Mode
	// ============================================
	
	enum class MageCombatMode
	{
		Spell = 0,   // Ranged spell casting (300-1950 units)
		Melee        // Close range melee with staff (<=299 units)
	};
	
	// Update mage combat mode based on distance (handles buffer zone and cooldown)
	// Call this every frame for mages to determine their current combat mode
	MageCombatMode UpdateMageCombatMode(UInt32 mageFormID, float distanceToTarget);
	
	// Check if mage is currently in melee mode
	bool IsMageInMeleeMode(UInt32 mageFormID);
	
	// Simple range check - is distance within melee threshold (?299)?
	bool IsMageInMeleeRange(float distanceToTarget);
	
	// Reset combat mode for a specific mage
	void ResetMageCombatMode(UInt32 mageFormID);
	
	// ============================================
	// Mage Tactical Retreat
	// ============================================
	// Every 15 seconds in combat, mages have a 25% chance to retreat
	// to a safe distance of 650-700 units before resuming combat.
	
	// Check if mage should retreat and handle retreat logic
	// Returns true if mage is currently retreating
	bool CheckAndTriggerMageRetreat(Actor* mage, Actor* horse, Actor* target, float distanceToTarget);
	
	// Check if a specific mage is retreating
	bool IsMageRetreating(UInt32 mageFormID);
	
	// Force start retreat for a mage
	bool StartMageRetreat(Actor* mage, Actor* horse, Actor* target);
	
	// Stop retreat and resume combat
	void StopMageRetreat(UInt32 mageFormID);
	
	// Reset retreat state for a specific mage
	void ResetMageRetreat(UInt32 mageFormID);
	
	// Reset all mage retreat states
	void ResetAllMageRetreats();
	
	// ============================================
	// Main Spell Casting Interface
	// ============================================
	
	// Update mage spell casting - handles charge, cast, and cooldown states
	// distanceToTarget: pre-calculated distance to avoid redundant sqrt calls
	// Returns true if mage is actively casting (charging or casting)
	// NOTE: Only casts spells when in Spell mode (300-1950 range)
	bool UpdateMageSpellCasting(Actor* caster, Actor* target, float distanceToTarget);
	
	// Check if mage is currently charging/casting any spell
	bool IsMageCharging(UInt32 casterFormID);
	
	// Reset all spell casting state for a specific mage
	void ResetMageSpellState(UInt32 casterFormID);
}
