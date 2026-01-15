#include "MultiMountedCombat.h"
#include "DynamicPackages.h"
#include "WeaponDetection.h"
#include "ArrowSystem.h"
#include "CombatStyles.h"
#include "SpecialMovesets.h"
#include "MountedCombat.h"
#include "CompanionCombat.h"
#include "MagicCastingSystem.h"
#include "AILogging.h"
#include "skse64/GameRTTI.h"
#include <cmath>

namespace MountedNPCCombatVR
{
	// ============================================
	// MULTI MOUNTED COMBAT
	// ============================================
	
	// ============================================
	// RANGED COMBAT ROLE CONFIGURATION
	// ============================================
	// Note: These values are now in config.h:
	// - RangedRoleMinDistance (500.0f default) - If closer, switch to melee
	// - RangedRoleIdealDistance (800.0f default) - Hold position here
	// - RangedRoleMaxDistanceMin (1200.0f default) - Minimum for randomized max distance
	// - RangedRoleMaxDistanceMax (1600.0f default) - Maximum for randomized max distance
	// - RangedPositionTolerance (100.0f default) - How close to ideal is "close enough"
	// - RangedFireMinDistance (300.0f default) - Minimum distance to fire bow
	// - RangedFireMaxDistance (2800.0f default) - Maximum distance to fire bow
	// - RoleCheckInterval (2.0f default) - Time in seconds to re-check roles
	//
	// NOTE: RangedRoleMaxDistance is now RANDOMIZED per rider between Min and Max
	// to prevent synchronized movement patterns and stale gameplay.
	
	const int MAX_MULTI_RIDERS = 4;     // Maximum number of multi riders supported
	
	// Horse Animation FormIDs (from Skyrim.esm)
	const UInt32 HORSE_MOVE_FORWARD = 0x000E0FA1;
	const UInt32 HORSE_MOVE_START = 0x000E09F9;
	const UInt32 HORSE_MOVE_STOP = 0x000E0FA0;
	const UInt32 HORSE_TURN_LEFT_180 = 0x00057023;
	const UInt32 HORSE_TURN_RIGHT_180 = 0x00057024;
	const UInt32 HORSE_TURN_RIGHT_90 = 0x00057028;
	const UInt32 HORSE_TURN_LEFT_90 = 0x00057029;
	
	// ============================================
	// RANGED HORSE STATE
	// ============================================
	
	enum class RangedHorseState
	{
		None,
		Stationary,      // At ideal distance, holding position, rotating to face target
		MovingCloser,  // Too far, moving toward target
		Turning   // Executing a turn animation
	};
	
	// ============================================
	// SYSTEM STATE
	// ============================================
	
	static bool g_multiCombatInitialized = false;
	static MultiRiderData g_multiRiders[MAX_MULTI_RIDERS];
	static int g_multiRiderCount = 0;
	static float g_lastPromotionCheckTime = 0.0f;
	
	// Ranged horse state tracking
	struct RangedHorseData
	{
		UInt32 horseFormID;
		RangedHorseState state;
		float stateStartTime;
		float lastAnimationTime;
		bool isMoving;
		bool isValid;
		
		void Reset()
		{
			horseFormID = 0;
			state = RangedHorseState::None;
			stateStartTime = 0;
			lastAnimationTime = 0;
			isMoving = false;
			isValid = false;
		}
	};
	
	static RangedHorseData g_rangedHorseData[MAX_MULTI_RIDERS];
	
	// ============================================
	// FORWARD DECLARATIONS
	// ============================================
	
	static UInt32 FindFurthestMeleeRider();
	static void PromoteFurthestToRanged();
	static RangedHorseData* GetOrCreateRangedHorseData(UInt32 horseFormID);
	static void SwitchCaptainToMelee(MultiRiderData* data, Actor* rider, Actor* horse);
	static void SwitchCaptainToRanged(MultiRiderData* data, Actor* rider, Actor* horse);
	
	// ============================================
	// UTILITY FUNCTIONS
	// ============================================
	
	// Use shared GetGameTime() from Helper.h instead of local function
	static float GetMultiCombatTime()
	{
		return GetGameTime();
	}
	
	static float CalculateDistance(Actor* a, Actor* b)
	{
		if (!a || !b) return 99999.0f;
		float dx = a->pos.x - b->pos.x;
		float dy = a->pos.y - b->pos.y;
		return sqrt(dx * dx + dy * dy);
	}
	
	// Generate a randomized RangedRoleMaxDistance value between min and max
	// Each rider gets a unique value to prevent synchronized behavior
	static float GenerateRandomRangedMaxDistance()
	{
		EnsureRandomSeeded();
		float range = RangedRoleMaxDistanceMax - RangedRoleMaxDistanceMin;
		float randomValue = (float)rand() / (float)RAND_MAX;
		float result = RangedRoleMaxDistanceMin + (randomValue * range);
		_MESSAGE("MultiMountedCombat: Generated random RangedMaxDistance: %.0f (range: %.0f - %.0f)",
			result, RangedRoleMaxDistanceMin, RangedRoleMaxDistanceMax);
		return result;
	}
	
	// Generate a randomized max distance for mages (shorter range than archers)
	static float GenerateRandomMageMaxDistance()
	{
		EnsureRandomSeeded();
		float range = MageRangedMaxDistanceMax - MageRangedMaxDistanceMin;
		float randomValue = (float)rand() / (float)RAND_MAX;
		float result = MageRangedMaxDistanceMin + (randomValue * range);
		_MESSAGE("MultiMountedCombat: Generated random MageMaxDistance: %.0f (range: %.0f - %.0f)",
			result, MageRangedMaxDistanceMin, MageRangedMaxDistanceMax);
		return result;
	}
	
	// Play a horse idle animation
	static bool PlayHorseAnimation(Actor* horse, UInt32 idleFormID)
	{
		if (!horse) return false;
		
		TESForm* idleForm = LookupFormByID(idleFormID);
		if (!idleForm) return false;
		
		TESIdleForm* idle = DYNAMIC_CAST(idleForm, TESForm, TESIdleForm);
		if (!idle) return false;
		
		if (!idle->animationEvent.c_str() || strlen(idle->animationEvent.c_str()) == 0)
			return false;
		
		BSFixedString eventName(idle->animationEvent.c_str());
		return get_vfunc<bool (*)(IAnimationGraphManagerHolder*, const BSFixedString&)>(&horse->animGraphHolder, 0x1)(&horse->animGraphHolder, eventName);
	}
	
