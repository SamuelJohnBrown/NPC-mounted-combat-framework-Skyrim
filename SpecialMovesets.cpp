#include "SpecialMovesets.h"
#include "MountedCombat.h"
#include "WeaponDetection.h"
#include "ArrowSystem.h"
#include "DynamicPackages.h"
#include "CombatStyles.h"
#include "FleeingBehavior.h"
#include "AILogging.h"
#include "Helper.h"
#include "config.h"
#include "FactionData.h"  // For IsActorHostileToActor
#include "skse64/GameRTTI.h"
#include "skse64/GameData.h"
#include "skse64/GameReferences.h"
#include "common/ITypes.h"
#include <cmath>
#include <cstdlib>
#include <ctime>

namespace MountedNPCCombatVR
{
	// ============================================
	// CONFIGURATION (now loaded from INI where possible)
	// ============================================
	
	// ============================================
	// MAXIMUM DISTANCE FOR ALL SPECIAL MOVESET
	// Don't trigger any special moveset if target is beyond this distance
	// ============================================
	const float SPECIAL_MOVESET_MAX_DISTANCE = 1700.0f;
	
	// Rear up on approach (facing target head-on)
	// REAR_UP_APPROACH_CHANCE now uses RearUpApproachChance from config
	const float REAR_UP_APPROACH_DISTANCE = 250.0f;  // Must be within this distance
	const float REAR_UP_APPROACH_ANGLE = 0.52f;   // ~30 degrees facing tolerance
	
	// Rear up on damage
	// REAR_UP_DAMAGE_CHANCE now uses RearUpDamageChance from config
	const float REAR_UP_DAMAGE_THRESHOLD = 50.0f;   // Minimum damage to trigger check
	
	// Cooldown (shared between all rear up triggers)
	// REAR_UP_COOLDOWN now uses RearUpCooldown from config
	const float REAR_UP_GLOBAL_COOLDOWN = 8.0f; // 8 seconds global cooldown - no horse can rear up if ANY horse reared up recently
	
	// Horse jump (obstruction escape)
	const UInt32 HORSE_JUMP_BASE_FORMID = 0x0008E6;  // From MountedNPCCombat.esp
	const char* HORSE_JUMP_ESP_NAME = "MountedNPCCombat.esp";
	const float HORSE_JUMP_COOLDOWN = 4.0f;  // 4 seconds between jump attempts
	
	// Charge maneuver configuration (some now loaded from INI)
	// CHARGE_MIN_DISTANCE, CHARGE_MAX_DISTANCE, CHARGE_COOLDOWN, CHARGE_CHANCE_PERCENT from config
	const float CHARGE_CHECK_INTERVAL = 10.0f;    // Check for charge every 10 seconds
	const float CHARGE_REAR_UP_DURATION = 1.5f;   // Time to wait for rear up animation
	const float CHARGE_EQUIP_DURATION = 1.0f; // Time to wait for weapon equip (increased for sheathe + equip)
	const float CHARGE_SPRINT_TIMEOUT = 5.0f;  // Max sprint time before auto-canceling charge
	const float CHARGE_MELEE_RANGE = 110.0f;    // Closer melee range during charge (horse gets right in front of player)
	
	// Rapid Fire maneuver configuration (most now loaded from INI)
	// RapidFireChancePercent, RapidFireCooldown, RapidFireDuration from config
	const float RAPID_FIRE_MIN_COMBAT_TIME = 20.0f;  // Must be 20+ seconds into combat
	const float RAPID_FIRE_CHECK_INTERVAL = 10.0f;   // Check for rapid fire every 10 seconds
	// Note: RAPID_FIRE_COOLDOWN, RAPID_FIRE_DURATION, RAPID_FIRE_CHANCE_PERCENT removed - using config values
	
	// Stand Ground maneuver configuration (now loaded from INI)
	// StandGroundChancePercent, StandGroundCooldown, StandGroundMinDuration, StandGroundMaxDuration from config
	// VALUES IN config.h ARE NOW IGNORED
	const float STAND_GROUND_MAX_DISTANCE = 260.0f;  // Must be within this distance
	const float STAND_GROUND_MIN_DURATION = 3.0f;    // Minimum stand time
	const float STAND_GROUND_MAX_DURATION = 8.0f; // Maximum stand time
	const int STAND_GROUND_CHANCE_PERCENT = 25;      // 25% chance to trigger
	const float STAND_GROUND_CHECK_INTERVAL = 2.0f;  // Check every 2 seconds
	const float STAND_GROUND_COOLDOWN = 5.0f;        // 5 seconds between stand ground attempts
	
	// Mounted vs Mounted turn direction cooldown
	// Prevents snap-turning/jitter when two mounted NPCs fight each other
	const float MOUNTED_VS_MOUNTED_TURN_COOLDOWN = 5.0f;  // 3 seconds before turn direction can change
	const float MOUNTED_VS_MOUNTED_CLOSE_RANGE = 200.0f;  // Within 200 units = close range combat

	// ============================================
	// SYSTEM STATE
	// ============================================
	
	static bool g_maneuverSystemInitialized = false;
	
	// Global rear up tracking (prevents synchronized rear ups)
	static float g_lastGlobalRearUpTime = -8.0f;  // Initialize to allow immediate first use
	
	// Global rapid fire tracking (prevents synchronized rapid fires)
	const float RAPID_FIRE_GLOBAL_COOLDOWN = 10.0f;  // 10 seconds global cooldown - no horse can rapid fire if ANY horse is rapid firing recently
	static float g_lastGlobalRapidFireTime = -10.0f;  // Initialize to allow immediate first use
	
	const int MAX_TRACKED_HORSES = 10;  // Array size (hardcoded max for memory safety)
	// Actual runtime limit uses MaxTrackedMountedNPCs from config
	
	// Cached horse jump idle
	static TESIdleForm* g_horseJumpIdle = nullptr;
	static bool g_jumpIdleInitialized = false;
	
	// Rear up tracking per horse
	struct HorseRearUpTracking
	{
		UInt32 horseFormID;
		float lastRearUpTime;
		float lastKnownHealth;
		bool isValid;
	};
	
	static HorseRearUpTracking g_horseRearUpTracking[MAX_TRACKED_HORSES];
	static int g_horseRearUpCount = 0;
	
	// 90-degree turn direction tracking per horse
	struct HorseTurnData
	{
		UInt32 horseFormID;
		bool clockwise;
		bool wasInMeleeRange;
		float lastTurnDirectionChangeTime;  // Cooldown for mounted vs mounted
		bool isValid;
	};
	
	static HorseTurnData g_horseTurnData[MAX_TRACKED_HORSES];
	static int g_horseTurnCount = 0;
	
	// Horse jump cooldown tracking
	struct HorseJumpData
	{
		UInt32 horseFormID;
		float lastJumpTime;
		bool isValid;
	};
	
	static HorseJumpData g_horseJumpData[MAX_TRACKED_HORSES];
	static int g_horseJumpCount = 0;
	
	// Charge maneuver tracking per horse
	enum class ChargeState
	{
		None = 0,
		RearingUp,       // Playing rear up animation
		EquippingWeapon, // Equipping melee weapon
		Charging,        // Sprinting toward target
		Completed        // Charge finished
	};
	
	struct HorseChargeData
	{
		UInt32 horseFormID;
		UInt32 riderFormID;
		ChargeState state;
		float lastChargeCheckTime;   // For 10-second interval between checks
		float lastChargeCompleteTime; // For 45-second cooldown after charge
		float stateStartTime;        // When current state started
		bool isValid;
	};
	
	static HorseChargeData g_horseChargeData[MAX_TRACKED_HORSES];
	static int g_horseChargeCount = 0;
	
	// Rapid Fire maneuver tracking per horse
	enum class RapidFireState
	{
		None = 0,
		Active,      // Stationary, firing bow
		Completed    // Rapid fire finished, transitioning back to normal
	};
	
	struct HorseRapidFireData
	{
		UInt32 horseFormID;
		UInt32 riderFormID;
		RapidFireState state;
		float lastCheckTime;         // For 10-second interval between checks
		float lastCompleteTime;    // For 45-second cooldown
		float stateStartTime;      // When current state started
		bool isValid;
	};
	
	static HorseRapidFireData g_horseRapidFireData[MAX_TRACKED_HORSES];
	static int g_horseRapidFireCount = 0;
	
	// ============================================
	// STAND GROUND MANEUVER (VS MOBILE NPC TARGETS)
	// ============================================
	// When fighting a non-player NPC target at close range,
	// the horse will stop moving and hold position for a few seconds.
	// This allows the rider to land attacks instead of constantly chasing.
	// Configuration values are in config.h:
	// - StandGroundEnabled, StandGroundMaxDistance, StandGroundMinDuration
	// - StandGroundMaxDuration, StandGroundChancePercent
	// - StandGroundCheckInterval, StandGroundCooldown
	
	enum class StandGroundState
	{
		None = 0,
		Active,  // Standing ground, not moving
		Completed    // Stand ground finished
	};
	
	struct HorseStandGroundData
	{
		UInt32 horseFormID;
		StandGroundState state;
		float lastCheckTime;     // For check interval
		float lastCompleteTime;   // For cooldown
		float stateStartTime;    // When current state started
		float standDuration;      // How long to stand (3-8 seconds)
		float lockedAngle;       // The angle to lock to once 90-degree turn is achieved
		float target90DegreeAngle;  // The target angle for the 90-degree turn (calculated ONCE at start)
		bool noRotation;      // 50% chance - if true, don't do 90-degree turn, just stay facing target
		bool rotationLocked;     // True once the 90-degree turn is complete - NO MORE ROTATION!
		bool target90DegreeAngleSet;  // True once the target angle has been calculated
		bool isValid;
	};
	
	static HorseStandGroundData g_horseStandGroundData[MAX_TRACKED_HORSES];
	static int g_horseStandGroundCount = 0;
	
	// ============================================
	// PLAYER AGGRO SWITCH (VS NON-PLAYER TARGET)
	// ============================================
	// When fighting a non-player NPC and player is nearby,
	// occasionally switch targets to the player with a charge.
	
	const float PLAYER_AGGRO_SWITCH_RANGE = 450.0f;      // Player must be within this range (reduced from 1500)
	const int PLAYER_AGGRO_SWITCH_CHANCE_MAX = 3;        // 3% chance at close range
	const int PLAYER_AGGRO_SWITCH_CHANCE_MIN = 1;      // 1% chance at max range
	const float PLAYER_AGGRO_SWITCH_INTERVAL = 25.0f;    // Check every 20 seconds
	
	struct PlayerAggroSwitchData
	{
		UInt32 horseFormID;
		float lastCheckTime;
		bool isValid;
	};
	
	static PlayerAggroSwitchData g_playerAggroSwitchData[MAX_TRACKED_HORSES];
	static int g_playerAggroSwitchCount = 0;
	
	// ============================================
	// CLOSE RANGE MELEE ASSAULT (EMERGENCY CLOSE COMBAT)
	// ============================================
	// When target gets within 145 units of the rider's side,
	// triggers rapid melee attacks (1 per second) until they move away.
	// 100% trigger chance, 0 cooldown - purely distance-based.
	
	struct CloseRangeMeleeAssaultData
	{
		UInt32 horseFormID;
		bool isActive;
		float lastAttackTime;
		bool isValid;
	};
	
	static CloseRangeMeleeAssaultData g_closeRangeMeleeAssaultData[MAX_TRACKED_HORSES];
	static int g_closeRangeMeleeAssaultCount = 0;
	
	// ============================================
	// MOBILE TARGET INTERCEPT DATA
	// ============================================
	struct MobileInterceptData
	{
		UInt32 horseFormID;
		UInt32 targetFormID;
		bool approachFromRight;  // Which side to approach from
		bool sideChosen;
		bool isValid;
	};
	
	static MobileInterceptData g_mobileInterceptData[MAX_TRACKED_HORSES];
	static int g_mobileInterceptCount = 0;
	
