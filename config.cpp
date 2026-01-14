#include "config.h"

namespace MountedNPCCombatVR {
		
	int logging = 1;
    int leftHandedMode = 0;
	
	// ============================================
	// GENERAL SETTINGS
	// ============================================
	
	bool PreventNPCDismountOnAttack = true;
	bool EnableRemounting = true;
	
	// ============================================
	// COMBAT RANGE SETTINGS
	// ============================================
	
	float WeaponSwitchDistance = 250.0f;  // Should be > MeleeRangeOnFoot so weapon switches BEFORE entering melee
	float WeaponSwitchDistanceMounted = 325.0f;  // Mounted vs mounted NPC combat
	float MeleeRangeOnFoot = 140.0f;
	float MeleeRangeOnFootNPC = 230.0f;
	float MeleeRangeMounted = 250.0f;

	// ============================================
	// WEAPON SWITCH SETTINGS
	// ============================================
	
	float WeaponSwitchCooldown = 1.0f;  // Reduced to 1 second for responsive switching
	float SheatheTransitionTime = 0.5f;  // Time to wait for sheathe animation

	// ============================================
	// MOUNT ROTATION SETTINGS
	// ============================================
	
	float HorseRotationSpeed = 0.15f;

	// ============================================
	// MELEE ATTACK ANGLE SETTINGS
	// ============================================
	
	float AttackAnglePlayer = 0.52f;  // Increased from 0.26 to 0.52 (~30 degrees) for better close-range attacks
	float AttackAngleNPC = 0.52f;
	float AttackAngleMounted = 0.35f;
	float CloseRangeAttackDistance = 120.0f;  // Skip angle check when closer than this distance

	// ============================================
	// CLOSE RANGE MELEE ASSAULT SETTINGS
	// ============================================
	
	float CloseRangeMeleeAssaultDistance = 145.0f;  // Trigger when target within this distance of rider's side
	float CloseRangeMeleeAssaultInterval = 1.0f;    // 1 second between attacks
	float CloseRangeRotationLockDistance = 140.0f;  // Distance at which rotation lock applies

	// ============================================
	// CHARGE MANEUVER SETTINGS
	// ============================================
	
	bool ChargeEnabled = true;
	int ChargeChancePercent = 7;
	float ChargeCooldown = 45.0f;
	float ChargeMinDistance = 700.0f;
	float ChargeMaxDistance = 1500.0f;
	
	// ============================================
	// RAPID FIRE MANEUVER SETTINGS
	// ============================================
	
	bool RapidFireEnabled = true;
	int RapidFireChancePercent = 7;
	float RapidFireCooldown = 45.0f;
	float RapidFireDuration = 7.0f;
	int RapidFireShotCount = 5;
	
	// ============================================
	// BOW ATTACK SETTINGS
	// ============================================
	
	bool RangedAttacksEnabled = true;
	float BowDrawMinTime = 2.0f;
	float BowDrawMaxTime = 3.5f;
	
	// ============================================
	// ARROW AIM SETTINGS
	// ============================================
	
	float ArrowShooterHeightOffset = 0.0f;    // Height offset for shooter position
	float ArrowTargetFootHeight = 80.0f;      // Target height when on foot (chest level)
	float ArrowTargetMountedHeight = 120.0f;  // Target height when mounted (chest level on horse)

	// ============================================
	// REAR UP SETTINGS
	// ============================================
	
	bool RearUpEnabled = true;
	int RearUpApproachChance = 7;
	int RearUpDamageChance = 10;
	float RearUpCooldown = 20.0f;
	
	// ============================================
	// STAND GROUND SETTINGS
	// ============================================
	
	bool StandGroundEnabled = true;
	float StandGroundMaxDistance = 260.0f;      // Must be within this distance to trigger
	float StandGroundMinDuration = 3.0f;        // Minimum stand time
	float StandGroundMaxDuration = 8.0f;     // Maximum stand time
	int StandGroundChancePercent = 25;          // 25% chance to trigger per check
	float StandGroundCheckInterval = 2.0f;      // Check every 2 seconds
	float StandGroundCooldown = 5.0f;  // 5 seconds between attempts
	
	// ============================================
	// SPECIAL RIDER COMBAT SETTINGS (Captains & Companions)
	// ============================================
	
	float RangedRoleMinDistance = 500.0f;       // If closer than this, switch to melee
	float RangedRoleIdealDistance = 800.0f;     // Ideal distance to hold position
	float RangedRoleMaxDistance = 1400.0f;      // If further than this, move closer
	float RangedPositionTolerance = 100.0f;     // How close to ideal is "close enough"
	float RangedFireMinDistance = 300.0f;       // Minimum distance to fire bow
	float RangedFireMaxDistance = 1900.0f;      // Maximum distance to fire bow
	float RoleCheckInterval = 2.0f;             // Time in seconds to re-check roles