	// Stop horse movement - use sprint stop and clear AI
	static void StopHorseMovement(Actor* horse)
	{
		if (!horse) return;
		
		RangedHorseData* data = GetOrCreateRangedHorseData(horse->formID);
		if (data && data->isMoving)
		{
			// Use SingleMountedCombat's sprint stop
			StopHorseSprint(horse);
			
			// Also play the move stop animation for visual feedback
			PlayHorseAnimation(horse, HORSE_MOVE_STOP);
			
			data->isMoving = false;
			_MESSAGE("MultiMountedCombat: Horse %08X STOPPED movement", horse->formID);
		}
	}
	
	// Start horse moving forward - use sprint to actually move
	static void StartHorseMovement(Actor* horse)
	{
		if (!horse) return;
		
		RangedHorseData* data = GetOrCreateRangedHorseData(horse->formID);
		if (data && !data->isMoving)
		{
			// Use SingleMountedCombat's sprint start to actually move the horse
			StartHorseSprint(horse);
			
			data->isMoving = true;
			_MESSAGE("MultiMountedCombat: Horse %08X STARTED movement (sprint)", horse->formID);
		}
	}
	
	static RangedHorseData* GetOrCreateRangedHorseData(UInt32 horseFormID)
	{
		// Find existing
		for (int i = 0; i < MAX_MULTI_RIDERS; i++)
		{
			if (g_rangedHorseData[i].isValid && g_rangedHorseData[i].horseFormID == horseFormID)
			{
				return &g_rangedHorseData[i];
			}
		}
		
		// Create new
		for (int i = 0; i < MAX_MULTI_RIDERS; i++)
		{
			if (!g_rangedHorseData[i].isValid)
			{
				g_rangedHorseData[i].horseFormID = horseFormID;
				g_rangedHorseData[i].state = RangedHorseState::None;
				g_rangedHorseData[i].stateStartTime = GetMultiCombatTime();
				g_rangedHorseData[i].lastAnimationTime = 0;
				g_rangedHorseData[i].isMoving = false;
				g_rangedHorseData[i].isValid = true;
				return &g_rangedHorseData[i];
			}
		}
		
		return nullptr;
	}
	
	// ============================================
	// INITIALIZATION
	// ============================================
	
	void InitMultiMountedCombat()
	{
		if (g_multiCombatInitialized) return;
		
		_MESSAGE("MultiMountedCombat: Initializing multi-rider combat system...");
		
		for (int i = 0; i < MAX_MULTI_RIDERS; i++)
		{
			g_multiRiders[i].Reset();
			g_rangedHorseData[i].Reset();
		}
		g_multiRiderCount = 0;
		g_lastPromotionCheckTime = 0.0f;
		
		g_multiCombatInitialized = true;
		_MESSAGE("MultiMountedCombat: System initialized (max %d riders)", MAX_MULTI_RIDERS);
	}
	
	void ShutdownMultiMountedCombat()
	{
		if (!g_multiCombatInitialized) return;
		
		_MESSAGE("MultiMountedCombat: Shutting down...");
		ClearAllMultiRiders();
		g_multiCombatInitialized = false;
	}
	
	void ClearAllMultiRiders()
	{
		_MESSAGE("MultiMountedCombat: Clearing all %d tracked riders", g_multiRiderCount);
		
		for (int i = 0; i < MAX_MULTI_RIDERS; i++)
		{
			g_multiRiders[i].Reset();
			g_rangedHorseData[i].Reset();
		}
		g_multiRiderCount = 0;
		g_lastPromotionCheckTime = 0.0f;
		
		// Reset initialization flag so system can be re-initialized
		g_multiCombatInitialized = false;
	}
	
	// ============================================
	// ROLE ASSIGNMENT HELPERS
	// ============================================
	
	static UInt32 FindFurthestMeleeRider()
	{
		UInt32 furthestRider = 0;
		float furthestDistance = 0.0f;
		
		for (int i = 0; i < MAX_MULTI_RIDERS; i++)
		{
			if (g_multiRiders[i].isValid && g_multiRiders[i].role == MultiCombatRole::Melee)
			{
				if (g_multiRiders[i].distanceToTarget > furthestDistance)
				{
					furthestDistance = g_multiRiders[i].distanceToTarget;
					furthestRider = g_multiRiders[i].riderFormID;
				}
			}
		}
		
		return furthestRider;
	}
	
	// Check if any registered rider is a Captain or Leader
	static bool HasCaptainOrLeaderRegistered()
	{
		for (int i = 0; i < MAX_MULTI_RIDERS; i++)
		{
			if (!g_multiRiders[i].isValid) continue;
			
			TESForm* riderForm = LookupFormByID(g_multiRiders[i].riderFormID);
			if (!riderForm) continue;
			
			Actor* rider = DYNAMIC_CAST(riderForm, TESForm, Actor);
			if (!rider) continue;
			
			const char* riderName = CALL_MEMBER_FN(rider, GetReferenceName)();
			if (riderName && (strstr(riderName, "Captain") != nullptr || strstr(riderName, "Leader") != nullptr))
			{
				return true;
			}
		}
		return false;
	}
	
	// Promote a melee rider to ranged role (when no captain exists)
	static void PromoteMeleeToRanged(MultiRiderData* data, Actor* rider, Actor* horse)
	{
		if (!data || !rider || !horse) return;
		
		const char* riderName = CALL_MEMBER_FN(rider, GetReferenceName)();
		
		_MESSAGE("MultiMountedCombat: ========================================");
		_MESSAGE("MultiMountedCombat: PROMOTING '%s' (%08X) TO RANGED ROLE", 
			riderName ? riderName : "Unknown", rider->formID);
		_MESSAGE("MultiMountedCombat: (No Captain present - needs ranged support)");
		_MESSAGE("MultiMountedCombat: ========================================");
		
		// Change role to ranged
		data->role = MultiCombatRole::Ranged;
		data->hasBow = true;
		
		// Give bow if they don't have one
		if (!HasBowInInventory(rider))
		{
			GiveDefaultBow(rider);
			_MESSAGE("MultiMountedCombat: Gave Hunting Bow to promoted '%s'", riderName ? riderName : "Unknown");
		}
		
		// Use centralized weapon system to switch to bow
		RequestWeaponSwitch(rider, WeaponRequest::Bow);
		
		_MESSAGE("MultiMountedCombat: Promoted '%s' bow switch requested", riderName ? riderName : "Unknown");
		
		// Initialize ranged horse data
		RangedHorseData* horseData = GetOrCreateRangedHorseData(horse->formID);
		if (horseData)
		{
			horseData->state = RangedHorseState::Stationary;
			horseData->stateStartTime = GetMultiCombatTime();
			horseData->isMoving = false;
		}
		
		// Stop horse and clear follow for stationary ranged behavior
		StopHorseSprint(horse);
		Actor_ClearKeepOffsetFromActor(horse);
		
		// Clear weapon state data for fresh start
		ClearWeaponStateData(rider->formID);
		
		_MESSAGE("MultiMountedCombat: '%s' now has FULL RANGED capabilities (same as Captain)",
			riderName ? riderName : "Unknown");
	}
	
