#pragma once

#include "skse64/GameReferences.h"
#include "skse64/NiTypes.h"

namespace MountedNPCCombatVR
{
	// ============================================
	// Special Movesets System
	// ============================================
	// Handles triggering special movement maneuvers
	// for mounted NPCs during combat.
	// ============================================

	// ============================================
	// Initialization
	// ============================================
	
	void InitSpecialMovesets();
	void ShutdownSpecialMovesets();
	
	// ============================================
	// 90-Degree Turn Maneuver
	// ============================================
	// When horse enters melee range, it turns 90 degrees
	// to present the rider's weapon side to the target.
	// Direction is randomly chosen on each approach.
	
	// Get which direction horse should turn when entering melee range
	// Returns true for clockwise (+90°), false for counter-clockwise (-90°)
	bool GetHorseTurnDirectionClockwise(UInt32 horseFormID);
	
	// Called when horse leaves melee range - will pick new random direction on next approach
	void NotifyHorseLeftMeleeRange(UInt32 horseFormID);
	
	// Clear turn direction data for a specific horse
	void ClearHorseTurnDirection(UInt32 horseFormID);
	
	// Clear all turn direction data
	void ClearAllHorseTurnDirections();
	
	// ============================================
	// Rear Up Moveset
	// ============================================
	
	// Check and trigger rear up when horse faces player head-on (7% chance)
	// Call this when horse enters melee range facing player
	// Returns true if rear up was triggered
	bool TryRearUpOnApproach(Actor* horse, Actor* target, float distanceToTarget);
	
	// Check and trigger rear up when horse takes large damage (10% chance)
	// Call this from damage hook when horse takes significant hit
	// Returns true if rear up was triggered
	bool TryRearUpOnDamage(Actor* horse, float damageAmount);
	
	// Get/set the last known health for damage detection
	void UpdateHorseHealth(UInt32 horseFormID, float currentHealth);
	float GetHorseLastHealth(UInt32 horseFormID);
	
	// Clear rear up tracking data for a horse
	void ClearRearUpData(UInt32 horseFormID);
	
	// ============================================
	// Horse Jump Maneuver (Obstruction Escape)
	// ============================================
	// When horse is obstructed/stuck, play jump animation
	// to attempt to clear the obstacle.
	// Has a 4-second cooldown between attempts.
	
	// Try to play horse jump animation to escape obstruction
	// Returns true if jump was triggered, false if on cooldown or failed
	bool TryHorseJumpToEscape(Actor* horse);
	
	// Check if horse jump is on cooldown
	bool IsHorseJumpOnCooldown(UInt32 horseFormID);
	
	// Clear jump cooldown data for a horse
	void ClearHorseJumpData(UInt32 horseFormID);
	
	// ============================================
	// Horse Trot Turn Maneuver (Obstruction Avoidance)
	// ============================================
	// When horse is obstructed on one side, turn away from
	// the obstruction using trot turn animations.
	// - Obstruction on LEFT ? Trot turn RIGHT
	// - Obstruction on RIGHT ? Trot turn LEFT
	
	// Try to play trot turn animation based on obstruction side
	// Returns true if turn was triggered
	bool TryHorseTrotTurnToAvoid(Actor* horse, bool turnRight);
	
	// Automatically determine turn direction based on obstruction side and execute
	// Returns true if turn was triggered
	bool TryHorseTrotTurnFromObstruction(Actor* horse);
	
	// ============================================
	// Stationary Rapid Fire Maneuver (Ranged)
	// ============================================
	// Horse stops all movement (except rotation) for 3 seconds
	// while rider rapidly aims and fires bow.
	// - 5% chance to trigger
	// - Only after 15 seconds of combat
	// - Not in melee range
	// - Requires bow equipped
	
	// State enum for rapid fire maneuver
	enum class RapidFireState
	{
		None = 0,
		Active,      // Currently in stationary rapid fire mode
		Cooldown     // Just finished, on cooldown
	};
	
	// Check conditions and potentially trigger stationary rapid fire
	// Returns true if maneuver was triggered or is currently active
	bool TryStationaryRapidFire(Actor* rider, Actor* horse, Actor* target, float distanceToTarget);
	
	// Update the rapid fire state (call each frame while active)
	// Returns true if still in rapid fire mode
	bool UpdateStationaryRapidFire(Actor* rider, Actor* horse, Actor* target);
	
	// Check if rider is currently in stationary rapid fire mode
	bool IsInStationaryRapidFire(UInt32 riderFormID);
	
	// Check if rider is currently in the draw/aim phase of rapid fire
	bool IsRapidFireDrawing(UInt32 riderFormID);
	
	// Get current rapid fire state
	RapidFireState GetRapidFireState(UInt32 riderFormID);
	
	// Force end rapid fire mode (e.g., if dismounted or combat ends)
	void EndStationaryRapidFire(UInt32 riderFormID);
	
	// Clear all rapid fire data
	void ClearAllRapidFireData();
	
	// Notify combat started (resets combat timer)
	void NotifyRangedCombatStarted();
}
