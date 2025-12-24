#pragma once

#include "skse64/GameReferences.h"
#include "skse64/NiTypes.h"

namespace MountedNPCCombatVR
{
	// ============================================
	// Target Selection System
	// ============================================
	// Handles dynamic target selection for mounted combat.
	// Allows riders to fight any valid combat target,
	// not just the player.
	// ============================================

	// ============================================
	// Target Type
	// ============================================
	
	enum class TargetType
	{
		None = 0,
		Player,      // Target is the player
		NPC,         // Target is another NPC
		Creature     // Target is a creature
	};

	// ============================================
	// Target Info Structure
	// ============================================
	
	struct CombatTargetInfo
	{
		Actor* target;
		TargetType type;
		float distance;
		bool isValid;
		bool isHostile;
		bool isAlive;
		bool isLoaded;
	};

	// ============================================
	// Core Target Functions
	// ============================================
	
	// Get the rider's actual combat target from the game's combat system
	// Returns nullptr if no valid target
	Actor* GetRiderCombatTarget(Actor* rider);
	
	// Check if target is valid for mounted combat
	// Validates: alive, loaded, not friendly, in range, etc.
	bool IsValidCombatTarget(Actor* rider, Actor* target);
	
	// Get detailed info about a combat target
	CombatTargetInfo GetCombatTargetInfo(Actor* rider, Actor* target);
	
	// ============================================
	// Target Validation
	// ============================================
	
	// Check if target is alive and not in bleedout
	bool IsTargetAlive(Actor* target);
	
	// Check if target's 3D is loaded (not too far away)
	bool IsTargetLoaded(Actor* target);
	
	// Check if target is hostile to rider
	bool IsTargetHostile(Actor* rider, Actor* target);
	
	// Check if target is within combat range
	bool IsTargetInRange(Actor* rider, Actor* target, float maxRange = 4096.0f);
	
	// ============================================
	// Target Type Detection
	// ============================================
	
	// Determine the type of target
	TargetType GetTargetType(Actor* target);
	
	// Check if target is the player
	bool IsTargetPlayer(Actor* target);
	
	// Check if target is an NPC (humanoid)
	bool IsTargetNPC(Actor* target);
	
	// Check if target is a creature
	bool IsTargetCreature(Actor* target);
	
	// ============================================
	// Distance Functions
	// ============================================
	
	// Get distance between rider and target
	float GetDistanceToTarget(Actor* rider, Actor* target);
	
	// Get 2D distance (ignores Z/height difference)
	float GetDistanceToTarget2D(Actor* rider, Actor* target);
	
	// ============================================
	// Target Priority (for future use)
	// ============================================
	
	// Calculate target priority score (higher = more important target)
	// Factors: distance, threat level, is player, etc.
	float CalculateTargetPriority(Actor* rider, Actor* target);
	
	// ============================================
	// Logging
	// ============================================
	
	// Log target info for debugging
	void LogTargetInfo(Actor* rider, Actor* target);
}