	// ============================================
	// COUNT HOSTILE RIDERS BY FACTION/SIDE
	// Groups riders by their combat target to determine which "team" they're on
	// Returns the count of hostile riders fighting the SAME target
	// NOTE: MAGES DO NOT COUNT - they are "extra" ranged, not part of distribution
	// ============================================
	
	static int GetHostileRidersOnSameSide(UInt32 targetFormID)
	{
		int count = 0;
		for (int i = 0; i < MAX_MULTI_RIDERS; i++)
		{
			if (g_multiRiders[i].isValid && 
				!g_multiRiders[i].isCompanion &&
				!g_multiRiders[i].isMageCaster &&  // Mages don't count
				g_multiRiders[i].targetFormID == targetFormID)
			{
				count++;
			}
		}
		return count;
	}
	
	// Get count of hostile ranged riders (excludes companions AND mages)
	// Mages are always ranged but don't count toward the ranged distribution
	static int GetHostileRangedRiderCount()
	{
		int count = 0;
		for (int i = 0; i < MAX_MULTI_RIDERS; i++)
		{
			if (g_multiRiders[i].isValid && 
				!g_multiRiders[i].isCompanion &&
				!g_multiRiders[i].isMageCaster &&  // Mages don't count toward ranged distribution
				g_multiRiders[i].role == MultiCombatRole::Ranged)
			{
				count++;
			}
		}
		return count;
	}
	
	// Get count of hostile melee riders (excludes companions AND mages)
	static int GetHostileMeleeRiderCount()
	{
		int count = 0;
		for (int i = 0; i < MAX_MULTI_RIDERS; i++)
		{
			if (g_multiRiders[i].isValid && 
				!g_multiRiders[i].isCompanion &&
				!g_multiRiders[i].isMageCaster &&  // Mages don't count
				g_multiRiders[i].role == MultiCombatRole::Melee)
			{
				count++;
			}
		}
		return count;
	}
	
	// ============================================
	// RANGED ROLE DISTRIBUTION LOGIC
	// - 2-3 hostile riders: 1 ranged,  rest melee
	// - 4+ hostile riders: 2 ranged,  rest melee
	// - Captain/Leader always gets ranged priority
	// - Companions are NOT included in this count
	// ============================================
	
	static int GetDesiredRangedCount(int totalHostileRiders)
	{
		if (totalHostileRiders >= 4)
		{
			return 2;  // 4+ riders = 2 ranged, 2+ melee
		}
		else if (totalHostileRiders >= 2)
		{
			return 1;  // 2-3 riders = 1 ranged, 1-2 melee
		}
		return 0;  // 1 rider = all melee (single combat)
	}
	
	// Check and promote riders to ranged based on hostile rider count
	// - Captain/Leader is always first priority for ranged
	// - If 4+ hostile riders, promote a second rider to ranged
	// Companions are NOT included in this logic
	// ALSO: Demote ranged back to melee if we have too many ranged
	static void CheckAndPromoteToRanged()
	{
		// Count hostile (non-companion) riders
		int hostileCount = GetHostileRiderCount();
		
		// Get current hostile ranged count
		int currentHostileRanged = GetHostileRangedRiderCount();
		
		// Determine how many ranged we want
		int desiredRanged = GetDesiredRangedCount(hostileCount);
		
		// ============================================
		// CHECK IF WE HAVE TOO MANY RANGED
		// This happens when hostile riders disengage and we're left
		// with more ranged than desired (e.g., 3 riders -> 1 rider)
		// ============================================
		if (currentHostileRanged > desiredRanged)
		{
			int needToDemote = currentHostileRanged - desiredRanged;
			
			_MESSAGE("MultiMountedCombat: CheckAndPromoteToRanged - TOO MANY RANGED! Hostile: %d, CurrentRanged: %d, DesiredRanged: %d, NeedToDemote: %d",
				hostileCount, currentHostileRanged, desiredRanged, needToDemote);
			
			// Find hostile ranged riders to demote (skip companions, captains, mages)
			for (int i = 0; i < MAX_MULTI_RIDERS && needToDemote > 0; i++)
			{
				if (!g_multiRiders[i].isValid) continue;
				if (g_multiRiders[i].isCompanion) continue;  // Skip companions
				if (g_multiRiders[i].isMageCaster) continue;  // Skip mages - they stay ranged
				if (g_multiRiders[i].role != MultiCombatRole::Ranged) continue;
				
				TESForm* riderForm = LookupFormByID(g_multiRiders[i].riderFormID);
				TESForm* horseForm = LookupFormByID(g_multiRiders[i].horseFormID);
				if (!riderForm || !horseForm) continue;
				
				Actor* rider = DYNAMIC_CAST(riderForm, TESForm, Actor);
				Actor* horse = DYNAMIC_CAST(horseForm, TESForm, Actor);
				if (!rider || !horse) continue;
				
				// Check if this is a Captain/Leader - skip them (they stay ranged)
				const char* riderName = CALL_MEMBER_FN(rider, GetReferenceName)();
				if (riderName && (strstr(riderName, "Captain") != nullptr || strstr(riderName, "Leader") != nullptr))
				{
					continue;  // Captains/Leaders stay ranged
				}
				
				// Demote this rider to melee
				_MESSAGE("MultiMountedCombat: ========================================");
				_MESSAGE("MultiMountedCombat: DEMOTING '%s' (%08X) TO MELEE ROLE", 
					riderName ? riderName : "Unknown", rider->formID);
				_MESSAGE("MultiMountedCombat: (Only %d hostile rider(s) - need melee)", hostileCount);
				_MESSAGE("MultiMountedCombat: ========================================");
				
				g_multiRiders[i].role = MultiCombatRole::Melee;
				
				// Clear ranged horse state
				for (int j = 0; j < MAX_MULTI_RIDERS; j++)
				{
					if (g_rangedHorseData[j].isValid && g_rangedHorseData[j].horseFormID == horse->formID)
					{
						g_rangedHorseData[j].Reset();
						break;
					}
				}
				
				// Stop horse and request melee weapon
				StopHorseSprint(horse);
				Actor_ClearKeepOffsetFromActor(horse);
				RequestWeaponSwitch(rider, WeaponRequest::Melee);
				ClearWeaponStateData(rider->formID);
				
				// Re-inject follow package for melee behavior
				TESForm* targetForm = LookupFormByID(g_multiRiders[i].targetFormID);
				if (targetForm)
				{
					Actor* target = DYNAMIC_CAST(targetForm, TESForm, Actor);
					if (target && !target->IsDead(1))
					{
						ForceHorseCombatWithTarget(horse, target);
						SetNPCFollowTarget(rider, target);
					}
				}
				
				needToDemote--;
			}
			
			return;  // Don't promote if we just demoted
		}
		
		// ============================================
		// CHECK IF WE NEED TO PROMOTE
		// ============================================
		
		// Already have enough ranged riders
		if (currentHostileRanged >= desiredRanged)
		{
			return;
		}
		
		// Need to promote (desiredRanged - currentHostileRanged) more riders to ranged
		int needToPromote = desiredRanged - currentHostileRanged;
		
		_MESSAGE("MultiMountedCombat: CheckAndPromoteToRanged - Hostile: %d, CurrentRanged: %d, DesiredRanged: %d, NeedToPromote: %d",
			hostileCount, currentHostileRanged, desiredRanged, needToPromote);
		
		// Find hostile melee riders to promote (skip companions)
		for (int i = 0; i < MAX_MULTI_RIDERS && needToPromote > 0; i++)
		{
			if (!g_multiRiders[i].isValid) continue;
			if (g_multiRiders[i].isCompanion) continue;  // Skip companions
			if (g_multiRiders[i].role != MultiCombatRole::Melee) continue;
			
			TESForm* riderForm = LookupFormByID(g_multiRiders[i].riderFormID);
			TESForm* horseForm = LookupFormByID(g_multiRiders[i].horseFormID);
			if (!riderForm || !horseForm) continue;
			
			Actor* rider = DYNAMIC_CAST(riderForm, TESForm, Actor);
			Actor* horse = DYNAMIC_CAST(horseForm, TESForm, Actor);
			if (!rider || !horse) continue;
			
			// Promote this rider
			PromoteMeleeToRanged(&g_multiRiders[i], rider, horse);
			needToPromote--;
		}
	}

