#pragma once
#include <iostream>
#include <string>
#include <fstream>
#include <unordered_set>
#include <unordered_map>
#include <vector>
#include <skse64/NiProperties.h>
#include <skse64/NiNodes.h>

#include "skse64\GameSettings.h"
#include "Utility.hpp"

#include <skse64/GameData.h>

#include "higgsinterface001.h"
#include "SkyrimVRESLAPI.h"

namespace MountedNPCCombatVR {

	const UInt32 MOD_VERSION = 0x10000;
	const std::string MOD_VERSION_STR = "1.0.0";
	extern int leftHandedMode;

	extern int logging;
	
	// ============================================
	// GENERAL SETTINGS
	// ============================================
	
	// NPC Dismount Prevention setting
	// When enabled, NPCs will not be forced to dismount when attacked
	extern bool PreventNPCDismountOnAttack;
	
	// Enable Remounting - allows dismounted NPCs to remount their horses
	extern bool EnableRemounting;
	
	// ============================================
	// COMBAT RANGE SETTINGS
	// ============================================
	
	// Weapon switch distance - distance at which rider switches between melee and bow
	extern float WeaponSwitchDistance;

	// Melee Range (On Foot) - how close the rider gets to an on-foot target (player)
	extern float MeleeRangeOnFoot;
	
	// Melee Range (On Foot NPC) - how close the rider gets to an on-foot NPC target (not player)
	// Needs to be larger because both are moving toward each other
	extern float MeleeRangeOnFootNPC;
	
	// Melee Range (Mounted) - how close the rider gets to a mounted target
	extern float MeleeRangeMounted;
	
	// ============================================
	// WEAPON SWITCH SETTINGS
	// ============================================
	
	// Weapon Switch Cooldown - seconds between weapon switches (prevents spam)
	extern float WeaponSwitchCooldown;
	
	// Sheathe Transition Time - seconds to wait for sheathe animation before equipping new weapon
	extern float SheatheTransitionTime;
	
	// ============================================
	// MOUNT ROTATION SETTINGS
	// ============================================
	
	// Horse Rotation Speed - how fast the horse rotates to face target (0.0-1.0)
	// Lower = slower/smoother rotation, Higher = faster/snappier rotation
	// Default: 0.15
	extern float HorseRotationSpeed;
	
	// ============================================
	// MELEE ATTACK ANGLE SETTINGS
	// ============================================
	
	// Attack Angle Threshold vs Player (radians) - how aligned the horse must be to attack
	// Default: 0.26 (~15 degrees) - tight requirement for player
	extern float AttackAnglePlayer;
	
	// Attack Angle Threshold vs NPC (radians) - more lenient for NPC targets
	// Default: 0.52 (~30 degrees) - easier to land hits on NPCs
	extern float AttackAngleNPC;
	
	// Attack Angle Threshold vs Mounted (radians) - for mounted vs mounted combat
	// Default: 0.35 (~20 degrees)
	extern float AttackAngleMounted;
	
	// ============================================
	// CHARGE MANEUVER SETTINGS
	// ============================================
	
	// Enable charge maneuver
	extern bool ChargeEnabled;
	
	// Chance (percent) to trigger charge when conditions are met
	extern int ChargeChancePercent;
	
	// Cooldown (seconds) between charge attempts
	extern float ChargeCooldown;
	
	// Minimum distance to trigger charge
	extern float ChargeMinDistance;
	
	// Maximum distance for charge
	extern float ChargeMaxDistance;
	
	// ============================================
	// RAPID FIRE MANEUVER SETTINGS
	// ============================================
	
	// Enable rapid fire maneuver
	extern bool RapidFireEnabled;
	
	// Chance (percent) to trigger rapid fire when conditions are met
	extern int RapidFireChancePercent;
	
	// Cooldown (seconds) between rapid fire attempts
	extern float RapidFireCooldown;
	
	// Duration (seconds) the horse stays stationary during rapid fire
	extern float RapidFireDuration;
	
	// Number of arrows fired during rapid fire
	extern int RapidFireShotCount;
	
	// ============================================
	// BOW ATTACK SETTINGS
	// ============================================
	
	// Enable ranged attacks
	extern bool RangedAttacksEnabled;
	
	// Minimum bow draw time (seconds)
	extern float BowDrawMinTime;
	
	// Maximum bow draw time (seconds)
	extern float BowDrawMaxTime;
	
	// ============================================
	// ARROW AIM SETTINGS
	// ============================================
	
	// Height offset for shooter position when firing arrows
	extern float ArrowShooterHeightOffset;
	
	// Target height offset when target is on foot (chest level)
	extern float ArrowTargetFootHeight;
	
	// Target height offset when target is mounted (chest level on horse)
	extern float ArrowTargetMountedHeight;
	