	// Forward declaration for InitTrotIdles REMOVED - trot turn system no longer in use
	
	// ============================================
	// UTILITY FUNCTIONS
	// ============================================
	
	// EnsureRandomSeeded() is now shared from Helper.h - use it directly
	
	// Use shared GetGameTime() from Helper.h instead of local function
	static float GetCurrentTime()
	{
		return GetGameTime();
	}
	
	// Helper function to calculate angle to target
	static float GetAngleToTarget(Actor* horse, Actor* target)
	{
		if (!horse || !target) return 0.0f;
		float dx = target->pos.x - horse->pos.x;
		float dy = target->pos.y - horse->pos.y;
		return atan2(dx, dy);
	}
	
	static HorseRearUpTracking* GetOrCreateRearUpTracking(UInt32 horseFormID)
	{
		// Check if already tracked
		for (int i = 0; i < g_horseRearUpCount; i++)
		{
			if (g_horseRearUpTracking[i].isValid && g_horseRearUpTracking[i].horseFormID == horseFormID)
			{
				return &g_horseRearUpTracking[i];
			}
		}
		
		// Create new entry
		if (g_horseRearUpCount < MAX_TRACKED_HORSES)
		{
			HorseRearUpTracking* data = &g_horseRearUpTracking[g_horseRearUpCount];
			data->horseFormID = horseFormID;
			data->lastRearUpTime = -RearUpCooldown;  // Allow immediate first use
			data->lastKnownHealth = 0;
			data->isValid = true;
			g_horseRearUpCount++;
			return data;
		}
		
		return nullptr;
	}
	
	// ============================================
	// HORSE JUMP IDLE INITIALIZATION
	// ============================================
	
	static void InitJumpIdle()
	{
		if (g_jumpIdleInitialized) return;
		
		_MESSAGE("SpecialMovesets: Loading horse jump animation from %s...", HORSE_JUMP_ESP_NAME);
		
		UInt32 jumpFormID = GetFullFormIdMine(HORSE_JUMP_ESP_NAME, HORSE_JUMP_BASE_FORMID);
		
		_MESSAGE("SpecialMovesets: Looking up HORSE_JUMP - Base: %08X, Full: %08X", 
			HORSE_JUMP_BASE_FORMID, jumpFormID);
		
		if (jumpFormID != 0)
		{
			TESForm* jumpForm = LookupFormByID(jumpFormID);
			if (jumpForm)
			{
				g_horseJumpIdle = DYNAMIC_CAST(jumpForm, TESForm, TESIdleForm);
				if (g_horseJumpIdle)
				{
					_MESSAGE("SpecialMovesets: Found HORSE_JUMP (FormID: %08X)", jumpFormID);
				}
				else
				{
					_MESSAGE("SpecialMovesets: ERROR - FormID %08X is not a TESIdleForm!", jumpFormID);
				}
			}
			else
			{
				_MESSAGE("SpecialMovesets: ERROR - LookupFormByID failed for %08X", jumpFormID);
			}
		}
		else
		{
			_MESSAGE("SpecialMovesets: ERROR - Could not resolve FormID for HORSE_JUMP from %s", HORSE_JUMP_ESP_NAME);
		}
		
		g_jumpIdleInitialized = true;
		_MESSAGE("SpecialMovesets: Horse jump animation initialized - %s", 
			g_horseJumpIdle ? "SUCCESS" : "FAILED");
	}
	
	// ============================================
	// INITIALIZATION
	// ============================================
	
	void InitSpecialMovesets()
	{
		if (g_maneuverSystemInitialized) return;
		
		_MESSAGE("SpecialMovesets: Initializing...");
		
		// Reset global rear up cooldown
		g_lastGlobalRearUpTime = -REAR_UP_GLOBAL_COOLDOWN;
		
		// Reset global rapid fire cooldown
		g_lastGlobalRapidFireTime = -RAPID_FIRE_GLOBAL_COOLDOWN;
		
		// Clear rear up tracking data
		for (int i = 0; i < MAX_TRACKED_HORSES; i++)
		{
			g_horseRearUpTracking[i].isValid = false;
		}
		g_horseRearUpCount = 0;
		
		// Clear turn direction data
		for (int i = 0; i < MAX_TRACKED_HORSES; i++)
		{
			g_horseTurnData[i].isValid = false;
			g_horseTurnData[i].horseFormID = 0;
			g_horseTurnData[i].clockwise = false;
			g_horseTurnData[i].wasInMeleeRange = false;
			g_horseTurnData[i].lastTurnDirectionChangeTime = -5.0f;  // Allow immediate first use
		}
		g_horseTurnCount = 0;
		
		// Clear jump cooldown data
		for (int i = 0; i < MAX_TRACKED_HORSES; i++)
		{
			g_horseJumpData[i].isValid = false;
			g_horseJumpData[i].horseFormID = 0;
			g_horseJumpData[i].lastJumpTime = -HORSE_JUMP_COOLDOWN;
		}
		g_horseJumpCount = 0;
		
		// Combat dismount data - REMOVED (system no longer exists)
		
		// Clear charge data
		for (int i = 0; i < MAX_TRACKED_HORSES; i++)
		{
			g_horseChargeData[i].isValid = false;
		}
		g_horseChargeCount = 0;
		
		// Clear rapid fire data
		for (int i = 0; i < MAX_TRACKED_HORSES; i++)
		{
			g_horseRapidFireData[i].isValid = false;
		}
		g_horseRapidFireCount = 0;
		
		// Clear stand ground data
		for (int i = 0; i < MAX_TRACKED_HORSES; i++)
		{
			g_horseStandGroundData[i].isValid = false;
		}
		g_horseStandGroundCount = 0;
		
		// Clear player aggro switch data
		for (int i = 0; i < MAX_TRACKED_HORSES; i++)
		{
			g_playerAggroSwitchData[i].isValid = false;
		}
		g_playerAggroSwitchCount = 0;
		
		// Clear close range melee assault data
		for (int i = 0; i < MAX_TRACKED_HORSES; i++)
		{
			g_closeRangeMeleeAssaultData[i].isValid = false;
			g_closeRangeMeleeAssaultData[i].horseFormID = 0;
			g_closeRangeMeleeAssaultData[i].isActive = false;
			g_closeRangeMeleeAssaultData[i].lastAttackTime = 0;
		}
		g_closeRangeMeleeAssaultCount = 0;
		
		// Note: g_mobileInterceptData is defined later in the file
		// It will be cleared in InitSpecialMovesets or when first accessed
		
		// Reset initialization flags so idles can be re-cached if needed
		g_jumpIdleInitialized = false;
		g_horseJumpIdle = nullptr;
		// Trot turn idles removed - system no longer in use
		
		_MESSAGE("SpecialMovesets: All state reset complete");
	}
	
	void ShutdownSpecialMovesets()
	{
		if (!g_maneuverSystemInitialized) return;
		
		_MESSAGE("SpecialMovesets: Shutting down...");
		
		for (int i = 0; i < MAX_TRACKED_HORSES; i++)
		{
			g_horseRearUpTracking[i].isValid = false;
			g_horseTurnData[i].isValid = false;
			g_horseJumpData[i].isValid = false;
			g_horseChargeData[i].isValid = false;
			g_horseRapidFireData[i].isValid = false;
		}
		g_horseRearUpCount = 0;
		g_horseTurnCount = 0;
		g_horseJumpCount = 0;
		g_horseChargeCount = 0;
		g_horseRapidFireCount = 0;
		
		g_maneuverSystemInitialized = false;
	}
	
	// ============================================
	// HORSE JUMP MANEUVER
	// ============================================
	
	static HorseJumpData* GetOrCreateJumpData(UInt32 horseFormID)
	{
		// Check if already tracked
		for (int i = 0; i < g_horseJumpCount; i++)
		{
			if (g_horseJumpData[i].isValid && g_horseJumpData[i].horseFormID == horseFormID)
			{
				return &g_horseJumpData[i];
			}
		}
		
		// Create new entry
		if (g_horseJumpCount < MAX_TRACKED_HORSES)
		{
			HorseJumpData* data = &g_horseJumpData[g_horseJumpCount];
			data->horseFormID = horseFormID;
			data->lastJumpTime = -HORSE_JUMP_COOLDOWN;  // Allow immediate first use
			data->isValid = true;
			g_horseJumpCount++;
			return data;
		}
		
		return nullptr;
	}
	
	bool IsHorseJumpOnCooldown(UInt32 horseFormID)
	{
		HorseJumpData* data = GetOrCreateJumpData(horseFormID);
		if (!data) return true;  // If can't track, assume on cooldown
		
		float currentTime = GetCurrentTime();
		return (currentTime - data->lastJumpTime) < HORSE_JUMP_COOLDOWN;
	}
	
	void ClearHorseJumpData(UInt32 horseFormID)
	{
		for (int i = 0; i < g_horseJumpCount; i++)
		{
			if (g_horseJumpData[i].isValid && g_horseJumpData[i].horseFormID == horseFormID)
			{
				// Shift remaining entries
				for (int j = i; j < g_horseJumpCount - 1; j++)
				{
					g_horseJumpData[j] = g_horseJumpData[j + 1];
				}
				g_horseJumpCount--;
				return;
			}
		}
	}
	
	bool TryHorseJumpToEscape(Actor* horse)
	{
		if (!horse) return false;
		
		// Block if in stand ground
		if (IsInStandGround(horse->formID)) return false;
		
		// Block if horse is charging
		if (IsHorseCharging(horse->formID)) return false;
		
		// Initialize jump idle if needed
		InitJumpIdle();
		
		if (!g_horseJumpIdle)
		{
			_MESSAGE("SpecialMovesets: Horse jump idle not available");
			return false;
		}
		
		// Check cooldown
		HorseJumpData* data = GetOrCreateJumpData(horse->formID);
		if (!data) return false;
		
		float currentTime = GetCurrentTime();
		float timeSinceLastJump = currentTime - data->lastJumpTime;
		
		if (timeSinceLastJump < HORSE_JUMP_COOLDOWN)
		{
			// Still on cooldown - silent fail
			return false;
		}
		
		// Get the animation event name
		const char* eventName = g_horseJumpIdle->animationEvent.c_str();
		if (!eventName || strlen(eventName) == 0)
		{
			_MESSAGE("SpecialMovesets: Horse jump idle has no animation event!");
			return false;
		}
		
		// Use the existing SendHorseAnimationEvent function from SingleMountedCombat
		bool result = SendHorseAnimationEvent(horse, eventName);
		
		if (result)
		{
			data->lastJumpTime = currentTime;
			_MESSAGE("SpecialMovesets: Horse %08X JUMPING to escape obstruction (event: %s, cooldown: %.1fs)", 
				horse->formID, eventName, HORSE_JUMP_COOLDOWN);
			return true;
		}
		else
		{
			_MESSAGE("SpecialMovesets: Horse %08X jump animation rejected (graph busy?)", horse->formID);
			return false;
		}
	}
	
	// ============================================
	// HORSE JUMP - ON APPROACH
	// ============================================
	