	// ============================================
	// RIDER DATA QUERY FUNCTIONS
	// ============================================
	
	MultiRiderData* GetMultiRiderData(UInt32 riderFormID)
	{
		for (int i = 0; i < MAX_MULTI_RIDERS; i++)
		{
			if (g_multiRiders[i].isValid && g_multiRiders[i].riderFormID == riderFormID)
			{
				return &g_multiRiders[i];
			}
		}
		return nullptr;
	}
	
	MultiRiderData* GetMultiRiderDataByHorse(UInt32 horseFormID)
	{
		for (int i = 0; i < MAX_MULTI_RIDERS; i++)
		{
			if (g_multiRiders[i].isValid && g_multiRiders[i].horseFormID == horseFormID)
			{
				return &g_multiRiders[i];
			}
		}
		return nullptr;
	}
	
	void UnregisterMultiRider(UInt32 riderFormID)
	{
		for (int i = 0; i < MAX_MULTI_RIDERS; i++)
		{
			if (g_multiRiders[i].isValid && g_multiRiders[i].riderFormID == riderFormID)
			{
				_MESSAGE("MultiMountedCombat: Unregistering rider %08X", riderFormID);
				
				// Clear magic spell state for this rider
				ResetMageSpellState(riderFormID);
				
				g_multiRiders[i].Reset();
				g_rangedHorseData[i].Reset();
				g_multiRiderCount--;
				return;
			}
		}
	}
	
	bool IsRiderInRangedRole(UInt32 riderFormID)
	{
		MultiRiderData* data = GetMultiRiderData(riderFormID);
		return data && data->role == MultiCombatRole::Ranged;
	}
	
	bool IsHorseRiderInRangedRole(UInt32 horseFormID)
	{
		MultiRiderData* data = GetMultiRiderDataByHorse(horseFormID);
		return data && data->role == MultiCombatRole::Ranged;
	}
	
	bool IsRiderMage(UInt32 riderFormID)
	{
		MultiRiderData* data = GetMultiRiderData(riderFormID);
		return data && data->isMageCaster;
	}
	
	int GetActiveMultiRiderCount()
	{
		return g_multiRiderCount;
	}
	
	int GetHostileRiderCount()
	{
		int count = 0;
		for (int i = 0; i < MAX_MULTI_RIDERS; i++)
		{
			if (g_multiRiders[i].isValid && !g_multiRiders[i].isCompanion)
				count++;
		}
		return count;
	}
	
	int GetCompanionRiderCount()
	{
		int count = 0;
		for (int i = 0; i < MAX_MULTI_RIDERS; i++)
		{
			if (g_multiRiders[i].isValid && g_multiRiders[i].isCompanion)
			count++;
		}
		return count;
	}
	
	int GetMeleeRiderCount()
	{
		int count = 0;
		for (int i = 0; i < MAX_MULTI_RIDERS; i++)
		{
			if (g_multiRiders[i].isValid && g_multiRiders[i].role == MultiCombatRole::Melee)
				count++;
		}
		return count;
	}
	
	int GetRangedRiderCount()
	{
		int count = 0;
		for (int i = 0; i < MAX_MULTI_RIDERS; i++)
		{
			if (g_multiRiders[i].isValid && g_multiRiders[i].role == MultiCombatRole::Ranged)
				count++;
		}
		return count;
	}

	// ============================================
	// ROLE ASSIGNMENT
	// ============================================
	
	MultiCombatRole DetermineOptimalRole(Actor* rider, Actor* target, float distanceToTarget)
	{
		return MultiCombatRole::Melee;
	}
	
	void SetRiderRole(UInt32 riderFormID, MultiCombatRole role)
	{
		MultiRiderData* data = GetMultiRiderData(riderFormID);
		if (data)
		{
			if (data->role != role)
			{
				const char* roleName = (role == MultiCombatRole::Ranged) ? "RANGED" : "MELEE";
				_MESSAGE("MultiMountedCombat: Rider %08X role changed to %s", riderFormID, roleName);
				data->role = role;
			}
		}
	}
	
	MultiCombatRole GetRiderRole(UInt32 riderFormID)
	{
		MultiRiderData* data = GetMultiRiderData(riderFormID);
		return data ? data->role : MultiCombatRole::None;
	}
	
	// ============================================
	// REGISTER MULTI RIDER - Check for Captain/Leader OR check if we need more ranged
	// NOTE: Companions are tracked separately and don't count toward hostile rider limit
	// Ranged distribution:
	// - 2-3 hostile riders: 1 ranged
	// - 4+ hostile riders: 2 ranged
	// - Captain/Leader always gets ranged priority
	// ============================================
	
