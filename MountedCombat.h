#pragma once

#include "Helper.h"
#include "WeaponDetection.h"
#include "NPCProtection.h"
#include "AILogging.h"

namespace MountedNPCCombatVR
{
	// ============================================
	// Configuration
	// ============================================
	
	extern float g_updateInterval;
	extern const int MAX_TRACKED_NPCS;
	extern const float FLEE_SAFE_DISTANCE;
	
	// ============================================
	// Player Mounted Combat State
	// ============================================
	
	extern bool g_playerInMountedCombat;
	extern bool g_playerTriggeredMountedCombat;
	extern bool g_playerWasMountedWhenCombatStarted;
	extern bool g_playerInExterior;
	extern bool g_playerIsDead;
	
	void UpdatePlayerMountedCombatState();
	bool IsPlayerMounted();
	bool IsPlayerInCombat();
	bool IsPlayerInExteriorCell();
	bool IsPlayerDead();
	void OnPlayerTriggeredMountedCombat(Actor* mountedNPC);

	// ============================================
	// Combat Class Types
	// ============================================
	
	enum class MountedCombatClass
	{
		None,
		GuardMelee,
		SoldierMelee,
		BanditRanged,
		MageCaster,
		CivilianFlee,
		Other           // Unknown/unclassified faction - defaults to aggressive melee behavior
	};
	
	// ============================================
	// Combat Class Global Bools
	// ============================================
	
	extern bool g_guardInMountedCombat;
	extern bool g_soldierInMountedCombat;
	extern bool g_banditInMountedCombat;
	extern bool g_mageInMountedCombat;
	extern bool g_civilianFleeing;

	// ============================================
	// Behavior Types
	// ============================================
	
	enum class MountedBehaviorType
	{
		Unknown,
		Aggressive,
		Passive
	};

	// ============================================
	// Combat State
	// ============================================
	
	enum class MountedCombatState
	{
		None,
		Engaging,
		Attacking,
		Circling,
		Charging,
		RangedAttack,
		Fleeing,
		Retreating
	};
	
	// ============================================
	// Mounted NPC Data
	// ============================================
	
	struct MountedNPCData
	{
		UInt32 actorFormID;
		UInt32 mountFormID;
		UInt32 targetFormID;
		MountedCombatState state;
		MountedBehaviorType behavior;
		MountedCombatClass combatClass;
		MountedWeaponInfo weaponInfo;
		float stateStartTime;
		float lastUpdateTime;
		float combatStartTime;
		bool weaponDrawn;
		bool isValid;
		
		MountedNPCData() : 
			actorFormID(0), mountFormID(0), targetFormID(0),
			state(MountedCombatState::None), behavior(MountedBehaviorType::Unknown),
			combatClass(MountedCombatClass::None), weaponInfo(),
			stateStartTime(0.0f), lastUpdateTime(0.0f), combatStartTime(0.0f),
			weaponDrawn(false), isValid(false) 
		{}
		
		void Reset()
		{
			actorFormID = 0; mountFormID = 0; targetFormID = 0;
			state = MountedCombatState::None;
			behavior = MountedBehaviorType::Unknown;
			combatClass = MountedCombatClass::None;
			weaponInfo = MountedWeaponInfo();
			stateStartTime = 0.0f; lastUpdateTime = 0.0f; combatStartTime = 0.0f;
			weaponDrawn = false; isValid = false;
		}
	};

	// ============================================
	// Core Functions
	// ============================================
	
	void InitMountedCombatSystem();
	void ShutdownMountedCombatSystem();
	void ResetAllMountedNPCs();
	void OnDismountBlocked(Actor* actor, Actor* mount);
	void UpdateMountedCombat();
	void UpdateCombatClassBools();

	// ============================================
	// NPC Tracking
	// ============================================
	
	MountedNPCData* GetOrCreateNPCData(Actor* actor);
	MountedNPCData* GetNPCData(UInt32 formID);
	MountedNPCData* GetNPCDataByIndex(int index);  // For iteration
	void RemoveNPCFromTracking(UInt32 formID);
	bool IsNPCTracked(UInt32 formID);
	int GetTrackedNPCCount();

	// ============================================
	// Faction / Behavior (defined in FactionData.cpp)
	// ============================================
	
	MountedBehaviorType DetermineBehaviorType(Actor* actor);
	MountedCombatClass DetermineCombatClass(Actor* actor);
	const char* GetCombatClassName(MountedCombatClass combatClass);
	bool IsGuardFaction(Actor* actor);
	bool IsSoldierFaction(Actor* actor);
	bool IsBanditFaction(Actor* actor);
	bool IsMageFaction(Actor* actor);
	bool IsCivilianFaction(Actor* actor);

	// ============================================
	// Combat Behavior
	// ============================================
	
	MountedCombatState DetermineAggressiveState(Actor* actor, Actor* mount, Actor* target, MountedWeaponInfo* weaponInfo);
	void ExecuteAggressiveBehavior(MountedNPCData* npcData, Actor* actor, Actor* mount, Actor* target);
	void ExecuteAttacking(Actor* actor, Actor* mount, Actor* target, MountedWeaponInfo* weaponInfo);
	void ExecuteCircling(Actor* actor, Actor* mount, Actor* target);

	// ============================================
	// Flee Behavior
	// ============================================
	
	MountedCombatState DeterminePassiveState(Actor* actor, Actor* mount, Actor* threat);
	void ExecutePassiveBehavior(MountedNPCData* npcData, Actor* actor, Actor* mount, Actor* threat);
	void ExecuteFleeing(Actor* actor, Actor* mount, Actor* threat);

	// ============================================
	// HOSTILE TARGET DETECTION & ENGAGEMENT
	// ============================================
	// Guards and soldiers will automatically engage
	// hostile NPCs within detection range (1400 units)
	// ============================================
	
	// Find the nearest hostile NPC within range of a mounted guard/soldier
	Actor* FindNearestHostileTarget(Actor* rider, float maxRange);
	
	// Force a mounted NPC into combat with a target
	// Sets combat target, draws weapon, injects follow package
	bool EngageHostileTarget(Actor* rider, Actor* target);
	
	// Scan all tracked mounted NPCs for nearby hostile targets
	// Called periodically from UpdateMountedCombat
	void ScanForHostileTargets();
	
	// Alert nearby mounted allies when a guard/soldier is attacked
	// This makes nearby guards join the fight against the attacker
	void AlertNearbyMountedAllies(Actor* attackedNPC, Actor* attacker);
	
	// ============================================
	// REMOUNT AI SCANNING
	// ============================================
	// Scans for dismounted NPCs in combat within 2000 units of player
	// and registers them with RemountAI for potential remount.
	// Applies to ANY humanoid NPC in combat - they don't need to have
	// been previously mounted. Called from UpdateMountedCombat.
	// ============================================
	
	void ScanForDismountedNPCsInCombat();

	// ============================================
	// External System Connections
	// ============================================
	
	Actor* GetCombatTarget(Actor* actor);
	float GetDistanceBetween(Actor* a, Actor* b);
	bool CanAttackTarget(Actor* actor, Actor* target, MountedWeaponInfo* weaponInfo);
	bool IsPathClear(Actor* mount, Actor* target);
	float GetCurrentGameTime();
	NiPoint3 GetFleeDirection(Actor* actor, Actor* threat);
}