	// ============================================
	// MOUNTED ATTACK STAGGER SETTINGS
	// ============================================
	
	bool MountedAttackStaggerEnabled = true;    // Enable stagger on unblocked hits vs NPCs
	int MountedAttackStaggerChance = 20;        // 20% chance to stagger
	float MountedAttackStaggerForce = 0.5f;     // Knockback force (gentle to avoid flying)
	
	// ============================================
	// MOUNTED ATTACK DAMAGE MULTIPLIER SETTINGS
	// ============================================
	
	float HostileRiderDamageMultiplier = 3.0f;  // 3x damage for hostile riders vs NPCs
	float CompanionRiderDamageMultiplier = 2.0f;   // 2x damage for companion riders vs NPCs

	// ============================================
	// MOUNTED WEAPON REACH SETTINGS
	// ============================================
	
	float TwoHandedReachBonus = 80.0f;  // Additional reach for 2H weapons (greatswords, battleaxes)

	// ============================================
	// COMBAT DISTANCE SETTINGS
	// ============================================
	
	float MaxCombatDistance = 2000.0f;   // Max distance before NPC disengages
	float MaxCompanionCombatDistance = 1950.0f;    // Max distance for companions (slightly less)

	// ============================================
	// HOSTILE DETECTION SETTINGS
	// ============================================
	
	float HostileDetectionRange = 1400.0f;
	float HostileScanInterval = 3.0f;
	
	// ============================================
	// TRACKING LIMITS
	// ============================================
	
	int MaxTrackedMountedNPCs = 5;
	
	// ============================================
	// COMPANION COMBAT SETTINGS
	// ============================================
	
	bool CompanionCombatEnabled = true;
	int MaxTrackedCompanions = 5;
	float CompanionScanRange = 2000.0f;
	float CompanionScanInterval = 1.0f;
	float CompanionTargetRange = 2000.0f;
	float CompanionEngageRange = 1500.0f;
	float CompanionUpdateInterval = 0.5f;
	float CompanionMeleeRange = 175.0f;

	// ============================================
	// COMPANION NAME LIST (for mod-added followers)
	// ============================================
	
	std::string CompanionNameList[MAX_COMPANION_NAMES];
	int CompanionNameCount = 0;
	
	static std::string ToLowerCase(const std::string& str)
	{
		std::string lower = str;
		std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
		return lower;
	}
	
	bool IsInCompanionNameList(const char* actorName)
	{
		if (!actorName || CompanionNameCount == 0) return false;
		
		std::string nameLower = ToLowerCase(actorName);
		
		for (int i = 0; i < CompanionNameCount; i++)
		{
			if (CompanionNameList[i].empty()) continue;
			if (nameLower.find(CompanionNameList[i]) != std::string::npos)
				return true;
		}
		
		return false;
	}