	MultiCombatRole RegisterMultiRider(Actor* rider, Actor* horse, Actor* target)
	{
		if (!rider || !horse || !target) return MultiCombatRole::None;
		
		const char* riderName = CALL_MEMBER_FN(rider, GetReferenceName)();
		bool isCompanion = IsCompanion(rider);
		
		// ============================================
		// CHECK IF CAPTAIN/LEADER/MAGE FIRST
		// THESE ALWAYS GET RANGED ROLE REGARDLESS OF EXISTING REGISTRATION
		// ============================================
		bool isCaptainOrLeader = false;
		bool isMageCaster = false;
		
		if (riderName && strlen(riderName) > 0)
		{
			if (strstr(riderName, "Captain") != nullptr || strstr(riderName, "Leader") != nullptr)
			{
				isCaptainOrLeader = true;
			}
		}
		
		MountedCombatClass combatClass = DetermineCombatClass(rider);
		if (combatClass == MountedCombatClass::MageCaster)
		{
			isMageCaster = true;
		}
		
		// Check if already registered
		MultiRiderData* existing = GetMultiRiderData(rider->formID);
		if (existing)
		{
			// Update target and distance
			existing->targetFormID = target->formID;
			existing->distanceToTarget = CalculateDistance(rider, target);
			
			// ============================================
			// CRITICAL: Force ranged role for Captain/Leader/Companion
			// even if they were previously registered as melee
			// Mages also use Ranged role for positioning, but cast spells via isMageCaster flag
			// ============================================
			if ((isCaptainOrLeader || isMageCaster || isCompanion) && existing->role != MultiCombatRole::Ranged)
			{
				_MESSAGE("MultiMountedCombat: UPGRADING '%s' (%08X) to RANGED (was %s)%s",
					riderName ? riderName : "Unknown", rider->formID,
					existing->role == MultiCombatRole::Melee ? "MELEE" : "NONE",
					isMageCaster ? " [MAGE CASTER]" : "");
				existing->role = MultiCombatRole::Ranged;
				
				// Set mage flag if this is a mage
				if (isMageCaster)
				{
					existing->isMageCaster = true;
				}
				
				// Give bow if needed (not for mages - they use staff)
				if (!isMageCaster && !HasBowInInventory(rider))
				{
					GiveDefaultBow(rider);
				}
			}
			// Also ensure mage flag is set even if already in ranged role
			else if (isMageCaster && !existing->isMageCaster)
			{
				_MESSAGE("MultiMountedCombat: Setting isMageCaster flag for '%s' (%08X)",
					riderName ? riderName : "Unknown", rider->formID);
				existing->isMageCaster = true;
			}
			
			return existing->role;
		}
		
		// ============================================
		// CHECK DISTANCE - DON'T REGISTER IF TOO FAR
		// ============================================
		float distance = CalculateDistance(rider, target);
		float maxDistance = isCompanion ? MaxCompanionCombatDistance : MaxCombatDistance;
		
		// ============================================
		// CHECK IF TARGET IS TOO FAR TO REGISTER
		// NOTE: WE DO NOT REJECT HERE - LET COMBATSTYLES HANDLE APPROACH
		// ONLY REJECT IF WAY TOO FAR (E.G., 2X MAX DISTANCE)
		// THIS ALLOWS MAGES AND RANGED TO BE REGISTERED AND THEN APPROACH
		// ============================================
		const float REJECTION_DISTANCE_MULTIPLIER = 2.5f;  // 2.5x max distance = reject
		if (distance > maxDistance * REJECTION_DISTANCE_MULTIPLIER)
		{
			_MESSAGE("MultiMountedCombat: REJECTING '%s' (%08X) - target too far (%.0f > %.0f)",
				riderName ? riderName : "Unknown", rider->formID, distance, maxDistance * REJECTION_DISTANCE_MULTIPLIER);
			return MultiCombatRole::None;
		}
		
		// ============================================
		// DECLARE ROLE VARIABLES
		// ============================================
		MultiCombatRole role = MultiCombatRole::Melee;
		bool hasBow = HasBowInInventory(rider);
		
		// ============================================
		// COUNT EXISTING HOSTILE RIDERS (for role distribution)
		// ============================================
		int hostileRiderCount = 0;
		for (int i = 0; i < MAX_MULTI_RIDERS; i++)
		{
			if (g_multiRiders[i].isValid && !g_multiRiders[i].isCompanion)
			{
				hostileRiderCount++;
			}
		}
		
		// ============================================
		// FIND EMPTY SLOT
		// ============================================
		int emptySlot = -1;
		for (int i = 0; i < MAX_MULTI_RIDERS; i++)
		{
			if (!g_multiRiders[i].isValid)
			{
				emptySlot = i;
				break;
			}
		}
		
		// No empty slots and this is not a companion - reject
		if (emptySlot < 0 && !isCompanion)
		{
			_MESSAGE("MultiMountedCombat: WARNING - No slots for rider '%s' (all %d slots full)",
				riderName ? riderName : "Unknown", MAX_MULTI_RIDERS);
			return MultiCombatRole::None;
		}
		
		// ============================================
		// DETERMINE ROLE FOR THIS RIDER
		// Companions always get ranged (handled separately from hostile rider count)
		// Captain/Leader always gets ranged priority
		// Mages use Captain moveset (ranged role)
		// Otherwise, check if we need more ranged based on hostile count
		// ============================================
		
		bool shouldBeRanged = false;
		
		if (isCompanion)
		{
			// Companions always get ranged (separate from hostile logic)
			shouldBeRanged = true;
			_MESSAGE("MultiMountedCombat: *** COMPANION DETECTED: '%s' (%08X) - FORCING RANGED ***", 
				riderName ? riderName : "Unknown", rider->formID);
		}
		else if (isCaptainOrLeader)
		{
			// Captain/Leader always gets ranged priority
			shouldBeRanged = true;
			_MESSAGE("MultiMountedCombat: *** CAPTAIN/LEADER DETECTED: '%s' (%08X) - FORCING RANGED ***", 
				riderName ? riderName : "Unknown", rider->formID);
		}
		else if (isMageCaster)
		{
			// Mages use Captain moveset - always ranged positioning
			// BUT they use staff, not bow - and NEVER switch weapons
			shouldBeRanged = true;
			// Already logged above
		}
		else
		{
			// Regular hostile rider - check if we need more ranged
			// Count will include this new rider
			int newHostileCount = hostileRiderCount + 1;
			int desiredRanged = GetDesiredRangedCount(newHostileCount);
			int currentHostileRanged = GetHostileRangedRiderCount();
			
			if (currentHostileRanged < desiredRanged)
			{
				// We need more ranged riders
				shouldBeRanged = true;
				_MESSAGE("MultiMountedCombat: '%s' (%08X) assigned RANGED (hostile: %d, ranged: %d/%d)", 
					riderName ? riderName : "Unknown", rider->formID, 
					newHostileCount, currentHostileRanged + 1, desiredRanged);
			}
			else
			{
				_MESSAGE("MultiMountedCombat: '%s' (%08X) assigned MELEE (hostile: %d, ranged: %d/%d)", 
					riderName ? riderName : "Unknown", rider->formID,
					newHostileCount, currentHostileRanged, desiredRanged);
			}
		}
		
		if (shouldBeRanged)
		{
			role = MultiCombatRole::Ranged;
			hasBow = true;
			
			// ============================================
			// MAGE CASTERS GET WARSTAFF INSTEAD OF BOW
			// MAGERS USE STAFF COMBAT - NOT BOW COMBAT
			// ============================================
			if (isMageCaster)
			{
				// Give warstaff if they don't have one
				if (!HasStaffInInventory(rider))
				{
					GiveWarstaff(rider);
					_MESSAGE("MultiMountedCombat: Gave Warstaff to Mage '%s'", riderName ? riderName : "Unknown");
				}
				
				// Use centralized weapon system to switch to staff
				RequestWeaponSwitch(rider, WeaponRequest::Staff);
			}
			else
			{
				// Non-mage ranged riders get bow
				if (!HasBowInInventory(rider))
				{
					GiveDefaultBow(rider);
					_MESSAGE("MultiMountedCombat: Gave Hunting Bow to '%s'", riderName ? riderName : "Unknown");
				}
				
				// Use centralized weapon system to switch to bow
				RequestWeaponSwitch(rider, WeaponRequest::Bow);
			}
			
			// Initialize ranged horse data
			RangedHorseData* horseData = GetOrCreateRangedHorseData(horse->formID);
			if (horseData)
			{
				horseData->state = RangedHorseState::Stationary;
				horseData->stateStartTime = GetMultiCombatTime();
				horseData->isMoving = false;
			}
			
			// Stop horse and clear follow for stationary ranged behavior
			StopHorseSprint(horse);
			Actor_ClearKeepOffsetFromActor(horse);
		}
		
		g_multiRiders[emptySlot].riderFormID = rider->formID;
		g_multiRiders[emptySlot].horseFormID = horse->formID;
		g_multiRiders[emptySlot].targetFormID = target->formID;
		g_multiRiders[emptySlot].role = role;
		g_multiRiders[emptySlot].distanceToTarget = distance;
		g_multiRiders[emptySlot].lastRoleCheckTime = GetMultiCombatTime();
		g_multiRiders[emptySlot].lastRangedSetupTime = 0.0f;
		// Use mage-specific distance for mages, regular for others
		g_multiRiders[emptySlot].rangedMaxDistance = isMageCaster ? GenerateRandomMageMaxDistance() : GenerateRandomRangedMaxDistance();
		g_multiRiders[emptySlot].hasBow = hasBow;
		g_multiRiders[emptySlot].isCompanion = isCompanion;
		g_multiRiders[emptySlot].isMageCaster = isMageCaster;  // Mages never switch to melee
		g_multiRiders[emptySlot].isValid = true;
		g_multiRiderCount++;
		
		const char* roleName = (role == MultiCombatRole::Ranged) ? "RANGED" : "MELEE";
		const char* typeStr = isCompanion ? " (COMPANION)" : "";
		_MESSAGE("MultiMountedCombat: Registered '%s' (%08X) as %s%s - distance: %.0f, maxDist: %.0f",
			riderName ? riderName : "Unknown", rider->formID, roleName, typeStr, distance, 
			g_multiRiders[emptySlot].rangedMaxDistance);
		
		return role;
	}
	
