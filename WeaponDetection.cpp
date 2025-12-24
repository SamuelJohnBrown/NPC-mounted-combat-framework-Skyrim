#include "WeaponDetection.h"
#include "skse64/GameRTTI.h"
#include "skse64/GameData.h"

namespace MountedNPCCombatVR
{
	// Iron Arrow FormID from Skyrim.esm
	const UInt32 IRON_ARROW_FORMID = 0x0001397D;
	
	// ============================================
	// Inventory Add Functions
	// ============================================
	
	bool AddArrowsToInventory(Actor* actor, UInt32 count)
	{
		if (!actor) return false;
		
		// Look up Iron Arrow form
		TESForm* arrowForm = LookupFormByID(IRON_ARROW_FORMID);
		if (!arrowForm)
		{
			_MESSAGE("WeaponDetection: Failed to find Iron Arrow (FormID: %08X)", IRON_ARROW_FORMID);
			return false;
		}
		
		// Use AddItem_Native to add arrows to the actor's inventory
		// Parameters: registry, stackId, target, form, count, silent
		AddItem_Native(nullptr, 0, actor, arrowForm, count, true);
		
		_MESSAGE("WeaponDetection: Added %d arrows to Actor %08X", count, actor->formID);
		return true;
	}
	
	bool AddAmmoToInventory(Actor* actor, UInt32 ammoFormID, UInt32 count)
	{
		if (!actor) return false;
		
		TESForm* ammoForm = LookupFormByID(ammoFormID);
		if (!ammoForm)
		{
			_MESSAGE("WeaponDetection: Failed to find ammo (FormID: %08X)", ammoFormID);
			return false;
		}
		
		// Verify it's actually ammo
		TESAmmo* ammo = DYNAMIC_CAST(ammoForm, TESForm, TESAmmo);
		if (!ammo)
		{
			_MESSAGE("WeaponDetection: FormID %08X is not ammo", ammoFormID);
			return false;
		}
		
		AddItem_Native(nullptr, 0, actor, ammoForm, count, true);
		
		_MESSAGE("WeaponDetection: Added %d ammo (%s) to Actor %08X", 
			count, ammo->fullName.name.data ? ammo->fullName.name.data : "Unknown", actor->formID);
		return true;
	}
	
	UInt32 RemoveArrowsFromInventory(Actor* actor, UInt32 count)
	{
		if (!actor) return 0;
		
		// Look up Iron Arrow form
		TESForm* arrowForm = LookupFormByID(IRON_ARROW_FORMID);
		if (!arrowForm)
		{
			_MESSAGE("WeaponDetection: Failed to find Iron Arrow (FormID: %08X) for removal", IRON_ARROW_FORMID);
			return 0;
		}
		
		// Get the actor's inventory
		ExtraContainerChanges* containerChanges = static_cast<ExtraContainerChanges*>(
			actor->extraData.GetByType(kExtraData_ContainerChanges));
		
		if (!containerChanges || !containerChanges->data || !containerChanges->data->objList)
		{
			_MESSAGE("WeaponDetection: Could not access inventory for Actor %08X", actor->formID);
			return 0;
		}
		
		// Find the arrow entry and count how many they have
		UInt32 arrowsInInventory = 0;
		tList<InventoryEntryData>* objList = containerChanges->data->objList;
		
		for (tList<InventoryEntryData>::Iterator it = objList->Begin(); !it.End(); ++it)
		{
			InventoryEntryData* entry = it.Get();
			if (!entry || !entry->type) continue;
			
			if (entry->type->formID == IRON_ARROW_FORMID)
			{
				arrowsInInventory = (entry->countDelta > 0) ? entry->countDelta : 1;
				break;
			}
		}
		
		if (arrowsInInventory == 0)
		{
			_MESSAGE("WeaponDetection: Actor %08X has no Iron Arrows to remove", actor->formID);
			return 0;
		}
		
		// Remove the arrows (up to the count requested or what they have)
		UInt32 toRemove = (count < arrowsInInventory) ? count : arrowsInInventory;
		
		// Use AddItem_Native with negative count to remove
		// Note: Some SKSE implementations require RemoveItem, but AddItem with negative works in most cases
		AddItem_Native(nullptr, 0, actor, arrowForm, -(SInt32)toRemove, true);
		
		_MESSAGE("WeaponDetection: Removed %d arrows from Actor %08X (had %d)", 
			toRemove, actor->formID, arrowsInInventory);
		return toRemove;
	}
	