    void loadConfig() 
 {
		std::string runtimeDirectory = GetRuntimeDirectory();

   if (runtimeDirectory.empty()) 
		{
			_MESSAGE("loadConfig: Using defaults");
			return;
		}
		
		std::string filepath = runtimeDirectory + "Data\\SKSE\\Plugins\\Mounted_NPC_Combat_VR.ini";
		std::ifstream file(filepath);

		if (!file.is_open()) 
		{
			transform(filepath.begin(), filepath.end(), filepath.begin(), ::tolower);
			file.open(filepath);
		}

		if (!file.is_open()) 
		{
			_MESSAGE("loadConfig: INI not found, using defaults");
			return;
		}
		
		std::string line;
		std::string currentSection;

		while (std::getline(file, line)) 
		{
			trim(line);
			skipComments(line);
			if (line.empty()) continue;

			if (line[0] == '[') 
			{
				size_t endBracket = line.find(']');
				if (endBracket != std::string::npos) 
				{
					currentSection = line.substr(1, endBracket - 1);
					trim(currentSection);
				}
			}
			else if (currentSection == "Settings") 
			{
				std::string variableName;
				std::string variableValueStr = GetConfigSettingsStringValue(line, variableName);

				// General Settings
				if (variableName == "Logging") logging = std::stoi(variableValueStr);
				else if (variableName == "PreventNPCDismountOnAttack") PreventNPCDismountOnAttack = (std::stoi(variableValueStr) != 0);
				else if (variableName == "EnableRemounting") EnableRemounting = (std::stoi(variableValueStr) != 0);
				// Combat Range
				else if (variableName == "WeaponSwitchDistance") WeaponSwitchDistance = std::stof(variableValueStr);
				else if (variableName == "WeaponSwitchDistanceMounted") WeaponSwitchDistanceMounted = std::stof(variableValueStr);
				else if (variableName == "MeleeRangeOnFoot") MeleeRangeOnFoot = std::stof(variableValueStr);
				else if (variableName == "MeleeRangeOnFootNPC") MeleeRangeOnFootNPC = std::stof(variableValueStr);
				else if (variableName == "MeleeRangeMounted") MeleeRangeMounted = std::stof(variableValueStr);
				// Weapon Switch
				else if (variableName == "WeaponSwitchCooldown") WeaponSwitchCooldown = std::stof(variableValueStr);
				else if (variableName == "SheatheTransitionTime") SheatheTransitionTime = std::stof(variableValueStr);
				// Mount Rotation
				else if (variableName == "HorseRotationSpeed") 
				{
					HorseRotationSpeed = std::stof(variableValueStr);
					if (HorseRotationSpeed < 0.01f) HorseRotationSpeed = 0.01f;
					if (HorseRotationSpeed > 1.0f) HorseRotationSpeed = 1.0f;
				}
				// Attack Angles
				else if (variableName == "AttackAnglePlayer") AttackAnglePlayer = std::stof(variableValueStr);
				else if (variableName == "AttackAngleNPC") AttackAngleNPC = std::stof(variableValueStr);
				else if (variableName == "AttackAngleMounted") AttackAngleMounted = std::stof(variableValueStr);
				// Charge
				else if (variableName == "ChargeEnabled") ChargeEnabled = (std::stoi(variableValueStr) != 0);
				else if (variableName == "ChargeChancePercent") ChargeChancePercent = std::stoi(variableValueStr);
				else if (variableName == "ChargeCooldown") ChargeCooldown = std::stof(variableValueStr);
				else if (variableName == "ChargeMinDistance") ChargeMinDistance = std::stof(variableValueStr);
				else if (variableName == "ChargeMaxDistance") ChargeMaxDistance = std::stof(variableValueStr);
				// Rapid Fire
				else if (variableName == "RapidFireEnabled") RapidFireEnabled = (std::stoi(variableValueStr) != 0);
				else if (variableName == "RapidFireChancePercent") RapidFireChancePercent = std::stoi(variableValueStr);
				else if (variableName == "RapidFireCooldown") RapidFireCooldown = std::stof(variableValueStr);
				else if (variableName == "RapidFireDuration") RapidFireDuration = std::stof(variableValueStr);
				else if (variableName == "RapidFireShotCount") RapidFireShotCount = std::stoi(variableValueStr);
				// Bow Attack
				else if (variableName == "RangedAttacksEnabled") RangedAttacksEnabled = (std::stoi(variableValueStr) != 0);
				else if (variableName == "BowDrawMinTime") BowDrawMinTime = std::stof(variableValueStr);
				else if (variableName == "BowDrawMaxTime") BowDrawMaxTime = std::stof(variableValueStr);
				// Arrow Aim
				else if (variableName == "ArrowShooterHeightOffset") ArrowShooterHeightOffset = std::stof(variableValueStr);
				else if (variableName == "ArrowTargetFootHeight") ArrowTargetFootHeight = std::stof(variableValueStr);
				else if (variableName == "ArrowTargetMountedHeight") ArrowTargetMountedHeight = std::stof(variableValueStr);
				// Rear Up
				else if (variableName == "RearUpEnabled") RearUpEnabled = (std::stoi(variableValueStr) != 0);
				else if (variableName == "RearUpApproachChance") RearUpApproachChance = std::stoi(variableValueStr);
				else if (variableName == "RearUpDamageChance") RearUpDamageChance = std::stoi(variableValueStr);
				else if (variableName == "RearUpCooldown") RearUpCooldown = std::stof(variableValueStr);
				// Stand Ground
				else if (variableName == "StandGroundEnabled") StandGroundEnabled = (std::stoi(variableValueStr) != 0);
				else if (variableName == "StandGroundMaxDistance") StandGroundMaxDistance = std::stof(variableValueStr);
				else if (variableName == "StandGroundMinDuration") StandGroundMinDuration = std::stof(variableValueStr);
				else if (variableName == "StandGroundMaxDuration") StandGroundMaxDuration = std::stof(variableValueStr);
				else if (variableName == "StandGroundChancePercent") StandGroundChancePercent = std::stoi(variableValueStr);
				else if (variableName == "StandGroundCheckInterval") StandGroundCheckInterval = std::stof(variableValueStr);
				else if (variableName == "StandGroundCooldown") StandGroundCooldown = std::stof(variableValueStr);
				// Special Rider Combat
				else if (variableName == "RangedRoleMinDistance") RangedRoleMinDistance = std::stof(variableValueStr);
				else if (variableName == "RangedRoleIdealDistance") RangedRoleIdealDistance = std::stof(variableValueStr);
				else if (variableName == "RangedRoleMaxDistance") RangedRoleMaxDistance = std::stof(variableValueStr);
				else if (variableName == "RangedPositionTolerance") RangedPositionTolerance = std::stof(variableValueStr);
				else if (variableName == "RangedFireMinDistance") RangedFireMinDistance = std::stof(variableValueStr);
				else if (variableName == "RangedFireMaxDistance") RangedFireMaxDistance = std::stof(variableValueStr);
				else if (variableName == "RoleCheckInterval") RoleCheckInterval = std::stof(variableValueStr);
				// Mounted Attack Stagger
				else if (variableName == "MountedAttackStaggerEnabled") MountedAttackStaggerEnabled = (std::stoi(variableValueStr) != 0);
				else if (variableName == "MountedAttackStaggerChance") MountedAttackStaggerChance = std::stoi(variableValueStr);
				else if (variableName == "MountedAttackStaggerForce") MountedAttackStaggerForce = std::stof(variableValueStr);
				// Damage Multipliers
				else if (variableName == "HostileRiderDamageMultiplier") HostileRiderDamageMultiplier = std::stof(variableValueStr);
				else if (variableName == "CompanionRiderDamageMultiplier") CompanionRiderDamageMultiplier = std::stof(variableValueStr);
				// Weapon Reach
				else if (variableName == "TwoHandedReachBonus") TwoHandedReachBonus = std::stof(variableValueStr);
				// Combat Distance
				else if (variableName == "MaxCombatDistance") MaxCombatDistance = std::stof(variableValueStr);
				else if (variableName == "MaxCompanionCombatDistance") MaxCompanionCombatDistance = std::stof(variableValueStr);
				// Hostile Detection
				else if (variableName == "HostileDetectionRange") HostileDetectionRange = std::stof(variableValueStr);
				else if (variableName == "HostileScanInterval") HostileScanInterval = std::stof(variableValueStr);
				// Tracking Limits
				else if (variableName == "MaxTrackedMountedNPCs") 
				{
					MaxTrackedMountedNPCs = std::stoi(variableValueStr);
					if (MaxTrackedMountedNPCs < 1) MaxTrackedMountedNPCs = 1;
					if (MaxTrackedMountedNPCs > 10) MaxTrackedMountedNPCs = 10;
				}
				// Companion Combat
				else if (variableName == "CompanionCombatEnabled") CompanionCombatEnabled = (std::stoi(variableValueStr) != 0);
				else if (variableName == "MaxTrackedCompanions") 
				{
					MaxTrackedCompanions = std::stoi(variableValueStr);
					if (MaxTrackedCompanions < 1) MaxTrackedCompanions = 1;
					if (MaxTrackedCompanions > 5) MaxTrackedCompanions = 5;
				}
				else if (variableName == "CompanionScanRange") CompanionScanRange = std::stof(variableValueStr);
				else if (variableName == "CompanionScanInterval") CompanionScanInterval = std::stof(variableValueStr);
				else if (variableName == "CompanionTargetRange") CompanionTargetRange = std::stof(variableValueStr);
				else if (variableName == "CompanionEngageRange") CompanionEngageRange = std::stof(variableValueStr);
				else if (variableName == "CompanionUpdateInterval") CompanionUpdateInterval = std::stof(variableValueStr);
				else if (variableName == "CompanionMeleeRange") CompanionMeleeRange = std::stof(variableValueStr);
				// Companion Names
				else if (variableName.find("CompanionName") == 0 && variableName.length() > 13)
				{
					std::string indexStr = variableName.substr(13);
					if (!indexStr.empty() && std::all_of(indexStr.begin(), indexStr.end(), ::isdigit))
					{
						int index = std::stoi(indexStr) - 1;
						if (index >= 0 && index < MAX_COMPANION_NAMES)
						{
							CompanionNameList[index] = ToLowerCase(variableValueStr);
							if (index >= CompanionNameCount) CompanionNameCount = index + 1;
						}
					}
				}
			}
		}
	
		file.close();
		_MESSAGE("loadConfig: INI loaded successfully");
	}

	void Log(const int msgLogLevel, const char* fmt, ...)
	{
		if (msgLogLevel > logging) return;

		va_list args;
		char logBuffer[4096];

		va_start(args, fmt);
		vsprintf_s(logBuffer, sizeof(logBuffer), fmt, args);
		va_end(args);

		_MESSAGE(logBuffer);
	}

}