	// ============================================
	// SWITCH CAPTAIN TO MELEE (player got too close)
	// ============================================
	
	// Cooldown for role switching to prevent spam
	static float g_lastRoleSwitchTime = 0.0f;
	const float ROLE_SWITCH_COOLDOWN = 5.0f;  // 5 seconds between role switches
	
	static void SwitchCaptainToMelee(MultiRiderData* data, Actor* rider, Actor* horse)
	{
		if (!data || !rider || !horse) return;
		
		// Check cooldown to prevent rapid switching
		float currentTime = GetMultiCombatTime();
		if ((currentTime - g_lastRoleSwitchTime) < ROLE_SWITCH_COOLDOWN)
		{
			return;  // On cooldown, don't switch
		}
		g_lastRoleSwitchTime = currentTime;
		
		const char* riderName = CALL_MEMBER_FN(rider, GetReferenceName)();
		_MESSAGE("MultiMountedCombat: CAPTAIN '%s' SWITCHING TO MELEE (target too close)", 
			riderName ? riderName : "Unknown");
		
		// Change role to melee
		data->role = MultiCombatRole::Melee;
		
		// Clear ranged horse state
		for (int j = 0; j < MAX_MULTI_RIDERS; j++)
		{
			if (g_rangedHorseData[j].isValid && g_rangedHorseData[j].horseFormID == horse->formID)
			{
				g_rangedHorseData[j].Reset();
				break;
			}
		}
		
		// Clear any keep-offset so patrol package can take over
		Actor_ClearKeepOffsetFromActor(horse);
		
		// Reset bow attack state
		ResetBowAttackState(rider->formID);
		
		// Use centralized weapon system to switch to melee
		RequestWeaponSwitch(rider, WeaponRequest::Melee);
		
		// Clear weapon state data for fresh start
		ClearWeaponStateData(rider->formID);
		
		_MESSAGE("MultiMountedCombat: Captain '%s' melee switch requested", riderName ? riderName : "Unknown");
		
		// Re-inject follow package
		Actor* target = nullptr;
		TESForm* targetForm = LookupFormByID(data->targetFormID);
		if (targetForm)
		{
			target = DYNAMIC_CAST(targetForm, TESForm, Actor);
		}
		
		if (target)
		{
			ForceHorseCombatWithTarget(horse, target);
			SetNPCFollowTarget(rider, target);
			_MESSAGE("MultiMountedCombat: Captain '%s' re-injected follow package for MELEE combat", 
				riderName ? riderName : "Unknown");
		}
		
		// START SPRINT to close distance quickly
		StartHorseSprint(horse);
	}
	
	// ============================================
	// SWITCH CAPTAIN BACK TO RANGED (player retreated to safe distance)
	// ============================================
	
