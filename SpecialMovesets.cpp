#include "SpecialMovesets.h"
#include "SingleMountedCombat.h"
#include "WeaponDetection.h"
#include "AILogging.h"
#include "Helper.h"
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
	// CONFIGURATION
	// ============================================
	
	// Rear up on approach (facing player head-on)
	const int REAR_UP_APPROACH_CHANCE = 7;  // 7% chance
	const float REAR_UP_APPROACH_DISTANCE = 250.0f;  // Must be within this distance
	const float REAR_UP_APPROACH_ANGLE = 0.52f;      // ~30 degrees facing tolerance
	
	// Rear up on damage
	const int REAR_UP_DAMAGE_CHANCE = 10; // 10% chance
	const float REAR_UP_DAMAGE_THRESHOLD = 50.0f;// Minimum damage to trigger check
	
	// Cooldown (shared between all rear up triggers)
	const float REAR_UP_COOLDOWN = 20.0f; // 20 seconds between rear ups
	
	// Horse jump (obstruction escape)
	const UInt32 HORSE_JUMP_BASE_FORMID = 0x0008E6;  // From MountedNPCCombat.esp
	const char* HORSE_JUMP_ESP_NAME = "MountedNPCCombat.esp";
	const float HORSE_JUMP_COOLDOWN = 4.0f;  // 4 seconds between jump attempts
	
	// Horse trot turns (obstruction avoidance) - From Skyrim.esm
	const UInt32 HORSE_TROT_LEFT_FORMID = 0x00056DAA;   // Trot forward to trot left
	const UInt32 HORSE_TROT_RIGHT_FORMID = 0x00052728;  // Trot forward to trot right
	const float HORSE_TROT_TURN_COOLDOWN = 0.5f;  // 0.5 second between trot turns
	
	// Stationary Rapid Fire configuration
	const float RAPID_FIRE_DURATION = 5.0f;     // 5 seconds of stationary firing
	const float RAPID_FIRE_COOLDOWN = 20.0f; // 20 seconds between uses (also initial delay)
	const float RAPID_FIRE_COMBAT_DELAY = 15.0f;    // Must be in combat for 15 seconds first
	const float RAPID_FIRE_MIN_DISTANCE = 200.0f;     // Must be at least this far from target
	const float RAPID_FIRE_MAX_DISTANCE = 2500.0f;    // Must be within bow range
	const int RAPID_FIRE_CHANCE = 5;       // 5% chance to trigger per tick
	const float RAPID_FIRE_DRAW_TIME = 1.0f;// 1 second to hold draw before release
	const float RAPID_FIRE_SHOT_INTERVAL = 1.0f;  // Same as draw time = no wait between shots
	const float RAPID_FIRE_ABORT_DISTANCE = 150.0f;   // If player gets closer than this, start abort timer
	const float RAPID_FIRE_ABORT_TIME = 2.0f;// Abort if player stays close for 2 seconds
	
	// ============================================
	// SYSTEM STATE
	// ============================================
	
	static bool g_maneuverSystemInitialized = false;
	static bool g_randomSeeded = false;
	
	const int MAX_TRACKED_HORSES = 5;
	
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
	
	// Rapid fire tracking per rider
	struct RapidFireData
	{
		UInt32 riderFormID;
		RapidFireState state;
		float stateStartTime;
		float lastShotTime;
		float drawStartTime;      // When current draw started
		float playerCloseStartTime;  // When player first got close (for abort check)
		float lastRollTime;       // When we last rolled for rapid fire chance
		bool isDrawing;    // Currently in draw phase (waiting to release)
		bool playerIsClose;     // Player is in melee range
		int shotsFired;
		bool isValid;
	};
	
	static RapidFireData g_rapidFireData[MAX_TRACKED_HORSES];
	static int g_rapidFireCount = 0;
	static float g_rangedCombatStartTime = 0.0f;
	
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
		for (int i = 0; i < g_horseRearUpCount; i++)
		{
			if (g_horseRearUpTracking[i].isValid && g_horseRearUpTracking[i].horseFormID == horseFormID)
			{
				return &g_horseRearUpTracking[i];
			}
		}
		
		if (g_horseRearUpCount < MAX_TRACKED_HORSES)
		{
			HorseRearUpTracking* data = &g_horseRearUpTracking[g_horseRearUpCount];
			data->horseFormID = horseFormID;
			data->lastRearUpTime = -REAR_UP_COOLDOWN;
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
		
		for (int i = 0; i < MAX_TRACKED_HORSES; i++)
		{
			g_horseRearUpTracking[i].isValid = false;
			g_horseTurnData[i].isValid = false;
			g_horseJumpData[i].isValid = false;
			g_horseTrotTurnData[i].isValid = false;
			g_rapidFireData[i].isValid = false;
		}
		g_horseRearUpCount = 0;
		g_horseTurnCount = 0;
		g_horseJumpCount = 0;
		g_horseTrotTurnCount = 0;
		g_rapidFireCount = 0;
		
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
			g_rapidFireData[i].isValid = false;
		}
		g_horseRearUpCount = 0;
		g_horseTurnCount = 0;
		g_horseJumpCount = 0;
		g_horseTrotTurnCount = 0;
		g_rapidFireCount = 0;
		
		g_maneuverSystemInitialized = false;
	}
	
	// ============================================
	// HORSE JUMP MANEUVER
	// ============================================
	
	static HorseJumpData* GetOrCreateJumpData(UInt32 horseFormID)
	{
		for (int i = 0; i < g_horseJumpCount; i++)
		{
			if (g_horseJumpData[i].isValid && g_horseJumpData[i].horseFormID == horseFormID)
			{
				return &g_horseJumpData[i];
			}
		}
		
		if (g_horseJumpCount < MAX_TRACKED_HORSES)
		{
			HorseJumpData* data = &g_horseJumpData[g_horseJumpCount];
			data->horseFormID = horseFormID;
			data->lastJumpTime = -HORSE_JUMP_COOLDOWN;
			data->isValid = true;
			g_horseJumpCount++;
			return data;
		}
		
		return nullptr;
	}
	
	bool IsHorseJumpOnCooldown(UInt32 horseFormID)
	{
		HorseJumpData* data = GetOrCreateJumpData(horseFormID);
		if (!data) return true;
		
		float currentTime = GetCurrentTime();
		return (currentTime - data->lastJumpTime) < HORSE_JUMP_COOLDOWN;
	}
	
	void ClearHorseJumpData(UInt32 horseFormID)
	{
		for (int i = 0; i < g_horseJumpCount; i++)
		{
			if (g_horseJumpData[i].isValid && g_horseJumpData[i].horseFormID == horseFormID)
			{
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
		
		InitJumpIdle();
		
		if (!g_horseJumpIdle) return false;
		
		HorseJumpData* data = GetOrCreateJumpData(horse->formID);
		if (!data) return false;
		
		float currentTime = GetCurrentTime();
		if ((currentTime - data->lastJumpTime) < HORSE_JUMP_COOLDOWN) return false;
		
		const char* eventName = g_horseJumpIdle->animationEvent.c_str();
		if (!eventName || strlen(eventName) == 0) return false;
		
		bool result = SendHorseAnimationEvent(horse, eventName);
		
		if (result)
		{
			data->lastJumpTime = currentTime;
			_MESSAGE("SpecialMovesets: Horse %08X JUMPING (event: %s)", horse->formID, eventName);
		}
		
		return result;
	}
	
	// ============================================
	// 90-DEGREE TURN MANEUVER
	// ============================================
	
	static HorseTurnData* GetOrCreateTurnData(UInt32 horseFormID)
	{
		for (int i = 0; i < g_horseTurnCount; i++)
		{
			if (g_horseTurnData[i].isValid && g_horseTurnData[i].horseFormID == horseFormID)
			{
				return &g_horseTurnData[i];
			}
		}
		
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
		if (!data) return (rand() % 2) == 0;
		
		if (!data->wasInMeleeRange)
		{
			data->clockwise = (rand() % 2) == 0;
			data->wasInMeleeRange = true;
			_MESSAGE("SpecialMovesets: Horse %08X turning %s", horseFormID, 
				data->clockwise ? "CLOCKWISE" : "COUNTER-CLOCKWISE");
		}
		
		return data->clockwise;
	}
	
	void NotifyHorseLeftMeleeRange(UInt32 horseFormID)
	{
		for (int i = 0; i < g_horseTurnCount; i++)
		{
			if (g_horseTurnData[i].isValid && g_horseTurnData[i].horseFormID == horseFormID)
			{
				if (g_horseTurnData[i].wasInMeleeRange)
				{
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
	// REAR UP MANEUVERS
	// ============================================
	
	bool TryRearUpOnApproach(Actor* horse, Actor* target, float distanceToTarget)
	{
		if (!horse || !target) return false;
		if (distanceToTarget > REAR_UP_APPROACH_DISTANCE) return false;
		
		float dx = target->pos.x - horse->pos.x;
		float dy = target->pos.y - horse->pos.y;
		float angleToTarget = atan2(dx, dy);
		float angleDiff = angleToTarget - horse->rot.z;
		
		while (angleDiff > 3.14159f) angleDiff -= 6.28318f;
		while (angleDiff < -3.14159f) angleDiff += 6.28318f;
		
		if (fabs(angleDiff) > REAR_UP_APPROACH_ANGLE) return false;
		
		HorseRearUpTracking* data = GetOrCreateRearUpTracking(horse->formID);
		if (!data) return false;
		
		float currentTime = GetCurrentTime();
		if ((currentTime - data->lastRearUpTime) < REAR_UP_COOLDOWN) return false;
		
		EnsureRandomSeeded();
		if ((rand() % 100) >= REAR_UP_APPROACH_CHANCE) return false;
		
		if (PlayHorseRearUpAnimation(horse))
		{
			data->lastRearUpTime = currentTime;
			_MESSAGE("SpecialMovesets: Horse %08X REAR UP on approach", horse->formID);
			return true;
		}
		
		return false;
	}
	
	bool TryRearUpOnDamage(Actor* horse, float damageAmount)
	{
		if (!horse) return false;
		if (damageAmount < REAR_UP_DAMAGE_THRESHOLD) return false;
		
		HorseRearUpTracking* data = GetOrCreateRearUpTracking(horse->formID);
		if (!data) return false;
		
		float currentTime = GetCurrentTime();
		if ((currentTime - data->lastRearUpTime) < REAR_UP_COOLDOWN) return false;
		
		EnsureRandomSeeded();
		if ((rand() % 100) >= REAR_UP_DAMAGE_CHANCE) return false;
		
		if (PlayHorseRearUpAnimation(horse))
		{
			data->lastRearUpTime = currentTime;
			_MESSAGE("SpecialMovesets: Horse %08X REAR UP on damage (%.0f)", horse->formID, damageAmount);
			return true;
		}
		
		return false;
	}
	
	void UpdateHorseHealth(UInt32 horseFormID, float currentHealth)
	{
		HorseRearUpTracking* data = GetOrCreateRearUpTracking(horseFormID);
		if (data) data->lastKnownHealth = currentHealth;
	}
	
	float GetHorseLastHealth(UInt32 horseFormID)
	{
		HorseRearUpTracking* data = GetOrCreateRearUpTracking(horseFormID);
		return data ? data->lastKnownHealth : 0;
	}
	
	void ClearRearUpData(UInt32 horseFormID)
	{
		for (int i = 0; i < g_horseRearUpCount; i++)
		{
			if (g_horseRearUpTracking[i].isValid && g_horseRearUpTracking[i].horseFormID == horseFormID)
			{
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
	// HORSE TROT TURN MANEUVER
	// ============================================
	
	static void InitTrotIdles()
	{
		if (g_trotIdlesInitialized) return;
		
		_MESSAGE("SpecialMovesets: Loading horse trot turn animations...");
		
		TESForm* trotLeftForm = LookupFormByID(HORSE_TROT_LEFT_FORMID);
		if (trotLeftForm)
		{
			g_horseTrotLeftIdle = DYNAMIC_CAST(trotLeftForm, TESForm, TESIdleForm);
		}
		
		TESForm* trotRightForm = LookupFormByID(HORSE_TROT_RIGHT_FORMID);
		if (trotRightForm)
		{
			g_horseTrotRightIdle = DYNAMIC_CAST(trotRightForm, TESForm, TESIdleForm);
		}
		
		g_trotIdlesInitialized = true;
	}
	
	static HorseTrotTurnData* GetOrCreateTrotTurnData(UInt32 horseFormID)
	{
		for (int i = 0; i < g_horseTrotTurnCount; i++)
		{
			if (g_horseTrotTurnData[i].isValid && g_horseTrotTurnData[i].horseFormID == horseFormID)
			{
				return &g_horseTrotTurnData[i];
			}
		}
		
		if (g_horseTrotTurnCount < MAX_TRACKED_HORSES)
		{
			HorseTrotTurnData* data = &g_horseTrotTurnData[g_horseTrotTurnCount];
			data->horseFormID = horseFormID;
			data->lastTrotTurnTime = -HORSE_TROT_TURN_COOLDOWN;
			data->isValid = true;
			g_horseTrotTurnCount++;
			return data;
		}
		
		return nullptr;
	}
	
	bool TryHorseTrotTurnToAvoid(Actor* horse, bool turnRight)
	{
		if (!horse) return false;
		
		InitTrotIdles();
		
		TESIdleForm* trotIdle = turnRight ? g_horseTrotRightIdle : g_horseTrotLeftIdle;
		if (!trotIdle) return false;
		
		HorseTrotTurnData* data = GetOrCreateTrotTurnData(horse->formID);
		if (!data) return false;
		
		float currentTime = GetCurrentTime();
		if ((currentTime - data->lastTrotTurnTime) < HORSE_TROT_TURN_COOLDOWN) return false;
		
		const char* eventName = trotIdle->animationEvent.c_str();
		if (!eventName || strlen(eventName) == 0) return false;
		
		bool result = SendHorseAnimationEvent(horse, eventName);
		
		if (result)
		{
			data->lastTrotTurnTime = currentTime;
			_MESSAGE("SpecialMovesets: Horse %08X TROT TURN %s", horse->formID, turnRight ? "RIGHT" : "LEFT");
		}
		
		return result;
	}
	
	bool TryHorseTrotTurnFromObstruction(Actor* horse)
	{
		if (!horse) return false;
		
		ObstructionSide side = GetObstructionSide(horse->formID);
		
		switch (side)
		{
			case ObstructionSide::Left:
				return TryHorseTrotTurnToAvoid(horse, true);
			case ObstructionSide::Right:
				return TryHorseTrotTurnToAvoid(horse, false);
			case ObstructionSide::Front:
			case ObstructionSide::Both:
				{
					EnsureRandomSeeded();
					return TryHorseTrotTurnToAvoid(horse, (rand() % 2) == 0);
				}
			default:
				return false;
		}
	}
	
	// ============================================
	// STATIONARY RAPID FIRE MANEUVER
	// ============================================
	
	static RapidFireData* GetOrCreateRapidFireData(UInt32 riderFormID)
	{
		for (int i = 0; i < g_rapidFireCount; i++)
		{
			if (g_rapidFireData[i].isValid && g_rapidFireData[i].riderFormID == riderFormID)
			{
				return &g_rapidFireData[i];
			}
		}
		
		if (g_rapidFireCount < MAX_TRACKED_HORSES)
		{
			RapidFireData* data = &g_rapidFireData[g_rapidFireCount];
			data->riderFormID = riderFormID;
			// Start on cooldown so rapid fire can't trigger immediately at combat start
			// 20 second initial delay before rapid fire is possible
			data->state = RapidFireState::Cooldown;
			data->stateStartTime = GetCurrentTime();
			data->lastShotTime = 0;
			data->drawStartTime = 0;
			data->playerCloseStartTime = 0;
			data->lastRollTime = 0;
			data->isDrawing = false;
			data->playerIsClose = false;
			data->shotsFired = 0;
			data->isValid = true;
			g_rapidFireCount++;
			
			_MESSAGE("SpecialMovesets: Rider %08X rapid fire initialized - on cooldown for 20 seconds", riderFormID);
			
			return data;
		}
		
		return nullptr;
	}
	
	void NotifyRangedCombatStarted()
	{
		float currentTime = GetCurrentTime();
		
		// Always set the start time when combat starts
		// This ensures the timer is properly initialized
		g_rangedCombatStartTime = currentTime;
		
		_MESSAGE("SpecialMovesets: Ranged combat started at time %.1f - rapid fire available in %.0f seconds", 
			currentTime, RAPID_FIRE_COMBAT_DELAY);
	}
	
	RapidFireState GetRapidFireState(UInt32 riderFormID)
	{
		for (int i = 0; i < g_rapidFireCount; i++)
		{
			if (g_rapidFireData[i].isValid && g_rapidFireData[i].riderFormID == riderFormID)
			{
				return g_rapidFireData[i].state;
			}
		}
		return RapidFireState::None;
	}
	
	bool IsInStationaryRapidFire(UInt32 riderFormID)
	{
		return GetRapidFireState(riderFormID) == RapidFireState::Active;
	}
	
	bool IsRapidFireDrawing(UInt32 riderFormID)
	{
		for (int i = 0; i < g_rapidFireCount; i++)
		{
			if (g_rapidFireData[i].isValid && g_rapidFireData[i].riderFormID == riderFormID)
			{
				return g_rapidFireData[i].state == RapidFireState::Active && g_rapidFireData[i].isDrawing;
			}
		}
		return false;
	}
	
	void EndStationaryRapidFire(UInt32 riderFormID)
	{
		for (int i = 0; i < g_rapidFireCount; i++)
		{
			if (g_rapidFireData[i].isValid && g_rapidFireData[i].riderFormID == riderFormID)
			{
				if (g_rapidFireData[i].state == RapidFireState::Active)
				{
					_MESSAGE("SpecialMovesets: Rider %08X ending rapid fire (fired %d shots)", 
						riderFormID, g_rapidFireData[i].shotsFired);
					g_rapidFireData[i].state = RapidFireState::Cooldown;
					g_rapidFireData[i].stateStartTime = GetCurrentTime();
					g_rapidFireData[i].isDrawing = false;
				}
				return;
			}
		}
	}
	
	void ClearAllRapidFireData()
	{
		for (int i = 0; i < MAX_TRACKED_HORSES; i++)
		{
			g_rapidFireData[i].isValid = false;
		}
		g_rapidFireCount = 0;
		g_rangedCombatStartTime = 0;
	}
	
	bool TryStationaryRapidFire(Actor* rider, Actor* horse, Actor* target, float distanceToTarget)
	{
		if (!rider || !horse || !target) return false;
		
		RapidFireData* data = GetOrCreateRapidFireData(rider->formID);
		if (!data) return false;
		
		float currentTime = GetCurrentTime();
		
		// If already active, return true (handled by Update)
		if (data->state == RapidFireState::Active)
		{
			return true;
		}
		
		// Check cooldown (20 seconds between uses)
		if (data->state == RapidFireState::Cooldown)
		{
			float timeSinceCooldownStart = currentTime - data->stateStartTime;
			if (timeSinceCooldownStart < RAPID_FIRE_COOLDOWN)
			{
				return false;
			}
			// Cooldown expired - reset to None
			data->state = RapidFireState::None;
			_MESSAGE("SpecialMovesets: Rider %08X rapid fire cooldown expired", rider->formID);
		}
		
		// Only roll once per second (not every tick)
		const float ROLL_INTERVAL = 1.0f;
		if ((currentTime - data->lastRollTime) < ROLL_INTERVAL)
		{
			return false;
		}
		data->lastRollTime = currentTime;
		
		// Check distance (must be 200-2500 units away)
		if (distanceToTarget < RAPID_FIRE_MIN_DISTANCE || distanceToTarget > RAPID_FIRE_MAX_DISTANCE)
		{
			return false;
		}
		
		// Check if rider has bow equipped
		if (!IsBowEquipped(rider))
		{
			return false;
		}
		
		// Roll chance (5% per second)
		EnsureRandomSeeded();
		int roll = rand() % 100;
		if (roll >= RAPID_FIRE_CHANCE)
		{
			return false;  // Didn't win the roll
		}
		
		// TRIGGER RAPID FIRE!
		data->state = RapidFireState::Active;
		data->stateStartTime = currentTime;
		data->lastShotTime = currentTime;
		data->drawStartTime = 0;
		data->playerCloseStartTime = 0;
		data->isDrawing = false;
		data->playerIsClose = false;
		data->shotsFired = 0;
		
		const char* riderName = CALL_MEMBER_FN(rider, GetReferenceName)();
		_MESSAGE("SpecialMovesets: ========================================");
		_MESSAGE("SpecialMovesets: RAPID FIRE TRIGGERED!");
		_MESSAGE("SpecialMovesets: Rider: '%s' (%08X)", riderName ? riderName : "Unknown", rider->formID);
		_MESSAGE("SpecialMovesets: Distance: %.0f, Duration: 5 seconds", distanceToTarget);
		_MESSAGE("SpecialMovesets: Horse will STOP MOVING");
		_MESSAGE("SpecialMovesets: ========================================");
		
		return true;
	}
	
	bool UpdateStationaryRapidFire(Actor* rider, Actor* horse, Actor* target)
	{
		if (!rider || !horse || !target) return false;
		
		RapidFireData* data = GetOrCreateRapidFireData(rider->formID);
		if (!data) return false;
		
		if (data->state != RapidFireState::Active)
		{
			return false;
		}
		
		float currentTime = GetCurrentTime();
		float elapsedTime = currentTime - data->stateStartTime;
		
		// ============================================
		// CHECK PLAYER DISTANCE - ABORT IF TOO CLOSE FOR TOO LONG
		// ============================================
		
		// Calculate distance to target
		float dx = target->pos.x - horse->pos.x;
		float dy = target->pos.y - horse->pos.y;
		float distanceToTarget = sqrt(dx * dx + dy * dy);
		
		if (distanceToTarget < RAPID_FIRE_ABORT_DISTANCE)
		{
			// Player is close
			if (!data->playerIsClose)
			{
				// Just entered close range - start timer
				data->playerIsClose = true;
				data->playerCloseStartTime = currentTime;
				_MESSAGE("SpecialMovesets: Rider %08X - Player entered close range (%.0f units) - abort in %.0f seconds if they stay",
					rider->formID, distanceToTarget, RAPID_FIRE_ABORT_TIME);
			}
			else
			{
				// Already close - check if 2 seconds have passed
				float closeTime = currentTime - data->playerCloseStartTime;
				if (closeTime >= RAPID_FIRE_ABORT_TIME)
				{
					// ABORT - player has been close for too long
					_MESSAGE("SpecialMovesets: Rider %08X ABORTING rapid fire - player close for %.1f seconds",
						rider->formID, closeTime);
						
					// Release arrow if we're drawing
					if (data->isDrawing)
					{
						PlayBowReleaseAnimation(rider, target);
					}
					
					EndStationaryRapidFire(rider->formID);
					return false;
				}
			}
		}
		else
		{
			// Player is far enough - reset close tracking
			if (data->playerIsClose)
			{
				_MESSAGE("SpecialMovesets: Rider %08X - Player left close range - abort timer reset", rider->formID);
			}
			data->playerIsClose = false;
			data->playerCloseStartTime = 0;
		}
		
		// ============================================
		// CHECK DURATION - END IF TIME EXPIRED
		// ============================================
		
		if (elapsedTime >= RAPID_FIRE_DURATION)
		{
			// Before ending, make sure we release if we're still drawing
			if (data->isDrawing)
			{
				PlayBowReleaseAnimation(rider, target);
				_MESSAGE("SpecialMovesets: Rider %08X rapid fire FINAL RELEASE before ending", rider->formID);
			}
			
			EndStationaryRapidFire(rider->formID);
			_MESSAGE("SpecialMovesets: Rider %08X rapid fire COMPLETE after %.1fs - resuming normal movement", 
				rider->formID, elapsedTime);
			return false;
		}
		
		// ============================================
		// HANDLE DRAW/RELEASE CYCLE
		// Draw 1 sec -> Release -> Immediately Draw -> etc (no wait)
		// ============================================
		
		if (data->isDrawing)
		{
			// Currently drawing - check if draw time has passed
			float drawElapsed = currentTime - data->drawStartTime;
			if (drawElapsed >= RAPID_FIRE_DRAW_TIME)
			{
				// Release the arrow!
				PlayBowReleaseAnimation(rider, target);
				data->isDrawing = false;
				data->lastShotTime = currentTime;
				data->shotsFired++;
				
				_MESSAGE("SpecialMovesets: Rider %08X rapid fire RELEASE shot #%d (elapsed: %.1fs)", 
					rider->formID, data->shotsFired, elapsedTime);
				
				// Immediately start next draw if time permits
				float timeRemaining = RAPID_FIRE_DURATION - elapsedTime;
				if (timeRemaining > RAPID_FIRE_DRAW_TIME + 0.1f)
				{
					if (PlayBowDrawAnimation(rider))
					{
						data->isDrawing = true;
						data->drawStartTime = currentTime;
						_MESSAGE("SpecialMovesets: Rider %08X rapid fire IMMEDIATE DRAW after release", rider->formID);
					}
				}
			}
		}
		else
		{
			// Not drawing - start drawing immediately (first shot or recovery)
			float timeRemaining = RAPID_FIRE_DURATION - elapsedTime;
			
			if (timeRemaining > RAPID_FIRE_DRAW_TIME + 0.1f)
			{
				if (PlayBowDrawAnimation(rider))
				{
					data->isDrawing = true;
					data->drawStartTime = currentTime;
					
					_MESSAGE("SpecialMovesets: Rider %08X rapid fire DRAW (elapsed: %.1fs, remaining: %.1fs)", 
						rider->formID, elapsedTime, timeRemaining);
				}
			}
		}
		
		return true;  // Still in rapid fire mode - horse should stay stationary
	}
}