	bool TryRearUpOnApproach(Actor* horse, Actor* target, float distanceToTarget)
	{
		if (!horse || !target) return false;
		
		// Check if rear up is enabled
		if (!RearUpEnabled) return false;
		
		// Block if rider is fleeing
		if (IsHorseRiderFleeing(horse->formID)) return false;
		
		// Block if in stand ground
		if (IsInStandGround(horse->formID)) return false;
		
		// ============================================
		// EXCLUDE CIVILIANS - They flee, not fight
		// ============================================
		NiPointer<Actor> rider;
		if (CALL_MEMBER_FN(horse, GetMountedBy)(rider) && rider)
		{
			MountedCombatClass combatClass = DetermineCombatClass(rider.get());
			if (combatClass == MountedCombatClass::CivilianFlee)
			{
				return false;
			}
		}
		
		// Check distance
		if (distanceToTarget > REAR_UP_APPROACH_DISTANCE)
		{
			return false;
		}
		
		// Check if horse is facing target head-on
		float dx = target->pos.x - horse->pos.x;
		float dy = target->pos.y - horse->pos.y;
		float angleToTarget = atan2(dx, dy);
		
		float currentAngle = horse->rot.z;
		float angleDiff = angleToTarget - currentAngle;
		
		// Normalize angle difference
		while (angleDiff > 3.14159f) angleDiff -= 6.28318f;
		while (angleDiff < -3.14159f) angleDiff += 6.28318f;
		
		if (fabs(angleDiff) > REAR_UP_APPROACH_ANGLE)
		{
			return false;  // Not facing target head-on
		}
		
		float currentTime = GetCurrentTime();
		
		// ============================================
		// CHECK GLOBAL COOLDOWN FIRST
		// Prevents multiple horses from rearing up at the same time
		// ============================================
		if ((currentTime - g_lastGlobalRearUpTime) < REAR_UP_GLOBAL_COOLDOWN)
		{
			return false;  // Another horse reared up recently
		}
		
		// Check per-horse cooldown (using config value)
		HorseRearUpTracking* data = GetOrCreateRearUpTracking(horse->formID);
		if (!data) return false;
		
		if ((currentTime - data->lastRearUpTime) < RearUpCooldown)
		{
			return false;  // This horse on cooldown
		}
		
		// Roll chance (using config value)
		EnsureRandomSeeded();
		int roll = rand() % 100;
		if (roll >= RearUpApproachChance)
		{
			return false;  // Didn't roll the chance
		}
		
		// Trigger rear up using SingleMountedCombat's animation function
		if (PlayHorseRearUpAnimation(horse))
		{
			data->lastRearUpTime = currentTime;
			g_lastGlobalRearUpTime = currentTime;  // Set global cooldown
			_MESSAGE("SpecialMovesets: Horse %08X REAR UP on approach (global cooldown: %.1fs)", 
				horse->formID, REAR_UP_GLOBAL_COOLDOWN);
			return true;
		}
		
		return false;
	}
	
	// ============================================
	// REAR UP - ON DAMAGE
	// ============================================
	
	bool TryRearUpOnDamage(Actor* horse, float damageAmount)
	{
		if (!horse) return false;
		
		// Check if rear up is enabled
		if (!RearUpEnabled) return false;
		
		// ============================================
		// EXCLUDE CIVILIANS - They flee, not fight
		// ============================================
		NiPointer<Actor> rider;
		if (CALL_MEMBER_FN(horse, GetMountedBy)(rider) && rider)
		{
			MountedCombatClass combatClass = DetermineCombatClass(rider.get());
			if (combatClass == MountedCombatClass::CivilianFlee)
			{
				return false;
			}
		}
		
		// Check if damage exceeds threshold
		if (damageAmount < REAR_UP_DAMAGE_THRESHOLD)
		{
			return false;
		}
		
		float currentTime = GetCurrentTime();
		
		// ============================================
		// CHECK GLOBAL COOLDOWN FIRST
		// Prevents multiple horses from rearing up at the same time
		// ============================================
		if ((currentTime - g_lastGlobalRearUpTime) < REAR_UP_GLOBAL_COOLDOWN)
		{
			return false;  // Another horse reared up recently
		}
		
		// Check per-horse cooldown (using config value)
		HorseRearUpTracking* data = GetOrCreateRearUpTracking(horse->formID);
		if (!data) return false;
		
		if ((currentTime - data->lastRearUpTime) < RearUpCooldown)
		{
			return false;  // This horse on cooldown
		}
		
		// Roll chance (using config value)
		EnsureRandomSeeded();
		int roll = rand() % 100;
		if (roll >= RearUpDamageChance)
		{
			return false;  // Didn't roll the chance
		}
		
		// Trigger rear up
		if (PlayHorseRearUpAnimation(horse))
		{
			data->lastRearUpTime = currentTime;
			g_lastGlobalRearUpTime = currentTime;  // Set global cooldown
			_MESSAGE("SpecialMovesets: Horse %08X REAR UP on damage (%.0f dmg, global cooldown: %.1fs)", 
				horse->formID, damageAmount, REAR_UP_GLOBAL_COOLDOWN);
			return true;
		}
		
		return false;
	}
	
	// ============================================
	// HEALTH TRACKING (for damage detection)
	// ============================================
	
	void UpdateHorseHealth(UInt32 horseFormID, float currentHealth)
	{
		HorseRearUpTracking* data = GetOrCreateRearUpTracking(horseFormID);
		if (data)
		{
			data->lastKnownHealth = currentHealth;
		}
	}
	
	float GetHorseLastHealth(UInt32 horseFormID)
	{
		HorseRearUpTracking* data = GetOrCreateRearUpTracking(horseFormID);
		if (data)
		{
			return data->lastKnownHealth;
		}
		return 0;
	}
	
	// ============================================
	// CLEANUP
	// ============================================
	
	void ClearRearUpData(UInt32 horseFormID)
	{
		for (int i = 0; i < g_horseRearUpCount; i++)
		{
			if (g_horseRearUpTracking[i].isValid && g_horseRearUpTracking[i].horseFormID == horseFormID)
			{
				// Shift remaining entries
				for (int j = i; j < g_horseRearUpCount - 1; j++)
				{
					g_horseRearUpTracking[j] = g_horseRearUpTracking[j + 1];
				}
				g_horseRearUpCount--;
				return;
			}
		}
	}
	
	// ============================================
	// HORSE TROT TURN MANEUVER (Obstruction Avoidance)
	// ============================================
	// REMOVED - Trot turn system is no longer in use
	// Relying on jump system for obstruction escape instead
	// Functions TryHorseTrotTurnToAvoid and TryHorseTrotTurnFromObstruction removed
	
	// ============================================
	// TARGET MOUNT CHECK
	// ============================================
	
	bool IsTargetMounted(Actor* target)
	{
		if (!target) return false;
		
		NiPointer<Actor> targetMount;
		return CALL_MEMBER_FN(target, GetMount)(targetMount) && targetMount;
	}
	
	// ============================================
	// 90-DEGREE TURN MANEUVER (VS ON-FOOT TARGET)
	// ============================================
	static HorseTurnData* GetOrCreateTurnData(UInt32 horseFormID)
	{
		// Check if already tracked
		for (int i = 0; i < g_horseTurnCount; i++)
		{
			if (g_horseTurnData[i].isValid && g_horseTurnData[i].horseFormID == horseFormID)
			{
				return &g_horseTurnData[i];
			}
		}
		
		// Create new entry
		if (g_horseTurnCount < MAX_TRACKED_HORSES)
		{
			HorseTurnData* data = &g_horseTurnData[g_horseTurnCount];
			data->horseFormID = horseFormID;
			data->clockwise = false;
			data->wasInMeleeRange = false;
			data->lastTurnDirectionChangeTime = -5.0f;  // Allow immediate first use
			data->isValid = true;
			g_horseTurnCount++;
			return data;
		}
		
		return nullptr;
	}
	
	bool GetHorseTurnDirectionClockwise(UInt32 horseFormID)
	{
		EnsureRandomSeeded();
		
		HorseTurnData* data = GetOrCreateTurnData(horseFormID);
		if (!data)
		{
			// Fallback to random if tracking is full
			return (rand() % 2) == 0;
		}
		
		// If we haven't chosen a direction yet (or we left melee range), pick one randomly
		if (!data->wasInMeleeRange)
		{
			data->clockwise = (rand() % 2) == 0;
			data->wasInMeleeRange = true;
			
			_MESSAGE("SpecialMovesets: Horse %08X chose %s turn direction for 90-degree maneuver", 
				horseFormID, data->clockwise ? "CLOCKWISE (+90)" : "COUNTER-CLOCKWISE (-90)");
		}
		
		return data->clockwise;
	}
	
	float Get90DegreeTurnAngle(UInt32 horseFormID, float angleToTarget)
	{
		bool clockwise = GetHorseTurnDirectionClockwise(horseFormID);
		
		// 90 degrees = PI/2 radians
		const float NINETY_DEGREES = 1.5708f;
		
		float targetAngle;
		if (clockwise)
		{
			targetAngle = angleToTarget + NINETY_DEGREES;
		}
		else
		{
			targetAngle = angleToTarget - NINETY_DEGREES;
		}
		
		// Normalize to -PI to PI
		while (targetAngle > 3.14159f) targetAngle -= 6.28318f;
		while (targetAngle < -3.14159f) targetAngle += 6.28318f;
		
		return targetAngle;
	}
	
	// ============================================
	// MOUNTED VS MOUNTED TURN ANGLE
	// Same as 90-degree turn but with cooldown to prevent jitter
	// ============================================
	float Get90DegreeTurnAngleForMountedTarget(UInt32 horseFormID, float angleToTarget, float distanceToTarget)
	{
		HorseTurnData* data = GetOrCreateTurnData(horseFormID);
		if (!data)
		{
			// Fallback - just return angle to target
			return angleToTarget;
		}
		
		float currentTime = GetCurrentTime();
		
		// If within close range of another mounted unit, apply cooldown before changing direction
		if (distanceToTarget <= MOUNTED_VS_MOUNTED_CLOSE_RANGE)
		{
			// Check if we're on cooldown for direction changes
			float timeSinceLastChange = currentTime - data->lastTurnDirectionChangeTime;
			
			if (timeSinceLastChange < MOUNTED_VS_MOUNTED_TURN_COOLDOWN)
			{
				// On cooldown - use existing direction without recalculating
				// This prevents constant snap turning
				const float NINETY_DEGREES = 1.5708f;
				float targetAngle;
				if (data->clockwise)
				{
					targetAngle = angleToTarget + NINETY_DEGREES;
				}
				else
				{
					targetAngle = angleToTarget - NINETY_DEGREES;
				}
				
				// Normalize
				while (targetAngle > 3.14159f) targetAngle -= 6.28318f;
				while (targetAngle < -3.14159f) targetAngle += 6.28318f;
				
				return targetAngle;
			}
		}
		
		// Not on cooldown or not close enough - allow normal direction selection
		// This will trigger a new direction if wasInMeleeRange was reset
		bool clockwise = GetHorseTurnDirectionClockwise(horseFormID);
		
		// Update the timestamp when direction is chosen
		if (data->wasInMeleeRange && distanceToTarget <= MOUNTED_VS_MOUNTED_CLOSE_RANGE)
		{
			data->lastTurnDirectionChangeTime = currentTime;
		}
		
		const float NINETY_DEGREES = 1.5708f;
		float targetAngle;
		if (clockwise)
		{
			targetAngle = angleToTarget + NINETY_DEGREES;
		}
		else
		{
			targetAngle = angleToTarget - NINETY_DEGREES;
		}
		
		// Normalize
		while (targetAngle > 3.14159f) targetAngle -= 6.28318f;
		while (targetAngle < -3.14159f) targetAngle += 6.28318f;
		
		return targetAngle;
	}
	
	void NotifyHorseLeftMeleeRange(UInt32 horseFormID)
	{
		for (int i = 0; i < g_horseTurnCount; i++)
		{
			if (g_horseTurnData[i].isValid && g_horseTurnData[i].horseFormID == horseFormID)
			{
				if (g_horseTurnData[i].wasInMeleeRange)
				{
					_MESSAGE("SpecialMovesets: Horse %08X LEFT melee range - will pick new turn direction on next approach", 
						horseFormID);
					g_horseTurnData[i].wasInMeleeRange = false;
				}
				return;
			}
		}
	}
	
	void ClearHorseTurnDirection(UInt32 horseFormID)
	{
		for (int i = 0; i < g_horseTurnCount; i++)
		{
			if (g_horseTurnData[i].isValid && g_horseTurnData[i].horseFormID == horseFormID)
			{
				// Shift remaining entries
				for (int j = i; j < g_horseTurnCount - 1; j++)
				{
					g_horseTurnData[j] = g_horseTurnData[j + 1];
				}
				g_horseTurnCount--;
				return;
			}
		}
	}
	