	// ============================================
	// REAR UP SETTINGS
	// ============================================
	
	// Enable rear up on approach
	extern bool RearUpEnabled;
	
	// Chance (percent) to rear up when approaching target
	extern int RearUpApproachChance;
	
	// Chance (percent) to rear up when taking damage
	extern int RearUpDamageChance;
	
	// Cooldown (seconds) between rear ups for the same horse
	extern float RearUpCooldown;
	
	// ============================================
	// STAND GROUND SETTINGS
	// ============================================
	
	// Enable stand ground maneuver
	extern bool StandGroundEnabled;
	
	// Must be within this distance to trigger
	extern float StandGroundMaxDistance;     	
	
	// Minimum stand time
	extern float StandGroundMinDuration;	
	
	// Maximum stand time
	extern float StandGroundMaxDuration;	
	
	// Chance to trigger per check
	extern int StandGroundChancePercent;	
	
	// How often to check
	extern float StandGroundCheckInterval;	
	
	// Cooldown between attempts
	extern float StandGroundCooldown;		

	// ============================================
	// SPECIAL RIDER COMBAT SETTINGS (Captains & Companions)
	// ============================================
	
	// Ranged combat role distance settings
	extern float RangedRoleMinDistance;       // If closer than this, switch to melee
	extern float RangedRoleIdealDistance;     // Ideal distance to hold position
	extern float RangedRoleMaxDistance;   // If further than this, move closer
	extern float RangedPositionTolerance;     // How close to ideal is "close enough"
	
	// Ranged fire distance settings
	extern float RangedFireMinDistance;       // Minimum distance to fire bow
	extern float RangedFireMaxDistance;  // Maximum distance to fire bow

	// Role checking interval
	extern float RoleCheckInterval;// Time in seconds to re-check roles

	// ============================================
	// HOSTILE DETECTION SETTINGS
	// ============================================
	
	// Range to detect hostile actors
	extern float HostileDetectionRange;
	
	// How often to scan for hostiles (seconds)
	extern float HostileScanInterval;
	
	// ============================================
	// TRACKING LIMITS
	// ============================================
	
	// Maximum number of mounted NPCs to track simultaneously
	// Range: 1-10 (hardcoded max is 10 for memory safety)
	extern int MaxTrackedMountedNPCs;
	
	// ============================================
	// COMPANION COMBAT SETTINGS
	// ============================================
	
	// Enable mounted companion combat
	// When enabled, player followers/teammates on horseback will 
	// automatically engage hostiles attacking the player
	extern bool CompanionCombatEnabled;
	
	// Maximum number of mounted companions to track
	// Range: 1-5 (hardcoded max is 5)
	extern int MaxTrackedCompanions;
	
	// Range to scan for mounted companions around player (game units)
	extern float CompanionScanRange;
	
	// How often to scan for new mounted companions (seconds)
	extern float CompanionScanInterval;
	
	// Range at which companions will detect hostile targets (game units)
	extern float CompanionTargetRange;
	
	// Range at which companions will engage a detected hostile (game units)
	extern float CompanionEngageRange;
	
	// How often to update companion combat behavior (seconds)
	extern float CompanionUpdateInterval;
	
	// Melee range for companions - how close they get to targets for melee attacks
	extern float CompanionMeleeRange;
	
	// ============================================
	// COMPANION NAME LIST (for mod-added followers)
	// ============================================
	
	// Maximum number of companion names that can be configured
	const int MAX_COMPANION_NAMES = 20;
	
	// Array of companion names to always treat as teammates
	// These are matched by name (case-insensitive partial match)
	extern std::string CompanionNameList[MAX_COMPANION_NAMES];
	extern int CompanionNameCount;
	
	// Check if an actor's name matches any in the companion list
	bool IsInCompanionNameList(const char* actorName);

	void loadConfig();
	
	void Log(const int msgLogLevel, const char* fmt, ...);
	enum eLogLevels
	{
		LOGLEVEL_ERR = 0,
		LOGLEVEL_WARN,
		LOGLEVEL_INFO,
	};


#define LOG(fmt, ...) Log(LOGLEVEL_WARN, fmt, ##__VA_ARGS__)
#define LOG_ERR(fmt, ...) Log(LOGLEVEL_ERR, fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...) Log(LOGLEVEL_INFO, fmt, ##__VA_ARGS__)


	
	// ============================================
	// MOUNTED ATTACK STAGGER SETTINGS
	// ============================================
	
	extern bool MountedAttackStaggerEnabled;      // Enable stagger on unblocked hits vs NPCs
	extern int MountedAttackStaggerChance;        // Chance to stagger (0-100)
	extern float MountedAttackStaggerForce;       // Knockback force

}