#include "MultiMountedCombat.h"
#include "DynamicPackages.h"
#include "WeaponDetection.h"
#include "ArrowSystem.h"
#include "CombatStyles.h"
#include "SpecialMovesets.h"
#include "SingleMountedCombat.h"
#include "skse64/GameRTTI.h"
#include <cmath>

namespace MountedNPCCombatVR
{
	// ============================================
	// MULTI MOUNTED COMBAT
	// ============================================
	
	const float RANGED_ROLE_MIN_DISTANCE = 500.0f;    // Minimum distance - if closer, switch to melee
	const float RANGED_ROLE_IDEAL_DISTANCE = 1000.0f;  // Ideal distance - hold position here
	const float RANGED_ROLE_MAX_DISTANCE = 1500.0f;   // Maximum distance - if further, move closer (DOUBLED from 1200)
	const float RANGED_POSITION_TOLERANCE = 100.0f; // How close to ideal is "close enough"
	const float RANGED_FIRE_MIN_DISTANCE = 300.0f; // Minimum distance to fire bow
	const float RANGED_FIRE_MAX_DISTANCE = 2000.0f;   // Maximum distance to fire bow (DOUBLED from 1500)
	const float ROLE_CHECK_INTERVAL = 2.0f;           // Time in seconds to re-check roles
	const int MAX_MULTI_RIDERS = 5;         // Maximum number of multi riders supported
	
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
	}
	
	// ============================================
	// QUERY FUNCTIONS
	// ============================================
	
	int GetActiveMultiRiderCount()
	{
		return g_multiRiderCount;
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
	
	static void PromoteFurthestToRanged()
	{
		// DISABLED: Only Captains should be ranged
		_MESSAGE("MultiMountedCombat: PromoteFurthestToRanged DISABLED - only Captains can be ranged");
		return;
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
	// REGISTER MULTI RIDER - Check for Captain to force RANGED
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
			return MultiCombatRole::None;
		}
		
		// Calculate distance
		float distance = CalculateDistance(rider, target);
		bool hasBow = HasBowInInventory(rider);
		
		const char* riderName = CALL_MEMBER_FN(rider, GetReferenceName)();
		bool isCaptain = false;
		
		if (riderName && strlen(riderName) > 0)
		{
			if (strstr(riderName, "Captain") != nullptr)
			{
				isCaptain = true;
			}
		}
		
		MultiCombatRole role = MultiCombatRole::Melee;
		
		if (isCaptain)
		{
			_MESSAGE("MultiMountedCombat: *** CAPTAIN DETECTED: '%s' (%08X) - FORCING RANGED ***", 
				riderName, rider->formID);
			
			// Log Captain's available spells
			_MESSAGE("MultiMountedCombat: --- Captain Spell Inventory ---");
			LogAvailableSpells(rider, rider->formID);
			
			role = MultiCombatRole::Ranged;
			
			if (!HasBowInInventory(rider))
			{
				_MESSAGE("MultiMountedCombat: Captain has no bow - giving Hunting Bow");
				GiveDefaultBow(rider);
			}
			
			_MESSAGE("MultiMountedCombat: Giving arrows to Captain");
			EquipArrows(rider);
			
			_MESSAGE("MultiMountedCombat: Equipping bow on Captain");
			EquipBestBow(rider);
			
			_MESSAGE("MultiMountedCombat: Drawing weapon on Captain");
			rider->DrawSheatheWeapon(true);
			
			hasBow = true;
			
			_MESSAGE("MultiMountedCombat: Captain '%s' RANGED setup complete", riderName);
			
			// Initialize ranged horse data
			RangedHorseData* horseData = GetOrCreateRangedHorseData(horse->formID);
			if (horseData)
			{
				horseData->state = RangedHorseState::Stationary;
				horseData->stateStartTime = GetMultiCombatTime();
			}
		}
		
		g_multiRiders[emptySlot].riderFormID = rider->formID;
		g_multiRiders[emptySlot].horseFormID = horse->formID;
		g_multiRiders[emptySlot].targetFormID = target->formID;
		g_multiRiders[emptySlot].role = role;
		g_multiRiders[emptySlot].distanceToTarget = distance;
		g_multiRiders[emptySlot].lastRoleCheckTime = GetMultiCombatTime();
		g_multiRiders[emptySlot].lastRangedSetupTime = 0.0f;
		g_multiRiders[emptySlot].hasBow = hasBow;
		g_multiRiders[emptySlot].isValid = true;
		g_multiRiderCount++;
		
		const char* roleName = (role == MultiCombatRole::Ranged) ? "RANGED" : "MELEE";
		_MESSAGE("MultiMountedCombat: Registered '%s' (%08X) as %s - distance: %.0f",
			riderName ? riderName : "Unknown", rider->formID, roleName, distance);
		
		return role;
	}
	
	// ============================================
	// SWITCH CAPTAIN TO MELEE (player got too close)
	// ============================================
	
	static void SwitchCaptainToMelee(MultiRiderData* data, Actor* rider, Actor* horse)
	{
		if (!data || !rider || !horse) return;
		
		const char* riderName = CALL_MEMBER_FN(rider, GetReferenceName)();
		_MESSAGE("MultiMountedCombat: ========================================");
		_MESSAGE("MultiMountedCombat: CAPTAIN '%s' SWITCHING TO MELEE (target too close)", 
			riderName ? riderName : "Unknown");
		_MESSAGE("MultiMountedCombat: ========================================");
		
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
		
		// START SPRINT to retreat quickly
		StartHorseSprint(horse);
		_MESSAGE("MultiMountedCombat: Captain horse SPRINTING away");
		
		// Sheathe bow
		rider->DrawSheatheWeapon(false);
		
		// Give melee weapon if needed
		if (!HasMeleeWeaponInInventory(rider))
		{
			_MESSAGE("MultiMountedCombat: Giving melee weapon to Captain");
			GiveDefaultMountedWeapon(rider);
		}
		
		// Equip best melee weapon
		EquipBestMeleeWeapon(rider);
		rider->DrawSheatheWeapon(true);
		
		_MESSAGE("MultiMountedCombat: Captain '%s' now in MELEE mode!", 
			riderName ? riderName : "Unknown");
	}
	
	// ============================================
	// SWITCH CAPTAIN BACK TO RANGED (player retreated to safe distance)
	// ============================================
	
	static void SwitchCaptainToRanged(MultiRiderData* data, Actor* rider, Actor* horse)
	{
		if (!data || !rider || !horse) return;
		
		const char* riderName = CALL_MEMBER_FN(rider, GetReferenceName)();
		_MESSAGE("MultiMountedCombat: ========================================");
		_MESSAGE("MultiMountedCombat: CAPTAIN '%s' RETURNING TO RANGED (target retreated)", 
			riderName ? riderName : "Unknown");
		_MESSAGE("MultiMountedCombat: ========================================");
		
		// Change role back to ranged
		data->role = MultiCombatRole::Ranged;
		
		// STOP SPRINT - Captain will be stationary now
		StopHorseSprint(horse);
		_MESSAGE("MultiMountedCombat: Captain horse STOPPED sprinting");
		
		// Sheathe melee weapon
		rider->DrawSheatheWeapon(false);
		
		// Give bow if needed
		if (!HasBowInInventory(rider))
		{
			_MESSAGE("MultiMountedCombat: Giving Hunting Bow to Captain");
			GiveDefaultBow(rider);
		}
		
		// Equip bow and arrows
		_MESSAGE("MultiMountedCombat: Equipping bow on Captain");
		EquipArrows(rider);
		EquipBestBow(rider);
		rider->DrawSheatheWeapon(true);
		
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
		
		_MESSAGE("MultiMountedCombat: Captain '%s' RANGED mode restored!", 
			riderName ? riderName : "Unknown");
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
		if (distance < RANGED_ROLE_MIN_DISTANCE)
		{
			_MESSAGE("MultiMountedCombat: Target too close (%.0f < %.0f) - switching Captain to melee",
				distance, RANGED_ROLE_MIN_DISTANCE);
			SwitchCaptainToMelee(data, rider, horse);
			return;  // Exit - now in melee mode
		}
		
		// ============================================
		// ENSURE BOW IS EQUIPPED
		// ============================================
		if (!HasBowInInventory(rider))
		{
			GiveDefaultBow(rider);
			data->hasBow = true;
		}
		
		if (!IsBowEquipped(rider))
		{
			if (HasBowInInventory(rider))
			{
				EquipBestBow(rider);
				rider->DrawSheatheWeapon(true);
				EquipArrows(rider);
			}
		}
		
		if (!IsWeaponDrawn(rider))
		{
			rider->DrawSheatheWeapon(true);
		}
		
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
		
		if (distance > RANGED_ROLE_MAX_DISTANCE)
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
				if (fabs(angleDiff) < 0.5f && distance >= RANGED_FIRE_MIN_DISTANCE && distance <= RANGED_FIRE_MAX_DISTANCE)
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
						offset.y = -RANGED_ROLE_IDEAL_DISTANCE;  // Stay behind at ideal distance
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
				if (fabs(angleDiff) < 0.5f && distance >= RANGED_FIRE_MIN_DISTANCE && distance <= RANGED_FIRE_MAX_DISTANCE)
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
		// CHECK IF CAPTAIN SHOULD SWITCH BACK TO RANGED
		// If this is a Captain (was ranged, now melee) and target is far enough,
		// switch back to ranged mode
		// ============================================
		const char* riderName = CALL_MEMBER_FN(rider, GetReferenceName)();
		bool isCaptain = false;
		if (riderName && strlen(riderName) > 0)
		{
			if (strstr(riderName, "Captain") != nullptr)
			{
				isCaptain = true;
			}
		}
		
		// Captain switches back to ranged when target is 500+ units away
		if (isCaptain && data->distanceToTarget >= RANGED_ROLE_MIN_DISTANCE)
		{
			_MESSAGE("MultiMountedCombat: Captain at %.0f units - returning to ranged", 
				data->distanceToTarget);
			SwitchCaptainToRanged(data, rider, horse);
			return;  // Exit - now in ranged mode, will be handled next frame
		}
		
		// ============================================
		// ENSURE MELEE WEAPON IS EQUIPPED
		// ============================================
		if (!IsMeleeEquipped(rider))
		{
			if (HasMeleeWeaponInInventory(rider))
			{
				EquipBestMeleeWeapon(rider);
				rider->DrawSheatheWeapon(true);
			}
			else
			{
				GiveDefaultMountedWeapon(rider);
				EquipBestMeleeWeapon(rider);
				rider->DrawSheatheWeapon(true);
			}
		}
		
		// Make sure weapon is drawn
		if (!IsWeaponDrawn(rider))
		{
			rider->DrawSheatheWeapon(true);
		}
		
		// ============================================
		// CAPTAIN MELEE ATTACK - Use the melee attack system
		// ============================================
		const float CAPTAIN_MELEE_ATTACK_RANGE = 300.0f;  // Distance to start melee attacks
		
		if (data->distanceToTarget <= CAPTAIN_MELEE_ATTACK_RANGE)
		{
			// Calculate which side target is on relative to rider
			float dx = target->pos.x - rider->pos.x;
			float dy = target->pos.y - rider->pos.y;
			float angleToTarget = atan2(dx, dy);
			
			float riderFacing = rider->rot.z;
			float relativeAngle = angleToTarget - riderFacing;
			
			// Normalize to [-PI, PI]
			while (relativeAngle > 3.14159f) relativeAngle -= 6.28318f;
			while (relativeAngle < -3.14159f) relativeAngle += 6.28318f;
			
			// Determine attack side
			const char* attackSide = (relativeAngle >= 0) ? "RIGHT" : "LEFT";
			
			// Try to play melee attack animation
			if (PlayMountedAttackAnimation(rider, attackSide))
			{
				// Attack triggered - check for hit
				UpdateMountedAttackHitDetection(rider, target);
			}
			else
			{
				// Animation might be on cooldown - still check for pending hits
				UpdateMountedAttackHitDetection(rider, target);
			}
		}
	}
	
	// ============================================
	// PROMOTE MELEE RIDER TO FULL COMBAT
	// Called when a melee rider dies, the remaining melee rider
	// gets a bow and full dynamic weapon switching capability
	// ============================================
	
	void PromoteMeleeRiderToFullCombat(MultiRiderData* data, Actor* rider, Actor* horse)
	{
		if (!data || !rider || !horse) return;
		
		const char* riderName = CALL_MEMBER_FN(rider, GetReferenceName)();
		_MESSAGE("MultiMountedCombat: ========================================");
		_MESSAGE("MultiMountedCombat: PROMOTING '%s' (%08X) TO FULL COMBAT!", 
			riderName ? riderName : "Unknown", rider->formID);
		_MESSAGE("MultiMountedCombat: ========================================");
		
		// Change role to FullCombat
		data->role = MultiCombatRole::FullCombat;
		
		// Give bow if they don't have one
		if (!HasBowInInventory(rider))
		{
			_MESSAGE("MultiMountedCombat: Giving Hunting Bow to promoted rider");
			GiveDefaultBow(rider);
		}
		
		// Give arrows
		_MESSAGE("MultiMountedCombat: Giving arrows to promoted rider");
		EquipArrows(rider);
		
		// Mark that they have a bow now
		data->hasBow = true;
		
		// Ensure they have melee weapon too
		if (!HasMeleeWeaponInInventory(rider))
		{
			_MESSAGE("MultiMountedCombat: Giving melee weapon to promoted rider");
			GiveDefaultMountedWeapon(rider);
		}
		
		_MESSAGE("MultiMountedCombat: '%s' now has FULL COMBAT capability!", 
			riderName ? riderName : "Unknown");
		_MESSAGE("MultiMountedCombat: - Can use BOW at range (>600 units)");
		_MESSAGE("MultiMountedCombat: - Can use MELEE up close (<400 units)");
		_MESSAGE("MultiMountedCombat: - Dynamic weapon switching enabled");
		_MESSAGE("MultiMountedCombat: - Charge maneuvers enabled");
		_MESSAGE("MultiMountedCombat: - Rapid fire maneuvers enabled");
	}
	
	// ============================================
	// FULL COMBAT ROLE BEHAVIOR
	// Dynamic weapon switching based on distance
	// ============================================
	
	// Distance thresholds for weapon switching
	const float FULL_COMBAT_RANGED_DISTANCE = 600.0f;   // Use bow when further than this
	const float FULL_COMBAT_MELEE_DISTANCE = 400.0f;    // Use melee when closer than this
	const float FULL_COMBAT_CHARGE_MIN = 700.0f;    // Minimum distance for charge
	const float FULL_COMBAT_CHARGE_MAX = 1500.0f;       // Maximum distance for charge
	
	void ExecuteFullCombatRoleBehavior(MultiRiderData* data, Actor* rider, Actor* horse, Actor* target)
	{
		if (!data || !rider || !horse || !target) return;
		
		float distance = data->distanceToTarget;
		const char* riderName = CALL_MEMBER_FN(rider, GetReferenceName)();
		
		// ============================================
		// CHECK IF IN CHARGE MANEUVER - Let it complete
		// ============================================
		if (IsHorseCharging(horse->formID))
		{
			// Continue the charge maneuver
			const float MELEE_RANGE = 250.0f;
			if (UpdateChargeManeuver(horse, rider, target, distance, MELEE_RANGE))
			{
				// Still charging - don't change weapons
				return;
			}
			// Charge completed - will continue to normal behavior below
		}
		
		// ============================================
		// CHECK IF IN RAPID FIRE - Let it complete
		// ============================================
		if (IsInRapidFire(horse->formID))
		{
			if (UpdateRapidFireManeuver(horse, rider, target))
			{
				// Still in rapid fire - rotate to face target
				float dx = target->pos.x - horse->pos.x;
				float dy = target->pos.y - horse->pos.y;
				float angleToTarget = atan2(dx, dy);
				float currentAngle = horse->rot.z;
				float angleDiff = angleToTarget - currentAngle;
				
				while (angleDiff > 3.14159f) angleDiff -= 6.28318f;
				while (angleDiff < -3.14159f) angleDiff += 6.28318f;
				
				const float ROTATION_SPEED = 0.25f;
				horse->rot.z = currentAngle + (angleDiff * ROTATION_SPEED);
				return;
			}
			// Rapid fire completed - continue to normal behavior
		}
		
		// ============================================
		// DYNAMIC WEAPON SWITCHING BASED ON DISTANCE
		// ============================================
		
		bool currentlyHasBow = IsBowEquipped(rider);
		bool currentlyHasMelee = IsMeleeEquipped(rider);
		
		if (distance > FULL_COMBAT_RANGED_DISTANCE)
		{
			// ============================================
			// FAR AWAY - USE BOW (RANGED MODE)
			// ============================================
			
			// Switch to bow if not already equipped
			if (!currentlyHasBow)
			{
				_MESSAGE("MultiMountedCombat: '%s' switching to BOW (distance: %.0f)", 
					riderName ? riderName : "Rider", distance);
				rider->DrawSheatheWeapon(false);  // Sheathe current weapon
				EquipBestBow(rider);
				EquipArrows(rider);
				rider->DrawSheatheWeapon(true);   // Draw bow
			}
			
			// Ensure weapon is drawn
			if (!IsWeaponDrawn(rider))
			{
				rider->DrawSheatheWeapon(true);
			}
			
			// Calculate angle to target
			float dx = target->pos.x - horse->pos.x;
			float dy = target->pos.y - horse->pos.y;
			float angleToTarget = atan2(dx, dy);
			float currentAngle = horse->rot.z;
			float angleDiff = angleToTarget - currentAngle;
			
			while (angleDiff > 3.14159f) angleDiff -= 6.28318f;
			while (angleDiff < -3.14159f) angleDiff += 6.28318f;
			
			// Rotate horse to face target
			const float ROTATION_SPEED = 0.25f;
			float newAngle = currentAngle + (angleDiff * ROTATION_SPEED);
			while (newAngle > 3.14159f) newAngle -= 6.28318f;
			while (newAngle < -3.14159f) newAngle += 6.28318f;
			horse->rot.z = newAngle;
			
			// Fire bow if facing target
			if (fabs(angleDiff) < 0.5f && distance <= RANGED_FIRE_MAX_DISTANCE)
			{
				UpdateBowAttack(rider, true, target);
			}
			
			// ============================================
			// TRY CHARGE MANEUVER (when far away)
			// ============================================
			if (distance >= FULL_COMBAT_CHARGE_MIN && distance <= FULL_COMBAT_CHARGE_MAX)
			{
				if (TryChargeManeuver(horse, rider, target, distance))
				{
					_MESSAGE("MultiMountedCombat: '%s' CHARGING toward target!", 
						riderName ? riderName : "Rider");
				}
			}
			
			// ============================================
			// TRY RAPID FIRE MANEUVER (when far and combat is long)
			// ============================================
			if (GetCombatElapsedTime() >= 20.0f)
			{
				const float DUMMY_MELEE_RANGE = 150.0f;
				if (TryRapidFireManeuver(horse, rider, target, distance, DUMMY_MELEE_RANGE))
				{
					_MESSAGE("MultiMountedCombat: '%s' entering RAPID FIRE mode!", 
						riderName ? riderName : "Rider");
				}
			}
		}
		else if (distance < FULL_COMBAT_MELEE_DISTANCE)
		{
			// ============================================
			// CLOSE - USE MELEE WEAPON
			// ============================================
			
			// Switch to melee if not already equipped
			if (!currentlyHasMelee)
			{
				_MESSAGE("MultiMountedCombat: '%s' switching to MELEE (distance: %.0f)", 
					riderName ? riderName : "Rider", distance);
				rider->DrawSheatheWeapon(false);  // Sheathe bow
				ResetBowAttackState(rider->formID);
				EquipBestMeleeWeapon(rider);
				rider->DrawSheatheWeapon(true);   // Draw melee weapon
			}
			
			// Ensure weapon is drawn
			if (!IsWeaponDrawn(rider))
			{
				rider->DrawSheatheWeapon(true);
			}
			
			// ============================================
			// MELEE ATTACK
			// ============================================
			const float MELEE_ATTACK_RANGE = 300.0f;
			
			if (distance <= MELEE_ATTACK_RANGE)
			{
				// Calculate which side target is on
				float dx = target->pos.x - rider->pos.x;
				float dy = target->pos.y - rider->pos.y;
				float angleToTarget = atan2(dx, dy);
				float riderFacing = rider->rot.z;
				float relativeAngle = angleToTarget - riderFacing;
				
				while (relativeAngle > 3.14159f) relativeAngle -= 6.28318f;
				while (relativeAngle < -3.14159f) relativeAngle += 6.28318f;
				
				const char* attackSide = (relativeAngle >= 0) ? "RIGHT" : "LEFT";
				
				// Try melee attack
				if (PlayMountedAttackAnimation(rider, attackSide))
				{
					UpdateMountedAttackHitDetection(rider, target);
				}
				else
				{
					UpdateMountedAttackHitDetection(rider, target);
				}
			}
		}
		else
		{
			// ============================================
			// MID-RANGE - Keep current weapon, prepare to switch
			// ============================================
			
			// If no weapon equipped, equip melee as default
			if (!currentlyHasBow && !currentlyHasMelee)
			{
				EquipBestMeleeWeapon(rider);
				rider->DrawSheatheWeapon(true);
			}
			
			// Ensure weapon is drawn
			if (!IsWeaponDrawn(rider))
			{
				rider->DrawSheatheWeapon(true);
			}
		}
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
		// COUNT ACTIVE RIDERS BEFORE UPDATE
		// ============================================
		int meleeCountBefore = GetMeleeRiderCount();
		int rangedCountBefore = GetRangedRiderCount();
		
		// ============================================
		// UPDATE EACH RIDER - Check for deaths/dismounts
		// ============================================
		for (int i = 0; i < MAX_MULTI_RIDERS; i++)
		{
			MultiRiderData* data = &g_multiRiders[i];
			if (!data->isValid) continue;
			
			// Track if this was a melee rider before we potentially remove them
			bool wasMeleeRider = (data->role == MultiCombatRole::Melee);
			
			// Look up actors
			TESForm* riderForm = LookupFormByID(data->riderFormID);
			TESForm* horseForm = LookupFormByID(data->horseFormID);
			TESForm* targetForm = LookupFormByID(data->targetFormID);
			
			if (!riderForm || !horseForm || !targetForm)
			{
				_MESSAGE("MultiMountedCombat: Rider %08X - forms invalid, removing", data->riderFormID);
				data->Reset();
				g_multiRiderCount--;
				continue;
			}
			
			Actor* rider = DYNAMIC_CAST(riderForm, TESForm, Actor);
			Actor* horse = DYNAMIC_CAST(horseForm, TESForm, Actor);
			Actor* target = DYNAMIC_CAST(targetForm, TESForm, Actor);
			
			if (!rider || !horse || !target)
			{
				_MESSAGE("MultiMountedCombat: Rider %08X - cast failed, removing", data->riderFormID);
				data->Reset();
				g_multiRiderCount--;
				continue;
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
			
			// Update distance
			data->distanceToTarget = CalculateDistance(rider, target);
			
			// Execute role-specific behavior
			if (data->role == MultiCombatRole::Ranged)
			{
				ExecuteRangedRoleBehavior(data, rider, horse, target);
			}
			else if (data->role == MultiCombatRole::FullCombat)
			{
				ExecuteFullCombatRoleBehavior(data, rider, horse, target);
			}
			else
			{
				ExecuteMeleeRoleBehavior(data, rider, horse, target);
			}
		}
		
		// ============================================
		// CHECK IF A MELEE RIDER DIED OR DISMOUNTED - PROMOTE REMAINING MELEE TO FULL COMBAT
		// ============================================
		int meleeCountAfter = GetMeleeRiderCount();
		int rangedCountAfter = GetRangedRiderCount();
		int totalAfter = g_multiRiderCount;
		
		// Count FullCombat riders
		int fullCombatCount = 0;
		for (int i = 0; i < MAX_MULTI_RIDERS; i++)
		{
			if (g_multiRiders[i].isValid && g_multiRiders[i].role == MultiCombatRole::FullCombat)
			{
				fullCombatCount++;
			}
		}
		
		// If we had 2+ melee riders and now have exactly 1, promote that one to FullCombat
		// This happens when one melee rider dies OR dismounts and another survives
		if (meleeCountBefore >= 2 && meleeCountAfter == 1 && fullCombatCount == 0)
		{
			_MESSAGE("MultiMountedCombat: ========================================");
			_MESSAGE("MultiMountedCombat: MELEE RIDER DOWN/DISMOUNTED! Promoting survivor to FULL COMBAT");
			_MESSAGE("MultiMountedCombat: Before: %d melee | After: %d melee",
				meleeCountBefore, meleeCountAfter);
			_MESSAGE("MultiMountedCombat: ========================================");
			
			// Find the remaining melee rider and promote them
			for (int i = 0; i < MAX_MULTI_RIDERS; i++)
			{
				MultiRiderData* data = &g_multiRiders[i];
				if (!data->isValid) continue;
				if (data->role != MultiCombatRole::Melee) continue;
				
				// Get rider actor
				TESForm* riderForm = LookupFormByID(data->riderFormID);
				TESForm* horseForm = LookupFormByID(data->horseFormID);
				if (!riderForm || !horseForm) continue;
				
				Actor* rider = DYNAMIC_CAST(riderForm, TESForm, Actor);
				Actor* horse = DYNAMIC_CAST(horseForm, TESForm, Actor);
				if (!rider || !horse) continue;
				
				// Promote to FullCombat!
				PromoteMeleeRiderToFullCombat(data, rider, horse);
				break;  // Only promote one rider
			}
		}
		
		// ============================================
		// CHECK IF ALL MELEE RIDERS GONE - SWITCH COMMANDER TO MELEE
		// (This handles the case where Captain is the only one left)
		// ============================================
		
		// Only switch Commander to melee if:
		// - We HAD melee riders before (meleeCountBefore >= 2)
		// - Now all melee riders are gone (meleeCountAfter == 0)
		// - No FullCombat riders either (fullCombatCount == 0)
		// - There's still a ranged rider to convert
		if (meleeCountBefore >= 2 && meleeCountAfter == 0 && fullCombatCount == 0 && rangedCountAfter > 0)
		{
			_MESSAGE("MultiMountedCombat: ========================================");
			_MESSAGE("MultiMountedCombat: ALL MELEE RIDERS DOWN! Switching Commander to MELEE");
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
				
				// Sheathe bow, equip melee weapon
				rider->DrawSheatheWeapon(false);  // Sheathe current weapon
				
				// Give melee weapon if needed
				if (!HasMeleeWeaponInInventory(rider))
				{
					_MESSAGE("MultiMountedCombat: Giving melee weapon to Commander");
					GiveDefaultMountedWeapon(rider);
				}
				
				// Equip best melee weapon
				EquipBestMeleeWeapon(rider);
				rider->DrawSheatheWeapon(true);  // Draw melee weapon
				
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