	bool EquipArrows(Actor* actor)
	{
		if (!actor) return false;
		
		// Look up Iron Arrow form
		TESForm* arrowForm = LookupFormByID(IRON_ARROW_FORMID);
		if (!arrowForm)
		{
			_MESSAGE("WeaponDetection: Failed to find Iron Arrow (FormID: %08X) for equip", IRON_ARROW_FORMID);
			return false;
		}
		
		TESAmmo* ammo = DYNAMIC_CAST(arrowForm, TESForm, TESAmmo);
		if (!ammo)
		{
			_MESSAGE("WeaponDetection: Iron Arrow FormID %08X is not TESAmmo", IRON_ARROW_FORMID);
			return false;
		}
		
		// Equip the arrows using EquipManager
		EquipManager* equipManager = EquipManager::GetSingleton();
		if (equipManager)
		{
			// For ammo, we don't use a specific slot - pass nullptr
			CALL_MEMBER_FN(equipManager, EquipItem)(actor, ammo, nullptr, 1, nullptr, true, false, false, nullptr);
			_MESSAGE("WeaponDetection: Equipped Iron Arrows on Actor %08X", actor->formID);
			return true;
		}
		
		_MESSAGE("WeaponDetection: Failed to get EquipManager for arrow equip");
		return false;
	}
	
	// ============================================
	// Weapon Detection
	// ============================================
	
	MountedWeaponInfo GetWeaponInfo(Actor* actor)
	{
		MountedWeaponInfo info;
		
		if (!actor) return info;
		
		info.hasWeaponEquipped = IsWeaponDrawn(actor);
		info.mainHandType = GetEquippedWeaponType(actor, false);
		info.offHandType = GetEquippedWeaponType(actor, true);
		info.isBow = (info.mainHandType == WeaponType::Bow || info.mainHandType == WeaponType::Crossbow);
		info.isShieldEquipped = (info.offHandType == WeaponType::Shield);
		info.weaponReach = GetWeaponReach(actor);
		info.hasWeaponSheathed = HasWeaponAvailable(actor);
		
		return info;
	}
	
	bool IsWeaponDrawn(Actor* actor)
	{
		if (!actor) return false;
		return actor->actorState.IsWeaponDrawn();
	}
	
	bool HasWeaponAvailable(Actor* actor)
	{
		if (!actor) return false;
		
		TESForm* rightHand = actor->GetEquippedObject(false);
		if (rightHand)
		{
			TESObjectWEAP* weapon = DYNAMIC_CAST(rightHand, TESForm, TESObjectWEAP);
			if (weapon) return true;
		}
		
		TESForm* leftHand = actor->GetEquippedObject(true);
		if (leftHand)
		{
			TESObjectWEAP* weapon = DYNAMIC_CAST(leftHand, TESForm, TESObjectWEAP);
			if (weapon) return true;
		}
		
		return false;
	}
	
	const char* GetWeaponTypeName(WeaponType type)
	{
		switch (type)
		{
			case WeaponType::None: return "None";
			case WeaponType::OneHandSword: return "One-Hand Sword";
			case WeaponType::OneHandAxe: return "One-Hand Axe";
			case WeaponType::OneHandMace: return "One-Hand Mace";
			case WeaponType::OneHandDagger: return "Dagger";
			case WeaponType::TwoHandSword: return "Two-Hand Sword";
			case WeaponType::TwoHandAxe: return "Two-Hand Axe/Hammer";
			case WeaponType::Bow: return "Bow";
			case WeaponType::Crossbow: return "Crossbow";
			case WeaponType::Staff: return "Staff";
			case WeaponType::Shield: return "Shield";
			default: return "Unknown";
		}
	}
	