	void ClearAllHorseTurnDirections()
	{
		for (int i = 0; i < MAX_TRACKED_HORSES; i++)
		{
			g_horseTurnData[i].isValid = false;
		}
		g_horseTurnCount = 0;
	}
	
	// ============================================
	// MOBILE TARGET INTERCEPTION
	// ============================================
	
	static MobileInterceptData* GetOrCreateMobileInterceptData(UInt32 horseFormID)
	{
		for (int i = 0; i < g_mobileInterceptCount; i++)
		{
			if (g_mobileInterceptData[i].isValid && g_mobileInterceptData[i].horseFormID == horseFormID)
			{
				return &g_mobileInterceptData[i];
			}
		}
		
		if (g_mobileInterceptCount < MAX_TRACKED_HORSES)
		{
			MobileInterceptData* data = &g_mobileInterceptData[g_mobileInterceptCount];
			data->horseFormID = horseFormID;
			data->targetFormID = 0;
			data->approachFromRight = false;
			data->sideChosen = false;
			data->isValid = true;
			g_mobileInterceptCount++;
			return data;
		}
		
		return nullptr;
	}
	
	bool IsTargetMobileNPC(Actor* target, UInt32 horseFormID)
	{
		if (!target) return false;
		
		// Player is not considered a "mobile NPC" for interception purposes
		if (g_thePlayer && (*g_thePlayer) && target == (*g_thePlayer))
		{
			return false;
		}
		
		if (target->IsDead(1)) return false;
		
		// ============================================
		// CRITICAL: Skip mobile interception if target is MOUNTED
		// When two mounted units fight each other, they should NOT use
		// mobile interception logic - it causes them to fight for the same
		// spot and can crash the MovementAgentActorAvoider system
		// ============================================
		NiPointer<Actor> targetMount;
		if (CALL_MEMBER_FN(target, GetMount)(targetMount) && targetMount)
		{
			// Target is mounted - use standard approach, not mobile interception
			return false;
		}
		
		return target->IsInCombat();
	}
	
	float GetMobileTargetInterceptionAngle(UInt32 horseFormID, Actor* horse, Actor* target)
	{
		if (!horse || !target)
		{
			return horse ? horse->rot.z : 0;
		}
		
		MobileInterceptData* data = GetOrCreateMobileInterceptData(horseFormID);
		
		float dx = target->pos.x - horse->pos.x;
		float dy = target->pos.y - horse->pos.y;
		float angleToTarget = atan2(dx, dy);
		
		if (!data || !data->sideChosen || data->targetFormID != target->formID)
		{
			if (data)
			{
				EnsureRandomSeeded();
				data->approachFromRight = (rand() % 2) == 0;
				data->sideChosen = true;
				data->targetFormID = target->formID;
				
				// // Rate-limit this log message too
			 static UInt32 lastLoggedHorseApproach = 0;
				static float lastLogTimeApproach = 0;
				float currentTime = GetGameTime();
				
				if (horseFormID != lastLoggedHorseApproach || ( currentTime - lastLogTimeApproach) > 3.0f)
				{
					_MESSAGE("SpecialMovesets: Horse %08X chose to approach mobile target from %s",
						horseFormID, data->approachFromRight ? "RIGHT" : "LEFT");
					lastLoggedHorseApproach = horseFormID;
					lastLogTimeApproach = currentTime;
				}
			}
		}
	
		const float INTERCEPT_ANGLE_OFFSET = 1.05f;
		
		float interceptAngle = angleToTarget;
		if (data && data->approachFromRight)
		{
			interceptAngle += INTERCEPT_ANGLE_OFFSET;
		}
		else
		{
			interceptAngle -= INTERCEPT_ANGLE_OFFSET;
		}
		
		while (interceptAngle > 3.14159f) interceptAngle -= 6.28318f;
		while (interceptAngle < -3.14159f) interceptAngle += 6.28318f;
		
		return interceptAngle;
	}
	
	void NotifyHorseLeftMobileTargetRange(UInt32 horseFormID)
	{
		for (int i = 0; i < g_mobileInterceptCount; i++)
		{
			if (g_mobileInterceptData[i].isValid && g_mobileInterceptData[i].horseFormID == horseFormID)
			{
				if (g_mobileInterceptData[i].sideChosen)
				{
					_MESSAGE("SpecialMovesets: Horse %08X left mobile target range - will pick new approach side",
						horseFormID);
					g_mobileInterceptData[i].sideChosen = false;
				}
				return;
			}
		}
	}
	
	void ClearMobileInterceptData(UInt32 horseFormID)
	{
		for (int i = 0; i < g_mobileInterceptCount; i++)
		{
			if (g_mobileInterceptData[i].isValid && g_mobileInterceptData[i].horseFormID == horseFormID)
			{
				for (int j = i; j < g_mobileInterceptCount - 1; j++)
				{
					g_mobileInterceptData[j] = g_mobileInterceptData[j + 1];
				}
				g_mobileInterceptCount--;
				return;
			}
		}
	}
	
	// ============================================
	// CHARGE MANEUVER
	// ============================================
	
	static HorseChargeData* GetOrCreateChargeData(UInt32 horseFormID)
	{
		// Check if already tracked
		for (int i = 0; i < g_horseChargeCount; i++)
		{
			if (g_horseChargeData[i].isValid && g_horseChargeData[i].horseFormID == horseFormID)
			{
				return &g_horseChargeData[i];
			}
		}
		
		// Create new entry
		if (g_horseChargeCount < MAX_TRACKED_HORSES)
		{
			HorseChargeData* data = &g_horseChargeData[g_horseChargeCount];
			data->horseFormID = horseFormID;
			data->riderFormID = 0;
			data->state = ChargeState::None;
			data->lastChargeCheckTime = -CHARGE_CHECK_INTERVAL;   // Allow immediate first check
			data->lastChargeCompleteTime = -ChargeCooldown;   // Allow immediate first charge
			data->stateStartTime = 0;
			data->isValid = true;
			g_horseChargeCount++;
			return data;
		}
		
		return nullptr;
	}
	
	bool IsHorseCharging(UInt32 horseFormID)
	{
		for (int i = 0; i < g_horseChargeCount; i++)
		{
			if (g_horseChargeData[i].isValid && g_horseChargeData[i].horseFormID == horseFormID)
			{
				return g_horseChargeData[i].state != ChargeState::None && 
				       g_horseChargeData[i].state != ChargeState::Completed;
			}
		}
		return false;
	}
	
	bool TryChargeManeuver(Actor* horse, Actor* rider, Actor* target, float distanceToTarget)
	{
		if (!horse || !rider || !target) return false;
		
		// Check if charge is enabled
		if (!ChargeEnabled) return false;
		
		// Block if rider is fleeing
		if (IsHorseRiderFleeing(horse->formID)) return false;
		
		// Block if in stand ground
		if (IsInStandGround(horse->formID)) return false;
		
		// ============================================
		// EXCLUDE MAGE CASTERS AND CIVILIANS
		// Mages don't charge into melee, civilians flee
		// ============================================
		MountedCombatClass combatClass = DetermineCombatClass(rider);
		if (combatClass == MountedCombatClass::MageCaster || combatClass == MountedCombatClass::CivilianFlee)
		{
			return false;
		}
		
		// ============================================
		// MAXIMUM DISTANCE CHECK - Don't trigger if target too far
		// ============================================
		if (distanceToTarget > SPECIAL_MOVESET_MAX_DISTANCE)
		{
			return false;
		}
		
		// Check if distance is in charge range (using config values)
		if (distanceToTarget < ChargeMinDistance || distanceToTarget > ChargeMaxDistance)
		{
			return false;
		}
		
		HorseChargeData* data = GetOrCreateChargeData(horse->formID);
		if (!data) return false;
		
		// Already charging?
		if (data->state != ChargeState::None && data->state != ChargeState::Completed)
		{
			return false;  // Already in a charge
		}
		
		float currentTime = GetCurrentTime();
		
		// Check cooldown for last charge (using config value)
		if ((currentTime - data->lastChargeCompleteTime) < ChargeCooldown)
		{
			return false;  // Still on cooldown from last charge
		}
		
		// Check 10-second interval
		if ((currentTime - data->lastChargeCheckTime) < CHARGE_CHECK_INTERVAL)
		{
			return false;  // Not time to check yet
		}
		data->lastChargeCheckTime = currentTime;
		
		// Roll chance (using config value)
		EnsureRandomSeeded();
		int roll = rand() % 100;
		if (roll >= ChargeChancePercent)
		{
			return false;  // Didn't roll the chance
		}
		
		// INITIATE CHARGE!
		_MESSAGE("SpecialMovesets: Horse %08X INITIATING CHARGE! (distance: %.0f, roll: %d < %d%%)", 
			horse->formID, distanceToTarget, roll, ChargeChancePercent);

		// Step 1: Play rear up animation
		if (PlayHorseRearUpAnimation(horse))
		{
			data->riderFormID = rider->formID;
			data->state = ChargeState::RearingUp;
			data->stateStartTime = currentTime;
			
			_MESSAGE("SpecialMovesets: Horse %08X rearing up for charge", horse->formID);
			return true;
		}
		
		return false;
	}
	
	bool UpdateChargeManeuver(Actor* horse, Actor* rider, Actor* target, float distanceToTarget, float meleeRange)
	{
		if (!horse) return false;
		
		HorseChargeData* data = GetOrCreateChargeData(horse->formID);
		if (!data) return false;
		
		// Not charging
		if (data->state == ChargeState::None || data->state == ChargeState::Completed)
		{
			return false;
		}
		
		float currentTime = GetCurrentTime();
		float timeInState = currentTime - data->stateStartTime;
		
		switch (data->state)
		{
			case ChargeState::RearingUp:
			{
				// Wait for rear up animation to finish
				if (timeInState >= CHARGE_REAR_UP_DURATION)
				{
					// Before charging, sheathe bow if equipped and equip melee
					if (rider && IsBowEquipped(rider))
					{
						// Sheathe the bow first
						SheatheCurrentWeapon(rider);
						_MESSAGE("SpecialMovesets: Rider %08X sheathing bow for charge", rider->formID);
					}
					
					// Transition to equipping weapon
					data->state = ChargeState::EquippingWeapon;
					data->stateStartTime = currentTime;
				}
				return true;  // Still in charge maneuver
			}
			
			case ChargeState::EquippingWeapon:
			{
				// Wait briefly for sheathe animation, then equip melee
				if (timeInState >= CHARGE_EQUIP_DURATION)
				{
					// Now equip melee weapon
					if (rider)
					{
						ResetBowAttackState(rider->formID);
						EquipBestMeleeWeapon(rider);
						rider->DrawSheatheWeapon(true);  // Draw the melee weapon
						_MESSAGE("SpecialMovesets: Rider %08X equipped melee for charge", rider->formID);
					}
					
					// Transition to charging
					data->state = ChargeState::Charging;
					data->stateStartTime = currentTime;
					
					_MESSAGE("SpecialMovesets: Transitioning to ChargeState::Charging for horse %08X", horse->formID);
					
					if (horse)
					{
						_MESSAGE("SpecialMovesets: About to call StartHorseSprint for horse %08X", horse->formID);
						StartHorseSprint(horse);
						_MESSAGE("SpecialMovesets: Horse %08X CHARGING toward target!", horse->formID);
					}
					else
					{
						_MESSAGE("SpecialMovesets: ERROR - horse is null when trying to start sprint!");
					}
				}
				return true;  // Still in charge maneuver
			}
			
			case ChargeState::Charging:
			{
				// During charge, use closer melee range (110 units instead of default ~195)
				// This makes the horse charge right up to the player
				const float chargeStopDistance = CHARGE_MELEE_RANGE;
				
				// Check if we've reached the closer charge melee range
				if (distanceToTarget <= chargeStopDistance)
				{
					StopHorseSprint(horse);
					data->state = ChargeState::Completed;
					data->stateStartTime = currentTime;
					data->lastChargeCompleteTime = currentTime;
					
					_MESSAGE("SpecialMovesets: Horse %08X charge COMPLETE - reached target (dist: %.0f, threshold: %.0f)", 
						horse->formID, distanceToTarget, chargeStopDistance);
					return false;
				}
				
				// FAILSAFE: Check for sprint timeout (5 seconds max)
				if (timeInState >= CHARGE_SPRINT_TIMEOUT)
				{
					StopHorseSprint(horse);
					data->state = ChargeState::Completed;
					data->stateStartTime = currentTime;
					data->lastChargeCompleteTime = currentTime;
					
					_MESSAGE("SpecialMovesets: Horse %08X charge TIMEOUT (dist: %.0f)", horse->formID, distanceToTarget);
					return false;
				 }
				
				return true;  // Still charging
			}
			
			case ChargeState::Completed:
			{
				if (timeInState >= 1.0f)
				{
					data->state = ChargeState::None;
					data->lastChargeCheckTime = currentTime;
					
					// ============================================
					// RESET WEAPON SWITCHING STATE
					// Allow normal distance-based switching to resume
					// ============================================
					if (rider)
					{
						ClearWeaponSwitchData(rider->formID);
						_MESSAGE("SpecialMovesets: Charge complete - cleared weapon switch data for rider %08X", rider->formID);
					}
				}
				return false;
			}
			
			default:
				return false;
		}
	}
	
