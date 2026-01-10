#include "SpecialMovesets.h"
#include "SingleMountedCombat.h"
#include "WeaponDetection.h"
#include "ArrowSystem.h"
#include "DynamicPackages.h"
#include "AILogging.h"
#include "Helper.h"
#include "config.h"
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
	
	// Horse trot turns (obstruction avoidance) - From Skyrim.esm
	const UInt32 HORSE_TROT_LEFT_FORMID = 0x00056DAA;   // Trot forward to trot left
	const UInt32 HORSE_TROT_RIGHT_FORMID = 0x00052728;  // Trot forward to trot right
	const float HORSE_TROT_TURN_COOLDOWN = 2.0f;  // 2 seconds between trot turns (increased from 0.5)
	
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
	const float STAND_GROUND_MAX_DURATION = 8.0f;    // Maximum stand time
	const int STAND_GROUND_CHANCE_PERCENT = 25;      // 25% chance to trigger
	const float STAND_GROUND_CHECK_INTERVAL = 2.0f;  // Check every 2 seconds
	const float STAND_GROUND_COOLDOWN = 5.0f;        // 5 seconds between stand ground attempts
	
	// ============================================
	// SYSTEM STATE
	// ============================================
	
	static bool g_maneuverSystemInitialized = false;
	static bool g_randomSeeded = false;
	
	// Global rear up tracking (prevents synchronized rear ups)
	static float g_lastGlobalRearUpTime = -8.0f;  // Initialize to allow immediate first use
	
	const int MAX_TRACKED_HORSES = 10;  // Array size (hardcoded max for memory safety)
	// Actual runtime limit uses MaxTrackedMountedNPCs from config
	
	// Cached horse jump idle
	static TESIdleForm* g_horseJumpIdle = nullptr;
	static bool g_jumpIdleInitialized = false;
	
	// Cached horse trot turn idles
	static TESIdleForm* g_horseTrotLeftIdle = nullptr;
	static TESIdleForm* g_horseTrotRightIdle = nullptr;
	static bool g_trotIdlesInitialized = false;
	
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
	
	// Horse trot turn cooldown tracking
	struct HorseTrotTurnData
	{
		UInt32 horseFormID;
		float lastTrotTurnTime;
		bool isValid;
	};
	
	static HorseTrotTurnData g_horseTrotTurnData[MAX_TRACKED_HORSES];
	static int g_horseTrotTurnCount = 0;
	
	// Adjacent riding tracking per horse (for mounted vs mounted combat)
	struct HorseAdjacentRidingData
	{
		UInt32 horseFormID;
		bool ridingOnRightSide;    // true = ride on target's right, false = left
		bool sideChosen;    // true once a side has been selected
		bool isValid;
	};
	
	static HorseAdjacentRidingData g_horseAdjacentData[MAX_TRACKED_HORSES];
	static int g_horseAdjacentCount = 0;
	
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
	// STAND GROUND MANEUVER (vs Mobile NPC Targets)
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
		bool noRotation;         // 50% chance - if true, don't do 90-degree turn, just stay facing target
		bool isValid;
	};
	
	static HorseStandGroundData g_horseStandGroundData[MAX_TRACKED_HORSES];
	static int g_horseStandGroundCount = 0;
	
	// ============================================
	// PLAYER AGGRO SWITCH (vs Non-Player Target)
	// ============================================
	// When fighting a non-player NPC and player is nearby,
	// occasionally switch targets to the player with a charge.
	
	const float PLAYER_AGGRO_SWITCH_RANGE = 1500.0f;     // Player must be within this range
	const int PLAYER_AGGRO_SWITCH_CHANCE = 15;           // 15% chance to switch
	const float PLAYER_AGGRO_SWITCH_INTERVAL = 20.0f; // Check every 20 seconds
	
	struct PlayerAggroSwitchData
	{
		UInt32 horseFormID;
		float lastCheckTime;
		bool isValid;
	};
	
	static PlayerAggroSwitchData g_playerAggroSwitchData[MAX_TRACKED_HORSES];
	static int g_playerAggroSwitchCount = 0;
	
	// ============================================
	// MELEE RIDER COLLISION AVOIDANCE
	// ============================================
	// Prevents melee riders from bunching up on top of each other
	// Uses trot left/right animations to steer away
	
	const float MELEE_RIDER_MIN_SEPARATION = 130.0f;  // Minimum distance between melee riders
	const float MELEE_RIDER_AVOIDANCE_COOLDOWN = 1.0f;  // Cooldown between avoidance maneuvers
	
	struct MeleeRiderAvoidanceData
	{
		UInt32 horseFormID;
		float lastAvoidanceTime;
		bool isValid;
	};
	
	static MeleeRiderAvoidanceData g_meleeAvoidanceData[MAX_TRACKED_HORSES];
	static int g_meleeAvoidanceCount = 0;
	
	// Forward declaration for InitTrotIdles (defined later in this file)
	static void InitTrotIdles();
	
	static MeleeRiderAvoidanceData* GetOrCreateMeleeAvoidanceData(UInt32 horseFormID)
	{
		// Check if already tracked
		for (int i = 0; i < g_meleeAvoidanceCount; i++)
		{
			if (g_meleeAvoidanceData[i].isValid && g_meleeAvoidanceData[i].horseFormID == horseFormID)
			{
				return &g_meleeAvoidanceData[i];
			}
		}
		
		// Create new entry
		if (g_meleeAvoidanceCount < MAX_TRACKED_HORSES)
		{
			MeleeRiderAvoidanceData* data = &g_meleeAvoidanceData[g_meleeAvoidanceCount];
			data->horseFormID = horseFormID;
			data->lastAvoidanceTime = -MELEE_RIDER_AVOIDANCE_COOLDOWN;
			data->isValid = true;
			g_meleeAvoidanceCount++;
			return data;
		}
		
		return nullptr;
	}
	
	// Check if another melee rider is too close and on which side
	// Returns: 0 = no collision, 1 = collision on LEFT, 2 = collision on RIGHT
	int CheckMeleeRiderCollision(Actor* horse, Actor* otherHorse)
	{
		if (!horse || !otherHorse) return 0;
		if (horse->formID == otherHorse->formID) return 0;
		
		// Calculate distance
		float dx = otherHorse->pos.x - horse->pos.x;
		float dy = otherHorse->pos.y - horse->pos.y;
		float distance = sqrt(dx * dx + dy * dy);
		
		// Not too close
		if (distance > MELEE_RIDER_MIN_SEPARATION) return 0;
		
		// Calculate which side the other horse is on relative to our facing direction
		float angleToOther = atan2(dx, dy);
		float myHeading = horse->rot.z;
		
		float relativeAngle = angleToOther - myHeading;
		
		// Normalize to [-PI, PI]
		while (relativeAngle > 3.14159f) relativeAngle -= 6.28318f;
		while (relativeAngle < -3.14159f) relativeAngle += 6.28318f;
		
		// Positive = other horse is on our RIGHT
		// Negative = other horse is on our LEFT
		if (relativeAngle < 0)
		{
			return 1;  // Collision on LEFT
		}
		else
		{
			return 2;  // Collision on RIGHT
		}
	}
	
	// Try to avoid collision with another melee rider
	// Returns true if avoidance maneuver was triggered
	bool TryMeleeRiderAvoidance(Actor* horse, Actor* otherHorse)
	{
		if (!horse || !otherHorse) return false;
		
		// Check cooldown
		MeleeRiderAvoidanceData* data = GetOrCreateMeleeAvoidanceData(horse->formID);
		if (!data) return false;
		
		float currentTime = GetCurrentTime();
		if ((currentTime - data->lastAvoidanceTime) < MELEE_RIDER_AVOIDANCE_COOLDOWN)
		{
			return false;  // Still on cooldown
		}
		
		// Check collision side
		int collisionSide = CheckMeleeRiderCollision(horse, otherHorse);
		if (collisionSide == 0) return false;
		
		// ============================================
		// DIRECT ROTATION APPROACH
		// Since trot animations may not work on AI horses,
		// we'll directly adjust the horse's rotation to steer away
		// ============================================
		
		const float AVOIDANCE_ROTATION = 0.5f;  // ~30 degrees per adjustment
		float currentAngle = horse->rot.z;
		float newAngle = currentAngle;
		
		if (collisionSide == 1)
		{
			// Collision on LEFT - rotate RIGHT to avoid
			newAngle = currentAngle + AVOIDANCE_ROTATION;
		}
		else if (collisionSide == 2)
		{
			// Collision on RIGHT - rotate LEFT to avoid
			newAngle = currentAngle - AVOIDANCE_ROTATION;
		}
		
		// Normalize angle
		while (newAngle > 3.14159f) newAngle -= 6.28318f;
		while (newAngle < -3.14159f) newAngle += 6.28318f;
		
		// Apply the rotation
		horse->rot.z = newAngle;
		
		_MESSAGE("SpecialMovesets: Horse %08X AVOIDING COLLISION - rotated %s (other horse %08X on %s, distance: %.0f)", 
			horse->formID,
			collisionSide == 1 ? "RIGHT" : "LEFT",
			otherHorse->formID,
			collisionSide == 1 ? "LEFT" : "RIGHT",
			sqrt((otherHorse->pos.x - horse->pos.x) * (otherHorse->pos.x - horse->pos.x) + 
			     (otherHorse->pos.y - horse->pos.y) * (otherHorse->pos.y - horse->pos.y)));
		
		data->lastAvoidanceTime = currentTime;
		
		// ============================================
		// ALSO TRY TROT ANIMATION (may work sometimes)
		// ============================================
		InitTrotIdles();
		
		if (collisionSide == 1 && g_horseTrotRightIdle)
		{
			const char* eventName = g_horseTrotRightIdle->animationEvent.c_str();
			if (eventName && strlen(eventName) > 0)
			{
				SendHorseAnimationEvent(horse, eventName);
				// Don't check result - rotation above is the main avoidance
			}
		}
		else if (collisionSide == 2 && g_horseTrotLeftIdle)
		{
			const char* eventName = g_horseTrotLeftIdle->animationEvent.c_str();
			if (eventName && strlen(eventName) > 0)
			{
				SendHorseAnimationEvent(horse, eventName);
			}
		}
		
		return true;
	}
	
	// Check all nearby melee riders and avoid if needed
	// Call this from the melee rider update loop
	bool UpdateMeleeRiderCollisionAvoidance(Actor* horse, Actor* target)
	{
		if (!horse || !target) return false;
		
		// Get the cell to scan for other horses
		TESObjectCELL* cell = horse->parentCell;
		if (!cell) return false;
		
		// Scan for other mounted NPCs
		for (UInt32 i = 0; i < cell->objectList.count; i++)
		{
			TESObjectREFR* ref = nullptr;
			cell->objectList.GetNthItem(i, ref);
			
			if (!ref) continue;
			
			Actor* otherActor = DYNAMIC_CAST(ref, TESObjectREFR, Actor);
			if (!otherActor) continue;
			
			// Skip self
			if (otherActor->formID == horse->formID) continue;
			
			// Skip player and player's horse
			if (g_thePlayer && (*g_thePlayer))
			{
				if (otherActor == (*g_thePlayer)) continue;
				if (otherActor->formID == 0x14) continue;
			}
			
			// Skip dead actors
			if (otherActor->IsDead(1)) continue;
			
			// Check if this is a horse with a rider
			NiPointer<Actor> rider;
			if (!CALL_MEMBER_FN(otherActor, GetMountedBy)(rider) || !rider)
			{
				continue;  // Not a ridden horse
			}
			
			// Check if this is another melee rider (not ranged)
			// We check by seeing if the other horse is close to the same target
			float otherDx = target->pos.x - otherActor->pos.x;
			float otherDy = target->pos.y - otherActor->pos.y;
			float otherDistToTarget = sqrt(otherDx * otherDx + otherDy * otherDy);
			
			// If other horse is also close to target, they're likely a melee rider
			if (otherDistToTarget < 600.0f)
			{
				// Try to avoid collision
				if (TryMeleeRiderAvoidance(horse, otherActor))
				{
					return true;  // Avoidance triggered
				}
			}
		}
		
		return false;
	}
	
	void ClearMeleeAvoidanceData(UInt32 horseFormID)
	{
		for (int i = 0; i < g_meleeAvoidanceCount; i++)
		{
			if (g_meleeAvoidanceData[i].isValid && g_meleeAvoidanceData[i].horseFormID == horseFormID)
			{
				// Shift remaining entries
				for (int j = i; j < g_meleeAvoidanceCount - 1; j++)
				{
					g_meleeAvoidanceData[j] = g_meleeAvoidanceData[j + 1];
				}
				g_meleeAvoidanceCount--;
				return;
			}
		}
	}
	
	// ============================================
	// UTILITY FUNCTIONS
	// ============================================
	
	static void EnsureRandomSeeded()
	{
		if (!g_randomSeeded)
		{
			unsigned int seed = (unsigned int)time(nullptr) ^ (unsigned int)clock();
			srand(seed);
			rand(); rand(); rand();
			g_randomSeeded = true;
		}
	}
	
	static float GetCurrentTime()
	{
		static auto startTime = clock();
		return (float)(clock() - startTime) / CLOCKS_PER_SEC;
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
		}
		g_horseTurnCount = 0;
		
		// Clear jump cooldown data
		for (int i = 0; i < MAX_TRACKED_HORSES; i++)
		{
			g_horseJumpData[i].isValid = false;
		}
		g_horseJumpCount = 0;
		
		// Clear trot turn cooldown data
		for (int i = 0; i < MAX_TRACKED_HORSES; i++)
		{
			g_horseTrotTurnData[i].isValid = false;
		}
		g_horseTrotTurnCount = 0;
		
		// Clear adjacent riding data
		for (int i = 0; i < MAX_TRACKED_HORSES; i++)
		{
			g_horseAdjacentData[i].isValid = false;
		}
		g_horseAdjacentCount = 0;
		
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
		
		// Clear melee avoidance data
		for (int i = 0; i < MAX_TRACKED_HORSES; i++)
		{
			g_meleeAvoidanceData[i].isValid = false;
		}
		g_meleeAvoidanceCount = 0;
		
		// Note: g_mobileInterceptData is defined later in the file
		// It will be cleared in InitSpecialMovesets or when first accessed
		
		// Reset initialization flags so idles can be re-cached if needed
		g_jumpIdleInitialized = false;
		g_trotIdlesInitialized = false;
		g_horseJumpIdle = nullptr;
		g_horseTrotLeftIdle = nullptr;
		g_horseTrotRightIdle = nullptr;
		
		EnsureRandomSeeded();
		
		g_maneuverSystemInitialized = true;
		_MESSAGE("SpecialMovesets: System initialized");
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
			g_horseTrotTurnData[i].isValid = false;
			g_horseAdjacentData[i].isValid = false;
			g_horseChargeData[i].isValid = false;
			g_horseRapidFireData[i].isValid = false;
		}
		g_horseRearUpCount = 0;
		g_horseTurnCount = 0;
		g_horseJumpCount = 0;
		g_horseTrotTurnCount = 0;
		g_horseAdjacentCount = 0;
		g_horseChargeCount = 0;
		g_horseRapidFireCount = 0;
		g_meleeAvoidanceCount = 0;
		
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
	// MOBILE TARGET INTERCEPTION
	// ============================================
	// When fighting a mobile NPC (not player), both horse and target
	// are moving toward each other. This causes head-on collisions
	// and circling. Instead, we use an interception approach angle.
	
	struct MobileTargetInterceptData
	{
		UInt32 horseFormID;
		UInt32 targetFormID;
		NiPoint3 lastTargetPos;     // Last known target position
		float lastTargetPosTime;       // When we recorded it
		bool approachFromRight;        // Which side to approach from
		bool sideChosen;    // Have we picked a side?
		bool isValid;
	};
	
	static MobileTargetInterceptData g_mobileInterceptData[MAX_TRACKED_HORSES];
	static int g_mobileInterceptCount = 0;
	
	const float MOBILE_TARGET_VELOCITY_THRESHOLD = 50.0f;  // Target must be moving faster than this to use interception
	const float INTERCEPTION_ANGLE_OFFSET = 1.0f;  // ~60 degrees offset for interception approach
	
	static MobileTargetInterceptData* GetOrCreateMobileInterceptData(UInt32 horseFormID)
	{
		// Check if already tracked
		for (int i = 0; i < g_mobileInterceptCount; i++)
		{
			if (g_mobileInterceptData[i].isValid && g_mobileInterceptData[i].horseFormID == horseFormID)
			{
				return &g_mobileInterceptData[i];
			}
		}
		
		// Create new entry
		if (g_mobileInterceptCount < MAX_TRACKED_HORSES)
		{
			MobileTargetInterceptData* data = &g_mobileInterceptData[g_mobileInterceptCount];
			data->horseFormID = horseFormID;
			data->targetFormID = 0;
			data->lastTargetPos = NiPoint3(0, 0, 0);
			data->lastTargetPosTime = 0;
			data->approachFromRight = false;
			data->sideChosen = false;
			data->isValid = true;
			g_mobileInterceptCount++;
			return data;
		}
		
		return nullptr;
	}
	
	// Check if target is a mobile NPC (not player, and moving)
	bool IsTargetMobileNPC(Actor* target, UInt32 horseFormID)
	{
		if (!target) return false;
		
		// Player is always considered "stationary" for interception purposes
		// (player movement is unpredictable, use normal 90-degree turns)
		if (g_thePlayer && (*g_thePlayer) && target == (*g_thePlayer))
		{
			return false;
		}
		
		MobileTargetInterceptData* data = GetOrCreateMobileInterceptData(horseFormID);
		if (!data) return false;
		
		float currentTime = GetCurrentTime();
		
		// If this is a new target, reset tracking
		if (data->targetFormID != target->formID)
		{
			data->targetFormID = target->formID;
			data->lastTargetPos = target->pos;
			data->lastTargetPosTime = currentTime;
			data->sideChosen = false;
			return false;  // Need at least one frame of data
		}
		
		// Calculate target velocity since last frame
		float timeDelta = currentTime - data->lastTargetPosTime;
		if (timeDelta < 0.05f)  // Less than 50ms, not enough data
		{
			return data->sideChosen;  // Return previous result if we already determined
		}
		
		float dx = target->pos.x - data->lastTargetPos.x;
		float dy = target->pos.y - data->lastTargetPos.y;
		float distance = sqrt(dx * dx + dy * dy);
		float velocity = distance / timeDelta;
		
		// Update tracking
		data->lastTargetPos = target->pos;
		data->lastTargetPosTime = currentTime;
		
		// Target is mobile if moving faster than threshold
		return velocity > MOBILE_TARGET_VELOCITY_THRESHOLD;
	}
	
	// Get interception angle for approaching a mobile target
	// This makes the horse approach from an angle instead of head-on
	float GetMobileTargetInterceptionAngle(UInt32 horseFormID, Actor* horse, Actor* target)
	{
		if (!horse || !target) return horse ? horse->rot.z : 0.0f;
		
		MobileTargetInterceptData* data = GetOrCreateMobileInterceptData(horseFormID);
		if (!data) 
		{
			// Fallback to simple angle
			float dx = target->pos.x - horse->pos.x;
			float dy = target->pos.y - horse->pos.y;
			return atan2(dx, dy);
		}
		
		// Choose approach side if not already chosen
		if (!data->sideChosen)
		{
			EnsureRandomSeeded();
			data->approachFromRight = (rand() % 2) == 0;
			data->sideChosen = true;
			
			_MESSAGE("SpecialMovesets: Horse %08X will INTERCEPT mobile target %08X from %s",
				horseFormID, target->formID, data->approachFromRight ? "RIGHT" : "LEFT");
		}
		
		// Calculate base angle to target
		float dx = target->pos.x - horse->pos.x;
		float dy = target->pos.y - horse->pos.y;
		float angleToTarget = atan2(dx, dy);
		
		// Apply offset based on chosen side
		// This makes us approach at an angle rather than head-on
		float interceptAngle;
		if (data->approachFromRight)
		{
			interceptAngle = angleToTarget + INTERCEPTION_ANGLE_OFFSET;
		}
		else
		{
			interceptAngle = angleToTarget - INTERCEPTION_ANGLE_OFFSET;
		}
		
		// Normalize angle
		while (interceptAngle > 3.14159f) interceptAngle -= 6.28318f;
		while (interceptAngle < -3.14159f) interceptAngle += 6.28318f;
		
		return interceptAngle;
	}
	
	// Called when leaving melee range - reset interception side for next approach
	void NotifyHorseLeftMobileTargetRange(UInt32 horseFormID)
	{
		for (int i = 0; i < g_mobileInterceptCount; i++)
		{
			if (g_mobileInterceptData[i].isValid && g_mobileInterceptData[i].horseFormID == horseFormID)
			{
				if (g_mobileInterceptData[i].sideChosen)
				{
					_MESSAGE("SpecialMovesets: Horse %08X left mobile target range - will pick new interception side", horseFormID);
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
				// Shift remaining entries
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
	// 90-DEGREE TURN MANEUVER
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
			// Fallback to pure random if tracking is full
			return (rand() % 2) == 0;
		}
		
		// If we weren't in melee range last frame, pick a NEW random direction
		if (!data->wasInMeleeRange)
		{
			data->clockwise = (rand() % 2) == 0;
			data->wasInMeleeRange = true;
			
			// Only log once when entering melee range (not every frame)
			_MESSAGE("SpecialMovesets: Horse %08X entered melee range - turning %s", 
				horseFormID, 
				data->clockwise ? "CLOCKWISE" : "COUNTER-CLOCKWISE");
		}
		
		return data->clockwise;
	}
	
	float Get90DegreeTurnAngle(UInt32 horseFormID, float angleToTarget)
	{
		bool turnClockwise = GetHorseTurnDirectionClockwise(horseFormID);
		
		float targetAngle;
		if (turnClockwise)
		{
			targetAngle = angleToTarget + 1.5708f;  // +90 degrees
		}
		else
		{
			targetAngle = angleToTarget - 1.5708f;  // -90 degrees
		}
		
		// Normalize angle to -PI to PI
		while (targetAngle > 3.14159f) targetAngle -= 6.28318f;
		while (targetAngle < -3.14159f) targetAngle += 6.28318f;
		
		// Log only on direction change (tracked in GetHorseTurnDirectionClockwise)
		
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
					// Only log once when leaving (significant state change)
					_MESSAGE("SpecialMovesets: Horse %08X left melee range", horseFormID);
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
		g_horseTurnCount = 0;
		for (int i = 0; i < MAX_TRACKED_HORSES; i++)
		{
			g_horseTurnData[i].isValid = false;
		}
	}
	
	// ============================================
	// REAR UP - ON APPROACH
	// ============================================
	
	bool TryRearUpOnApproach(Actor* horse, Actor* target, float distanceToTarget)
	{
		if (!horse || !target) return false;
		
		// Check if rear up is enabled
		if (!RearUpEnabled) return false;
		
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
	
	static void InitTrotIdles()
	{
		if (g_trotIdlesInitialized) return;
		
		_MESSAGE("SpecialMovesets: Loading horse trot turn animations from Skyrim.esm...");
		
		// Load trot left idle
		TESForm* trotLeftForm = LookupFormByID(HORSE_TROT_LEFT_FORMID);
		if (trotLeftForm)
		{
			g_horseTrotLeftIdle = DYNAMIC_CAST(trotLeftForm, TESForm, TESIdleForm);
			if (g_horseTrotLeftIdle)
			{
				_MESSAGE("SpecialMovesets: Found HORSE_TROT_LEFT (FormID: %08X)", HORSE_TROT_LEFT_FORMID);
			}
			else
			{
				_MESSAGE("SpecialMovesets: ERROR - FormID %08X is not a TESIdleForm!", HORSE_TROT_LEFT_FORMID);
			}
		}
		else
		{
			_MESSAGE("SpecialMovesets: ERROR - LookupFormByID failed for HORSE_TROT_LEFT %08X", HORSE_TROT_LEFT_FORMID);
		}
		
		// Load trot right idle
		TESForm* trotRightForm = LookupFormByID(HORSE_TROT_RIGHT_FORMID);
		if (trotRightForm)
		{
			g_horseTrotRightIdle = DYNAMIC_CAST(trotRightForm, TESForm, TESIdleForm);
			if (g_horseTrotRightIdle)
			{
				_MESSAGE("SpecialMovesets: Found HORSE_TROT_RIGHT (FormID: %08X)", HORSE_TROT_RIGHT_FORMID);
			}
			else
			{
				_MESSAGE("SpecialMovesets: ERROR - FormID %08X is not a TESIdleForm!", HORSE_TROT_RIGHT_FORMID);
			}
		}
		else
		{
			_MESSAGE("SpecialMovesets: ERROR - LookupFormByID failed for HORSE_TROT_RIGHT %08X", HORSE_TROT_RIGHT_FORMID);
		}
		
		g_trotIdlesInitialized = true;
		_MESSAGE("SpecialMovesets: Horse trot turn animations initialized - Left: %s, Right: %s", 
			g_horseTrotLeftIdle ? "SUCCESS" : "FAILED",
			g_horseTrotRightIdle ? "SUCCESS" : "FAILED");
	}
	
	static HorseTrotTurnData* GetOrCreateTrotTurnData(UInt32 horseFormID)
	{
		// Check if already tracked
		for (int i = 0; i < g_horseTrotTurnCount; i++)
		{
			if (g_horseTrotTurnData[i].isValid && g_horseTrotTurnData[i].horseFormID == horseFormID)
			{
				return &g_horseTrotTurnData[i];
			}
		}
		
		// Create new entry
		if (g_horseTrotTurnCount < MAX_TRACKED_HORSES)
		{
			HorseTrotTurnData* data = &g_horseTrotTurnData[g_horseTrotTurnCount];
			data->horseFormID = horseFormID;
			data->lastTrotTurnTime = -HORSE_TROT_TURN_COOLDOWN;  // Allow immediate first use
			data->isValid = true;
			g_horseTrotTurnCount++;
			return data;
		}
		
		return nullptr;
	}
	
	bool TryHorseTrotTurnToAvoid(Actor* horse, bool turnRight)
	{
		if (!horse) return false;
		
		// Initialize trot idles if needed
		InitTrotIdles();
		
		// Select the appropriate idle
		TESIdleForm* trotIdle = turnRight ? g_horseTrotRightIdle : g_horseTrotLeftIdle;
		
		if (!trotIdle)
		{
			return false;
		}
		
		// Check cooldown
		HorseTrotTurnData* data = GetOrCreateTrotTurnData(horse->formID);
		if (!data) 
		{
			return false;
		}
		
		float currentTime = GetCurrentTime();
		float timeSinceLastTurn = currentTime - data->lastTrotTurnTime;
		
		if (timeSinceLastTurn < HORSE_TROT_TURN_COOLDOWN)
		{
			// Still on cooldown - silent fail
			return false;
		}
		
		// Get the animation event name
		const char* eventName = trotIdle->animationEvent.c_str();
		if (!eventName || strlen(eventName) == 0)
		{
			return false;
		}
		
		// Send the animation event
		bool result = SendHorseAnimationEvent(horse, eventName);
		
		if (result)
		{
			data->lastTrotTurnTime = currentTime;
			_MESSAGE("SpecialMovesets: Horse %08X TROT TURN %s to avoid obstruction", 
				horse->formID, turnRight ? "RIGHT" : "LEFT");
		}
		// Don't log rejections - they happen frequently when animation graph is busy
		
		return result;
	}
	
	bool TryHorseTrotTurnFromObstruction(Actor* horse)
	{
		if (!horse) return false;
		
		// Get the obstruction side from AILogging
		ObstructionSide side = GetObstructionSide(horse->formID);
		
		// Only log if we're actually going to attempt something
		switch (side)
		{
			case ObstructionSide::Left:
				// Obstruction on LEFT ? Turn RIGHT to avoid
				return TryHorseTrotTurnToAvoid(horse, true);
				
			case ObstructionSide::Right:
				// Obstruction on RIGHT ? Turn LEFT to avoid
				return TryHorseTrotTurnToAvoid(horse, false);
				
			case ObstructionSide::Front:
				// Obstruction in FRONT ? Pick a random direction
				{
					EnsureRandomSeeded();
					bool turnRight = (rand() % 2) == 0;
					return TryHorseTrotTurnToAvoid(horse, turnRight);
				}
				
			case ObstructionSide::Both:
				// Both sides blocked - try random turn
				{
					EnsureRandomSeeded();
					bool turnRight = (rand() % 2) == 0;
					return TryHorseTrotTurnToAvoid(horse, turnRight);
				}
				
			case ObstructionSide::Unknown:
			default:
				// Unknown side - don't turn
				return false;
		}
	}
	
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
	// ADJACENT RIDING MANEUVER (Mounted vs Mounted)
	// ============================================
	
	static HorseAdjacentRidingData* GetOrCreateAdjacentData(UInt32 horseFormID)
	{
		// Check if already tracked
		for (int i = 0; i < g_horseAdjacentCount; i++)
		{
			if (g_horseAdjacentData[i].isValid && g_horseAdjacentData[i].horseFormID == horseFormID)
			{
				return &g_horseAdjacentData[i];
			}
		}
		
		// Create new entry
		if (g_horseAdjacentCount < MAX_TRACKED_HORSES)
		{
			HorseAdjacentRidingData* data = &g_horseAdjacentData[g_horseAdjacentCount];
			data->horseFormID = horseFormID;
			data->ridingOnRightSide = false;
			data->sideChosen = false;
			data->isValid = true;
			g_horseAdjacentCount++;
			return data;
		}
		
		return nullptr;
	}
	
	bool GetAdjacentRidingSide(UInt32 horseFormID)
	{
		EnsureRandomSeeded();
		
		HorseAdjacentRidingData* data = GetOrCreateAdjacentData(horseFormID);
		if (!data)
		{
			// Fallback to random if tracking is full
			return (rand() % 2) == 0;
		}
		
		// If we haven't chosen a side yet, pick one randomly
		if (!data->sideChosen)
		{
			data->ridingOnRightSide = (rand() % 2) == 0;
			data->sideChosen = true;
			
			_MESSAGE("SpecialMovesets: Horse %08X chose to ride on %s side of mounted target", 
				horseFormID, data->ridingOnRightSide ? "RIGHT" : "LEFT");
		}
		
		return data->ridingOnRightSide;
	}
	
	float GetAdjacentRidingAngle(UInt32 horseFormID, const NiPoint3& targetPos, const NiPoint3& horsePos, float targetHeading)
	{
		// Get which side to ride on
		bool rideOnRight = GetAdjacentRidingSide(horseFormID);
		
		// Calculate angle from horse to target
		float dx = targetPos.x - horsePos.x;
		float dy = targetPos.y - horsePos.y;
		float angleToTarget = atan2(dx, dy);
		
		// For adjacent riding, we want to match the target's heading direction
		// but offset slightly to be alongside them, not directly behind/in front
		
		// Offset angle: roughly 30-45 degrees from the target's heading
		// This puts the NPC horse alongside the target's horse
		const float ADJACENT_ANGLE_OFFSET = 0.52f;  // ~30 degrees
		
		float targetAngle;
		if (rideOnRight)
		{
			// Ride on target's right side - aim slightly ahead on their right
			targetAngle = targetHeading + ADJACENT_ANGLE_OFFSET;
		}
		else
		{
			// Ride on target's left side - aim slightly ahead on their left
			targetAngle = targetHeading - ADJACENT_ANGLE_OFFSET;
		}
		
		// Normalize angle to -PI to PI
		while (targetAngle > 3.14159f) targetAngle -= 6.28318f;
		while (targetAngle < -3.14159f) targetAngle += 6.28318f;
		
		return targetAngle;
	}
	
	void NotifyHorseLeftAdjacentRange(UInt32 horseFormID)
	{
		for (int i = 0; i < g_horseAdjacentCount; i++)
		{
			if (g_horseAdjacentData[i].isValid && g_horseAdjacentData[i].horseFormID == horseFormID)
			{
				if (g_horseAdjacentData[i].sideChosen)
				{
					_MESSAGE("SpecialMovesets: Horse %08X LEFT adjacent range - will pick new side on next approach", 
						horseFormID);
					g_horseAdjacentData[i].sideChosen = false;
				}
				return;
			}
		}
	}
	
	void ClearAdjacentRidingData(UInt32 horseFormID)
	{
		for (int i = 0; i < g_horseAdjacentCount; i++)
		{
			if (g_horseAdjacentData[i].isValid && g_horseAdjacentData[i].horseFormID == horseFormID)
			{
				// Shift remaining entries
				for (int j = i; j < g_horseAdjacentCount - 1; j++)
				{
					g_horseAdjacentData[j] = g_horseAdjacentData[j + 1];
				}
				g_horseAdjacentCount--;
				_MESSAGE("SpecialMovesets: Cleared adjacent riding data for horse %08X", horseFormID);
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
			data->lastChargeCheckTime = -CHARGE_CHECK_INTERVAL;  // Allow immediate first check
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
		
		// Check cooldown since last charge (using config value)
		if ((currentTime - data->lastChargeCompleteTime) < ChargeCooldown)
		{
			return false;  // Still on cooldown from last charge
		}
		
		// Check 10-second interval between checks
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
				if (g_horseChargeData[i].state == ChargeState::Charging)
				{
					// Stop the sprint if we were charging
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
		
		// Must NOT be in melee range
		if (distanceToTarget <= meleeRange)
		{
			return false;
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
		
		// Check cooldown since last rapid fire (using config value)
		if ((currentTime - data->lastCompleteTime) < RapidFireCooldown)
		{
			return false;  // Still on cooldown
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
		_MESSAGE("SpecialMovesets: Horse %08X RAPID FIRE INITIATED! (rolled %d < %d%%) - Horse will STOP for %.1f seconds",
			horse->formID, roll, RapidFireChancePercent, RapidFireDuration);
		
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
		// EQUIP BOW AND ARROWS FOR RAPID FIRE
		// ============================================
		
		// First, reset any existing bow attack state
		ResetBowAttackState(rider->formID);
		
		// Equip bow if not already equipped
		if (!IsBowEquipped(rider))
		{
			if (EquipBestBow(rider))
			{
				_MESSAGE("SpecialMovesets: Rider %08X equipped bow for rapid fire", rider->formID);
			}
			else
			{
				// Try to give a default bow if they don't have one
				GiveDefaultBow(rider);
				EquipBestBow(rider);
				_MESSAGE("SpecialMovesets: Rider %08X given default bow for rapid fire", rider->formID);
			}
		 }
		
		// Ensure arrows are equipped
		EquipArrows(rider);
		
		// Draw the weapon
		rider->DrawSheatheWeapon(true);
		
		// Start the rapid fire bow attack sequence
		StartRapidFireBowAttack(rider->formID);
		
		_MESSAGE("SpecialMovesets: Rider %08X RAPID FIRE bow attack started (%d shots)", 
			rider->formID, RapidFireShotCount);
		
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
					// FAILSAFE: Force bow release to exit draw animation
					PlayBowReleaseAnimation(rider, target);
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
			
			const float RAPID_FIRE_ABORT_DISTANCE = 5000.0f;
			
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
				
				// 1. Force bow release to exit draw animation
				// 2. Reset all bow attack state
				// 3. Clear weapon switch data to allow normal switching
				
				// FAILSAFE: Force bow release animation to prevent getting stuck in draw pose
				// This ensures the NPC exits the bow draw animation cleanly
				PlayBowReleaseAnimation(rider, target);
				
				// Reset bow attack state immediately
				ResetRapidFireBowAttack(rider->formID);
				ResetBowAttackState(rider->formID);
				
				// Clear weapon switch data to reset any pending sheathe states
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
			data->noRotation = false;
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
	
	bool TryStandGroundManeuver(Actor* horse, Actor* rider, Actor* target, float distanceToTarget)
	{
		if (!horse || !rider || !target) return false;
		
		// Check if enabled in config
		if (!StandGroundEnabled) return false;
		
		// Only trigger when fighting a NON-PLAYER target
		if (g_thePlayer && (*g_thePlayer) && target == (*g_thePlayer))
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
			return false;
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
		
		// 50% chance to skip 90-degree rotation - just face target directly
		data->noRotation = ((rand() % 100) < 50);
		
		data->state = StandGroundState::Active;
		data->stateStartTime = currentTime;
		
		_MESSAGE("SpecialMovesets: Horse %08X STANDING GROUND for %.1f seconds %s (rolled %d < %d%%, dist: %.0f)",
			horse->formID, data->standDuration, 
			data->noRotation ? "(NO ROTATION)" : "(with 90-deg turn)",
			roll, StandGroundChancePercent, distanceToTarget);
		
		// Stop horse movement
		StopHorseSprint(horse);
		Actor_ClearKeepOffsetFromActor(horse);
		
		return true;
	}
	
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
				_MESSAGE("SpecialMovesets: Horse %08X stand ground fully RESET", horse->formID);
			}
			return false;
		}
		
		// Active state - check if duration complete or target moved away
		if (data->state == StandGroundState::Active)
		{
			// Check if target moved too far away - abort stand ground
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
		
		// Only trigger when fighting a NON-PLAYER target
		if (!g_thePlayer || !(*g_thePlayer)) return false;
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
		
		// Roll 15% chance
		EnsureRandomSeeded();
		int roll = rand() % 100;
		if (roll >= PLAYER_AGGRO_SWITCH_CHANCE)
		{
			return false;  // Didn't roll the chance
		}
		
		// SUCCESS - Switch target to player!
		_MESSAGE("SpecialMovesets: Horse %08X SWITCHING TARGET to PLAYER! (was fighting %08X, rolled %d < %d%%, player dist: %.0f)",
			horse->formID, currentTarget->formID, roll, PLAYER_AGGRO_SWITCH_CHANCE, distanceToPlayer);
		
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
	
	void ClearAllMovesetData(UInt32 horseFormID)
	{
		_MESSAGE("SpecialMovesets: Clearing ALL moveset data for horse %08X", horseFormID);
		
		// Clear 90-degree turn direction
		ClearHorseTurnDirection(horseFormID);
		
		// Clear rear up tracking
		ClearRearUpData(horseFormID);
		
		// Clear jump cooldown
		ClearHorseJumpData(horseFormID);
		
		// Clear trot turn cooldown
		for (int i = 0; i < g_horseTrotTurnCount; i++)
		{
			if (g_horseTrotTurnData[i].isValid && g_horseTrotTurnData[i].horseFormID == horseFormID)
			{
				for (int j = i; j < g_horseTrotTurnCount - 1; j++)
				{
					g_horseTrotTurnData[j] = g_horseTrotTurnData[j + 1];
				}
				g_horseTrotTurnCount--;
				break;
			}
		}
		
		// Clear adjacent riding data
		ClearAdjacentRidingData(horseFormID);
		
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
		
		// Clear melee avoidance data
		ClearMeleeAvoidanceData(horseFormID);
	}
	
	// ============================================
	// RESET ALL SPECIAL MOVESETS
	// Called when game loads/reloads to reset all state
	// ============================================
	
	void ResetAllSpecialMovesets()
	{
		_MESSAGE("SpecialMovesets: === RESETTING ALL STATE ===");
		
		// Reset global rear up cooldown
		g_lastGlobalRearUpTime = -REAR_UP_GLOBAL_COOLDOWN;
		
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
		
		// Clear trot turn cooldown data
		for (int i = 0; i < MAX_TRACKED_HORSES; i++)
		{
			g_horseTrotTurnData[i].isValid = false;
			g_horseTrotTurnData[i].horseFormID = 0;
			g_horseTrotTurnData[i].lastTrotTurnTime = -HORSE_TROT_TURN_COOLDOWN;
		}
		g_horseTrotTurnCount = 0;
		
		// Clear adjacent riding data
		for (int i = 0; i < MAX_TRACKED_HORSES; i++)
		{
			g_horseAdjacentData[i].isValid = false;
			g_horseAdjacentData[i].horseFormID = 0;
			g_horseAdjacentData[i].ridingOnRightSide = false;
			g_horseAdjacentData[i].sideChosen = false;
		}
		g_horseAdjacentCount = 0;
		
		// Clear mobile target intercept data
		for (int i = 0; i < MAX_TRACKED_HORSES; i++)
		{
			g_mobileInterceptData[i].isValid = false;
			g_mobileInterceptData[i].horseFormID = 0;
			g_mobileInterceptData[i].targetFormID = 0;
			g_mobileInterceptData[i].sideChosen = false;
		}
		g_mobileInterceptCount = 0;
		
		// Clear charge data
		for (int i = 0; i < MAX_TRACKED_HORSES; i++)
		{
			g_horseChargeData[i].isValid = false;
			g_horseChargeData[i].horseFormID = 0;
			g_horseChargeData[i].riderFormID = 0;
			g_horseChargeData[i].state = ChargeState::None;
			g_horseChargeData[i].lastChargeCheckTime = -CHARGE_CHECK_INTERVAL;
			g_horseChargeData[i].lastChargeCompleteTime = -ChargeCooldown;
			g_horseChargeData[i].stateStartTime = 0;
		}
		g_horseChargeCount = 0;
		
		// Clear rapid fire data
		for (int i = 0; i < MAX_TRACKED_HORSES; i++)
		{
			g_horseRapidFireData[i].isValid = false;
			g_horseRapidFireData[i].horseFormID = 0;
			g_horseRapidFireData[i].riderFormID = 0;
			g_horseRapidFireData[i].state = RapidFireState::None;
			g_horseRapidFireData[i].lastCheckTime = -RAPID_FIRE_CHECK_INTERVAL;
			g_horseRapidFireData[i].lastCompleteTime = -RapidFireCooldown;
			g_horseRapidFireData[i].stateStartTime = 0;
		}
		g_horseRapidFireCount = 0;
		
		// Clear stand ground data
		for (int i = 0; i < MAX_TRACKED_HORSES; i++)
		{
			g_horseStandGroundData[i].isValid = false;
			g_horseStandGroundData[i].horseFormID = 0;
			g_horseStandGroundData[i].state = StandGroundState::None;
			g_horseStandGroundData[i].lastCheckTime = -StandGroundCheckInterval;
			g_horseStandGroundData[i].lastCompleteTime = -StandGroundCooldown;
			g_horseStandGroundData[i].stateStartTime = 0;
			g_horseStandGroundData[i].standDuration = 0;
			g_horseStandGroundData[i].noRotation = false;
		}
		g_horseStandGroundCount = 0;
		
		// Clear player aggro switch data
		for (int i = 0; i < MAX_TRACKED_HORSES; i++)
		{
			g_playerAggroSwitchData[i].isValid = false;
			g_playerAggroSwitchData[i].horseFormID = 0;
			g_playerAggroSwitchData[i].lastCheckTime = -PLAYER_AGGRO_SWITCH_INTERVAL;
		}
		g_playerAggroSwitchCount = 0;
		
		// Clear melee avoidance data
		for (int i = 0; i < MAX_TRACKED_HORSES; i++)
		{
			g_meleeAvoidanceData[i].isValid = false;
			g_meleeAvoidanceData[i].horseFormID = 0;
			g_meleeAvoidanceData[i].lastAvoidanceTime = -MELEE_RIDER_AVOIDANCE_COOLDOWN;
		}
		g_meleeAvoidanceCount = 0;
		
		// Reset initialization flags so idles can be re-cached if needed
		g_jumpIdleInitialized = false;
		g_trotIdlesInitialized = false;
		g_horseJumpIdle = nullptr;
		g_horseTrotLeftIdle = nullptr;
		g_horseTrotRightIdle = nullptr;
		
		// Reset random seed flag
		g_randomSeeded = false;
		
		_MESSAGE("SpecialMovesets: All state reset complete");
	}
}