	WeaponType GetEquippedWeaponType(Actor* actor, bool leftHand)
	{
		if (!actor) return WeaponType::None;
		
		TESForm* equippedItem = actor->GetEquippedObject(leftHand);
		if (!equippedItem) return WeaponType::None;
		
		TESObjectWEAP* weapon = DYNAMIC_CAST(equippedItem, TESForm, TESObjectWEAP);
		if (weapon)
		{
			UInt8 type = weapon->type();
			switch (type)
			{
				case TESObjectWEAP::GameData::kType_OneHandSword: return WeaponType::OneHandSword;
				case TESObjectWEAP::GameData::kType_OneHandDagger: return WeaponType::OneHandDagger;
				case TESObjectWEAP::GameData::kType_OneHandAxe: return WeaponType::OneHandAxe;
				case TESObjectWEAP::GameData::kType_OneHandMace: return WeaponType::OneHandMace;
				case TESObjectWEAP::GameData::kType_TwoHandSword: return WeaponType::TwoHandSword;
				case TESObjectWEAP::GameData::kType_TwoHandAxe: return WeaponType::TwoHandAxe;
				case TESObjectWEAP::GameData::kType_Bow: return WeaponType::Bow;
				case TESObjectWEAP::GameData::kType_Staff: return WeaponType::Staff;
				case TESObjectWEAP::GameData::kType_CrossBow: return WeaponType::Crossbow;
				default: return WeaponType::Unknown;
			}
		}
		
		TESObjectARMO* armor = DYNAMIC_CAST(equippedItem, TESForm, TESObjectARMO);
		if (armor && leftHand)
		{
			return WeaponType::Shield;
		}
		
		return WeaponType::None;
	}
	
	float GetWeaponReach(Actor* actor)
	{
		if (!actor) return 0.0f;
		
		const float DEFAULT_UNARMED_REACH = 64.0f;
		const float DEFAULT_MELEE_REACH = 96.0f;
		const float DEFAULT_BOW_REACH = 4096.0f;
		
		TESForm* rightHand = actor->GetEquippedObject(false);
		if (!rightHand) return DEFAULT_UNARMED_REACH;
		
		TESObjectWEAP* weapon = DYNAMIC_CAST(rightHand, TESForm, TESObjectWEAP);
		if (!weapon) return DEFAULT_UNARMED_REACH;
		
		UInt8 type = weapon->type();
		if (type == TESObjectWEAP::GameData::kType_Bow || 
			type == TESObjectWEAP::GameData::kType_CrossBow ||
			type == TESObjectWEAP::GameData::kType_Staff)
		{
			return DEFAULT_BOW_REACH;
		}
		
		float reach = weapon->reach();
		if (reach > 0.0f)
		{
			return DEFAULT_MELEE_REACH * reach;
		}
		
		return DEFAULT_MELEE_REACH;
	}
	
	// ============================================
	// Weapon Equip/Switch Functions
	// ============================================
	
	bool IsBowEquipped(Actor* actor)
	{
		if (!actor) return false;
		WeaponType type = GetEquippedWeaponType(actor, false);
		return (type == WeaponType::Bow || type == WeaponType::Crossbow);
	}
	
	bool IsMeleeEquipped(Actor* actor)
	{
		if (!actor) return false;
		WeaponType type = GetEquippedWeaponType(actor, false);
		// Exclude daggers - they are not suitable for mounted combat
		return (type == WeaponType::OneHandSword || 
				type == WeaponType::OneHandAxe || 
				type == WeaponType::OneHandMace ||
				type == WeaponType::TwoHandSword ||
				type == WeaponType::TwoHandAxe);
	}
	
