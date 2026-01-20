#pragma once

#include "skse64/GameReferences.h"
#include "skse64/GameForms.h"
#include <Windows.h>

namespace MountedNPCCombatVR
{
	// ============================================
	// AI Package Logging
	// ============================================
	
	const char* GetPackageTypeName(UInt8 packageType);
	bool IsDialogueOrCrimePackage(UInt8 packageType);
	TESPackage* GetActorCurrentPackage(Actor* actor);
	
	// Detect and log dialogue/crime package issues (LOGGING ONLY - no handling)
	bool DetectDialoguePackageIssue(Actor* actor);
	
	void LogCurrentAIPackage(Actor* actor, UInt32 formID);
	void LogMountAIPackage(Actor* mount, UInt32 formID);
	void LogMountedCombatAIState(Actor* rider, Actor* mount, UInt32 riderFormID);
	
	// ============================================
	// Combat Alarm Control (for remounting feature)
	// ============================================
	
	// Stop combat alarm on actor - clears crime/alarm state so NPC can remount
	// If called from a non-main thread this will queue the request to run on main thread
	// Has built-in cooldown to prevent CTD when multiple riders disengage
	void StopActorCombatAlarm(Actor* actor);

	// Set main thread id for AILogging (should be called from InitMountedCombatSystem)
	void SetAILoggingMainThreadId(DWORD id);

	// Process any pending StopCombatAlarm requests queued from other threads
	void ProcessPendingStopCombatAlarms();
	
	// Clear all alarm cooldowns (call on game load/reset)
	void ClearAlarmCooldowns();
	
	// ============================================
	// Disengage Queue System (for multi-rider scenarios)
	// ============================================
	
	// Process queued disengages - call this from the main update loop
	void ProcessQueuedDisengages();
	
	// Clear the disengage queue (call on game load/reset)
	void ClearDisengageQueue();

	// ============================================
	// Mount Obstruction Detection & Logging
	// ============================================
	
	// Obstruction state for a horse
	enum class ObstructionType
	{
		None = 0,
		Stationary, // Not moving but should be
		RunningInPlace, // Animating but not moving
		CollisionBlocked, // Blocked by geometry/actors
		PathfindingFailed // Can't find path to target
	};
	
	// Which side the obstruction is on
	enum class ObstructionSide
	{
		Unknown = 0,
		Front, // Directly ahead
		Left, // Left side blocked
		Right, // Right side blocked
		Both // Both sides blocked
	};
	
	struct HorseObstructionInfo
	{
		UInt32 horseFormID;
		ObstructionType type;
		ObstructionSide side; // Which side is obstructed
		float stuckDuration; // How long has it been stuck
		float lastMovementTime; // When it last moved significantly
		NiPoint3 lastPosition; // Last known good position
		NiPoint3 intendedDirection; // Where it's trying to go
		int stuckCount; // How many times stuck this session
		bool isValid;
	};
	
	// Check if horse is obstructed and log details
	// Returns the obstruction type (None if not obstructed)
	ObstructionType CheckAndLogHorseObstruction(Actor* horse, Actor* target, float distanceToTarget);
	
	// Get detailed obstruction info for a horse
	HorseObstructionInfo* GetHorseObstructionInfo(UInt32 horseFormID);
	
	// Get which side the obstruction is on for a horse
	ObstructionSide GetObstructionSide(UInt32 horseFormID);
	
	// Clear obstruction tracking for a horse
	void ClearHorseObstructionInfo(UInt32 horseFormID);
	
	// Clear all obstruction tracking
	void ClearAllObstructionInfo();
	
	// Log detailed obstruction diagnostic
	void LogObstructionDiagnostic(Actor* horse, Actor* target, ObstructionType type, ObstructionSide side);

	// ============================================
	// Sheer Drop Detection
	// ============================================
	
	bool CheckAndLogSheerDrop(Actor* horse);
	
	bool IsHorseNearSheerDrop(UInt32 horseFormID);
}