	void StopChargeManeuver(UInt32 horseFormID)
	{
		for (int i = 0; i < g_horseChargeCount; i++)
		{
			if (g_horseChargeData[i].isValid && g_horseChargeData[i].horseFormID == horseFormID)
			{
				// Stop the sprint if we were charging
				if (g_horseChargeData[i].state == ChargeState::Charging)
				{
					TESForm* horseForm = LookupFormByID(horseFormID);
					if (horseForm)
					{
						Actor* horse = DYNAMIC_CAST(horseForm, TESForm, Actor);
						if (horse)
						{
							StopHorseSprint(horse);
						}
					}
				}
				
				g_horseChargeData[i].state = ChargeState::None;
				_MESSAGE("SpecialMovesets: Stopped charge maneuver for horse %08X", horseFormID);
				return;
			}
		}
	}
	
	void ClearChargeData(UInt32 horseFormID)
	{
		for (int i = 0; i < g_horseChargeCount; i++)
		{
			if (g_horseChargeData[i].isValid && g_horseChargeData[i].horseFormID == horseFormID)
			{
				// Stop any active charge first
				if (g_horseChargeData[i].state == ChargeState::Charging)
				{
					TESForm* horseForm = LookupFormByID(horseFormID);
					if (horseForm)
					{
						Actor* horse = DYNAMIC_CAST(horseForm, TESForm, Actor);
						if (horse)
						{
							StopHorseSprint(horse);
						}
					}
				}
				
				// Shift remaining entries
				for (int j = i; j < g_horseChargeCount - 1; j++)
				{
					g_horseChargeData[j] = g_horseChargeData[j + 1];
				}
				g_horseChargeCount--;
				_MESSAGE("SpecialMovesets: Cleared charge data for horse %08X", horseFormID);
				return;
			}
		}
	}
	
	// ============================================
	// RAPID FIRE MANEUVER
	// ============================================
	
	static HorseRapidFireData* GetOrCreateRapidFireData(UInt32 horseFormID)
	{
		// Check if already tracked
		for (int i = 0; i < g_horseRapidFireCount; i++)
		{
			if (g_horseRapidFireData[i].isValid && g_horseRapidFireData[i].horseFormID == horseFormID)
			{
				return &g_horseRapidFireData[i];
			}
		}
		
		// Create new entry
		if (g_horseRapidFireCount < MAX_TRACKED_HORSES)
		{
			HorseRapidFireData* data = &g_horseRapidFireData[g_horseRapidFireCount];
			data->horseFormID = horseFormID;
			data->riderFormID = 0;
			data->state = RapidFireState::None;
			data->lastCheckTime = -RAPID_FIRE_CHECK_INTERVAL;  // Allow immediate first check
			data->lastCompleteTime = -RapidFireCooldown;     // Allow immediate first rapid fire
			data->stateStartTime = 0;
			data->isValid = true;
			g_horseRapidFireCount++;
			return data;
		}
		
		return nullptr;
	}
	
	bool IsInRapidFire(UInt32 horseFormID)
	{
		for (int i = 0; i < g_horseRapidFireCount; i++)
		{
			if (g_horseRapidFireData[i].isValid && g_horseRapidFireData[i].horseFormID == horseFormID)
			{
				// Only Active state counts as "in rapid fire" - Completed means transitioning back
				return g_horseRapidFireData[i].state == RapidFireState::Active;
			}
		}
		return false;
	}
	
	bool TryRapidFireManeuver(Actor* horse, Actor* rider, Actor* target, float distanceToTarget, float meleeRange)
	{
		if (!horse || !rider || !target) return false;
		
		// Check if rapid fire is enabled
		if (!RapidFireEnabled) return false;
		
		// Block if rider is fleeing
		if (IsHorseRiderFleeing(horse->formID)) return false;
		
		// Block if in stand ground
		if (IsInStandGround(horse->formID)) return false;
		
		// ============================================
		// EXCLUDE CIVILIANS - They flee, not fight
		// ============================================
		MountedCombatClass combatClass = DetermineCombatClass(rider);
		if (combatClass == MountedCombatClass::CivilianFlee)
		{
			return false;
		}
		
		// Check if rider is a mage
		bool isMage = (combatClass == MountedCombatClass::MageCaster);
		
		// ============================================
		// MUST HAVE BOW ALREADY EQUIPPED (or be a mage)
		// Don't force equip - rely on dynamic weapon switching
		// Mages don't need bows - they cast Ice Spike instead
		// ============================================
		if (!isMage && !IsBowEquipped(rider))
		{
			return false;  // No bow equipped - can't do rapid fire
		}

		// ============================================
		// MAXIMUM DISTANCE CHECK - Don't trigger if target too far
		// ============================================
		if (distanceToTarget > SPECIAL_MOVESET_MAX_DISTANCE)
		{
			return false;
		}
		
		// ============================================
		// DISTANCE CHECK FOR RAPID FIRE
		// Must be at a reasonable distance to fire bow - NOT too close!
		// Minimum: 400 units (to avoid triggering in melee combat)
		// Maximum: 1500 units (beyond this, charge is more appropriate)
		// ============================================
		const float RAPID_FIRE_MIN_DISTANCE = 400.0f;
		const float RAPID_FIRE_MAX_DISTANCE = 1500.0f;
		
		if (distanceToTarget < RAPID_FIRE_MIN_DISTANCE)
		{
			return false;  // Too close - melee combat instead
		}
		
		if (distanceToTarget > RAPID_FIRE_MAX_DISTANCE)
		{
			return false;  // Too far - should charge instead
		}
		
		// Must be 20+ seconds into combat
		float combatTime = GetCurrentTime();  // TODO: Get actual combat start time
		// For now we'll check this in DynamicPackages where we have combat start time
		
		HorseRapidFireData* data = GetOrCreateRapidFireData(horse->formID);
		if (!data) return false;
		
		// Already in rapid fire?
		if (data->state == RapidFireState::Active)
		{
			return false;
		}
		
		float currentTime = GetCurrentTime();
		
		// ============================================
		// GLOBAL COOLDOWN CHECK - Prevent synchronized rapid fires
		// No horse can rapid fire if ANY horse rapid fired recently
		// ============================================
		if ((currentTime - g_lastGlobalRapidFireTime) < RAPID_FIRE_GLOBAL_COOLDOWN)
		{
			return false;  // Another horse rapid fired recently - blocked
		}
		
		// Check cooldown since last rapid fire for THIS horse (using config value)
		if ((currentTime - data->lastCompleteTime) < RapidFireCooldown)
		{
			return false;  // This horse still on cooldown
		}
		
		// Check 10-second interval between checks
		if ((currentTime - data->lastCheckTime) < RAPID_FIRE_CHECK_INTERVAL)
		{
			return false;  // Not time to check yet
		}
		data->lastCheckTime = currentTime;
		
		// Roll chance (using config value)
		EnsureRandomSeeded();
		int roll = rand() % 100;
		if (roll >= RapidFireChancePercent)
		{
			return false;  // Didn't roll the chance
		}
		
		// SUCCESS - Initiate rapid fire!
		_MESSAGE("SpecialMovesets: Horse %08X RAPID FIRE INITIATED! (rolled %d < %d%%) - Horse will STOP for %.1f seconds (global cooldown: %.1fs)",
			horse->formID, roll, RapidFireChancePercent, RapidFireDuration, RAPID_FIRE_GLOBAL_COOLDOWN);
		
		// Set global cooldown
		g_lastGlobalRapidFireTime = currentTime;
		
		data->riderFormID = rider->formID;
		data->state = RapidFireState::Active;
		data->stateStartTime = currentTime;
		
		// ============================================
		// STOP THE HORSE MOVEMENT
		// ============================================
		
		// Stop any sprint
		StopHorseSprint(horse);
		
		// Force AI re-evaluation to stop movement
		Actor_EvaluatePackage(horse, false, false);
		
		// ============================================
		// BOW IS ALREADY EQUIPPED - Just start rapid fire
		// No forced equip - weapon detection handles equipping
		// For mages: Skip bow-related setup, just start spell casting
		// ============================================
		
		// Reset any existing bow attack state (only for non-mages)
		if (!isMage)
		{
			ResetBowAttackState(rider->formID);
			
			// Make sure weapon is drawn
			if (!IsWeaponDrawn(rider))
			{
				rider->DrawSheatheWeapon(true);
			}
		}
		
		// Start the rapid fire attack sequence (bow for archers, Ice Spike for mages)
		StartRapidFireBowAttack(rider->formID, isMage);
		
		if (isMage)
		{
			_MESSAGE("SpecialMovesets: Rider %08X MAGE RAPID FIRE started (%d Ice Spikes)", 
				rider->formID, RapidFireShotCount);
		}
		else
		{
			_MESSAGE("SpecialMovesets: Rider %08X RAPID FIRE bow attack started (%d shots)", 
				rider->formID, RapidFireShotCount);
		}
		
		return true;
	}
	