	bool HasBowInInventory(Actor* actor)
	{
		if (!actor) return false;
		
		ExtraContainerChanges* containerChanges = static_cast<ExtraContainerChanges*>(
			actor->extraData.GetByType(kExtraData_ContainerChanges));
		
		if (!containerChanges || !containerChanges->data || !containerChanges->data->objList)
			return false;
		
		tList<InventoryEntryData>* objList = containerChanges->data->objList;
		
		for (tList<InventoryEntryData>::Iterator it = objList->Begin(); !it.End(); ++it)
		{
			InventoryEntryData* entry = it.Get();
			if (!entry || !entry->type) continue;
			
			TESObjectWEAP* weapon = DYNAMIC_CAST(entry->type, TESForm, TESObjectWEAP);
			if (weapon)
			{
				UInt8 type = weapon->type();
				if (type == TESObjectWEAP::GameData::kType_Bow || 
					type == TESObjectWEAP::GameData::kType_CrossBow)
				{
					return true;
				}
			}
		}
		return false;
	}
	
	bool HasMeleeWeaponInInventory(Actor* actor)
	{
		if (!actor) return false;
		
		ExtraContainerChanges* containerChanges = static_cast<ExtraContainerChanges*>(
			actor->extraData.GetByType(kExtraData_ContainerChanges));
		
		if (!containerChanges || !containerChanges->data || !containerChanges->data->objList)
			return false;
		
		tList<InventoryEntryData>* objList = containerChanges->data->objList;
		
		for (tList<InventoryEntryData>::Iterator it = objList->Begin(); !it.End(); ++it)
		{
			InventoryEntryData* entry = it.Get();
			if (!entry || !entry->type) continue;
			
			TESObjectWEAP* weapon = DYNAMIC_CAST(entry->type, TESForm, TESObjectWEAP);
			if (weapon)
			{
				UInt8 type = weapon->type();
				// Exclude daggers - they are not suitable for mounted combat
				if (type == TESObjectWEAP::GameData::kType_OneHandSword ||
					type == TESObjectWEAP::GameData::kType_OneHandAxe ||
					type == TESObjectWEAP::GameData::kType_OneHandMace ||
					type == TESObjectWEAP::GameData::kType_TwoHandSword ||
					type == TESObjectWEAP::GameData::kType_TwoHandAxe)
				{
					return true;
				}
			}
		}
		return false;
	}
	
	TESObjectWEAP* FindBestBowInInventory(Actor* actor)
	{
		if (!actor) return nullptr;
		
		ExtraContainerChanges* containerChanges = static_cast<ExtraContainerChanges*>(
			actor->extraData.GetByType(kExtraData_ContainerChanges));
		
		if (!containerChanges || !containerChanges->data || !containerChanges->data->objList)
			return nullptr;
		
		TESObjectWEAP* bestBow = nullptr;
		int bestDamage = 0;
		
		tList<InventoryEntryData>* objList = containerChanges->data->objList;
		
		for (tList<InventoryEntryData>::Iterator it = objList->Begin(); !it.End(); ++it)
		{
			InventoryEntryData* entry = it.Get();
			if (!entry || !entry->type) continue;
			
			TESObjectWEAP* weapon = DYNAMIC_CAST(entry->type, TESForm, TESObjectWEAP);
			if (weapon)
			{
				UInt8 type = weapon->type();
				if (type == TESObjectWEAP::GameData::kType_Bow || 
					type == TESObjectWEAP::GameData::kType_CrossBow)
				{
					int damage = weapon->damage.GetAttackDamage();
					if (damage > bestDamage || bestBow == nullptr)
					{
						bestBow = weapon;
						bestDamage = damage;
					}
				}
			}
		}
		return bestBow;
	}
	
