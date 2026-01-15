#pragma once

#include "skse64/GameReferences.h"

namespace MountedNPCCombatVR
{
	// ============================================
	// TACTICAL FLEE SYSTEM
	// ============================================
	// Mounted riders (excluding companions, captains, leaders, mages)
	// have a 15% chance every 5 seconds to tactically retreat
	// when below 30% health.
	// 
	// Flee lasts 4-10 seconds (randomized), then they return to combat.
	// Only ONE rider can flee at a time to prevent mass retreats.
	// ============================================
	
	// Initialize tactical flee system
	void InitTacticalFlee();
	
	// Shutdown tactical flee system
	void ShutdownTacticalFlee();
	
	// Reset all tactical flee state (on game load)
	void ResetTacticalFlee();
	
	// Start tactical flee for a rider
	// Returns true if flee was started, false if not eligible or another rider is fleeing
	bool StartTacticalFlee(Actor* rider, Actor* horse, Actor* target);
	
	// Stop tactical flee and return to combat
	void StopTacticalFlee(UInt32 riderFormID);
	
	// Check if rider should start fleeing (based on health, cooldown, and chance)
	// Returns true if flee was triggered
	bool CheckAndTriggerTacticalFlee(Actor* rider, Actor* horse, Actor* target);
	
	// Update tactical flee state - call every frame from main update loop
	void UpdateTacticalFlee();
	
	// Query functions
	bool IsRiderFleeing(UInt32 riderFormID);
	bool IsAnyRiderFleeing();
	UInt32 GetFleeingRiderFormID();
	float GetFleeTimeRemaining(UInt32 riderFormID);
	
	// Check if a horse's rider is fleeing (includes both tactical and civilian flee)
	bool IsHorseRiderFleeing(UInt32 horseFormID);

	// ============================================
	// CIVILIAN FLEE SYSTEM
	// ============================================
	// Mounted civilians (merchants, traders, caravans, etc.) 
	// will flee from any threat until they reach a safe distance
	// of 2000 units, then reset to default AI behavior.
	// 
	// NO combat logic is applied to civilians - only flee package.
	// ============================================
	
	// Initialize civilian flee system
	void InitCivilianFlee();
	
	// Shutdown civilian flee system
	void ShutdownCivilianFlee();
	
	// Reset civilian flee state (on game load)
	void ResetCivilianFlee();
	
	// Check if a rider is a civilian and should flee
	// Returns true if civilian flee was started or is already active
	bool ProcessCivilianMountedNPC(Actor* rider, Actor* horse, Actor* threat);
	
	// Start civilian flee
	bool StartCivilianFlee(Actor* rider, Actor* horse, Actor* threat);
	
	// Stop civilian flee
	// resetToDefaultAI: if true, clears combat state and returns to normal AI
	void StopCivilianFlee(UInt32 riderFormID, bool resetToDefaultAI);
	
	// Update all fleeing civilians - call every frame
	void UpdateCivilianFlee();
	
	// Check if a civilian is currently fleeing
	bool IsCivilianFleeing(UInt32 riderFormID);

	// ============================================
	// Legacy Namespace - For compatibility
	// ============================================

	namespace CivilianFlee
	{
		// Initialize flee behavior system (both tactical and civilian)
		void InitFleeingBehavior();
		
		// Shutdown flee behavior system
		void ShutdownFleeingBehavior();
	}
}
