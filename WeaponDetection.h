#pragma once

#include "skse64/GameReferences.h"
#include "skse64/GameObjects.h"
#include "skse64/GameExtraData.h"
#include "skse64/NiNodes.h"

namespace MountedNPCCombatVR
{
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
	// ============================================
	
	bool HasBowInInventory(Actor* actor);
	bool HasMeleeWeaponInInventory(Actor* actor);
	bool EquipBestBow(Actor* actor);
	bool EquipBestMeleeWeapon(Actor* actor);
	bool IsBowEquipped(Actor* actor);
	bool IsMeleeEquipped(Actor* actor);
	
	// Give an Iron Mace to NPCs without suitable melee weapons for mounted combat
	// Only adds if they don't already have a 1H melee weapon in inventory
	// Returns true if weapon was added and equipped
	bool GiveDefaultMountedWeapon(Actor* actor);
	
	// Give a Hunting Bow to NPCs for ranged mounted combat
	// Only adds if they don't already have a bow in inventory
	// Returns true if weapon was added
	bool GiveDefaultBow(Actor* actor);
	
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
	// Returns true if weapon is close enough to deal damage
	// outDistance: optional output for the actual distance
	bool CheckMountedAttackHit(Actor* rider, Actor* target, float* outDistance = nullptr);
	
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
}