	TESObjectWEAP* FindBestMeleeInInventory(Actor* actor)
	{
		if (!actor) return nullptr;
		
		ExtraContainerChanges* containerChanges = static_cast<ExtraContainerChanges*>(
			actor->extraData.GetByType(kExtraData_ContainerChanges));
		
		if (!containerChanges || !containerChanges->data || !containerChanges->data->objList)
			return nullptr;
		
		TESObjectWEAP* bestMelee = nullptr;
		int bestDamage = 0;
		
		tList<InventoryEntryData>* objList = containerChanges->data->objList;
		
		for (tList<InventoryEntryData>::Iterator it = objList->Begin(); !it.End(); ++it)
		{
			InventoryEntryData* entry = it.Get();
			if (!entry || !entry->type) continue;
			
			TESObjectWEAP* weapon = DYNAMIC_CAST(entry->type, TESForm, TESObjectWEAP);
			if (weapon)
			{
				UInt8 type = weapon->type();
				// Exclude daggers - they are not suitable for mounted combat
				if (type == TESObjectWEAP::GameData::kType_OneHandSword ||
					type == TESObjectWEAP::GameData::kType_OneHandAxe ||
					type == TESObjectWEAP::GameData::kType_OneHandMace ||
					type == TESObjectWEAP::GameData::kType_TwoHandSword ||
					type == TESObjectWEAP::GameData::kType_TwoHandAxe)
				{
					int damage = weapon->damage.GetAttackDamage();
					if (damage > bestDamage || bestMelee == nullptr)
					{
						bestMelee = weapon;
						bestDamage = damage;
					}
				}
			}
		}
		return bestMelee;
	}
	
	bool EquipBestBow(Actor* actor)
	{
		if (!actor) return false;
		
		TESObjectWEAP* bow = FindBestBowInInventory(actor);
		if (!bow) return false;
		
		// Sheathe current weapon first
		if (IsWeaponDrawn(actor))
		{
			actor->DrawSheatheWeapon(false);
		}
		
		// Equip the bow
		EquipManager* equipManager = EquipManager::GetSingleton();
		if (equipManager)
		{
			BGSEquipSlot* rightSlot = GetRightHandSlot();
			CALL_MEMBER_FN(equipManager, EquipItem)(actor, bow, nullptr, 1, rightSlot, true, false, false, nullptr);
			return true;
		}
		return false;
	}
	
	bool EquipBestMeleeWeapon(Actor* actor)
	{
		if (!actor) return false;
		
		TESObjectWEAP* melee = FindBestMeleeInInventory(actor);
		if (!melee) return false;
		
		// Sheathe current weapon first
		if (IsWeaponDrawn(actor))
		{
			actor->DrawSheatheWeapon(false);
		}
		
		// Equip the melee weapon
		EquipManager* equipManager = EquipManager::GetSingleton();
		if (equipManager)
		{
			BGSEquipSlot* rightSlot = GetRightHandSlot();
			CALL_MEMBER_FN(equipManager, EquipItem)(actor, melee, nullptr, 1, rightSlot, true, false, false, nullptr);
			return true;
		}
		return false;
	}

	// ============================================
	// Weapon Logging
	// ============================================
	