	static void SwitchCaptainToRanged(MultiRiderData* data, Actor* rider, Actor* horse)
	{
		if (!data || !rider || !horse) return;
		
		// Check cooldown to prevent rapid switching
		float currentTime = GetMultiCombatTime();
		if ((currentTime - g_lastRoleSwitchTime) < ROLE_SWITCH_COOLDOWN)
		{
			return;  // On cooldown, don't switch
		}
		g_lastRoleSwitchTime = currentTime;
		
		const char* riderName = CALL_MEMBER_FN(rider, GetReferenceName)();
		_MESSAGE("MultiMountedCombat: CAPTAIN '%s' RETURNING TO RANGED (target retreated)", 
			riderName ? riderName : "Unknown");
		
		// Change role back to ranged
		data->role = MultiCombatRole::Ranged;
		
		// STOP SPRINT - Captain will be stationary now
		StopHorseSprint(horse);
		
		// Re-initialize ranged horse data
		RangedHorseData* horseData = GetOrCreateRangedHorseData(horse->formID);
		if (horseData)
		{
			horseData->state = RangedHorseState::Stationary;
			horseData->stateStartTime = GetMultiCombatTime();
			horseData->isMoving = false;
		}
		
		// Clear any existing follow offset so Captain stays stationary
		Actor_ClearKeepOffsetFromActor(horse);
		
		// Use centralized weapon system - bow will be equipped based on distance
		RequestWeaponSwitch(rider, WeaponRequest::Bow);
		
		_MESSAGE("MultiMountedCombat: Captain '%s' ranged switch requested", riderName ? riderName : "Unknown");
	}
	
	// ============================================
	// RANGED ROLE BEHAVIOR - STATE MACHINE
	// ============================================
	
	void ExecuteRangedRoleBehavior(MultiRiderData* data, Actor* rider, Actor* horse, Actor* target)
	{
		if (!data || !rider || !horse || !target) return;
		
		// Safety check: if somehow called when not in ranged role, exit immediately
		if (data->role != MultiCombatRole::Ranged) return;
		
		float currentTime = GetMultiCombatTime();
		float distance = data->distanceToTarget;
		
		// Use the per-rider randomized max distance
		float rangedMaxDistance = data->rangedMaxDistance;
		
		// Get or create ranged horse state data
		RangedHorseData* horseData = GetOrCreateRangedHorseData(horse->formID);
		if (!horseData) return;
		
		// ============================================
		// CHECK IF IN RAPID FIRE - DynamicPackages handles this
		// Just return early and let DynamicPackages manage the rapid fire
		// ============================================
		if (IsInRapidFire(horse->formID))
		{
			return;
		}
		
		// ============================================
		// HYSTERESIS VALUES - Prevent oscillation between states
		// ============================================
		const float APPROACH_HYSTERESIS = 50.0f;
		
		// ============================================
		// CHECK IF TARGET IS TOO CLOSE
		// MAGES: Stand their ground - NO retreating, just stay stationary
		// CAPTAINS/OTHERS: Switch to melee role
		// ============================================
		if (distance < RangedRoleMinDistance)
		{
			// MAGES: Stand ground - ensure stationary state
			if (data->isMageCaster)
			{
				if (horseData->isMoving)
				{
					StopHorseSprint(horse);
					Actor_ClearKeepOffsetFromActor(horse);
					horseData->isMoving = false;
				}
				horseData->state = RangedHorseState::Stationary;
				
				// Still rotate to face target
				float dx = target->pos.x - horse->pos.x;
				float dy = target->pos.y - horse->pos.y;
				float angleToTarget = atan2(dx, dy);
				float currentAngle = horse->rot.z;
				float angleDiff = angleToTarget - currentAngle;
				
				while (angleDiff > 3.14159f) angleDiff -= 6.28318f;
				while (angleDiff < -3.14159f) angleDiff += 6.28318f;
				
				const float ROTATION_SPEED = 0.25f;
				float newAngle = currentAngle + (angleDiff * ROTATION_SPEED);
				while (newAngle > 3.14159f) newAngle -= 6.28318f;
				while (newAngle < -3.14159f) newAngle += 6.28318f;
				horse->rot.z = newAngle;
				
				// ============================================
				// CLOSE RANGE MAGE BEHAVIOR
				// Use concentration spells (Flames, Sparks, Frostbite) at close range
				// ============================================
				
				// Check angle before casting - need to be roughly facing target
				if (fabs(angleDiff) < 0.6f)  // ~35 degrees tolerance for concentration
				{
					// Cast concentration spell at target
					UpdateMageSpellCasting(rider, target, distance);
				}
				
				return;  // Exit - mage is standing ground with concentration spells
			}
			
			// NON-MAGE: Switch to melee
			static UInt32 lastSwitchAttemptRider = 0;
			static float lastSwitchAttemptTime = 0;
			
			if (data->riderFormID == lastSwitchAttemptRider && 
				(currentTime - lastSwitchAttemptTime) < 5.0f)
			{
				return;
			}
			
			lastSwitchAttemptRider = data->riderFormID;
			lastSwitchAttemptTime = currentTime;
			
			_MESSAGE("MultiMountedCombat: Ranged rider %08X too close (%.0f < %.0f) - switching to MELEE",
				data->riderFormID, distance, RangedRoleMinDistance);
			
			SwitchCaptainToMelee(data, rider, horse);
			return;
		}
		
		// NOTE: Bow equipping handled by centralized system in DynamicPackages
		
		// ============================================
		// CALCULATE ANGLE TO TARGET
		// ============================================
		float dx = target->pos.x - horse->pos.x;
		float dy = target->pos.y - horse->pos.y;
		float angleToTarget = atan2(dx, dy);
		
		float currentAngle = horse->rot.z;
		float angleDiff = angleToTarget - currentAngle;
		
		while (angleDiff > 3.14159f) angleDiff -= 6.28318f;
		while (angleDiff < -3.14159f) angleDiff += 6.28318f;
		
		// Rotate horse to face target
		const float TURN_SPEED = 0.2f;
		float turnAngle = angleDiff * TURN_SPEED;
		
		// Clamp turn angle to prevent flipping
		if (turnAngle > 0.3f) turnAngle = 0.3f;
		if (turnAngle < -0.3f) turnAngle = -0.3f;
		
		horse->rot.z = currentAngle + turnAngle;
		
		// ============================================
		// MOVE TOWARD TARGET IF TOO FAR
		// Both mages and archers use their rangedMaxDistance
		// (which is already randomized based on their type during registration)
		// ============================================
		if (distance > data->rangedMaxDistance)
		{
			if (!horseData->isMoving)
			{
				_MESSAGE("MultiMountedCombat: %s rider %08X moving closer (%.0f > %.0f)",
					data->isMageCaster ? "Mage" : "Ranged", data->riderFormID, distance, data->rangedMaxDistance);
				horseData->state = RangedHorseState::MovingCloser;
				StopHorseSprint(horse);
				StartHorseMovement(horse);
			}
			
			// Keep moving closer until within ideal range
			// Mages use a slightly shorter ideal range (80% of their max)
			float idealRange = data->isMageCaster ? (data->rangedMaxDistance * 0.8f) : RangedRoleIdealDistance;
			if (distance > idealRange)
			{
				float moveX = target->pos.x - horse->pos.x;
				float moveY = target->pos.y - horse->pos.y;
				float moveAngle = atan2(moveX, moveY);
				
				horse->pos.x += cos(moveAngle) * 10.0f;
				horse->pos.y += sin(moveAngle) * 10.0f;
			}
			
			return;
		}

		// ============================================
		// WITHIN IDEAL RANGE - HOLD POSITION AND ROTATE
		// ============================================
		horseData->state = RangedHorseState::Stationary;
		StopHorseMovement(horse);
		
		// ============================================
		// FIRE BOW IF TARGET IN RANGE (or cast spell for mages)
		// ============================================
		if (fabs(angleDiff) < 0.35f)  // Within ~20 degrees of facing target
		{
			if (data->isMageCaster)
			{
				// Mages cast fire-and-forget spells at range
				UpdateMageSpellCasting(rider, target, distance);
			}
			else
			{
				// Archers fire arrows
				UpdateBowAttack(rider, true, target);
			}
		}
	}
	
