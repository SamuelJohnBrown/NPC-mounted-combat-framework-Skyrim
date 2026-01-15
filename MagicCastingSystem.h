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
	// FIRE AND FORGET (Long Range - 240+ units):
	// - Firebolt, Fireball, Ice Spike
	// - Charge time: 2.5-3.5 seconds
	// - Cooldown: 0.5 seconds
	// 
	// CONCENTRATION (Close Range - under 240 units):
	// - Flames, Frostbite, Sparks
	// - Burst duration: 3-7 seconds
	// - Cooldown: 3 seconds between bursts
	// ============================================
	
	// ============================================
	// System Initialization
	// ============================================
	
	// Initialize the magic casting system (call once at mod startup)
	void InitMagicCastingSystem();
	
	// Shutdown the magic casting system
	void ShutdownMagicCastingSystem();
	
	// Reset cached forms on game load (prevents stale pointers)
	void ResetMagicCastingSystemCache();
	
	// Reset all magic casting state (call on game load/reload)
	void ResetMagicCastingSystem();
	
	// ============================================
	// Main Spell Casting Interface
	// ============================================
	
	// Update mage spell casting - automatically chooses fire-and-forget or concentration
	// based on distance to target
	// Returns true if mage is actively casting
	bool UpdateMageSpellCasting(Actor* caster, Actor* target, float distanceToTarget);
	
	// Update concentration spell casting specifically (close range)
	// Returns true if mage is actively casting concentration spell
	bool UpdateConcentrationSpellCasting(Actor* caster, Actor* target, float distanceToTarget);
	
	// Check if mage is currently charging/casting any spell
	bool IsMageCharging(UInt32 casterFormID);
	
	// Check if mage is specifically casting a concentration spell
	bool IsMageCastingConcentration(UInt32 casterFormID);
	
	// Reset all spell casting state for a specific mage
	void ResetMageSpellState(UInt32 casterFormID);
	
	// ============================================
	// Legacy Interface (for compatibility)
	// ============================================
	
	// Update spell casting state for a mounted mage (calculates distance internally)
	bool UpdateSpellCasting(Actor* caster, Actor* target);
	
	// Reset spell casting state for a specific mage
	void ResetSpellCastingState(UInt32 casterFormID);
	
	// Check if mage is currently casting a spell
	bool IsSpellCastingActive(UInt32 casterFormID);
	
	// Check if actor has offensive spells (always true for mage class)
	bool HasOffensiveSpellEquipped(Actor* actor);
	
	// Get a random offensive spell
	SpellItem* GetBestOffensiveSpell(Actor* caster, Actor* target);
	
	// Cast spell at target immediately
	bool CastSpellAtTarget(Actor* caster, Actor* target);
	
	// Legacy delayed cast functions (no longer used but kept for compatibility)
	void ScheduleDelayedSpellCast(Actor* caster, Actor* target);
	void UpdateDelayedSpellCasts();
	void ClearDelayedSpellCasts();
}