	void LogEquippedWeapons(Actor* actor, UInt32 formID)
	{
		if (!actor) return;
		
		TESForm* rightHand = actor->GetEquippedObject(false);
		if (rightHand)
		{
			TESObjectWEAP* weapon = DYNAMIC_CAST(rightHand, TESForm, TESObjectWEAP);
			if (weapon)
			{
				const char* weaponName = weapon->fullName.name.data;
				WeaponType type = GetEquippedWeaponType(actor, false);
				_MESSAGE("MountedCombat: NPC %08X Right Hand: '%s' (%s) | Reach: %.1f", 
					formID, weaponName ? weaponName : "Unknown", GetWeaponTypeName(type), weapon->reach());
			}
		}
		else
		{
			_MESSAGE("MountedCombat: NPC %08X Right Hand: Empty", formID);
		}
		
		TESForm* leftHand = actor->GetEquippedObject(true);
		if (leftHand)
		{
			TESObjectWEAP* weapon = DYNAMIC_CAST(leftHand, TESForm, TESObjectWEAP);
			if (weapon)
			{
				const char* weaponName = weapon->fullName.name.data;
				WeaponType type = GetEquippedWeaponType(actor, true);
				_MESSAGE("MountedCombat: NPC %08X Left Hand: '%s' (%s)", 
					formID, weaponName ? weaponName : "Unknown", GetWeaponTypeName(type));
			}
			else
			{
				TESObjectARMO* armor = DYNAMIC_CAST(leftHand, TESForm, TESObjectARMO);
				if (armor)
				{
					const char* shieldName = armor->fullName.name.data;
					_MESSAGE("MountedCombat: NPC %08X Left Hand: '%s' (Shield)", 
						formID, shieldName ? shieldName : "Unknown Shield");
				}
				else
				{
					_MESSAGE("MountedCombat: NPC %08X Left Hand: Spell/Other (FormID: %08X)", 
						formID, leftHand->formID);
				}
			}
		}
		else
		{
			_MESSAGE("MountedCombat: NPC %08X Left Hand: Empty", formID);
		}
	}
	
	void LogInventoryWeapons(Actor* actor, UInt32 formID)
	{
		if (!actor) return;
		
		ExtraContainerChanges* containerChanges = static_cast<ExtraContainerChanges*>(
			actor->extraData.GetByType(kExtraData_ContainerChanges));
		
		if (!containerChanges || !containerChanges->data || !containerChanges->data->objList)
		{
			_MESSAGE("MountedCombat: NPC %08X Inventory: Could not access", formID);
			return;
		}
		
		_MESSAGE("MountedCombat: NPC %08X Inventory Weapons:", formID);
		
		int weaponCount = 0;
		tList<InventoryEntryData>* objList = containerChanges->data->objList;
		
		for (tList<InventoryEntryData>::Iterator it = objList->Begin(); !it.End(); ++it)
		{
			InventoryEntryData* entry = it.Get();
			if (!entry || !entry->type) continue;
			
			TESObjectWEAP* weapon = DYNAMIC_CAST(entry->type, TESForm, TESObjectWEAP);
			if (weapon)
			{
				const char* weaponName = weapon->fullName.name.data;
				UInt8 type = weapon->type();
				const char* typeName = "Unknown";
				
				switch (type)
				{
					case TESObjectWEAP::GameData::kType_OneHandSword: typeName = "1H Sword"; break;
					case TESObjectWEAP::GameData::kType_OneHandDagger: typeName = "Dagger"; break;
					case TESObjectWEAP::GameData::kType_OneHandAxe: typeName = "1H Axe"; break;
					case TESObjectWEAP::GameData::kType_OneHandMace: typeName = "1H Mace"; break;
					case TESObjectWEAP::GameData::kType_TwoHandSword: typeName = "2H Sword"; break;
					case TESObjectWEAP::GameData::kType_TwoHandAxe: typeName = "2H Axe"; break;
					case TESObjectWEAP::GameData::kType_Bow: typeName = "Bow"; break;
					case TESObjectWEAP::GameData::kType_Staff: typeName = "Staff"; break;
					case TESObjectWEAP::GameData::kType_CrossBow: typeName = "Crossbow"; break;
				}
				
				SInt32 count = entry->countDelta;
				_MESSAGE("  - '%s' (%s) x%d | Damage: %d | Reach: %.2f", 
					weaponName ? weaponName : "Unknown", typeName,
					count > 0 ? count : 1, weapon->damage.GetAttackDamage(), weapon->reach());
				weaponCount++;
			}
		}
		
		if (weaponCount == 0)
		{
			_MESSAGE("  - No weapons found in inventory");
		}
	}
	