	bool UpdateRapidFireManeuver(Actor* horse, Actor* rider, Actor* target)
	{
		if (!horse) return false;
		
		HorseRapidFireData* data = GetOrCreateRapidFireData(horse->formID);
		if (!data) return false;
		
		// Not in rapid fire (None or already fully reset)
		if (data->state == RapidFireState::None)
		{
			return false;
		}
		
		float currentTime = GetCurrentTime();
		float timeInState = currentTime - data->stateStartTime;
		
		// ============================================
		// COMPLETED STATE - Reset after short delay
		// ============================================
		if (data->state == RapidFireState::Completed)
		{
			// Reset state after a short delay (1 second)
			if (timeInState >= 1.0f)
			{
				// FULL RESET of all rapid fire state
				data->state = RapidFireState::None;
				data->riderFormID = 0;
				data->stateStartTime = 0;
				// lastCheckTime stays as-is so the 10-second interval works
				// lastCompleteTime stays as-is for the 45-second cooldown
				
				// Reset the rapid fire bow attack state for future use
				if (rider)
				{
					// Final cleanup - ensure bow attack state is fully reset
					ResetRapidFireBowAttack(rider->formID);
					ResetBowAttackState(rider->formID);
					
					// ============================================
					// RESET WEAPON SWITCHING STATE
					// Allow normal distance-based switching to resume
					// ============================================
					ClearWeaponSwitchData(rider->formID);
					_MESSAGE("SpecialMovesets: Rapid fire complete - cleared weapon switch data for rider %08X", rider->formID);
				}
				
				_MESSAGE("SpecialMovesets: Horse %08X rapid fire state fully RESET - ready for next use after %.0f sec cooldown", 
					horse->formID, RapidFireCooldown);
			}
			return false;
		}
		
		// ============================================
		// ACTIVE STATE - Horse stationary, firing arrows
		// ============================================
		
		// EVERY FRAME: Ensure horse stays stopped
		StopHorseSprint(horse);
		
		// Check if target is too far away (5000+ units) - abort rapid fire
		// Increased from 2000 to prevent premature abort
		if (target && horse)
		{
			float dx = target->pos.x - horse->pos.x;
			float dy = target->pos.y - horse->pos.y;
			float distanceToTarget = sqrt(dx * dx + dy * dy);
			
			const float RAPID_FIRE_ABORT_DISTANCE = 1800.0f;
			
			if (distanceToTarget >= RAPID_FIRE_ABORT_DISTANCE)
			{
				// Target too far - abort rapid fire and transition to completed
				data->state = RapidFireState::Completed;
				data->stateStartTime = currentTime;
				
				// Reset bow attack state
				if (rider)
				{
					ResetRapidFireBowAttack(rider->formID);
				}
				
				_MESSAGE("SpecialMovesets: Horse %08X rapid fire ABORTED - target escaped (%.0f units)", horse->formID, distanceToTarget);
				return false;
			}
		}
		
		// Check if duration complete OR all shots fired (using config value)
		bool allShotsFired = rider && !IsRapidFireBowAttackActive(rider->formID);
		
		if (timeInState >= RapidFireDuration || allShotsFired)
		{
			// Rapid fire complete - transition to Completed state
			data->state = RapidFireState::Completed;
			data->stateStartTime = currentTime;
			data->lastCompleteTime = currentTime;  // Start cooldown
			
			_MESSAGE("SpecialMovesets: Horse %08X rapid fire complete after %.1f sec. Cooldown: %.0f sec", 
				horse->formID, timeInState, RapidFireCooldown);
			
			if (rider)
			{
				_MESSAGE("SpecialMovesets: Rider %08X returning to normal combat", rider->formID);
				
				// =========================
				// CLEAN EXIT FROM RAPID FIRE
				// =========================
				
				// Reset bow attack state first - DON'T fire another arrow!
				// PlayBowReleaseAnimation now fires immediately, so skip it
				ResetRapidFireBowAttack(rider->formID);
				ResetBowAttackState(rider->formID);
				
				// Clear weapon switch data to allow normal distance-based switching
				ClearWeaponSwitchData(rider->formID);
			}
			
			return false;
		}
		
		// ============================================
		// STILL IN RAPID FIRE - Update bow attack sequence
		// ============================================
		
		if (rider && target)
		{
			_MESSAGE("SpecialMovesets: Updating rapid fire bow attack for rider %08X (timeInState: %.2f)", 
				rider->formID, timeInState);
			UpdateRapidFireBowAttack(rider, target);
		}
		else
		{
			_MESSAGE("SpecialMovesets: WARNING - Cannot update rapid fire bow attack - rider: %p, target: %p", 
				rider, target);
		}
		
		return true;  // Still in rapid fire
	}
	
	void StopRapidFireManeuver(UInt32 horseFormID)
	{
		for (int i = 0; i < g_horseRapidFireCount; i++)
		{
			if (g_horseRapidFireData[i].isValid && g_horseRapidFireData[i].horseFormID == horseFormID)
			{
				float currentTime = GetCurrentTime();
				g_horseRapidFireData[i].state = RapidFireState::None;
				g_horseRapidFireData[i].lastCompleteTime = currentTime;
				_MESSAGE("SpecialMovesets: Stopped rapid fire for horse %08X", horseFormID);
				return;
			}
		}
	}
	
	void ClearRapidFireData(UInt32 horseFormID)
	{
		for (int i = 0; i < g_horseRapidFireCount; i++)
		{
			if (g_horseRapidFireData[i].isValid && g_horseRapidFireData[i].horseFormID == horseFormID)
			{
				// Also clear the rider's rapid fire bow attack state
				if (g_horseRapidFireData[i].riderFormID != 0)
				{
					ResetRapidFireBowAttack(g_horseRapidFireData[i].riderFormID);
				}
				
				// Shift remaining entries
				for (int j = i; j < g_horseRapidFireCount - 1; j++)
				{
					g_horseRapidFireData[j] = g_horseRapidFireData[j + 1];
				}
				g_horseRapidFireCount--;
				_MESSAGE("SpecialMovesets: Cleared rapid fire data for horse %08X", horseFormID);
				return;
			}
		}
	}
	
	// ============================================
	// STAND GROUND MANEUVER IMPLEMENTATION
	// ============================================
	
	static HorseStandGroundData* GetOrCreateStandGroundData(UInt32 horseFormID)
	{
		// Check if already tracked
		for (int i = 0; i < g_horseStandGroundCount; i++)
		{
			if (g_horseStandGroundData[i].isValid && g_horseStandGroundData[i].horseFormID == horseFormID)
			{
				return &g_horseStandGroundData[i];
			}
		}
		
		// Find empty slot
		if (g_horseStandGroundCount < MAX_TRACKED_HORSES)
		{
			HorseStandGroundData* data = &g_horseStandGroundData[g_horseStandGroundCount];
			data->horseFormID = horseFormID;
			data->state = StandGroundState::None;
			data->lastCheckTime = -StandGroundCheckInterval;
			data->lastCompleteTime = -StandGroundCooldown;
			data->stateStartTime = 0;
			data->standDuration = 0;
			data->lockedAngle = 0;
			data->target90DegreeAngle = 0;
			data->noRotation = false;
			data->rotationLocked = false;
			data->target90DegreeAngleSet = false;
			data->isValid = true;
			g_horseStandGroundCount++;
			return data;
		}

		return nullptr;
	}
	
	bool IsInStandGround(UInt32 horseFormID)
	{
		for (int i = 0; i < g_horseStandGroundCount; i++)
		{
			if (g_horseStandGroundData[i].isValid && g_horseStandGroundData[i].horseFormID == horseFormID)
			{
				return g_horseStandGroundData[i].state == StandGroundState::Active;
			}
		}
		return false;
	}
	
	bool IsStandGroundNoRotation(UInt32 horseFormID)
	{
		for (int i = 0; i < g_horseStandGroundCount; i++)
		{
			if (g_horseStandGroundData[i].isValid && g_horseStandGroundData[i].horseFormID == horseFormID)
			{
				return g_horseStandGroundData[i].noRotation;
			}
		}
		return false;
	}
	
	// Check if stand ground rotation is LOCKED (90-degree turn complete)
	bool IsStandGroundRotationLocked(UInt32 horseFormID)
	{
		for (int i = 0; i < g_horseStandGroundCount; i++)
		{
			if (g_horseStandGroundData[i].isValid && g_horseStandGroundData[i].horseFormID == horseFormID)
			{
				return g_horseStandGroundData[i].rotationLocked;
			}
		}
		return false;
	}
	
	// Get the locked angle for a stand ground horse
	float GetStandGroundLockedAngle(UInt32 horseFormID)
	{
		for (int i = 0; i < g_horseStandGroundCount; i++)
		{
			if (g_horseStandGroundData[i].isValid && g_horseStandGroundData[i].horseFormID == horseFormID)
			{
				return g_horseStandGroundData[i].lockedAngle;
			}
		}
		return 0.0f;
	}
	
	// Lock the rotation for a stand ground horse at a specific angle
	void LockStandGroundRotation(UInt32 horseFormID, float angle)
	{
		for (int i = 0; i < g_horseStandGroundCount; i++)
		{
			if (g_horseStandGroundData[i].isValid && g_horseStandGroundData[i].horseFormID == horseFormID)
			{
				g_horseStandGroundData[i].rotationLocked = true;
				g_horseStandGroundData[i].lockedAngle = angle;
				_MESSAGE("SpecialMovesets: Horse %08X rotation LOCKED at angle %.2f", horseFormID, angle);
				return;
			}
		}
	}
	
	// Get the target 90-degree angle for stand ground (calculated ONCE at start)
	// Returns the stored angle, or calculates it if not set yet
	float GetStandGroundTarget90DegreeAngle(UInt32 horseFormID, float angleToTarget)
	{
		for (int i = 0; i < g_horseStandGroundCount; i++)
		{
			if (g_horseStandGroundData[i].isValid && g_horseStandGroundData[i].horseFormID == horseFormID)
			{
				// If target angle already set, return the stored value (no recalculation!)
				if (g_horseStandGroundData[i].target90DegreeAngleSet)
				{
					return g_horseStandGroundData[i].target90DegreeAngle;
				}
				
				// Not set yet - calculate it once and store it
				float target90Angle = Get90DegreeTurnAngle(horseFormID, angleToTarget);
				g_horseStandGroundData[i].target90DegreeAngle = target90Angle;
				g_horseStandGroundData[i].target90DegreeAngleSet = true;
				
				_MESSAGE("SpecialMovesets: Horse %08X - 90-degree target angle SET to %.2f (will NOT change)", 
					horseFormID, target90Angle);
				
				return target90Angle;
			}
		}
		
		// Fallback - calculate on the fly (shouldn't happen during stand ground)
		return Get90DegreeTurnAngle(horseFormID, angleToTarget);
	}
	
	bool TryStandGroundManeuver(Actor* horse, Actor* rider, Actor* target, float distanceToTarget)
	{
		if (!horse || !rider || !target) return false;
		
		// Check if enabled in config
		if (!StandGroundEnabled) return false;
		
		// Block if rider is fleeing
		if (IsHorseRiderFleeing(horse->formID)) return false;
		
		// Only trigger when fighting a NON-PLAYER target
		if (g_thePlayer && (*g_thePlayer) && target == (*g_thePlayer))
		{
			return false;
		}
		
		// ============================================
		// EXCLUDE MAGE CASTERS AND CIVILIANS
		// Mages use their own stationary behavior, civilians flee
		// ============================================
		MountedCombatClass combatClass = DetermineCombatClass(rider);
		if (combatClass == MountedCombatClass::MageCaster || combatClass == MountedCombatClass::CivilianFlee)
		{
			return false;
		}
		
		// ============================================
		// MAXIMUM DISTANCE CHECK - Don't trigger if target too far
		// ============================================
		if (distanceToTarget > SPECIAL_MOVESET_MAX_DISTANCE)
		{
			return false;
		}
		
		// Must be within stand ground range
		if (distanceToTarget > StandGroundMaxDistance)
		{
			return false;
		}
		
		HorseStandGroundData* data = GetOrCreateStandGroundData(horse->formID);
		if (!data) return false;
		
		// Already standing ground?
		if (data->state == StandGroundState::Active)
		{
			return false;
		}
		
		float currentTime = GetCurrentTime();
		
		// Check cooldown since last stand ground
		if ((currentTime - data->lastCompleteTime) < StandGroundCooldown)
		{
			return false;
		}
		
		// Check interval between checks
		if ((currentTime - data->lastCheckTime) < StandGroundCheckInterval)
		{
			return false;  // Not time to check yet
		}
		data->lastCheckTime = currentTime;
		
		// Roll chance
		EnsureRandomSeeded();
		int roll = rand() % 100;
		if (roll >= StandGroundChancePercent)
		{
			return false;
		}
		
		// SUCCESS - Initiate stand ground!
		// Calculate random duration between min-max seconds
		float durationRange = StandGroundMaxDuration - StandGroundMinDuration;
		data->standDuration = StandGroundMinDuration + (((float)(rand() % 100)) / 100.0f * durationRange);
		
		// 50% chance to skip 90-degree rotation - abort if true
		data->noRotation = ((rand() % 100) < 50);
		
		// Set target angle for 90-degree turn (if not skipping rotation)
		if (!data->noRotation)
		{
			// always use horse->formID here
			data->target90DegreeAngle = Get90DegreeTurnAngle(horse->formID, GetAngleToTarget(horse, target));
			data->target90DegreeAngleSet = true;
		}
		else
		{
			data->target90DegreeAngleSet = false;
		}
		
		data->state = StandGroundState::Active;
		data->stateStartTime = currentTime;

		_MESSAGE("SpecialMovesets: Horse %08X STANDING GROUND for %.1f seconds %s (rolled %d < %d%%, dist: %.0f)",
			horse->formID, data->standDuration, 
			data->noRotation ? "(NO ROTATION)" : "(with 90-deg turn)",
			roll, StandGroundChancePercent, distanceToTarget);
		
		return true;  // Stand ground initiated
	}

