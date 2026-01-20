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
	// Mobile Target Interception (vs Moving NPCs)
	// ============================================
	// When fighting a mobile NPC (wolf, bandit, etc.), both the
	// horse and target move toward each other causing collisions.
	// This system makes the horse approach from an angle instead.
	
	// Check if target is a mobile NPC (not player, and moving)
	bool IsTargetMobileNPC(Actor* target, UInt32 horseFormID);
	
	// Get interception angle for approaching a mobile target
	// Makes the horse approach at ~60 degree offset to avoid head-on collision
	float GetMobileTargetInterceptionAngle(UInt32 horseFormID, Actor* horse, Actor* target);
	
	// Called when leaving melee range - reset interception side for next approach
	void NotifyHorseLeftMobileTargetRange(UInt32 horseFormID);
	
	// Clear mobile intercept data for a horse
	void ClearMobileInterceptData(UInt32 horseFormID);
	
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
	
	// Get 90-degree turn angle with cooldown for mounted vs mounted combat
	// Prevents constant snap-turning when two mounted NPCs fight at close range
	// horseFormID: the NPC's horse
	// angleToTarget: current angle from horse to target
	// distanceToTarget: distance to the target for close-range detection
	float Get90DegreeTurnAngleForMountedTarget(UInt32 horseFormID, float angleToTarget, float distanceToTarget);

	// Called when horse leaves melee range - will pick new random direction on next approach
	void NotifyHorseLeftMeleeRange(UInt32 horseFormID);
	
	// Clear turn direction data for a specific horse
	void ClearHorseTurnDirection(UInt32 horseFormID);
	
	// Clear all turn direction data
	void ClearAllHorseTurnDirections();
	
	// ============================================
	// Adjacent Riding Maneuver - REMOVED
	// This system is no longer in use
	// ============================================
	
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
	// Stand Ground Maneuver (vs Mobile NPC Targets)
	// ============================================
	// When fighting a non-player NPC target at close range (<260 units),
	// the horse will stop moving and hold position for 3-8 seconds.
	// This allows the rider to land attacks instead of constantly chasing.
	// Only used against non-player targets (both are moving toward each other).
	// 25% chance to trigger when conditions are met.
	// NOTE: Only for mounted vs on-foot combat (not mounted vs mounted)
	
	// Check and trigger stand ground maneuver
	// Returns true if stand ground was initiated
	bool TryStandGroundManeuver(Actor* horse, Actor* rider, Actor* target, float distanceToTarget);
	
	// Check if horse is currently in stand ground mode
	bool IsInStandGround(UInt32 horseFormID);
	
	// Check if horse in stand ground has noRotation flag (50% chance - faces target directly)
	bool IsStandGroundNoRotation(UInt32 horseFormID);
	
	// Check if stand ground rotation is LOCKED (90-degree turn complete)
	bool IsStandGroundRotationLocked(UInt32 horseFormID);
	
	// Get the locked angle for a stand ground horse
	float GetStandGroundLockedAngle(UInt32 horseFormID);
	
	// Lock the rotation for a stand ground horse at a specific angle
	void LockStandGroundRotation(UInt32 horseFormID, float angle);
	
	// Get the target 90-degree angle for stand ground (calculated ONCE at start)
	// Returns the stored angle to prevent jitter from recalculating as target moves
	float GetStandGroundTarget90DegreeAngle(UInt32 horseFormID, float angleToTarget);
	
	// Update stand ground state - call every frame while standing ground
	// Returns true if still standing ground, false if completed
	bool UpdateStandGroundManeuver(Actor* horse, Actor* target);
	
	// Stop/cancel stand ground
	void StopStandGroundManeuver(UInt32 horseFormID);
	
	// Clear stand ground data for a horse
	void ClearStandGroundData(UInt32 horseFormID);

	// ============================================
	// Player Aggro Switch (vs Non-Player Target)
	// ============================================
	// When fighting a non-player NPC and player is within 900 units,
	// there's a distance-scaled chance (15% close, 2% at max range) 
	// every 20 seconds to switch targets to the player and trigger a charge.
	
	// Check and trigger player aggro switch
	// Returns true if switch was initiated (also triggers charge)
	bool TryPlayerAggroSwitch(Actor* horse, Actor* rider, Actor* currentTarget);
	
	// Clear player aggro switch data for a horse
	void ClearPlayerAggroSwitchData(UInt32 horseFormID);

	// ============================================
	// Close Range Melee Assault (Emergency Close Combat)
	// ============================================
	// When target gets within 145 units of the rider's side,
	// triggers rapid melee attacks (1 per second) until they move away.
	// 100% trigger chance, 0 cooldown - purely distance-based.
	// Uses the same melee attack logic as normal mounted combat.
	// Attacks from LEFT if target is on left side, RIGHT if on right.
	
	// Check and trigger close range melee assault
	// Returns true if assault is active (target within range)
	bool TryCloseRangeMeleeAssault(Actor* horse, Actor* rider, Actor* target);
	
	// Check if horse is currently in close range melee assault mode
	bool IsInCloseRangeMeleeAssault(UInt32 horseFormID);
	
	// Update close range melee assault - triggers attacks every 1 second
	// Returns true if still in assault mode, false if target moved away
	bool UpdateCloseRangeMeleeAssault(Actor* horse, Actor* rider, Actor* target);
	
	// Stop close range melee assault (target moved out of range)
	void StopCloseRangeMeleeAssault(UInt32 horseFormID);
	
	// Clear close range melee assault data for a horse
	void ClearCloseRangeMeleeAssault(UInt32 horseFormID);

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