	// ============================================
	// Spell Detection
	// ============================================
	
	void LogEquippedSpells(Actor* actor, UInt32 formID)
	{
		if (!actor) return;
		
		_MESSAGE("MountedCombat: NPC %08X Equipped Spells:", formID);
		
		int spellCount = 0;
		
		TESForm* leftHand = actor->GetEquippedObject(true);
		if (leftHand)
		{
			SpellItem* spell = DYNAMIC_CAST(leftHand, TESForm, SpellItem);
			if (spell)
			{
				const char* spellName = spell->fullName.name.data;
				_MESSAGE("  - Left Hand: '%s' (FormID: %08X)", 
					spellName ? spellName : "Unknown Spell", spell->formID);
				spellCount++;
			}
		}
		
		TESForm* rightHand = actor->GetEquippedObject(false);
		if (rightHand)
		{
			SpellItem* spell = DYNAMIC_CAST(rightHand, TESForm, SpellItem);
			if (spell)
			{
				const char* spellName = spell->fullName.name.data;
				_MESSAGE("  - Right Hand: '%s' (FormID: %08X)", 
					spellName ? spellName : "Unknown Spell", spell->formID);
				spellCount++;
			}
		}
		
		if (spellCount == 0)
		{
			_MESSAGE("  - No spells currently equipped");
		}
	}
	
	bool HasSpellsAvailable(Actor* actor)
	{
		if (!actor) return false;
		return actor->addedSpells.Length() > 0;
	}
	
	void LogAvailableSpells(Actor* actor, UInt32 formID)
	{
		if (!actor) return;
		
		_MESSAGE("MountedCombat: NPC %08X Available Spells:", formID);
		
		int spellCount = 0;
		UInt32 numSpells = actor->addedSpells.Length();
		
		for (UInt32 i = 0; i < numSpells; i++)
		{
			SpellItem* spell = actor->addedSpells.Get(i);
			if (spell)
			{
				const char* spellName = spell->fullName.name.data;
				UInt32 spellType = (UInt32)spell->data.spellType;
				const char* typeName = "Unknown";
				
				switch (spellType)
				{
					case 0: typeName = "Spell"; break;
					case 1: typeName = "Disease"; break;
					case 2: typeName = "Power"; break;
					case 3: typeName = "Lesser Power"; break;
					case 4: typeName = "Ability"; break;
					case 5: typeName = "Poison"; break;
					case 6: typeName = "Enchantment"; break;
					case 7: typeName = "Potion"; break;
					case 8: typeName = "Ingredient"; break;
					case 9: typeName = "Leveled Spell"; break;
					case 10: typeName = "Addiction"; break;
					case 11: typeName = "Voice/Shout"; break;
					case 12: typeName = "Staff Enchant"; break;
					case 13: typeName = "Scroll"; break;
				}
				
				_MESSAGE("  - '%s' (%s) | FormID: %08X", 
					spellName ? spellName : "Unknown", typeName, spell->formID);
				spellCount++;
			}
		}
		
		if (spellCount == 0)
		{
			_MESSAGE("  - No spells found");
		}
	}
	
	// ============================================
	// Weapon Node / Hitbox Detection
	// ============================================
	
	// Common skeleton bone names for weapon position
	static const char* WEAPON_BONE_NAMES[] = {
		"WEAPON",
		"Weapon",
		"NPC R Hand [RHnd]",
		"NPC R Forearm [RLar]",
		"WeaponSword",
		"WeaponAxe",
		"WeaponMace",
		"WeaponDagger"
	};
	