	// NOTE: TryMountedVsMountedStandGround has been REMOVED
	// Mounted vs mounted combat no longer uses stand ground maneuver
	// The horses will continue to move and face each other directly
	
	bool UpdateStandGroundManeuver(Actor* horse, Actor* target)
	{
		if (!horse) return false;
		
		HorseStandGroundData* data = GetOrCreateStandGroundData(horse->formID);
		if (!data) return false;
		
		// Not standing ground
		if (data->state == StandGroundState::None)
		{
			return false;
		}
		
		float currentTime = GetCurrentTime();
		float timeInState = currentTime - data->stateStartTime;
		
		// Completed state - reset after short delay
		if (data->state == StandGroundState::Completed)
		{
			if (timeInState >= 0.5f)
			{
				data->state = StandGroundState::None;
				data->rotationLocked = false;  // Reset rotation lock
				data->lockedAngle = 0;
				_MESSAGE("SpecialMovesets: Horse %08X stand ground fully RESET", horse->formID);
			}
			return false;
		}
		
		// Active state - check if duration complete or target moved away
		if (data->state == StandGroundState::Active)
		{
			// Check if target moved too far away (aborts stand ground)
			if (target)
			{
				float dx = target->pos.x - horse->pos.x;
				float dy = target->pos.y - horse->pos.y;
				float distanceToTarget = sqrt(dx * dx + dy * dy);
				
				// If target is now far away (>400 units), stop standing ground
				if (distanceToTarget > 400.0f)
				{
					data->state = StandGroundState::Completed;
					data->stateStartTime = currentTime;
					data->lastCompleteTime = currentTime;
					data->rotationLocked = false;  // Reset rotation lock
					data->lockedAngle = 0;
					
					// Let dynamic weapon switching handle weapon equip based on distance
					// Don't force melee - the distance-based system will choose appropriately
					
					_MESSAGE("SpecialMovesets: Horse %08X stand ground ENDED - target moved away (%.0f units)", 
						horse->formID, distanceToTarget);
					return false;
				}
			}
			
			// Check if duration complete
			if (timeInState >= data->standDuration)
			{
				data->state = StandGroundState::Completed;
				data->stateStartTime = currentTime;
				data->lastCompleteTime = currentTime;
				data->rotationLocked = false;  // Reset rotation lock
				data->lockedAngle = 0;
				
				_MESSAGE("SpecialMovesets: Horse %08X stand ground COMPLETE after %.1f seconds", 
					horse->formID, timeInState);
				
				return false;
			}
			
			// Still standing ground - ensure horse stays stopped
			StopHorseSprint(horse);
			
			return true;
		}
		
		return false;
	}
	
	void StopStandGroundManeuver(UInt32 horseFormID)
	{
		for (int i = 0; i < g_horseStandGroundCount; i++)
		{
			if (g_horseStandGroundData[i].isValid && g_horseStandGroundData[i].horseFormID == horseFormID)
			{
				float currentTime = GetCurrentTime();
				g_horseStandGroundData[i].state = StandGroundState::None;
				g_horseStandGroundData[i].lastCompleteTime = currentTime;
				g_horseStandGroundData[i].rotationLocked = false;  // Reset rotation lock
				g_horseStandGroundData[i].lockedAngle = 0;
				_MESSAGE("SpecialMovesets: Stopped stand ground for horse %08X", horseFormID);
				return;
			}
		}
	}
	
	void ClearStandGroundData(UInt32 horseFormID)
	{
		for (int i = 0; i < g_horseStandGroundCount; i++)
		{
			if (g_horseStandGroundData[i].isValid && g_horseStandGroundData[i].horseFormID == horseFormID)
			{
				// Shift remaining entries
				for (int j = i; j < g_horseStandGroundCount - 1; j++)
				{
					g_horseStandGroundData[j] = g_horseStandGroundData[j + 1];
				}
				g_horseStandGroundCount--;
				_MESSAGE("SpecialMovesets: Cleared stand ground data for horse %08X", horseFormID);
				return;
			}
		}
	}
	
	// ============================================
	// PLAYER AGGRO SWITCH IMPLEMENTATION
	// ============================================
	
	static PlayerAggroSwitchData* GetOrCreatePlayerAggroSwitchData(UInt32 horseFormID)
	{
		// Check if already tracked
		for (int i = 0; i < g_playerAggroSwitchCount; i++)
		{
			if (g_playerAggroSwitchData[i].isValid && g_playerAggroSwitchData[i].horseFormID == horseFormID)
			{
				return &g_playerAggroSwitchData[i];
			}
		}
		
		// Create new entry
		if (g_playerAggroSwitchCount < MAX_TRACKED_HORSES)
		{
			PlayerAggroSwitchData* data = &g_playerAggroSwitchData[g_playerAggroSwitchCount];
			data->horseFormID = horseFormID;
			data->lastCheckTime = -PLAYER_AGGRO_SWITCH_INTERVAL;  // Allow immediate first check
			data->isValid = true;
			g_playerAggroSwitchCount++;
			return data;
		}
		
		return nullptr;
	}
	
	bool TryPlayerAggroSwitch(Actor* horse, Actor* rider, Actor* currentTarget)
	{
		if (!horse || !rider || !currentTarget) return false;
		
		// ============================================
		// EXCLUDE CIVILIANS - They flee, not fight
		// ============================================
		MountedCombatClass combatClass = DetermineCombatClass(rider);
		if (combatClass == MountedCombatClass::CivilianFlee)
		{
			return false;
		}
		
		// Only trigger when fighting a NON-PLAYER target
		if (g_thePlayer && (*g_thePlayer) && currentTarget == (*g_thePlayer))
		{
			return false;
		}
		
		Actor* player = *g_thePlayer;
		
		// Skip if already targeting player
		if (currentTarget == player) return false;
		
		// Check if player is within range
		float dx = player->pos.x - horse->pos.x;
		float dy = player->pos.y - horse->pos.y;
		float distanceToPlayer = sqrt(dx * dx + dy * dy);
		
		if (distanceToPlayer > PLAYER_AGGRO_SWITCH_RANGE)
		{
			return false;  // Player too far
		}
		
		// ============================================
		// MAXIMUM DISTANCE CHECK - Don't trigger if player beyond special moveset range
		// ============================================
		if (distanceToPlayer > SPECIAL_MOVESET_MAX_DISTANCE)
		{
			return false;
		}
		
		// Check if player is alive
		if (player->IsDead(1))
		{
			return false;
		}
		
		PlayerAggroSwitchData* data = GetOrCreatePlayerAggroSwitchData(horse->formID);
		if (!data) return false;
		
		float currentTime = GetCurrentTime();
		
		// Check 20-second interval
		if ((currentTime - data->lastCheckTime) < PLAYER_AGGRO_SWITCH_INTERVAL)
		{
			return false;  // Not time to check yet
		}
		data->lastCheckTime = currentTime;
		
		// ============================================
		// DISTANCE-SCALED CHANCE
		// Closer player = higher chance (15% at 0 units)
		// Further player = lower chance (2% at 900 units)
		// Linear interpolation based on distance
		// ============================================
		float distanceRatio = distanceToPlayer / PLAYER_AGGRO_SWITCH_RANGE;  // 0.0 at closest, 1.0 at max range
		int scaledChance = PLAYER_AGGRO_SWITCH_CHANCE_MAX - 
			(int)((PLAYER_AGGRO_SWITCH_CHANCE_MAX - PLAYER_AGGRO_SWITCH_CHANCE_MIN) * distanceRatio);
		
		// Ensure chance is within bounds
		if (scaledChance < PLAYER_AGGRO_SWITCH_CHANCE_MIN) scaledChance = PLAYER_AGGRO_SWITCH_CHANCE_MIN;
		if (scaledChance > PLAYER_AGGRO_SWITCH_CHANCE_MAX) scaledChance = PLAYER_AGGRO_SWITCH_CHANCE_MAX;
		
		// Roll distance-scaled chance
		EnsureRandomSeeded();
		int roll = rand() % 100;
		if (roll >= scaledChance)
		{
			return false;  // Didn't roll the chance
		}
		
		// SUCCESS - Switch target to player!
		_MESSAGE("SpecialMovesets: Horse %08X SWITCHING TARGET to PLAYER! (was fighting %08X, rolled %d < %d%% [dist: %.0f])",
			horse->formID, currentTarget->formID, roll, scaledChance, distanceToPlayer);
		
		// Set combat target to player
		UInt32 playerHandle = player->CreateRefHandle();
		if (playerHandle != 0 && playerHandle != *g_invalidRefHandle)
		{
			rider->currentCombatTarget = playerHandle;
			horse->currentCombatTarget = playerHandle;
		}
		
		// Set rider to attack-on-sight
		rider->flags2 |= Actor::kFlag_kAttackOnSight;
		
		// Force the rider to draw weapon if not drawn
		if (!IsWeaponDrawn(rider))
		{
			rider->DrawSheatheWeapon(true);
		}
		
		// Now trigger a charge toward the player!
		// This makes the switch dramatic and aggressive
		if (TryChargeManeuver(horse, rider, player, distanceToPlayer))
		{
			_MESSAGE("SpecialMovesets: Horse %08X CHARGING at player after aggro switch!", horse->formID);
		}
		
		return true;
	}
	
	// ============================================
	// CLEANUP FUNCTIONS
	// ============================================
	
	void ClearPlayerAggroSwitchData(UInt32 horseFormID)
	{
		for (int i = 0; i < g_playerAggroSwitchCount; i++)
		{
			if (g_playerAggroSwitchData[i].isValid && g_playerAggroSwitchData[i].horseFormID == horseFormID)
			{
				// Shift remaining entries
				for (int j = i; j < g_playerAggroSwitchCount - 1; j++)
				{
					g_playerAggroSwitchData[j] = g_playerAggroSwitchData[j + 1];
				}
				g_playerAggroSwitchCount--;
				_MESSAGE("SpecialMovesets: Cleared player aggro switch data for horse %08X", horseFormID);
				return;
			}
		}
	}
	
	// ============================================
	// CLOSE RANGE MELEE ASSAULT IMPLEMENTATION
	// ============================================
	
	static CloseRangeMeleeAssaultData* GetOrCreateCloseRangeMeleeAssaultData(UInt32 horseFormID)
	{
		// Check if already tracked
		for (int i = 0; i < g_closeRangeMeleeAssaultCount; i++)
		{
			if (g_closeRangeMeleeAssaultData[i].isValid && g_closeRangeMeleeAssaultData[i].horseFormID == horseFormID)
			{
				return &g_closeRangeMeleeAssaultData[i];
			}
		}
		
		// Create new entry
		if (g_closeRangeMeleeAssaultCount < MAX_TRACKED_HORSES)
		{
			CloseRangeMeleeAssaultData* data = &g_closeRangeMeleeAssaultData[g_closeRangeMeleeAssaultCount];
			data->horseFormID = horseFormID;
			data->isActive = false;
			data->lastAttackTime = 0;
			data->isValid = true;
			g_closeRangeMeleeAssaultCount++;
			return data;
		}
		
		return nullptr;
	}
	
