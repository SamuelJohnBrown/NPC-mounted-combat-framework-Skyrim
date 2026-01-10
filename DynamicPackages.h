#pragma once

#include "skse64/GameReferences.h"
#include "skse64/GameForms.h"
#include "skse64_common/Relocation.h"

namespace MountedNPCCombatVR
{
	// ============================================
	// STRUCTURES FOR DYNAMIC PACKAGE CREATION
	// From ActiveRagdoll mod analysis
	// ============================================
	
	struct PackageLocation
	{
		union Data
		{
			TESForm* object;
			UInt32 refHandle;
		};
		static_assert(sizeof(Data) == 0x8);
		
		void* vtbl;      // 00
		UInt8 locType;   // 08
		UInt32 rad;  // 0C
		Data data;    // 10
	};
	static_assert(sizeof(PackageLocation) == 0x18);
	
	struct PackageTarget
	{
		UInt8 targType;  // 00
		UInt64 target; // 08
		UInt32 value;    // 10
		UInt32 pad14; // 14
	};
	static_assert(sizeof(PackageTarget) == 0x18);

	// ============================================
	// FUNCTION SIGNATURES
	// ============================================
	
	// Package Creation
	typedef TESPackage* (*_CreatePackageByType)(int type);
	typedef void (*_PackageLocation_CTOR)(PackageLocation* _this);
	typedef void (*_PackageLocation_SetNearReference)(PackageLocation* _this, TESObjectREFR* refr);
	typedef void (*_TESPackage_SetPackageLocation)(TESPackage* _this, PackageLocation* packageLocation);
	typedef void (*_PackageTarget_CTOR)(PackageTarget* _this);
	typedef void (*_TESPackage_SetPackageTarget)(TESPackage* _this, PackageTarget* packageTarget);
	typedef void (*_PackageTarget_ResetValueByTargetType)(PackageTarget* _this, int a2);
	typedef void (*_PackageTarget_SetFromReference)(PackageTarget* _this, TESObjectREFR* refr);
	typedef void (*_TESPackage_sub_140439BE0)(TESPackage* _this, UInt64 a2);
	typedef void (*_TESPackage_CopyFlagsFromOtherPackage)(TESPackage* _this, TESPackage* other);
	
	// Package Evaluation & AI Control
	typedef bool (*_Actor_EvaluatePackage)(Actor* _this, bool a2, bool resetAI);
	typedef void (*_Actor_GetBumped)(Actor* _this, Actor* bumper, bool isLargeBump, bool exitFurniture);
	typedef bool (*_Actor_HasLargeMovementDelta)(Actor* _this);
	typedef void (*_Actor_sub_140600400)(Actor* _this, float a2);
	
	// Bump System
	typedef void (*_ActorProcess_SetBumpState)(ActorProcessManager* _this, UInt32 bumpState);
	typedef void (*_ActorProcess_SetBumpDirection)(ActorProcessManager* _this, float direction);
	typedef void (*_ActorProcess_ResetBumpWaitTimer)(ActorProcessManager* _this);
	typedef void (*_sub_140654E10)(ActorProcessManager* _this, bool a2);
	typedef void (*_ActorProcess_PlayIdle)(ActorProcessManager* _this, Actor* actor, int a3, int a4, int a5, int a6, TESIdleForm* idle);
	typedef void (*_ActorProcess_SetPlayerActionReaction)(ActorProcessManager* _this, int a2);
	
	// Keep Offset System (NPC Follow Target)
	typedef void (*_Actor_KeepOffsetFromActor)(Actor* _this, UInt32& targetHandle, NiPoint3& offset, NiPoint3& offsetAngleEulerRadians, float catchUpRadius, float followRadius);
	typedef void (*_Actor_ClearKeepOffsetFromActor)(Actor* _this);
	
	// Dialogue Control
	typedef void (*_ActorProcess_TriggerDialogue)(ActorProcessManager* _this, Actor* actor, int dialogueType, int dialogueSubtype, Actor* target, UInt64 a6, bool a7, bool a8, bool a9, bool a10);
	typedef bool (*_Actor_IsGhost)(Actor* _this);
	
	// Virtual function typedefs (accessed via vtable)
	typedef void (*_Actor_PauseCurrentDialogue)(Actor* _this);  // vtable index 0x4F
	typedef void (*_Actor_PutCreatedPackage)(Actor* _this, TESPackage* package, bool dontExitFurniture, bool a4);  // vtable index 0xE1
	
	// ============================================
	// ADDRESS DECLARATIONS (Skyrim VR)
	// These are VR addresses - adjust base for SE/AE
	// ============================================
	
	extern RelocAddr<_CreatePackageByType>     CreatePackageByType;
	extern RelocAddr<_PackageLocation_CTOR> PackageLocation_CTOR;
	extern RelocAddr<_PackageLocation_SetNearReference> PackageLocation_SetNearReference;
	extern RelocAddr<_TESPackage_SetPackageLocation>    TESPackage_SetPackageLocation;
	extern RelocAddr<_PackageTarget_CTOR>               PackageTarget_CTOR;
	extern RelocAddr<_TESPackage_SetPackageTarget>      TESPackage_SetPackageTarget;
	extern RelocAddr<_PackageTarget_ResetValueByTargetType> PackageTarget_ResetValueByTargetType;
	extern RelocAddr<_PackageTarget_SetFromReference>   PackageTarget_SetFromReference;
	extern RelocAddr<_TESPackage_sub_140439BE0>  TESPackage_sub_140439BE0;
	extern RelocAddr<_TESPackage_CopyFlagsFromOtherPackage> TESPackage_CopyFlagsFromOtherPackage;
	
