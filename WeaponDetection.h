#pragma once

#include "skse64/GameReferences.h"
#include "skse64/GameObjects.h"
#include "skse64/GameExtraData.h"
#include "skse64/NiNodes.h"

namespace MountedNPCCombatVR
{
	// ============================================
	// CENTRALIZED WEAPON STATE MACHINE
	// ============================================
	// ALL weapon equip/sheathe/draw operations MUST go through this system.
	// This prevents invisible weapons caused by:
	// - Race conditions between multiple equip calls
	// - Not waiting for sheathe animation before equipping new weapon
	// - Not waiting for equip to complete before drawing
	// ============================================
	
	enum class WeaponState
	{
		Idle,     // No operation in progress
		Sheathing,         // Waiting for sheathe animation
		Equipping,         // Equipping new weapon
		Drawing, // Waiting for draw animation
		Ready     // Weapon is equipped and drawn
	};
	
	enum class WeaponRequest
	{
		None,
		Melee,
		Bow,
		Glaive,  // Preferred weapon for mounted vs mounted combat
		Staff    // Warstaff for MageCaster class riders ONLY
	};
	
	// ============================================
	// Weapon State Machine API
	// ============================================
	
	// Initialize/Reset the weapon state system
	void InitWeaponStateSystem();
	void ResetWeaponStateSystem();
	
	// Update - MUST be called every frame from main update loop
	void UpdateWeaponStates();
	
	// Request a weapon switch (the ONLY way to change weapons)
	bool RequestWeaponSwitch(Actor* actor, WeaponRequest request);
	
	// Request weapon based on distance to target
	bool RequestWeaponForDistance(Actor* actor, float distanceToTarget, bool targetIsMounted = false);
	
	// Force weapon switch - bypasses cooldown (for emergency situations like bow in melee)
	bool ForceWeaponSwitch(Actor* actor, WeaponRequest request);
	
	// Force draw current weapon (if sheathed)
	bool RequestWeaponDraw(Actor* actor);
	
	// Force sheathe current weapon
	bool RequestWeaponSheathe(Actor* actor);
	
	// State queries
	WeaponState GetWeaponState(UInt32 actorFormID);
	bool IsWeaponReady(Actor* actor);
	bool IsWeaponTransitioning(Actor* actor);
	bool CanSwitchWeapon(Actor* actor);
	
	// Cleanup
	void ClearWeaponStateData(UInt32 actorFormID);
	
	// ============================================
	// Weapon Types
	// ============================================
	
	enum class WeaponType
	{
		None,
		OneHandSword,
		OneHandAxe,
		OneHandMace,
		OneHandDagger,
		TwoHandSword,
		TwoHandAxe,
		Bow,
		Crossbow,
		Staff,
		Shield,
		Unknown
	};
	
	// ============================================
	// Weapon Info Structure
	// ============================================
	
	struct MountedWeaponInfo
	{
		bool hasWeaponEquipped = false;
		bool hasWeaponSheathed = false;
		bool isBow = false;
		bool isShieldEquipped = false;
		bool hasBowInInventory = false;
		bool hasMeleeInInventory = false;
		WeaponType mainHandType = WeaponType::None;
		WeaponType offHandType = WeaponType::None;
		float weaponReach = 0.0f;
	};
	
	// ============================================
	// Weapon Detection Functions
	// ============================================
	
	MountedWeaponInfo GetWeaponInfo(Actor* actor);
	bool IsWeaponDrawn(Actor* actor);
	bool HasWeaponAvailable(Actor* actor);
	WeaponType GetEquippedWeaponType(Actor* actor, bool leftHand);
	float GetWeaponReach(Actor* actor);
	const char* GetWeaponTypeName(WeaponType type);
	
	// ============================================
	// Inventory Add Functions
	// ============================================
	
	// Add Iron Arrows to an actor's inventory
	// Returns true if successful
	bool AddArrowsToInventory(Actor* actor, UInt32 count = 5);
	
	// Add any ammo type to an actor's inventory by FormID
	// Returns true if successful
	bool AddAmmoToInventory(Actor* actor, UInt32 ammoFormID, UInt32 count);
	
	// Count total arrows (any ammo type) in actor's inventory
	// Returns total count of all ammo items
	UInt32 CountArrowsInInventory(Actor* actor);
	
	// Equip arrows on an actor (for bow combat)
	// Only adds 5 arrows if actor has less than 5 arrows of any type
	// Returns true if successful
	bool EquipArrows(Actor* actor);
	
	// ============================================
	// Weapon Equip/Switch Functions
	// NOTE: These are now INTERNAL - use RequestWeaponSwitch() instead!
	// ============================================
	
	bool HasBowInInventory(Actor* actor);
	bool HasMeleeWeaponInInventory(Actor* actor);
	bool IsBowEquipped(Actor* actor);
	bool IsMeleeEquipped(Actor* actor);
	bool IsTwoHandedWeaponEquipped(Actor* actor);
	
	// Find best weapon in inventory (used internally)
	TESObjectWEAP* FindBestBowInInventory(Actor* actor);
	TESObjectWEAP* FindBestMeleeInInventory(Actor* actor);
	
	// Direct equip functions - INTERNAL USE ONLY
	// External code should use RequestWeaponSwitch()
	bool EquipBestBow(Actor* actor);
	bool EquipBestMeleeWeapon(Actor* actor);
	bool GiveDefaultMountedWeapon(Actor* actor);
	bool GiveDefaultBow(Actor* actor);
	bool RemoveDefaultBow(Actor* actor);
	
	// Staff functions - FOR MAGE CLASS ONLY
	bool HasStaffInInventory(Actor* actor);
	bool IsStaffEquipped(Actor* actor);
	bool GiveWarstaff(Actor* actor);
	TESObjectWEAP* FindStaffInInventory(Actor* actor);
	
	// ============================================
	// Weapon Node / Hitbox Detection
	// ============================================
	
	// Get the weapon bone node from actor's skeleton
	NiAVObject* GetWeaponBoneNode(Actor* actor);
	
	// Get the world position of the weapon during animation
	// Returns true if weapon node was found, false if using fallback position
	bool GetWeaponWorldPosition(Actor* actor, NiPoint3* outPosition);
	
	// Get distance from a point to the player (utility function)
	float GetDistanceToPlayer(NiPoint3* position);
	
	// ============================================
	// MELEE HIT DETECTION
	// ============================================
	
	// Check if a mounted attack should hit the target
	// Uses simple distance-based check for reliable detection
	// Returns true if target is within hit range
	bool CheckMountedAttackHit(Actor* rider, Actor* target, float* outDistance = nullptr);
	
	// Check if target would block the hit (blocking state)
	bool WouldTargetBlockHit(Actor* rider, Actor* target);
	
	// ============================================
	// Weapon Logging
	// ============================================
	
	void LogEquippedWeapons(Actor* actor, UInt32 formID);
	void LogInventoryWeapons(Actor* actor, UInt32 formID);
	
	// ============================================
	// Spell Detection
	// ============================================
	
	bool HasSpellsAvailable(Actor* actor);
	void LogEquippedSpells(Actor* actor, UInt32 formID);
	void LogAvailableSpells(Actor* actor, UInt32 formID);
	
	// Sheathe weapons - DEPRECATED, use RequestWeaponSheathe()
	bool SheatheCurrentWeapon(Actor* actor);
}
