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
	// Target Mount Check
	// ============================================
	
	// Check if the target is currently mounted
	// Returns true if target is on a horse/mount
	bool IsTargetMounted(Actor* target);
	
	// ============================================
	// 90-Degree Turn Maneuver (vs On-Foot Target)
	// ============================================
	// When horse enters melee range, it turns 90 degrees
	// to present the rider's weapon side to the target.
	// Direction is randomly chosen on each approach.
	// ONLY used when target is ON FOOT.
	
	// Get which direction horse should turn when entering melee range
	// Returns true for clockwise (+90°), false for counter-clockwise (-90°)
	bool GetHorseTurnDirectionClockwise(UInt32 horseFormID);
	
	// Get the target angle for 90-degree turn maneuver (mounted vs on-foot combat)
	// Returns the angle the horse should face (perpendicular to target)
	// horseFormID: the NPC's horse
	// angleToTarget: current angle from horse to target
	float Get90DegreeTurnAngle(UInt32 horseFormID, float angleToTarget);
	
	// Called when horse leaves melee range - will pick new random direction on next approach
	void NotifyHorseLeftMeleeRange(UInt32 horseFormID);
	
	// Clear turn direction data for a specific horse
	void ClearHorseTurnDirection(UInt32 horseFormID);
	
	// Clear all turn direction data
	void ClearAllHorseTurnDirections();
	
	// ============================================
	// Adjacent Riding Maneuver (vs Mounted Target)
	// ============================================
	// When fighting a mounted target, the NPC horse tries to
	// ride alongside the target's horse for melee combat.
	// Uses a flanking angle instead of perpendicular.
	
	// Get the target angle for adjacent riding (mounted vs mounted combat)
	// Returns the angle the horse should face to ride alongside target
	// horseFormID: the NPC's horse
	// targetPos: position of the target
	// horsePos: position of the NPC's horse
	// targetHeading: direction the target is facing (rot.z)
	float GetAdjacentRidingAngle(UInt32 horseFormID, const NiPoint3& targetPos, const NiPoint3& horsePos, float targetHeading);
	
	// Get which side the horse should ride on (true = right side, false = left side)
	// Once chosen, stays consistent until target is lost
	bool GetAdjacentRidingSide(UInt32 horseFormID);
	
	// Notify that horse is no longer in adjacent riding range
	void NotifyHorseLeftAdjacentRange(UInt32 horseFormID);
	
	// Clear adjacent riding data for a horse
	void ClearAdjacentRidingData(UInt32 horseFormID);
	
	// ============================================
	// Rear Up Moveset
	// ============================================
	
	// Check and trigger rear up when horse faces target head-on (7% chance)
	// Call this when horse enters melee range facing target
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
	// - Obstruction on LEFT -> Trot turn RIGHT
	// - Obstruction on RIGHT -> Trot turn LEFT
	
	// Try to play trot turn animation based on obstruction side
	// Returns true if turn was triggered
	bool TryHorseTrotTurnToAvoid(Actor* horse, bool turnRight);
	
	// Automatically determine turn direction based on obstruction side and execute
	// Returns true if turn was triggered
	bool TryHorseTrotTurnFromObstruction(Actor* horse);
	
	// ============================================
	// Horse Charge Maneuver (Long Distance Charge)
	// ============================================
	// When target is far away (700-1500 units), there's a 7% chance
	// every 10 seconds to trigger a dramatic charge:
	// 1. Horse rears up
	// 2. Rider equips melee weapon
	// 3. Horse sprints toward target
	// 4. Sprint stops when reaching melee range
	
	// Check and trigger charge maneuver when target is far
	// Returns true if charge was initiated
	bool TryChargeManeuver(Actor* horse, Actor* rider, Actor* target, float distanceToTarget);
	
	// Check if horse is currently in a charge
	bool IsHorseCharging(UInt32 horseFormID);
	
	// Update charge state - call every frame while charging
	// Returns true if charge is still active, false if completed
	bool UpdateChargeManeuver(Actor* horse, Actor* rider, Actor* target, float distanceToTarget, float meleeRange);
	
	// Stop/cancel a charge (e.g., when target dies or escapes)
	void StopChargeManeuver(UInt32 horseFormID);
	
	// Clear charge data for a horse
	void ClearChargeData(UInt32 horseFormID);

	// ============================================
	// Rapid Fire Maneuver (Stationary Bow Attack)
	// ============================================
	// When not in melee range and combat has been going for 20+ seconds,
	// there's a 7% chance every check to trigger rapid fire:
	// 1. Horse stops (follow package removed)
	// 2. Rider equips bow and stays stationary for 5 seconds
	// 3. Horse continues to face target during this time
	// 4. After 5 seconds, normal follow behavior resumes
	// Has 45 second cooldown
	
	// Check and trigger rapid fire maneuver
	// Returns true if rapid fire was initiated
	bool TryRapidFireManeuver(Actor* horse, Actor* rider, Actor* target, float distanceToTarget, float meleeRange);
	
	// Check if horse is currently in rapid fire mode
	bool IsInRapidFire(UInt32 horseFormID);
	
	// Update rapid fire state - call every frame while in rapid fire
	// Returns true if rapid fire is still active, false if completed
	bool UpdateRapidFireManeuver(Actor* horse, Actor* rider, Actor* target);
	
	// Stop/cancel rapid fire
	void StopRapidFireManeuver(UInt32 horseFormID);
	
	// Clear rapid fire data for a horse
	void ClearRapidFireData(UInt32 horseFormID);

	// ============================================
	// Melee Rider Collision Avoidance
	// ============================================
	// Prevents melee riders from bunching up on top of each other.
	// When two melee riders get too close (130 units or less),
	// they use trot animations to steer away from each other.
	
	// Check if another melee rider is too close and on which side
	// Returns: 0 = no collision, 1 = collision on LEFT, 2 = collision on RIGHT
	int CheckMeleeRiderCollision(Actor* horse, Actor* otherHorse);
	
	// Try to avoid collision with another melee rider
	// Returns true if avoidance maneuver was triggered
	bool TryMeleeRiderAvoidance(Actor* horse, Actor* otherHorse);
	
	// Check all nearby melee riders and avoid if needed
	// Call this from the melee rider update loop
	// Returns true if avoidance was triggered
	bool UpdateMeleeRiderCollisionAvoidance(Actor* horse, Actor* target);
	
	// Clear melee avoidance data for a horse
	void ClearMeleeAvoidanceData(UInt32 horseFormID);

	// ============================================
	// Clear All Moveset Data for a Horse
	// ============================================
	// Call this when combat ends (death, escape, dismount, etc.)
	
	// Clear all special moveset tracking data for a specific horse
	void ClearAllMovesetData(UInt32 horseFormID);
	
	// ============================================
	// RESET ALL SPECIAL MOVESETS
	// ============================================
	// Call this when game loads/reloads to reset all state
	
	void ResetAllSpecialMovesets();
}