	extern RelocAddr<_Actor_EvaluatePackage>            Actor_EvaluatePackage;
	extern RelocAddr<_Actor_GetBumped>        Actor_GetBumped;
	extern RelocAddr<_Actor_HasLargeMovementDelta>      Actor_HasLargeMovementDelta;
	extern RelocAddr<_Actor_sub_140600400>              Actor_sub_140600400;
	
	extern RelocAddr<_ActorProcess_SetBumpState>        ActorProcess_SetBumpState;
	extern RelocAddr<_ActorProcess_SetBumpDirection>    ActorProcess_SetBumpDirection;
	extern RelocAddr<_ActorProcess_ResetBumpWaitTimer>  ActorProcess_ResetBumpWaitTimer;
	extern RelocAddr<_sub_140654E10>  sub_140654E10;
	extern RelocAddr<_ActorProcess_PlayIdle>  ActorProcess_PlayIdle;
	extern RelocAddr<_ActorProcess_SetPlayerActionReaction> ActorProcess_SetPlayerActionReaction;
	
	extern RelocAddr<_Actor_KeepOffsetFromActor>Actor_KeepOffsetFromActor;
	extern RelocAddr<_Actor_ClearKeepOffsetFromActor>   Actor_ClearKeepOffsetFromActor;
	
	extern RelocAddr<_ActorProcess_TriggerDialogue>     ActorProcess_TriggerDialogue;
	extern RelocAddr<_Actor_IsGhost>Actor_IsGhost;
	
	// ============================================
	// FORCE HORSE INTO COMBAT
	// Override blockers preventing mounted horses from combat
	// ============================================
	
	// Force horse to follow and attack a specific target
	// ALWAYS use this with explicit target - never default to player!
	bool ForceHorseCombatWithTarget(Actor* horse, Actor* target);
	
	// Force companion horse to follow target at CompanionMeleeRange (tighter engagement)
	// Used for player follower companions who need to get closer for melee
	bool ForceCompanionHorseCombatWithTarget(Actor* horse, Actor* target);

	// ============================================
	// HELPER TEMPLATE FOR VTABLE ACCESS
	// ============================================
	
	template <typename T>
	T get_vfunc(void* obj, UInt32 idx)
	{
		auto vtbl = *reinterpret_cast<void***>(obj);
		return reinterpret_cast<T>(vtbl[idx]);
	}
	
	// ============================================
	// PACKAGE TYPES
	// ============================================
	
	enum PackageType
	{
		kPackageType_Find = 0,
		kPackageType_Follow = 1,
		kPackageType_Escort = 2,
		kPackageType_Eat = 3,
		kPackageType_Sleep = 4,
		kPackageType_Wander = 5,
		kPackageType_Travel = 6,
		kPackageType_Accompany = 7,
		kPackageType_UseItemAt = 8,
		kPackageType_Ambush = 9,
		kPackageType_FleeNotCombat = 10,
		kPackageType_CastMagic = 11,
		kPackageType_Sandbox = 12,
		kPackageType_Patrol = 13,
		kPackageType_Guard = 14,
		kPackageType_Dialogue = 15,
		kPackageType_UseWeapon = 16,
		kPackageType_Combat = 18,
		kPackageType_Alarm = 21,
		kPackageType_Flee = 22,
		kPackageType_Spectator = 24,
		kPackageType_InGameDialogue = 28,
		kPackageType_BumpReaction = 32,  // Run-once bump/reaction package
	};
	
	// ============================================
	// HIGH-LEVEL FUNCTIONS
	// ============================================
	
	// Initialize the dynamic package system (call once at plugin load)
	bool InitDynamicPackageSystem();
	
	// Create and inject a follow package to make NPC follow the target
	bool InjectFollowPackage(Actor* actor, Actor* target, int* outAttackState = nullptr);
	
	// Create and inject a bump/reaction package
	bool InjectBumpPackage(Actor* actor, Actor* bumper, bool isLargeBump = false, bool pauseDialogue = true);
	
	// Clear any injected packages from NPC
	bool ClearInjectedPackages(Actor* actor);
	
	// Make NPC keep offset from target (movement-level, not package-level)
	// ALWAYS use this with explicit target - never default to player!
	bool SetNPCKeepOffsetFromTarget(Actor* actor, Actor* target, float catchUpRadius = 300.0f, float followRadius = 150.0f);
	
	// Make NPC keep ranged offset from target (500 units distance for ranged combat)
	// - Faces target when stationary or traveling toward them
	// - Faces travel direction when moving away (no backwards walking)
	// ALWAYS use this with explicit target - never default to player!
	bool SetNPCRangedFollowFromTarget(Actor* actor, Actor* target);
	
	// Clear the keep-offset and force package re-evaluation
	bool ClearNPCKeepOffset(Actor* actor);
	
	// ============================================
	// TRAVEL PACKAGE FOR HORSE
	// Returns: 0 = traveling, 1 = in melee range, 2 = in attack position
	// ============================================
	
	int InjectTravelPackageToHorse(Actor* horse, Actor* target);
	
	// ============================================
	// RESET FUNCTIONS (for game load/reload)
	// ============================================
	
	// Reset all DynamicPackage state (weapon switch tracking, horse movement, etc.)
	void ResetDynamicPackageState();
	
	// Clear weapon switch data for a specific actor
	void ClearWeaponSwitchData(UInt32 actorFormID);
}
