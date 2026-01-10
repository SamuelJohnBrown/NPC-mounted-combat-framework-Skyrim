#include "MultiMountedCombat.h"
#include "DynamicPackages.h"
#include "WeaponDetection.h"
#include "ArrowSystem.h"
#include "CombatStyles.h"
#include "SpecialMovesets.h"
#include "SingleMountedCombat.h"
#include "CompanionCombat.h"
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
	// - RangedRoleMaxDistance (1400.0f default) - If further, move closer
	// - RangedPositionTolerance (100.0f default) - How close to ideal is "close enough"
	// - RangedFireMinDistance (300.0f default) - Minimum distance to fire bow
	// - RangedFireMaxDistance (2800.0f default) - Maximum distance to fire bow
	// - RoleCheckInterval (2.0f default) - Time in seconds to re-check roles
	
	const int MAX_MULTI_RIDERS = 5;           // Maximum number of multi riders supported
	
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
		MovingCloser,    // Too far, moving toward target
		Turning   // Executing a turn animation
	};
	
	// ============================================
	// SYSTEM STATE
	// ============================================
	
	static bool g_multiCombatInitialized = false;
	static MultiRiderData g_multiRiders[MAX_MULTI_RIDERS];
	static int g_multiRiderCount = 0;
	static float g_lastPromotionCheckTime = 0.0f;
	
	// ============================================
	// TEMPORARY RANGED MODE TRACKING
	// Melee riders can temporarily switch to ranged (25% chance every 25 sec)
	// ============================================
	
	struct TempRangedData
	{
		UInt32 riderFormID;
		float startTime;           // When temp ranged started
		float duration;            // How long to stay ranged (35-70 sec)
		bool gaveBow;    // True if we gave them a bow (need to remove it)
		bool isValid;
		
		void Reset()
		{
			riderFormID = 0;
			startTime = 0;
			duration = 0;
			gaveBow = false;
			isValid = false;
		}
	};
	
	static TempRangedData g_tempRangedData[MAX_MULTI_RIDERS];
	static float g_lastTempRangedCheckTime = 0.0f;
	static float g_lastTempRangedTriggerTime = 0.0f;  // Cooldown tracker
	
	const float TEMP_RANGED_CHECK_INTERVAL = 25.0f;    // Check every 25 seconds
	const float TEMP_RANGED_COOLDOWN = 60.0f;    // 60 second cooldown after a temp ranged ends
	const int TEMP_RANGED_CHANCE_PERCENT = 25;         // 25% chance
	const float TEMP_RANGED_MIN_DURATION = 35.0f;      // Minimum 35 seconds
	const float TEMP_RANGED_MAX_DURATION = 70.0f;      // Maximum 70 seconds
	
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
	
	// Temporary ranged mode helpers
	static TempRangedData* GetTempRangedData(UInt32 riderFormID);
	static bool IsInTempRangedMode(UInt32 riderFormID);
	static bool TrySwitchMeleeToTempRanged(MultiRiderData* data, Actor* rider, Actor* horse);
	static void EndTempRangedMode(MultiRiderData* data, Actor* rider, Actor* horse, TempRangedData* tempData);
	static void UpdateTempRangedModes();

	// ============================================
	// UTILITY FUNCTIONS
	// ============================================
	
	static float GetMultiCombatTime()
	{
		static auto startTime = clock();
		return (float)(clock() - startTime) / CLOCKS_PER_SEC;
	}
	
	static float CalculateDistance(Actor* a, Actor* b)
	{
		if (!a || !b) return 99999.0f;
		float dx = a->pos.x - b->pos.x;
		float dy = a->pos.y - b->pos.y;
		return sqrt(dx * dx + dy * dy);
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
			g_tempRangedData[i].Reset();
		}
		g_multiRiderCount = 0;
		g_lastPromotionCheckTime = 0.0f;
		g_lastTempRangedCheckTime = 0.0f;
		g_lastTempRangedTriggerTime = -TEMP_RANGED_COOLDOWN;  // Allow immediate first trigger
		
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
			g_tempRangedData[i].Reset();
		}
		g_multiRiderCount = 0;
		g_lastPromotionCheckTime = 0.0f;
		g_lastTempRangedCheckTime = 0.0f;
		g_lastTempRangedTriggerTime = -TEMP_RANGED_COOLDOWN;
	}
	
	// ============================================
	// TEMPORARY RANGED MODE FUNCTIONS
	// ============================================
	
	static TempRangedData* GetTempRangedData(UInt32 riderFormID)
	{
		for (int i = 0; i < MAX_MULTI_RIDERS; i++)
		{
			if (g_tempRangedData[i].isValid && g_tempRangedData[i].riderFormID == riderFormID)
			{
				return &g_tempRangedData[i];
			}
		}
		return nullptr;
	}
	
	static bool IsInTempRangedMode(UInt32 riderFormID)
	{
		return GetTempRangedData(riderFormID) != nullptr;
	}
	
	static int GetTempRangedCount()
	{
		int count = 0;
		for (int i = 0; i < MAX_MULTI_RIDERS; i++)
		{
			if (g_tempRangedData[i].isValid)
			{
				count++;
			}
		}
		return count;
	}

	static bool TrySwitchMeleeToTempRanged(MultiRiderData* data, Actor* rider, Actor* horse)
	{
		if (!data || !rider || !horse) return false;
		
		const char* riderName = CALL_MEMBER_FN(rider, GetReferenceName)();
		
		// Don't allow if already in temp ranged
		if (IsInTempRangedMode(rider->formID)) return false;
		
		// Find empty slot for temp ranged data
		int emptySlot = -1;
		for (int i = 0; i < MAX_MULTI_RIDERS; i++)
		{
			if (!g_tempRangedData[i].isValid)
			{
				emptySlot = i;
				break;
			}
		}
		
		if (emptySlot < 0) return false;
		
		_MESSAGE("MultiMountedCombat: *** MELEE RIDER '%s' SWITCHING TO TEMP RANGED ***", 
			riderName ? riderName : "Unknown");
		
		// Give bow if they don't have one
		bool gaveBow = false;
		if (!HasBowInInventory(rider))
		{
			GiveDefaultBow(rider);
			gaveBow = true;
			_MESSAGE("MultiMountedCombat: Gave Hunting Bow to '%s'", riderName ? riderName : "Unknown");
		}
		
		// Give arrows and equip bow
		EquipArrows(rider);
		EquipBestBow(rider);
		rider->DrawSheatheWeapon(true);
		
		// Change role to ranged
		data->role = MultiCombatRole::Ranged;
		
		// Initialize ranged horse data
		RangedHorseData* horseData = GetOrCreateRangedHorseData(horse->formID);
		if (horseData)
		{
			horseData->state = RangedHorseState::Stationary;
			horseData->stateStartTime = GetMultiCombatTime();
			horseData->isMoving = false;
		}
		
		// Stop horse and clear follow
		StopHorseSprint(horse);
		Actor_ClearKeepOffsetFromActor(horse);
		
		// Calculate random duration (35-70 seconds)
		float duration = TEMP_RANGED_MIN_DURATION + 
			((float)(rand() % 100) / 100.0f) * (TEMP_RANGED_MAX_DURATION - TEMP_RANGED_MIN_DURATION);
		
		// Store temp ranged data
		g_tempRangedData[emptySlot].riderFormID = rider->formID;
		g_tempRangedData[emptySlot].startTime = GetMultiCombatTime();
		g_tempRangedData[emptySlot].duration = duration;
		g_tempRangedData[emptySlot].gaveBow = gaveBow;
		g_tempRangedData[emptySlot].isValid = true;
		
		// Clear weapon switch cooldown
		ClearWeaponSwitchData(rider->formID);
		
		_MESSAGE("MultiMountedCombat: '%s' now TEMP RANGED for %.0f seconds (gaveBow: %s)",
			riderName ? riderName : "Unknown", duration, gaveBow ? "YES" : "NO");
		
		return true;
	}
	
	static void EndTempRangedMode(MultiRiderData* data, Actor* rider, Actor* horse, TempRangedData* tempData)
	{
		if (!data || !rider || !horse || !tempData) return;
		
		const char* riderName = CALL_MEMBER_FN(rider, GetReferenceName)();
		
		_MESSAGE("MultiMountedCombat: *** TEMP RANGED ENDED for '%s' - RETURNING TO MELEE ***",
			riderName ? riderName : "Unknown");
		
		// Remove bow if we gave it to them
		if (tempData->gaveBow)
		{
			RemoveDefaultBow(rider);
			_MESSAGE("MultiMountedCombat: Removed Hunting Bow from '%s'", riderName ? riderName : "Unknown");
		}
		
		// Reset bow attack state
		ResetBowAttackState(rider->formID);
		
		// Change role back to melee
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
		
		// Equip melee weapon
		if (HasMeleeWeaponInInventory(rider))
		{
			EquipBestMeleeWeapon(rider);
		}
		else
		{
			GiveDefaultMountedWeapon(rider);
		}
		rider->DrawSheatheWeapon(true);
		
		// Clear weapon switch cooldown
		ClearWeaponSwitchData(rider->formID);
		
		// Re-inject follow package for melee combat
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
		}
		
		// Start sprint to close distance
		StartHorseSprint(horse);
		
		// Clear temp ranged data
		tempData->Reset();
		
		// Set cooldown - 60 seconds before another temp ranged can trigger
		g_lastTempRangedTriggerTime = GetMultiCombatTime();
		
		_MESSAGE("MultiMountedCombat: '%s' now back in MELEE mode (60 sec cooldown started)", 
			riderName ? riderName : "Unknown");
	}
	
	static void UpdateTempRangedModes()
	{
		float currentTime = GetMultiCombatTime();
		
		// Check each temp ranged entry
		for (int i = 0; i < MAX_MULTI_RIDERS; i++)
		{
			TempRangedData* tempData = &g_tempRangedData[i];
			if (!tempData->isValid) continue;
			
			// Check if duration expired
			float elapsed = currentTime - tempData->startTime;
			if (elapsed >= tempData->duration)
			{
				// Find the rider data
				MultiRiderData* data = GetMultiRiderData(tempData->riderFormID);
				if (!data)
				{
					tempData->Reset();
					continue;
				}
				
				// Get rider and horse actors
				TESForm* riderForm = LookupFormByID(data->riderFormID);
				TESForm* horseForm = LookupFormByID(data->horseFormID);
				if (!riderForm || !horseForm)
				{
					tempData->Reset();
					continue;
				}
				
				Actor* rider = DYNAMIC_CAST(riderForm, TESForm, Actor);
				Actor* horse = DYNAMIC_CAST(horseForm, TESForm, Actor);
				if (!rider || !horse)
				{
					tempData->Reset();
					continue;
				}
				
				// End temp ranged mode
				EndTempRangedMode(data, rider, horse, tempData);
			}
		}
	}
	
	// ============================================
	// QUERY FUNCTIONS
	// ============================================
	
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
			{
				count++;
			}
		}
		return count;
	}
	
	int GetCompanionRiderCount()
	{
		int count = 0;
		for (int i = 0; i < MAX_MULTI_RIDERS; i++)
		{
			if (g_multiRiders[i].isValid && g_multiRiders[i].isCompanion)
			{
				count++;
			}
		}
		return count;
	}
	
	int GetMeleeRiderCount()
	{
		int count = 0;
		for (int i = 0; i < MAX_MULTI_RIDERS; i++)
		{
			if (g_multiRiders[i].isValid && g_multiRiders[i].role == MultiCombatRole::Melee)
			{
				count++;
			}
		}
		return count;
	}
	
	int GetRangedRiderCount()
	{
		int count = 0;
		for (int i = 0; i < MAX_MULTI_RIDERS; i++)
		{
			if (g_multiRiders[i].isValid && g_multiRiders[i].role == MultiCombatRole::Ranged)
			{
				count++;
			}
		}
		return count;
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
	
	// ============================================
	// RIDER MANAGEMENT
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
				g_multiRiders[i].Reset();
				g_multiRiderCount--;
				return;
			}
		}
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
	
	// Check if any registered rider is a Captain
	static bool HasCaptainRegistered()
	{
		for (int i = 0; i < MAX_MULTI_RIDERS; i++)
		{
			if (!g_multiRiders[i].isValid) continue;
			
			TESForm* riderForm = LookupFormByID(g_multiRiders[i].riderFormID);
			if (!riderForm) continue;
			
			Actor* rider = DYNAMIC_CAST(riderForm, TESForm, Actor);
			if (!rider) continue;
			
			const char* riderName = CALL_MEMBER_FN(rider, GetReferenceName)();
			if (riderName && strstr(riderName, "Captain") != nullptr)
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
		
		// ============================================
		// FULL RANGED SETUP - SAME AS CAPTAIN
		// Give bow, arrows, equip, draw weapon
		// ============================================
		
		// Give bow if they don't have one
		if (!HasBowInInventory(rider))
		{
			GiveDefaultBow(rider);
			_MESSAGE("MultiMountedCombat: Gave Hunting Bow to promoted '%s'", riderName ? riderName : "Unknown");
		}
		
		// Give arrows and equip bow
		EquipArrows(rider);
		EquipBestBow(rider);
		
		// Draw weapon so they're ready to fire
		rider->DrawSheatheWeapon(true);
		
		_MESSAGE("MultiMountedCombat: Promoted '%s' bow equipped and drawn", riderName ? riderName : "Unknown");
		
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
		
		// Clear weapon switch cooldown so future switches work correctly
		ClearWeaponSwitchData(rider->formID);
		
		_MESSAGE("MultiMountedCombat: '%s' now has FULL RANGED capabilities (same as Captain)",
			riderName ? riderName : "Unknown");
	}
	
	// Check and promote first registered rider if no captain and 2+ riders
	static void CheckAndPromoteToRanged()
	{
		// Skip if already have a ranged rider (captain or promoted)
		if (GetRangedRiderCount() > 0) return;
		
		// Need at least 2 riders for promotion (1 stays melee, 1 becomes ranged)
		if (g_multiRiderCount < 2) return;
		
		// Check if a captain exists
		if (HasCaptainRegistered()) return;
		
		// Find the first valid melee rider to promote
		for (int i = 0; i < MAX_MULTI_RIDERS; i++)
		{
			if (!g_multiRiders[i].isValid) continue;
			if (g_multiRiders[i].role != MultiCombatRole::Melee) continue;
			
			TESForm* riderForm = LookupFormByID(g_multiRiders[i].riderFormID);
			TESForm* horseForm = LookupFormByID(g_multiRiders[i].horseFormID);
			if (!riderForm || !horseForm) continue;
			
			Actor* rider = DYNAMIC_CAST(riderForm, TESForm, Actor);
			Actor* horse = DYNAMIC_CAST(horseForm, TESForm, Actor);
			if (!rider || !horse) continue;
			
			// Promote this rider
			PromoteMeleeToRanged(&g_multiRiders[i], rider, horse);
			return;  // Only promote one
		}
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
	// REGISTER MULTI RIDER - Check for Captain OR Companion to force RANGED
	// NOTE: Companions are tracked separately and don't count toward hostile rider limit
	// ============================================
	
	MultiCombatRole RegisterMultiRider(Actor* rider, Actor* horse, Actor* target)
	{
		if (!rider || !horse || !target) return MultiCombatRole::None;
		
		// Check if already registered
		MultiRiderData* existing = GetMultiRiderData(rider->formID);
		if (existing)
		{
			// Update target and distance
			existing->targetFormID = target->formID;
			existing->distanceToTarget = CalculateDistance(rider, target);
			return existing->role;
		}
		
		const char* riderName = CALL_MEMBER_FN(rider, GetReferenceName)();
		bool isCaptain = false;
		bool isCompanion = IsCompanion(rider);
		
		if (riderName && strlen(riderName) > 0)
		{
			if (strstr(riderName, "Captain") != nullptr)
			{
				isCaptain = true;
			}
		}
		
		// ============================================
		// CHECK SLOT AVAILABILITY
		// Companions get special treatment - they should always be able to register
		// Only hostile NPCs are limited by MAX_MULTI_RIDERS
		// ============================================
		
		// Count how many hostile (non-companion) riders are registered
		int hostileRiderCount = 0;
		for (int i = 0; i < MAX_MULTI_RIDERS; i++)
		{
			if (g_multiRiders[i].isValid && !g_multiRiders[i].isCompanion)
			{
				hostileRiderCount++;
			}
		}
		
		// Find empty slot
		int emptySlot = -1;
		for (int i = 0; i < MAX_MULTI_RIDERS; i++)
		{
			if (!g_multiRiders[i].isValid)
			{
				emptySlot = i;
				break;
			}
		}
		
		if (emptySlot < 0)
		{
			// No empty slots at all
			if (isCompanion)
			{
				_MESSAGE("MultiMountedCombat: WARNING - No slots for companion '%s' (all %d slots full)",
					riderName ? riderName : "Unknown", MAX_MULTI_RIDERS);
			}
			return MultiCombatRole::None;
		}
		
		// If this is a hostile NPC (not companion), check the hostile limit
		if (!isCompanion && hostileRiderCount >= MAX_MULTI_RIDERS)
		{
			_MESSAGE("MultiMountedCombat: WARNING - Hostile rider limit reached (%d), cannot register '%s'",
				MAX_MULTI_RIDERS, riderName ? riderName : "Unknown");
			return MultiCombatRole::None;
		}
		
		// Calculate distance
		float distance = CalculateDistance(rider, target);
		bool hasBow = HasBowInInventory(rider);
		
		MultiCombatRole role = MultiCombatRole::Melee;
		
		// ============================================
		// COMPANIONS AND CAPTAINS GET RANGED ROLE
		// They favor ranged combat, keeping distance, but switch to melee if target gets close
		// ============================================
		if (isCaptain || isCompanion)
		{
			const char* roleReason = isCaptain ? "CAPTAIN" : "COMPANION";
			
			_MESSAGE("MultiMountedCombat: *** %s DETECTED: '%s' (%08X) - FORCING RANGED ***", 
				roleReason, riderName ? riderName : "Unknown", rider->formID);
			
			role = MultiCombatRole::Ranged;
			hasBow = true;  // Mark as having bow - centralized system will equip
			
			// Give bow if they don't have one (companions especially may not have one)
			if (!HasBowInInventory(rider))
			{
				GiveDefaultBow(rider);
				_MESSAGE("MultiMountedCombat: Gave Hunting Bow to %s '%s'", roleReason, riderName ? riderName : "Unknown");
			}
			
			// Give arrows and equip bow
			EquipArrows(rider);
			EquipBestBow(rider);
			rider->DrawSheatheWeapon(true);
			
			_MESSAGE("MultiMountedCombat: %s '%s' RANGED setup complete", roleReason, riderName ? riderName : "Unknown");
			
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
		g_multiRiders[emptySlot].hasBow = hasBow;
		g_multiRiders[emptySlot].isCompanion = isCompanion;  // Track if this is a companion
		g_multiRiders[emptySlot].isValid = true;
		g_multiRiderCount++;
		
		const char* roleName = (role == MultiCombatRole::Ranged) ? "RANGED" : "MELEE";
		const char* typeStr = isCompanion ? " (COMPANION)" : "";
		_MESSAGE("MultiMountedCombat: Registered '%s' (%08X) as %s%s - distance: %.0f",
			riderName ? riderName : "Unknown", rider->formID, roleName, typeStr, distance);
		
		return role;
	}
	
	// ============================================
	// SWITCH CAPTAIN TO MELEE (player got too close)
	// ============================================
	
	static void SwitchCaptainToMelee(MultiRiderData* data, Actor* rider, Actor* horse)
	{
		if (!data || !rider || !horse) return;
		
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
		
		// ============================================
		// FORCE IMMEDIATE WEAPON SWITCH TO MELEE
		// Bypass cooldown - Captain needs melee weapon NOW to attack
		// ============================================
		ResetBowAttackState(rider->formID);
		
		if (HasMeleeWeaponInInventory(rider))
		{
			EquipBestMeleeWeapon(rider);
			_MESSAGE("MultiMountedCombat: Captain '%s' equipped MELEE weapon", riderName ? riderName : "Unknown");
		}
		else
		{
			// Give default melee weapon if Captain has none
			GiveDefaultMountedWeapon(rider);
			_MESSAGE("MultiMountedCombat: Captain '%s' given default MELEE weapon", riderName ? riderName : "Unknown");
		}
		rider->DrawSheatheWeapon(true);
		
		// Clear weapon switch cooldown so future switches work immediately
		ClearWeaponSwitchData(rider->formID);
		
		// ============================================
		// RE-INJECT FOLLOW PACKAGE TO ENABLE MELEE ATTACKS
		// This ensures InjectTravelPackageToHorse runs with melee attack logic
		// ============================================
		Actor* target = nullptr;
		TESForm* targetForm = LookupFormByID(data->targetFormID);
		if (targetForm)
		{
			target = DYNAMIC_CAST(targetForm, TESForm, Actor);
		}
		
		if (target)
		{
			// Force horse combat with target - this sets up follow package
			ForceHorseCombatWithTarget(horse, target);
			
			// Also set NPC to follow target
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
		
		// NOTE: Weapon switching handled by centralized system in DynamicPackages
	}
	
	// ============================================
	// RANGED ROLE BEHAVIOR - STATE MACHINE
	// ============================================
	
	void ExecuteRangedRoleBehavior(MultiRiderData* data, Actor* rider, Actor* horse, Actor* target)
	{
		if (!data || !rider || !horse || !target) return;
		
		float currentTime = GetMultiCombatTime();
		float distance = data->distanceToTarget;
		
		// Get or create ranged horse state data
		RangedHorseData* horseData = GetOrCreateRangedHorseData(horse->formID);
		if (!horseData) return;
		
		// ============================================
		// CHECK IF IN RAPID FIRE - Let SpecialMovesets handle everything
		// ============================================
		if (IsInRapidFire(horse->formID))
		{
			// Update rapid fire - horse stays still, rider fires bow
			if (UpdateRapidFireManeuver(horse, rider, target))
			{
				// Still in rapid fire - just rotate to face target
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
				return;
			}
			// Rapid fire completed - continue to normal behavior
		}
		
		// ============================================
		// CHECK IF TARGET IS TOO CLOSE - SWITCH TO MELEE
		// ============================================
		if (distance < RangedRoleMinDistance)
		{
			_MESSAGE("MultiMountedCombat: Ranged rider %08X too close (%.0f < %.0f) - switching to MELEE",
				data->riderFormID, distance, RangedRoleMinDistance);
			SwitchCaptainToMelee(data, rider, horse);
			return;  // Exit - now in melee mode
		}
		
		// NOTE: Bow equipping handled by centralized system in DynamicPackages
		// DynamicPackages::UpdateRiderWeaponForDistance will equip bow when at range
		
		// ============================================
		// CALCULATE ANGLE TO TARGET
		// ============================================
		float dx = target->pos.x - horse->pos.x;
		float dy = target->pos.y - horse->pos.y;
		float angleToTarget = atan2(dx, dy);
		
		float currentAngle = horse->rot.z;
		float angleDiff = angleToTarget - currentAngle;
		
		// Normalize angle
		while (angleDiff > 3.14159f) angleDiff -= 6.28318f;
		while (angleDiff < -3.14159f) angleDiff += 6.28318f;
		
		// ============================================
		// STATE MACHINE - SIMPLIFIED
		// ============================================
		
		// Determine what state we SHOULD be in based on distance
		RangedHorseState desiredState = RangedHorseState::Stationary;
		
		if (distance > RangedRoleMaxDistance)
		{
			desiredState = RangedHorseState::MovingCloser;
		}
		else
		{
			// Within acceptable range - stay stationary
			desiredState = RangedHorseState::Stationary;
		}
		
		// Handle state transitions
		if (horseData->state != desiredState)
		{
			_MESSAGE("MultiMountedCombat: Horse %08X state change: %d -> %d (distance: %.0f)",
				horse->formID, (int)horseData->state, (int)desiredState, distance);
			
			// Stop movement and clear offsets when transitioning TO stationary
			if (desiredState == RangedHorseState::Stationary)
			{
				// Stop sprint and clear follow offset
				StopHorseSprint(horse);
				Actor_ClearKeepOffsetFromActor(horse);
				horseData->isMoving = false;
				_MESSAGE("MultiMountedCombat: Captain now STATIONARY - cleared follow offset");
			}
			
			// Reset isMoving when leaving MovingCloser so follow gets re-applied if needed
			if (horseData->state == RangedHorseState::MovingCloser)
			{
				horseData->isMoving = false;
			}
			
			horseData->state = desiredState;
			horseData->stateStartTime = currentTime;
		}
		
		// ============================================
		// EXECUTE STATE BEHAVIOR
		// ============================================
		
		switch (horseData->state)
		{
			case RangedHorseState::Stationary:
			{
				// ============================================
				// STATIONARY: Only rotate to face target, no movement
				// ============================================
				
				// Apply smooth rotation to face target
				const float ROTATION_SPEED = 0.25f;
				float newAngle = currentAngle + (angleDiff * ROTATION_SPEED);
				
				while (newAngle > 3.14159f) newAngle -= 6.28318f;
				while (newAngle < -3.14159f) newAngle += 6.28318f;
				
				horse->rot.z = newAngle;
				
				// Fire bow if facing target (within ~30 degrees) and in range
				if (fabs(angleDiff) < 0.5f && distance >= RangedFireMinDistance && distance <= RangedFireMaxDistance)
				{
					UpdateBowAttack(rider, true, target);
				}
				
				// ============================================
				// TRY RAPID FIRE MANEUVER (7% chance every 10 seconds)
				// Only when stationary and combat has been going for 20+ seconds
				// ============================================
				if (GetCombatElapsedTime() >= 20.0f)
				{
					const float DUMMY_MELEE_RANGE = 150.0f;
					if (TryRapidFireManeuver(horse, rider, target, distance, DUMMY_MELEE_RANGE))
					{
						_MESSAGE("MultiMountedCombat: Ranged rider %08X entering RAPID FIRE mode!", rider->formID);
					}
				}
				
				break;
			}
			
			case RangedHorseState::MovingCloser:
			{
				// ============================================
				// MOVING CLOSER: Use same keep-offset follow as melee riders
				// This uses the proper AI follow package to close distance
				// ============================================
				
				// Apply keep-offset to follow target (same as melee riders)
				if (!horseData->isMoving)
				{
					// Get target handle
					UInt32 targetHandle = target->CreateRefHandle();
					if (targetHandle != 0 && targetHandle != *g_invalidRefHandle)
					{
						// Use offset that puts Captain at ideal ranged distance
						NiPoint3 offset;
						offset.x = 0;
						offset.y = -RangedRoleIdealDistance;  // Stay behind at ideal distance
						offset.z = 0;
						
						NiPoint3 offsetAngle;
						offsetAngle.x = 0;
						offsetAngle.y = 0;
						offsetAngle.z = 0;
						
						// Use same follow parameters as melee riders
						Actor_KeepOffsetFromActor(horse, targetHandle, offset, offsetAngle, 1500.0f, 300.0f);
						Actor_EvaluatePackage(horse, false, false);
						
						horseData->isMoving = true;
						_MESSAGE("MultiMountedCombat: Captain using FOLLOW PACKAGE to close distance (%.0f units away)", distance);
					}
				}
				
				// Still rotate to face target while moving
				const float ROTATION_SPEED = 0.25f;
				float newAngle = currentAngle + (angleDiff * ROTATION_SPEED);
				
				while (newAngle > 3.14159f) newAngle -= 6.28318f;
				while (newAngle < -3.14159f) newAngle += 6.28318f;
				
				horse->rot.z = newAngle;
				
				// Can fire while moving closer if facing target
				if (fabs(angleDiff) < 0.5f && distance >= RangedFireMinDistance && distance <= RangedFireMaxDistance)
				{
					UpdateBowAttack(rider, true, target);
				}
				
				break;
			}
			
			default:
				break;
		}
	}
	
	void ExecuteMeleeRoleBehavior(MultiRiderData* data, Actor* rider, Actor* horse, Actor* target)
	{
		if (!data || !rider || !horse || !target) return;
		
		// ============================================
		// CHECK IF CAPTAIN OR COMPANION SHOULD SWITCH BACK TO RANGED
		// If this is a Captain/Companion (was ranged, now melee) and target is far enough,
		// switch back to ranged mode
		// ============================================
		const char* riderName = CALL_MEMBER_FN(rider, GetReferenceName)();
		bool isCaptain = false;
		bool isCompanion = IsCompanion(rider);
		
		if (riderName && strlen(riderName) > 0)
		{
			if (strstr(riderName, "Captain") != nullptr)
			{
				isCaptain = true;
			}
		}
		
		// Captains and Companions switch back to ranged when target is far enough away
		if ((isCaptain || isCompanion) && data->distanceToTarget >= RangedRoleMinDistance)
		{
			const char* roleType = isCaptain ? "Captain" : "Companion";
			_MESSAGE("MultiMountedCombat: %s '%s' at %.0f units - returning to ranged", 
				roleType, riderName ? riderName : "Unknown", data->distanceToTarget);
			SwitchCaptainToRanged(data, rider, horse);
			return;  // Exit - now in ranged mode, will be handled next frame
		}
		
		// NOTE: Weapon switching handled by centralized system in DynamicPackages
		// No equip calls here - DynamicPackages::UpdateRiderWeaponForDistance handles everything
	}
	
	// ============================================
	// UPDATE FUNCTIONS
	// ============================================
	
	void UpdateMultiMountedCombat()
	{
		if (!g_multiCombatInitialized) return;
		if (g_multiRiderCount == 0) return;
		
		float currentTime = GetMultiCombatTime();
		
		// ============================================
		// CHECK FOR RANGED PROMOTION (if no captain)
		// If we have 2+ riders but no ranged, promote one
		// ============================================
		CheckAndPromoteToRanged();
		
		// ============================================
		// UPDATE TEMP RANGED MODES (check for expiration)
		// ============================================
		UpdateTempRangedModes();
		
		// ============================================
		// CHECK FOR NEW TEMP RANGED OPPORTUNITY
		// 25% chance every 25 seconds if conditions are met:
		// - Must have ranged rider (Captain or promoted) + at least 2 melee riders
		// - Only 1 melee rider can be in temp ranged at a time
		// - 60 second cooldown after temp ranged ends
		// ============================================
		if ((currentTime - g_lastTempRangedCheckTime) >= TEMP_RANGED_CHECK_INTERVAL)
		{
			g_lastTempRangedCheckTime = currentTime;
			
			// Check cooldown first
			if ((currentTime - g_lastTempRangedTriggerTime) < TEMP_RANGED_COOLDOWN)
			{
				// Still on cooldown, skip
			}
			// Check if someone is already in temp ranged mode (only 1 at a time)
			else if (GetTempRangedCount() > 0)
			{
				// Already have a temp ranged rider, skip
			}
			else
			{
				// Check conditions: Need ranged rider (Captain or promoted) + at least 2 melee riders
				int meleeCount = GetMeleeRiderCount();
				int rangedCount = GetRangedRiderCount();
				
				// Need: 1 ranged rider + at least 2 melee riders (so 1 can switch and 1 stays melee)
				// This ensures single mounted combat (1 rider) is never affected
				if (rangedCount >= 1 && meleeCount >= 2)
				{
					// 25% chance
					int roll = rand() % 100;
					if (roll < TEMP_RANGED_CHANCE_PERCENT)
					{
						_MESSAGE("MultiMountedCombat: TEMP RANGED CHECK - Roll: %d (need < %d) - TRIGGERED!",
							roll, TEMP_RANGED_CHANCE_PERCENT);
						
						// Find a melee rider to switch (pick random one)
						int meleeIndices[MAX_MULTI_RIDERS];
						int meleeIndexCount = 0;
						
						for (int i = 0; i < MAX_MULTI_RIDERS; i++)
						{
							if (g_multiRiders[i].isValid && 
								g_multiRiders[i].role == MultiCombatRole::Melee &&
								!IsInTempRangedMode(g_multiRiders[i].riderFormID))
							{
								// Check this isn't a Captain
								TESForm* riderForm = LookupFormByID(g_multiRiders[i].riderFormID);
								if (riderForm)
								{
									Actor* rider = DYNAMIC_CAST(riderForm, TESForm, Actor);
									if (rider)
									{
										const char* riderName = CALL_MEMBER_FN(rider, GetReferenceName)();
										bool isCaptain = (riderName && strstr(riderName, "Captain") != nullptr);
										if (!isCaptain)
										{
											meleeIndices[meleeIndexCount++] = i;
										}
									}
								}
							}
						}
						
						if (meleeIndexCount > 0)
						{
							// Pick random melee rider
							int chosen = rand() % meleeIndexCount;
							int chosenIndex = meleeIndices[chosen];
							
							MultiRiderData* data = &g_multiRiders[chosenIndex];
							
							TESForm* riderForm = LookupFormByID(data->riderFormID);
							TESForm* horseForm = LookupFormByID(data->horseFormID);
							
							if (riderForm && horseForm)
							{
								Actor* rider = DYNAMIC_CAST(riderForm, TESForm, Actor);
								Actor* horse = DYNAMIC_CAST(horseForm, TESForm, Actor);
								
								if (rider && horse)
								{
									TrySwitchMeleeToTempRanged(data, rider, horse);
								}
							}
						}
					}
				}
			}
		}
		
		// ============================================
		// COUNT ACTIVE RIDERS BEFORE UPDATE
		// ============================================
		int meleeCountBefore = GetMeleeRiderCount();
		int rangedCountBefore = GetRangedRiderCount();
		
		// ============================================
		// UPDATE EACH RIDER - Check for deaths/dismounts AND UPDATE TARGETS
		// ============================================
		for (int i = 0; i < MAX_MULTI_RIDERS; i++)
		{
			MultiRiderData* data = &g_multiRiders[i];
			if (!data->isValid) continue;
			
			// Look up actors
			TESForm* riderForm = LookupFormByID(data->riderFormID);
			TESForm* horseForm = LookupFormByID(data->horseFormID);
			TESForm* targetForm = LookupFormByID(data->targetFormID);
			
			if (!riderForm || !horseForm)
			{
				_MESSAGE("MultiMountedCombat: Rider %08X - forms invalid, removing", data->riderFormID);
				data->Reset();
				g_multiRiderCount--;
				continue;
			}
			
			Actor* rider = DYNAMIC_CAST(riderForm, TESForm, Actor);
			Actor* horse = DYNAMIC_CAST(horseForm, TESForm, Actor);
			
			if (!rider || !horse)
			{
				_MESSAGE("MultiMountedCombat: Rider %08X - cast failed, removing", data->riderFormID);
				data->Reset();
				g_multiRiderCount--;
				continue;
			}
			
			// ============================================
		// DYNAMIC TARGET UPDATE - Use rider's actual combat target
		// This allows Captains to fight wolves, bandits, etc - not locked to player
		// ============================================
		Actor* target = nullptr;
		
		// First try to get the rider's current combat target
		UInt32 combatTargetHandle = rider->currentCombatTarget;
		if (combatTargetHandle != 0)
		{
			NiPointer<TESObjectREFR> combatTargetRef;
			LookupREFRByHandle(combatTargetHandle, combatTargetRef);
			if (combatTargetRef && combatTargetRef->formType == kFormType_Character)
			{
				Actor* combatTarget = static_cast<Actor*>(combatTargetRef.get());
				if (combatTarget && !combatTarget->IsDead(1))
				{
					// Update to actual combat target if different
					if (combatTarget->formID != data->targetFormID)
					{
						const char* riderName = CALL_MEMBER_FN(rider, GetReferenceName)();
						const char* newTargetName = CALL_MEMBER_FN(combatTarget, GetReferenceName)();
						_MESSAGE("MultiMountedCombat: %s target updated: %08X -> %08X ('%s')",
							riderName ? riderName : "Rider",
							data->targetFormID,
							combatTarget->formID,
							newTargetName ? newTargetName : "Unknown");
						data->targetFormID = combatTarget->formID;
					}
					target = combatTarget;
				}
			}
		}
		
		// Fall back to stored target if no current combat target
		if (!target && targetForm)
		{
			target = DYNAMIC_CAST(targetForm, TESForm, Actor);
		}
		
		// If still no target, skip this rider
		if (!target)
		{
			continue;
		}
		
		// Check if target is dead - need new target
		if (target->IsDead(1))
		{
			const char* riderName = CALL_MEMBER_FN(rider, GetReferenceName)();
			_MESSAGE("MultiMountedCombat: %s's target died - looking for new target",
				riderName ? riderName : "Rider");
			
			// Try to find a new combat target
			combatTargetHandle = rider->currentCombatTarget;
			if (combatTargetHandle != 0)
			{
				NiPointer<TESObjectREFR> newTargetRef;
				LookupREFRByHandle(combatTargetHandle, newTargetRef);
				if (newTargetRef && newTargetRef->formType == kFormType_Character)
				{
					Actor* newTarget = static_cast<Actor*>(newTargetRef.get());
					if (newTarget && !newTarget->IsDead(1))
					{
						data->targetFormID = newTarget->formID;
						target = newTarget;
						_MESSAGE("MultiMountedCombat: Found new target: %08X", newTarget->formID);
					}
				}
			}
			
			// If still no valid target, remove this rider
			if (!target || target->IsDead(1))
			{
				_MESSAGE("MultiMountedCombat: No valid target found - removing rider");
				data->Reset();
				g_multiRiderCount--;
				continue;
			}
		}
		
		// Check if rider is dead
		if (rider->IsDead(1))
		{
			const char* riderName = CALL_MEMBER_FN(rider, GetReferenceName)();
			_MESSAGE("MultiMountedCombat: Rider '%s' (%08X) DIED - removing from tracking",
				riderName ? riderName : "Unknown", data->riderFormID);
			
			// Clear ranged horse data if this was a ranged rider
			if (data->role == MultiCombatRole::Ranged)
			{
				for (int j = 0; j < MAX_MULTI_RIDERS; j++)
				{
					if (g_rangedHorseData[j].isValid && g_rangedHorseData[j].horseFormID == data->horseFormID)
					{
						g_rangedHorseData[j].Reset();
						break;
					}
				}
			}
			
			data->Reset();
			g_multiRiderCount--;
			continue;
		}
		
		// Check if horse is dead
		if (horse->IsDead(1))
		{
			const char* riderName = CALL_MEMBER_FN(rider, GetReferenceName)();
			_MESSAGE("MultiMountedCombat: Horse of '%s' (%08X) DIED - removing rider from tracking",
				riderName ? riderName : "Unknown", data->riderFormID);
			
			// Clear ranged horse data if this was a ranged rider
			if (data->role == MultiCombatRole::Ranged)
			{
				for (int j = 0; j < MAX_MULTI_RIDERS; j++)
				{
					if (g_rangedHorseData[j].isValid && g_rangedHorseData[j].horseFormID == data->horseFormID)
					{
						g_rangedHorseData[j].Reset();
						break;
					}
				}
			}
			
			data->Reset();
			g_multiRiderCount--;
			continue;
		}
		
		// Check if rider dismounted
		NiPointer<Actor> currentMount;
		bool stillMounted = CALL_MEMBER_FN(rider, GetMount)(currentMount);
		if (!stillMounted || !currentMount || currentMount->formID != data->horseFormID)
		{
			const char* riderName = CALL_MEMBER_FN(rider, GetReferenceName)();
			_MESSAGE("MultiMountedCombat: Rider '%s' (%08X) DISMOUNTED - removing from tracking",
				riderName ? riderName : "Unknown", data->riderFormID);
			
			// Clear ranged horse data if this was a ranged rider
			if (data->role == MultiCombatRole::Ranged)
			{
				for (int j = 0; j < MAX_MULTI_RIDERS; j++)
				{
					if (g_rangedHorseData[j].isValid && g_rangedHorseData[j].horseFormID == data->horseFormID)
					{
						g_rangedHorseData[j].Reset();
						break;
					}
				}
			}
			
			data->Reset();
			g_multiRiderCount--;
			continue;
		}
		
		// Update distance to CURRENT target (may have changed)
		data->distanceToTarget = CalculateDistance(rider, target);
		
		// Execute role-specific behavior with the CURRENT target
		if (data->role == MultiCombatRole::Ranged)
		{
			ExecuteRangedRoleBehavior(data, rider, horse, target);
		}
		else
		{
			ExecuteMeleeRoleBehavior(data, rider, horse, target);
		}
		}
		
		// ============================================
		// CHECK IF MELEE RIDERS DROPPED - SWITCH COMMANDER TO MELEE
		// ============================================
		int meleeCountAfter = GetMeleeRiderCount();
		int rangedCountAfter = GetRangedRiderCount();
		int totalAfter = g_multiRiderCount;
		
		// Only switch Commander to melee if:
		// - We HAD melee riders before (meleeCountBefore >= 2)
		// - Now all melee riders are gone (meleeCountAfter == 0)
		// - There's still a ranged rider to convert
		if (meleeCountBefore >= 2 && meleeCountAfter == 0 && rangedCountAfter > 0)
		{
			_MESSAGE("MultiMountedCombat: ========================================");
			_MESSAGE("MultiMountedCombat: MELEE RIDER(S) DOWN! Switching Commander to MELEE");
			_MESSAGE("MultiMountedCombat: Before: %d melee, %d ranged | After: %d melee, %d ranged",
				meleeCountBefore, rangedCountBefore, meleeCountAfter, rangedCountAfter);
			_MESSAGE("MultiMountedCombat: ========================================");
			
			// Find and convert ranged rider(s) to melee
			for (int i = 0; i < MAX_MULTI_RIDERS; i++)
			{
				MultiRiderData* data = &g_multiRiders[i];
				if (!data->isValid) continue;
				if (data->role != MultiCombatRole::Ranged) continue;
				
				// Get rider actor
				TESForm* riderForm = LookupFormByID(data->riderFormID);
				TESForm* horseForm = LookupFormByID(data->horseFormID);
				if (!riderForm || !horseForm) continue;
				
				Actor* rider = DYNAMIC_CAST(riderForm, TESForm, Actor);
				Actor* horse = DYNAMIC_CAST(horseForm, TESForm, Actor);
				if (!rider || !horse) continue;
				
				const char* riderName = CALL_MEMBER_FN(rider, GetReferenceName)();
				_MESSAGE("MultiMountedCombat: Converting '%s' (%08X) from RANGED to MELEE",
					riderName ? riderName : "Unknown", data->riderFormID);
				
				// Change role to melee
				data->role = MultiCombatRole::Melee;
				
				// Clear ranged horse state
				for (int j = 0; j < MAX_MULTI_RIDERS; j++)
				{
					if (g_rangedHorseData[j].isValid && g_rangedHorseData[j].horseFormID == data->horseFormID)
					{
						// Stop any movement from ranged behavior
						StopHorseSprint(horse);
						g_rangedHorseData[j].Reset();
						break;
					}
				}
				
				// NOTE: Weapon switching handled by centralized system in DynamicPackages
				
				_MESSAGE("MultiMountedCombat: Commander '%s' now in FULL MELEE mode!",
					riderName ? riderName : "Unknown");
			}
		}
	}
	
	int UpdateMultiRiderBehavior(Actor* horse, Actor* target)
	{
		if (!horse || !target) return 0;
		
		MultiRiderData* data = GetMultiRiderDataByHorse(horse->formID);
		if (!data) return 0;
		
		// If ranged role, handle rotation and return early
		if (data->role == MultiCombatRole::Ranged)
		{
			NiPointer<Actor> rider;
			if (!CALL_MEMBER_FN(horse, GetMountedBy)(rider) || !rider)
			{
				return 0;
			}
			
			// Execute ranged behavior (rotation, bow equip, firing)
			ExecuteRangedRoleBehavior(data, rider.get(), horse, target);
			
			// Return 6 to indicate ranged role is active
			return 6;
		}
		
		return 0;
	}
}