	// ============================================
	// EXECUTE MELEE ROLE BEHAVIOR
	// ============================================
	
	void ExecuteMeleeRoleBehavior(MultiRiderData* data, Actor* rider, Actor* horse, Actor* target)
	{
		if (!data || !rider || !horse || !target) return;
		
		// Melee riders use the standard combat follow package
		// They close distance and attack when in range
		// This is handled by the main mounted combat system
	}
	
	// ============================================
	// UPDATE MULTI RIDER BEHAVIOR
	// ============================================
	
	int UpdateMultiRiderBehavior(Actor* horse, Actor* target)
	{
		if (!horse || !target) return 0;
		
		MultiRiderData* data = GetMultiRiderDataByHorse(horse->formID);
		if (!data || !data->isValid) return 0;
		
		// Get rider
		TESForm* riderForm = LookupFormByID(data->riderFormID);
		if (!riderForm) return 0;
		
		Actor* rider = DYNAMIC_CAST(riderForm, TESForm, Actor);
		if (!rider || rider->IsDead(1)) return 0;
		
		// Update distance
		float dx = target->pos.x - horse->pos.x;
		float dy = target->pos.y - horse->pos.y;
		data->distanceToTarget = sqrt(dx * dx + dy * dy);
		
		// Execute role-specific behavior
		switch (data->role)
		{
			case MultiCombatRole::Ranged:
				ExecuteRangedRoleBehavior(data, rider, horse, target);
				return 3;  // Ranged state
				
			case MultiCombatRole::Melee:
				ExecuteMeleeRoleBehavior(data, rider, horse, target);
				if (data->distanceToTarget < MeleeRangeOnFoot)
					return 2;  // Attack position
				return 1;  // Traveling to melee
				
			default:
				return 0;
		}
	}
	
	// ============================================
	// UPDATE MULTI MOUNTED COMBAT
	// ============================================
	
	void UpdateMultiMountedCombat()
	{
		float currentTime = GetGameTime();
		
		for (int i = 0; i < MAX_MULTI_RIDERS; i++)
		{
			if (!g_multiRiders[i].isValid) continue;
			
			MultiRiderData* data = &g_multiRiders[i];
			
			// Get actors
			TESForm* riderForm = LookupFormByID(data->riderFormID);
			TESForm* horseForm = LookupFormByID(data->horseFormID);
			TESForm* targetForm = LookupFormByID(data->targetFormID);
			
			if (!riderForm || !horseForm || !targetForm)
			{
				data->Reset();
				continue;
			}
			
			Actor* rider = DYNAMIC_CAST(riderForm, TESForm, Actor);
			Actor* horse = DYNAMIC_CAST(horseForm, TESForm, Actor);
			Actor* target = DYNAMIC_CAST(targetForm, TESForm, Actor);
			
			if (!rider || !horse || !target)
			{
				data->Reset();
				continue;
			}
			
			if (rider->IsDead(1) || target->IsDead(1))
			{
				data->Reset();
				continue;
			}
			
			// Update distance
			float dx = target->pos.x - horse->pos.x;
			float dy = target->pos.y - horse->pos.y;
			data->distanceToTarget = sqrt(dx * dx + dy * dy);
			
			// Check if target is too far (disengage)
			float maxDist = data->isCompanion ? MaxCompanionCombatDistance : MaxCombatDistance;
			if (data->distanceToTarget > maxDist)
			{
				_MESSAGE("MultiMountedCombat: Rider %08X target too far (%.0f > %.0f), disengaging",
					data->riderFormID, data->distanceToTarget, maxDist);
				
				// ============================================
				// CLEANUP FOR DISENGAGING RIDER (INCLUDING MAGES)
				// ============================================
				
				// Clear magic spell state for mages
				ResetMageSpellState(data->riderFormID);
				
				// Clear weapon state data
				ClearWeaponStateData(data->riderFormID);
				
				// Sheathe weapon (also stops mage casting animations)
				if (IsWeaponDrawn(rider))
				{
					rider->DrawSheatheWeapon(false);
				}
				
				// Stop combat for the rider
				StopActorCombatAlarm(rider);
				
				// Stop horse sprint
				StopHorseSprint(horse);
				
				// Clear horse combat state
				horse->currentCombatTarget = 0;
				horse->flags2 &= ~Actor::kFlag_kAttackOnSight;
				
				// Clear special movesets for the horse
				ClearAllMovesetData(horse->formID);
				
				// Force AI re-evaluation
				Actor_EvaluatePackage(horse, false, false);
				
				// Add to disengage cooldown
				AddNPCToDisengageCooldown(data->riderFormID);
				
				data->Reset();
				continue;
			}
			
			// Re-evaluate role periodically
			if ((currentTime - data->lastRoleCheckTime) >= RoleCheckInterval)
			{
				data->lastRoleCheckTime = currentTime;
				
				// Mages never change role
				if (!data->isMageCaster)
				{
					MultiCombatRole newRole = DetermineOptimalRole(rider, target, data->distanceToTarget);
					if (newRole != data->role)
					{
						_MESSAGE("MultiMountedCombat: Rider %08X role change: %d -> %d",
							data->riderFormID, (int)data->role, (int)newRole);
						data->role = newRole;
					}
				}
			}
			
			// Execute role behavior
			switch (data->role)
			{
				case MultiCombatRole::Ranged:
					ExecuteRangedRoleBehavior(data, rider, horse, target);
					break;
					
				case MultiCombatRole::Melee:
					ExecuteMeleeRoleBehavior(data, rider, horse, target);
					break;
					
				default:
					break;
			}
		}
	}
}