	bool IsInCloseRangeMeleeAssault(UInt32 horseFormID)
	{
		for (int i = 0; i < g_closeRangeMeleeAssaultCount; i++)
		{
			if (g_closeRangeMeleeAssaultData[i].isValid && g_closeRangeMeleeAssaultData[i].horseFormID == horseFormID)
			{
				return g_closeRangeMeleeAssaultData[i].isActive;
			}
		}
		return false;
	}
	
	bool TryCloseRangeMeleeAssault(Actor* horse, Actor* rider, Actor* target)
	{
		if (!horse || !rider || !target) return false;
		
		// Block if rider is fleeing
		if (IsHorseRiderFleeing(horse->formID)) return false;
		
		// ============================================
		// 3-SECOND LOCK AT COMBAT START
		// Don't trigger close range assault in the first 3 seconds of combat
		// This prevents immediate aggressive behavior when combat begins
		// ============================================
		float combatTime = GetCombatElapsedTime();
		if (combatTime < 3.0f)
		{
			return false;
		}
		
		// ============================================
		// EXCLUDE MAGE CASTERS AND CIVILIANS
		// Mages and civilians don't use melee assault
		// ============================================
		MountedCombatClass combatClass = DetermineCombatClass(rider);
		if (combatClass == MountedCombatClass::MageCaster || combatClass == MountedCombatClass::CivilianFlee)
		{
			return false;
		}
		
		// Calculate distance to target
		float dx = target->pos.x - horse->pos.x;
		float dy = target->pos.y - horse->pos.y;
		float distanceToTarget = sqrt(dx * dx + dy * dy);
		
		// Check if target is within close range melee assault distance
		if (distanceToTarget > CloseRangeMeleeAssaultDistance)
		{
			// Target not close enough - check if we need to deactivate
			CloseRangeMeleeAssaultData* data = GetOrCreateCloseRangeMeleeAssaultData(horse->formID);
			if (data && data->isActive)
			{
				data->isActive = false;
				_MESSAGE("SpecialMovesets: Horse %08X close range melee assault ENDED - target moved away (%.0f units)",
					horse->formID, distanceToTarget);
			}
			return false;
		}
		
		// ============================================
		// TARGET IS WITHIN RANGE - FORCE MELEE WEAPON!
		// THIS OVERRIDES ANY BOW/RANGED STATE
		// ============================================
		if (!IsMeleeEquipped(rider))
		{
			_MESSAGE("SpecialMovesets: Horse %08X - FORCING MELEE EQUIP for close range assault!",
				horse->formID);
			
			// Sheathe any current weapon (bow)
			if (IsBowEquipped(rider))
			{
				SheatheCurrentWeapon(rider);
			}
			
			// Force equip melee weapon
			EquipBestMeleeWeapon(rider);
			rider->DrawSheatheWeapon(true);  // Force draw
			
			// Reset any bow attack state
			ResetBowAttackState(rider->formID);
		}
		
		// Target is within range - activate assault mode
		CloseRangeMeleeAssaultData* data = GetOrCreateCloseRangeMeleeAssaultData(horse->formID);
		if (!data) return false;
		
		// If not already active, log activation
		if (!data->isActive)
		{
			data->isActive = true;
			data->lastAttackTime = GetCurrentTime() - CloseRangeMeleeAssaultInterval; // Allow immediate first attack
			_MESSAGE("SpecialMovesets: Horse %08X CLOSE RANGE MELEE ASSAULT activated! (target at %.0f units)",
				horse->formID, distanceToTarget);
		}
		
		return true;
	}
	
	bool UpdateCloseRangeMeleeAssault(Actor* horse, Actor* rider, Actor* target)
	{
		if (!horse || !rider || !target) return false;
		
		CloseRangeMeleeAssaultData* data = GetOrCreateCloseRangeMeleeAssaultData(horse->formID);
		if (!data || !data->isActive) return false;
		
		// Check distance again - target may have moved
		float dx = target->pos.x - horse->pos.x;
		float dy = target->pos.y - horse->pos.y;
		float distanceToTarget = sqrt(dx * dx + dy * dy);
		
		// If target moved out of range, deactivate
		if (distanceToTarget > CloseRangeMeleeAssaultDistance)
		{
			data->isActive = false;
			_MESSAGE("SpecialMovesets: Horse %08X close range melee assault ENDED - target escaped (%.0f units)",
				horse->formID, distanceToTarget);
			return false;
		}
		
		// Check if enough time has passed for another attack
		float currentTime = GetCurrentTime();
		float timeSinceLastAttack = currentTime - data->lastAttackTime;
			
		if (timeSinceLastAttack < CloseRangeMeleeAssaultInterval)
		{
			return true; // Still in assault mode, waiting for next attack
		}
		
		// Time for an attack! Determine which side the target is on
		float horseAngle = horse->rot.z;
		float horseRightX = cos(horseAngle);
		float horseRightY = -sin(horseAngle);
		
		float toTargetX = target->pos.x - horse->pos.x;
		float toTargetY = target->pos.y - horse->pos.y;
		
		float dotRight = (toTargetX * horseRightX) + (toTargetY * horseRightY);
		const char* targetSide = (dotRight > 0) ? "RIGHT" : "LEFT";
		
		// Play the melee attack animation using existing function from CombatStyles
		PlayMountedAttackAnimation(rider, targetSide);
		
		// Update hit detection if rider is attacking
		if (IsRiderAttacking(rider))
		{
			UpdateMountedAttackHitDetection(rider, target);
		}
		
		// Update last attack time
		data->lastAttackTime = currentTime;
		
		_MESSAGE("Special Movesets: Horse %08X close range assault ATTACK (%s side, dist: %.0f)",
			horse->formID, targetSide, distanceToTarget);
		
		return true;
	}
	
	void StopCloseRangeMeleeAssault(UInt32 horseFormID)
	{
		for (int i = 0; i < g_closeRangeMeleeAssaultCount; i++)
		{
			if (g_closeRangeMeleeAssaultData[i].isValid && g_closeRangeMeleeAssaultData[i].horseFormID == horseFormID)
			{
				if (g_closeRangeMeleeAssaultData[i].isActive)
				{
				 g_closeRangeMeleeAssaultData[i].isActive = false;
					_MESSAGE("SpecialMovesets: Stopped close range melee assault for horse %08X", horseFormID);
				}
				return;
			}
		}
	}
	
	void ClearCloseRangeMeleeAssaultData(UInt32 horseFormID)
	{
		for (int i = 0; i < g_closeRangeMeleeAssaultCount; i++)
		{
			if (g_closeRangeMeleeAssaultData[i].isValid && g_closeRangeMeleeAssaultData[i].horseFormID == horseFormID)
			{
				// Shift remaining entries
				for (int j = i; j < g_closeRangeMeleeAssaultCount - 1; j++)
				{
					g_closeRangeMeleeAssaultData[j] = g_closeRangeMeleeAssaultData[j + 1];
				}
				g_closeRangeMeleeAssaultCount--;
				_MESSAGE("SpecialMovesets: Cleared close range melee assault data for horse %08X", horseFormID);
				return;
			}
		}
	}

	void ClearAllMovesetData(UInt32 horseFormID)
	{
		_MESSAGE("SpecialMovesets: Clearing ALL moveset data for horse %08X", horseFormID);
		
		// Clear 90-degree turn direction
		ClearHorseTurnDirection(horseFormID);
		
		// Clear rear up tracking
		ClearRearUpData(horseFormID);
		
		// Clear jump cooldown
		ClearHorseJumpData(horseFormID);
		
		// Clear 90-degree turn direction data
		for (int i = 0; i < g_horseTurnCount; i++)
		{
			if (g_horseTurnData[i].isValid && g_horseTurnData[i].horseFormID == horseFormID)
			{
				for (int j = i; j < g_horseTurnCount - 1; j++)
				{
					g_horseTurnData[j] = g_horseTurnData[j + 1];
				}
				g_horseTurnCount--;
				break;
			}
		}
		
		// Clear mobile target intercept data
		ClearMobileInterceptData(horseFormID);
		
		// Clear charge data (also stops any active charge)
		ClearChargeData(horseFormID);
		
		// Clear rapid fire data
		ClearRapidFireData(horseFormID);
		
		// Clear stand ground data
		ClearStandGroundData(horseFormID);
		
		// Clear player aggro switch data
		ClearPlayerAggroSwitchData(horseFormID);
		
		// Clear close range melee assault data
		ClearCloseRangeMeleeAssaultData(horseFormID);
	}
	
	// ============================================
	// RESET ALL SPECIAL MOVESET
	// Called when game loads/reloads to reset all state
	// ============================================
	
	void ResetAllSpecialMovesets()
	{
		_MESSAGE("SpecialMovesets: === RESETTING ALL STATE ===");
		
		// Reset global rear up cooldown
		g_lastGlobalRearUpTime = -REAR_UP_GLOBAL_COOLDOWN;
		
		// Reset global rapid fire cooldown
		g_lastGlobalRapidFireTime = -RAPID_FIRE_GLOBAL_COOLDOWN;
		
		// Clear rear up tracking data
		for (int i = 0; i < MAX_TRACKED_HORSES; i++)
		{
			g_horseRearUpTracking[i].isValid = false;
			g_horseRearUpTracking[i].horseFormID = 0;
			g_horseRearUpTracking[i].lastRearUpTime = -RearUpCooldown;
			g_horseRearUpTracking[i].lastKnownHealth = 0;
		}
		g_horseRearUpCount = 0;
		
		// Clear turn direction data
		for (int i = 0; i < MAX_TRACKED_HORSES; i++)
		{
			g_horseTurnData[i].isValid = false;
			g_horseTurnData[i].horseFormID = 0;
			g_horseTurnData[i].clockwise = false;
			g_horseTurnData[i].wasInMeleeRange = false;
			g_horseTurnData[i].lastTurnDirectionChangeTime = -5.0f;  // Allow immediate first use
		}
		g_horseTurnCount = 0;
		
		// Clear jump cooldown data
		for (int i = 0; i < MAX_TRACKED_HORSES; i++)
		{
			g_horseJumpData[i].isValid = false;
			g_horseJumpData[i].horseFormID = 0;
			g_horseJumpData[i].lastJumpTime = -HORSE_JUMP_COOLDOWN;
		}
		g_horseJumpCount = 0;
		
		// Combat dismount data - REMOVED (system no longer exists)
		
		// Clear charge data
		for (int i = 0; i < MAX_TRACKED_HORSES; i++)
		{
			g_horseChargeData[i].isValid = false;
		}
		g_horseChargeCount = 0;
		
		// Clear rapid fire data
		for (int i = 0; i < MAX_TRACKED_HORSES; i++)
		{
			g_horseRapidFireData[i].isValid = false;
		}
		g_horseRapidFireCount = 0;
		
		// Clear stand ground data
		for (int i = 0; i < MAX_TRACKED_HORSES; i++)
		{
			g_horseStandGroundData[i].isValid = false;
		}
		g_horseStandGroundCount = 0;
		
		// Clear player aggro switch data
		for (int i = 0; i < MAX_TRACKED_HORSES; i++)
		{
			g_playerAggroSwitchData[i].isValid = false;
		}
		g_playerAggroSwitchCount = 0;
		
		// Clear close range melee assault data
		for (int i = 0; i < MAX_TRACKED_HORSES; i++)
		{
			g_closeRangeMeleeAssaultData[i].isValid = false;
			g_closeRangeMeleeAssaultData[i].horseFormID = 0;
			g_closeRangeMeleeAssaultData[i].isActive = false;
			g_closeRangeMeleeAssaultData[i].lastAttackTime = 0;
		}
		g_closeRangeMeleeAssaultCount = 0;
		
		// Note: g_mobileInterceptData is defined later in the file
		// It will be cleared in InitSpecialMovesets or when first accessed
		
		// Reset initialization flags so idles can be re-cached if needed
		g_jumpIdleInitialized = false;
		g_horseJumpIdle = nullptr;
		// Trot turn idles removed - system no longer in use
		
		_MESSAGE("SpecialMovesets: All state reset complete");
	}
}