	NiAVObject* GetWeaponBoneNode(Actor* actor)
	{
		if (!actor) return nullptr;
		
		NiNode* root = actor->GetNiNode();
		if (!root) return nullptr;
		
		// Try each bone name until we find one
		for (int i = 0; i < sizeof(WEAPON_BONE_NAMES) / sizeof(WEAPON_BONE_NAMES[0]); i++)
		{
			const char* boneName = WEAPON_BONE_NAMES[i];
			NiAVObject* node = root->GetObjectByName(&boneName);
			if (node)
			{
				return node;
			}
		}
		
		return nullptr;
	}
	
	bool GetWeaponWorldPosition(Actor* actor, NiPoint3* outPosition)
	{
		if (!actor || !outPosition) return false;
		
		NiAVObject* weaponNode = GetWeaponBoneNode(actor);
		if (!weaponNode) 
		{
			// Fallback to actor position + offset for right hand
			outPosition->x = actor->pos.x;
			outPosition->y = actor->pos.y;
			outPosition->z = actor->pos.z + 100.0f;  // Approximate hand height
			return false;
		}
		
		// Get world transform from the node
		// The world position is in the node's world transform matrix
		outPosition->x = weaponNode->m_worldTransform.pos.x;
		outPosition->y = weaponNode->m_worldTransform.pos.y;
		outPosition->z = weaponNode->m_worldTransform.pos.z;
		
		return true;
	}
	
	float GetDistanceToPlayer(NiPoint3* position)
	{
		if (!position) return 999999.0f;
		if (!g_thePlayer || !(*g_thePlayer)) return 999999.0f;
		
		Actor* player = *g_thePlayer;
		
		float dx = position->x - player->pos.x;
		float dy = position->y - player->pos.y;
		float dz = position->z - player->pos.z;
		
		return sqrt(dx * dx + dy * dy + dz * dz);
	}
	
	bool IsWeaponInHitRange(Actor* attacker, Actor* target, float hitRadius)
	{
		if (!attacker || !target) return false;
		
		NiPoint3 weaponPos;
		bool gotPosition = GetWeaponWorldPosition(attacker, &weaponPos);
		
		// Calculate distance from weapon to target center
		float dx = weaponPos.x - target->pos.x;
		float dy = weaponPos.y - target->pos.y;
		float dz = weaponPos.z - (target->pos.z + 80.0f);  // Target chest height
		
		float distance = sqrt(dx * dx + dy * dy + dz * dz);
		
		// Get weapon reach to add to hit radius
		float weaponReach = GetWeaponReach(attacker);
		float effectiveHitRadius = hitRadius + (weaponReach * 0.3f);  // Weapon reach adds to radius
		
		return distance <= effectiveHitRadius;
	}
	
	// Check if weapon swing should hit the player during mounted attack animation
	// Returns true if weapon is close enough to deal damage
	bool CheckMountedAttackHit(Actor* rider, Actor* target, float* outDistance)
	{
		if (!rider || !target) return false;
		
		NiPoint3 weaponPos;
		GetWeaponWorldPosition(rider, &weaponPos);
		
		// Calculate distance from weapon to target
		// For player, we check against their position + height offset
		float targetHeight = 80.0f;  // Approximate chest height
		
		float dx = weaponPos.x - target->pos.x;
		float dy = weaponPos.y - target->pos.y;
		float dz = weaponPos.z - (target->pos.z + targetHeight);
		
		float distance = sqrt(dx * dx + dy * dy + dz * dz);
		
		if (outDistance)
		{
			*outDistance = distance;
		}
		
		// Get weapon reach for hit threshold
		float weaponReach = GetWeaponReach(rider);
		
		// Mounted combat has extended reach due to height advantage
		// Base hit threshold is weapon reach + bonus for mounted position
		const float MOUNTED_REACH_BONUS = 50.0f;  // Units
		const float HIT_THRESHOLD = 120.0f;  // Base hit distance
		
		float effectiveThreshold = HIT_THRESHOLD + (weaponReach * 0.5f) + MOUNTED_REACH_BONUS;
		
		return distance <= effectiveThreshold;
	}
}
