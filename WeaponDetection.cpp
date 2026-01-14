#include "WeaponDetection.h"
#include "DynamicPackages.h"  // For get_vfunc
#include "Helper.h" // For GetGameTime, GetFullFormIdMine
#include "config.h"    // For WeaponSwitchDistance, SheatheTransitionTime
#include "skse64/GameRTTI.h"
#include "skse64/GameData.h"
#include <ctime>
#include <cmath>
#include <algorithm>

namespace MountedNPCCombatVR
{
	// ============================================
	// WEAPON FORMIDS
	// ============================================
	
	// Iron Arrow FormID from Skyrim.esm
	const UInt32 IRON_ARROW_FORMID = 0x0001397D;
	
	// Glaive BASE FormID from MountedNPCCombat.esp (ESL-flagged) - FALLBACK
	// Must use GetFullFormIdMine() to resolve at runtime!
	const UInt32 MOUNTED_GLAIVE_BASE_FORMID = 0x0008F8;
	const char* WEAPON_ESP_NAME = "MountedNPCCombat.esp";
	
	// ============================================
	// GLAIVE DANGER ESP - OPTIONAL VARIETY
	// GlaiveDanger.esp provides multiple glaive varieties
	// This is OPTIONAL - if not installed, falls back to MountedNPCCombat.esp
	// ============================================
	const char* GLAIVE_DANGER_ESP_NAME = "GlaiveDanger.esp";
	
	// Two-Handed Glaives from GlaiveDanger.esp (excluding Daedric)
	// These are BASE FormIDs - need GetFullFormIdMine() to resolve
	// 2H Glaives are preferred for mounted combat due to longer reach
	const UInt32 GLAIVE_DANGER_2H_FORMIDS[] = {
		0x000839,  // IronGlaive2H - Iron Glaive 2H
		0x00083A,  // SteelGlaive2H - Steel Glaive 2H
		0x00083B,  // DwarvenGlaive2H - Dwarven Glaive 2H
		0x00083C,  // ElvenGlaive2H - Elven Glaive 2H
		0x00083D,  // GlassGlaive2H - Glass Glaive 2H
		0x00083E,  // OrcishGlaive2H - Orcish Glaive 2H
		0x000840,  // EbonyGlaive2H - Ebony Glaive 2H
		0x000841,  // DragonboneGlaive2H - Dragonbone Glaive 2H
		0x000842,  // StalhrimGlaive2H - Stalhrim Glaive 2H
		0x000843,  // DraugrGlaive2H - Draugr Glaive 2H
	};
	const int GLAIVE_DANGER_2H_COUNT = sizeof(GLAIVE_DANGER_2H_FORMIDS) / sizeof(GLAIVE_DANGER_2H_FORMIDS[0]);
	
	// Flag to track if GlaiveDanger.esp is available
	static bool g_glaiveDangerChecked = false;
	static bool g_glaiveDangerAvailable = false;
	
	// Hunting Bow FormID from Skyrim.esm
	const UInt32 HUNTING_BOW_FORMID = 0x00013985;
	
	// ============================================
	// CHECK IF GLAIVE DANGER ESP IS AVAILABLE
	// ============================================
	static bool IsGlaiveDangerAvailable()
	{
		if (g_glaiveDangerChecked)
		{
			return g_glaiveDangerAvailable;
		}
		
		g_glaiveDangerChecked = true;
		
		// Try to resolve any glaive from the ESP to check if it's loaded
		UInt32 testFormID = GetFullFormIdMine(GLAIVE_DANGER_ESP_NAME, GLAIVE_DANGER_2H_FORMIDS[0]);
		if (testFormID != 0)
		{
			TESForm* testForm = LookupFormByID(testFormID);
			if (testForm)
			{
				g_glaiveDangerAvailable = true;
				_MESSAGE("WeaponDetection: GlaiveDanger.esp DETECTED - using random 2H glaive variety!");
				return true;
			}
		}
		
		g_glaiveDangerAvailable = false;
		_MESSAGE("WeaponDetection: GlaiveDanger.esp not found - using fallback glaive from MountedNPCCombat.esp");
		return false;
	}
	
	// ============================================
	// GET RANDOM GLAIVE FROM GLAIVE DANGER ESP
	// Returns nullptr if ESP not available or glaive not found
	// Always uses 2H glaives for mounted combat (better reach)
	// ============================================
	static TESObjectWEAP* GetRandomGlaiveFromGlaiveDanger()
	{
		if (!IsGlaiveDangerAvailable())
		{
			return nullptr;
		}
		
		// Random selection from 2H glaives
		int randomIndex = rand() % GLAIVE_DANGER_2H_COUNT;
		UInt32 baseFormID = GLAIVE_DANGER_2H_FORMIDS[randomIndex];
		
		UInt32 fullFormID = GetFullFormIdMine(GLAIVE_DANGER_ESP_NAME, baseFormID);
		if (fullFormID == 0)
		{
			_MESSAGE("WeaponDetection: Failed to resolve GlaiveDanger FormID %04X", baseFormID);
			return nullptr;
		}
		
		TESForm* form = LookupFormByID(fullFormID);
		if (!form)
		{
			_MESSAGE("WeaponDetection: Could not find GlaiveDanger form %08X", fullFormID);
			return nullptr;
		}
		
		TESObjectWEAP* weapon = DYNAMIC_CAST(form, TESForm, TESObjectWEAP);
		if (!weapon)
		{
			_MESSAGE("WeaponDetection: GlaiveDanger form %08X is not a weapon!", fullFormID);
			return nullptr;
		}
		
		const char* weaponName = weapon->fullName.name.data;
		_MESSAGE("WeaponDetection: Selected random 2H glaive: '%s' (FormID: %08X)", 
			weaponName ? weaponName : "Unknown", fullFormID);
		
		return weapon;
	}
	
	// ============================================
	// CHECK IF ACTOR HAS ANY GLAIVE
	// Checks both equipped and inventory for any glaive weapon
	// This prevents NPCs from accumulating multiple glaives
	// ============================================
	static bool HasAnyGlaiveEquippedOrInInventory(Actor* actor)
	{
		if (!actor) return false;
		
		// Check equipped weapon first
		TESForm* equippedWeapon = actor->GetEquippedObject(false);
		if (equippedWeapon)
		{
			TESObjectWEAP* weapon = DYNAMIC_CAST(equippedWeapon, TESForm, TESObjectWEAP);
			if (weapon)
			{
				const char* weaponName = weapon->fullName.name.data;
				if (weaponName)
				{
					// Check if name contains "Glaive" (case-sensitive but covers all glaive types)
					if (strstr(weaponName, "Glaive") != nullptr || strstr(weaponName, "glaive") != nullptr)
					{
						return true;
					}
				}
			}
		}
		
		// Check inventory
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
			if (weapon && entry->countDelta > 0)
			{
				const char* weaponName = weapon->fullName.name.data;
				if (weaponName)
				{
					if (strstr(weaponName, "Glaive") != nullptr || strstr(weaponName, "glaive") != nullptr)
					{
						return true;
					}
				}
			}
		}
		
		return false;
	}
	
	// ============================================
	// GET EXISTING GLAIVE FROM INVENTORY
	// Returns any glaive already in inventory (to equip instead of adding new)
	// ============================================
	static TESObjectWEAP* GetExistingGlaiveFromInventory(Actor* actor)
	{
		if (!actor) return nullptr;
		
		// Check equipped weapon first
		TESForm* equippedWeapon = actor->GetEquippedObject(false);
		if (equippedWeapon)
		{
			TESObjectWEAP* weapon = DYNAMIC_CAST(equippedWeapon, TESForm, TESObjectWEAP);
			if (weapon)
			{
				const char* weaponName = weapon->fullName.name.data;
				if (weaponName)
				{
					if (strstr(weaponName, "Glaive") != nullptr || strstr(weaponName, "glaive") != nullptr)
					{
						return weapon;
					}
				}
			}
		}
		
		// Check inventory
		ExtraContainerChanges* containerChanges = static_cast<ExtraContainerChanges*>(
			actor->extraData.GetByType(kExtraData_ContainerChanges));
		
		if (!containerChanges || !containerChanges->data || !containerChanges->data->objList)
			return nullptr;
		
		tList<InventoryEntryData>* objList = containerChanges->data->objList;
		
		for (tList<InventoryEntryData>::Iterator it = objList->Begin(); !it.End(); ++it)
		{
			InventoryEntryData* entry = it.Get();
			if (!entry || !entry->type) continue;
			
			TESObjectWEAP* weapon = DYNAMIC_CAST(entry->type, TESForm, TESObjectWEAP);
			if (weapon && entry->countDelta > 0)
			{
				const char* weaponName = weapon->fullName.name.data;
				if (weaponName)
				{
					if (strstr(weaponName, "Glaive") != nullptr || strstr(weaponName, "glaive") != nullptr)
					{
						return weapon;
					}
				}
			}
		}
		
		return nullptr;
	}
	
	// ============================================
	// CENTRALIZED WEAPON STATE MACHINE
	// ============================================
	
	// Animation timing - SHEATHE uses config value, others are hardcoded
	// SheatheTransitionTime from config.h controls sheathe animation wait
	const float WEAPON_EQUIP_DURATION = 0.4f; // Time to wait after equipping
	const float WEAPON_DRAW_DURATION = 0.6f;   // Time to wait for draw animation
	// WeaponSwitchCooldown from config.h controls minimum time between weapon switches
	
	struct WeaponStateData
	{
		UInt32 actorFormID;
		WeaponState state;
		WeaponRequest pendingRequest;
		float stateStartTime;
		float lastSwitchTime;
		bool isValid;
	};
	
	static WeaponStateData g_weaponStateData[10];
	static int g_weaponStateCount = 0;
	static bool g_weaponStateInitialized = false;
	
	// ============================================
	// Internal Helpers
	// ============================================
	
	static WeaponStateData* GetOrCreateWeaponStateData(UInt32 actorFormID)
	{
		// Find existing
		for (int i = 0; i < g_weaponStateCount; i++)
		{
			if (g_weaponStateData[i].isValid && g_weaponStateData[i].actorFormID == actorFormID)
			{
				return &g_weaponStateData[i];
			}
		}
		
		// Create new
		if (g_weaponStateCount < 10)
		{
			WeaponStateData* data = &g_weaponStateData[g_weaponStateCount];
			data->actorFormID = actorFormID;
			data->state = WeaponState::Idle;
			data->pendingRequest = WeaponRequest::None;
			data->stateStartTime = 0;
			data->lastSwitchTime = -WeaponSwitchCooldown;  // Use config value
			data->isValid = true;
			g_weaponStateCount++;
			return data;
		}
		
		return nullptr;
	}
	
	static Actor* GetActorFromFormID(UInt32 formID)
	{
		TESForm* form = LookupFormByID(formID);
		if (!form || form->formType != kFormType_Character) return nullptr;
		return static_cast<Actor*>(form);
	}
	
	// ============================================
	// State Machine Operations (internal)
	// ============================================
	
	static void DoSheatheWeapon(Actor* actor)
	{
		if (!actor) return;
		if (IsWeaponDrawn(actor))
		{
			actor->DrawSheatheWeapon(false);
		}
	}
	
	static void DoEquipWeapon(Actor* actor, WeaponRequest request)
	{
		if (!actor) return;
		
		const char* actorName = CALL_MEMBER_FN(actor, GetReferenceName)();
		
		// ============================================
		// GLAIVE REQUEST - Specifically for mounted vs mounted combat
		// First check if actor already has a glaive - use that instead of adding new
		// ============================================
		if (request == WeaponRequest::Glaive)
		{
			// Check if actor already has any glaive - use it instead of adding new
			TESObjectWEAP* existingGlaive = GetExistingGlaiveFromInventory(actor);
			if (existingGlaive)
			{
				EquipManager* equipManager = EquipManager::GetSingleton();
				if (equipManager)
				{
					BGSEquipSlot* rightSlot = GetRightHandSlot();
					CALL_MEMBER_FN(equipManager, EquipItem)(actor, existingGlaive, nullptr, 1, rightSlot, false, false, false, nullptr);
					const char* weaponName = existingGlaive->fullName.name.data;
					_MESSAGE("WeaponState: Actor %08X '%s' EQUIPPED existing '%s'", 
						actor->formID, actorName ? actorName : "Unknown", weaponName ? weaponName : "Glaive");
					return;
				}
			}
			
			// No existing glaive - try to get a random one from GlaiveDanger.esp
			TESObjectWEAP* glaive = GetRandomGlaiveFromGlaiveDanger();
			
			if (glaive)
			{
				// Add glaive to inventory and equip
				AddItem_Native(nullptr, 0, actor, glaive, 1, true);
				
				EquipManager* equipManager = EquipManager::GetSingleton();
				if (equipManager)
				{
					BGSEquipSlot* rightSlot = GetRightHandSlot();
					CALL_MEMBER_FN(equipManager, EquipItem)(actor, glaive, nullptr, 1, rightSlot, false, false, false, nullptr);
					const char* weaponName = glaive->fullName.name.data;
					_MESSAGE("WeaponState: Actor %08X '%s' EQUIPPED NEW '%s' for mounted combat", 
						actor->formID, actorName ? actorName : "Unknown", weaponName ? weaponName : "Glaive");
					return;
				}
			}
			
			// Fallback to MountedNPCCombat.esp glaive
			UInt32 glaiveFormID = GetFullFormIdMine(WEAPON_ESP_NAME, MOUNTED_GLAIVE_BASE_FORMID);
			if (glaiveFormID != 0)
			{
				TESForm* glaiveForm = LookupFormByID(glaiveFormID);
				if (glaiveForm)
				{
					TESObjectWEAP* fallbackGlaive = DYNAMIC_CAST(glaiveForm, TESForm, TESObjectWEAP);
					if (fallbackGlaive)
					{
						AddItem_Native(nullptr, 0, actor, glaiveForm, 1, true);
						
						EquipManager* equipManager = EquipManager::GetSingleton();
						if (equipManager)
						{
							BGSEquipSlot* rightSlot = GetRightHandSlot();
							CALL_MEMBER_FN(equipManager, EquipItem)(actor, fallbackGlaive, nullptr, 1, rightSlot, false, false, false, nullptr);
							_MESSAGE("WeaponState: Actor %08X '%s' EQUIPPED fallback glaive (FormID: %08X)", 
								actor->formID, actorName ? actorName : "Unknown", glaiveFormID);
							return;
						}
					}
				}
			}
			
			// Neither ESP has glaive - fall through to regular melee
			_MESSAGE("WeaponState: ERROR - No glaive available, falling back to melee for actor %08X", actor->formID);
			request = WeaponRequest::Melee;
		}
		
		if (request == WeaponRequest::Melee)
		{
			// ============================================
			// MELEE REQUEST - Check for existing glaive first
			// Falls back to GlaiveDanger.esp, then MountedNPCCombat.esp, then best melee
			// ============================================
			
			// Check if actor already has any glaive - use it
			TESObjectWEAP* existingGlaive = GetExistingGlaiveFromInventory(actor);
			if (existingGlaive)
			{
				EquipManager* equipManager = EquipManager::GetSingleton();
				if (equipManager)
				{
					BGSEquipSlot* rightSlot = GetRightHandSlot();
					CALL_MEMBER_FN(equipManager, EquipItem)(actor, existingGlaive, nullptr, 1, rightSlot, false, false, false, nullptr);
					const char* weaponName = existingGlaive->fullName.name.data;
					_MESSAGE("WeaponState: Actor %08X '%s' EQUIPPED existing '%s'", 
						actor->formID, actorName ? actorName : "Unknown", weaponName ? weaponName : "Glaive");
					return;
				}
			}
			
			// Try GlaiveDanger.esp
			TESObjectWEAP* glaive = GetRandomGlaiveFromGlaiveDanger();
			
			if (glaive)
			{
				AddItem_Native(nullptr, 0, actor, glaive, 1, true);
				
				EquipManager* equipManager = EquipManager::GetSingleton();
				if (equipManager)
				{
					BGSEquipSlot* rightSlot = GetRightHandSlot();
					CALL_MEMBER_FN(equipManager, EquipItem)(actor, glaive, nullptr, 1, rightSlot, false, false, false, nullptr);
					const char* weaponName = glaive->fullName.name.data;
					_MESSAGE("WeaponState: Actor %08X '%s' EQUIPPED NEW '%s'", 
						actor->formID, actorName ? actorName : "Unknown", weaponName ? weaponName : "Glaive");
					return;
				}
			}
			
			// Fallback to MountedNPCCombat.esp glaive
			UInt32 glaiveFormID = GetFullFormIdMine(WEAPON_ESP_NAME, MOUNTED_GLAIVE_BASE_FORMID);
			if (glaiveFormID != 0)
			{
				TESForm* glaiveForm = LookupFormByID(glaiveFormID);
				if (glaiveForm)
				{
					TESObjectWEAP* fallbackGlaive = DYNAMIC_CAST(glaiveForm, TESForm, TESObjectWEAP);
					if (fallbackGlaive)
					{
						AddItem_Native(nullptr, 0, actor, glaiveForm, 1, true);
						
						EquipManager* equipManager = EquipManager::GetSingleton();
						if (equipManager)
						{
							BGSEquipSlot* rightSlot = GetRightHandSlot();
							CALL_MEMBER_FN(equipManager, EquipItem)(actor, fallbackGlaive, nullptr, 1, rightSlot, false, false, false, nullptr);
							_MESSAGE("WeaponState: Actor %08X '%s' EQUIPPED fallback glaive (FormID: %08X)", 
								actor->formID, actorName ? actorName : "Unknown", glaiveFormID);
							return;
						}
					}
				}
			}
			
			// Last resort - best melee in inventory
			_MESSAGE("WeaponState: No glaive available - using best melee in inventory for actor %08X", actor->formID);
			TESObjectWEAP* melee = FindBestMeleeInInventory(actor);
			if (melee)
			{
				EquipManager* equipManager = EquipManager::GetSingleton();
				if (equipManager)
				{
					BGSEquipSlot* rightSlot = GetRightHandSlot();
					CALL_MEMBER_FN(equipManager, EquipItem)(actor, melee, nullptr, 1, rightSlot, false, false, false, nullptr);
					_MESSAGE("WeaponState: Actor %08X '%s' EQUIPPED fallback melee", actor->formID, actorName ? actorName : "Unknown");
				}
			}
		}
		else if (request == WeaponRequest::Bow)
		{
			TESObjectWEAP* bow = FindBestBowInInventory(actor);
			if (bow)
			{
				EquipManager* equipManager = EquipManager::GetSingleton();
				if (equipManager)
				{
					BGSEquipSlot* rightSlot = GetRightHandSlot();
					CALL_MEMBER_FN(equipManager, EquipItem)(actor, bow, nullptr, 1, rightSlot, false, false, false, nullptr);
					_MESSAGE("WeaponState: Actor %08X '%s' EQUIPPED bow", actor->formID, actorName ? actorName : "Unknown");
				}
				EquipArrows(actor);
			}
			else
			{
				// No bow - fall back to melee
				_MESSAGE("WeaponState: Actor %08X '%s' has no bow - falling back to melee", actor->formID, actorName ? actorName : "Unknown");
				DoEquipWeapon(actor, WeaponRequest::Melee);
			}
		}
	}
	
	static void DoDrawWeapon(Actor* actor)
	{
		if (!actor) return;
		if (!IsWeaponDrawn(actor))
		{
			actor->DrawSheatheWeapon(true);
		}
	}
	
	static void ProcessWeaponState(WeaponStateData* data)
	{
		if (!data || !data->isValid) return;
		
		Actor* actor = GetActorFromFormID(data->actorFormID);
		if (!actor || actor->IsDead(1))
		{
			data->isValid = false;
			return;
		}
		
		float currentTime = GetGameTime();
		float timeInState = currentTime - data->stateStartTime;
		
		switch (data->state)
		{
			case WeaponState::Idle:
				// Nothing to do
				break;
				
			case WeaponState::Sheathing:
				// Use SheatheTransitionTime from config for sheathe animation wait
				if (timeInState >= SheatheTransitionTime)
				{
					data->state = WeaponState::Equipping;
					data->stateStartTime = currentTime;
					DoEquipWeapon(actor, data->pendingRequest);
				}
				break;
				
			case WeaponState::Equipping:
				if (timeInState >= WEAPON_EQUIP_DURATION)
				{
					data->state = WeaponState::Drawing;
					data->stateStartTime = currentTime;
					DoDrawWeapon(actor);
				}
				break;
				
			case WeaponState::Drawing:
				if (timeInState >= WEAPON_DRAW_DURATION)
				{
					data->state = WeaponState::Ready;
					data->stateStartTime = currentTime;
					data->lastSwitchTime = currentTime;
					data->pendingRequest = WeaponRequest::None;
					_MESSAGE("WeaponState: Actor %08X weapon READY", actor->formID);
				}
				break;
				
			case WeaponState::Ready:
				// Ensure weapon stays drawn
				if (!IsWeaponDrawn(actor))
				{
					DoDrawWeapon(actor);
				}
				break;
		}
	}
	
	// ============================================
	// Public API Implementation
	// ============================================
	
	void InitWeaponStateSystem()
	{
		if (g_weaponStateInitialized) return;
		
		_MESSAGE("WeaponState: Initializing...");
		_MESSAGE("WeaponState: WeaponSwitchDistance=%.1f, WeaponSwitchDistanceMounted=%.1f", 
			WeaponSwitchDistance, WeaponSwitchDistanceMounted);
		_MESSAGE("WeaponState: WeaponSwitchCooldown=%.1f, SheatheTransitionTime=%.1f", 
			WeaponSwitchCooldown, SheatheTransitionTime);
		
		for (int i = 0; i < 10; i++)
		{
			g_weaponStateData[i].isValid = false;
			g_weaponStateData[i].actorFormID = 0;
		}
		g_weaponStateCount = 0;
		g_weaponStateInitialized = true;
		_MESSAGE("WeaponState: Initialized");
	}
	
	void ResetWeaponStateSystem()
	{
		_MESSAGE("WeaponState: Resetting...");
		for (int i = 0; i < 10; i++)
		{
			g_weaponStateData[i].isValid = false;
			g_weaponStateData[i].actorFormID = 0;
		}
		g_weaponStateCount = 0;
		
		// Reset GlaiveDanger availability check so it re-checks on next equip
		g_glaiveDangerChecked = false;
		g_glaiveDangerAvailable = false;
		
		_MESSAGE("WeaponState: Reset complete");
	}
	
	void UpdateWeaponStates()
	{
		if (!g_weaponStateInitialized) return;
		
		for (int i = 0; i < g_weaponStateCount; i++)
		{
			if (g_weaponStateData[i].isValid)
			{
				ProcessWeaponState(&g_weaponStateData[i]);
			}
		}
	}
	
	bool RequestWeaponSwitch(Actor* actor, WeaponRequest request)
	{
		if (!actor || request == WeaponRequest::None) return false;
		
		WeaponStateData* data = GetOrCreateWeaponStateData(actor->formID);
		if (!data) return false;
		
		// Don't interrupt ongoing transitions
		if (data->state != WeaponState::Idle && data->state != WeaponState::Ready)
		{
			return false;
		}
		
		// Check cooldown using config value
		float currentTime = GetGameTime();
		if ((currentTime - data->lastSwitchTime) < WeaponSwitchCooldown)
		{
			return false;
		}
		
		// Check if we already have what's requested
		// For Glaive request, check if glaive is specifically equipped
		if (request == WeaponRequest::Glaive)
		{
			// Check if glaive is equipped by checking the weapon FormID
			TESForm* equippedWeapon = actor->GetEquippedObject(false);
			if (equippedWeapon)
			{
				UInt32 glaiveFormID = GetFullFormIdMine(WEAPON_ESP_NAME, MOUNTED_GLAIVE_BASE_FORMID);
				if (glaiveFormID != 0 && equippedWeapon->formID == glaiveFormID)
				{
					// Glaive already equipped
					if (!IsWeaponDrawn(actor)) DoDrawWeapon(actor);
					data->state = WeaponState::Ready;
					return true;
				}
			}
		}
		else if (request == WeaponRequest::Melee && IsMeleeEquipped(actor))
		{
			if (!IsWeaponDrawn(actor)) DoDrawWeapon(actor);
			data->state = WeaponState::Ready;
			return true;
		}
		else if (request == WeaponRequest::Bow && IsBowEquipped(actor))
		{
			if (!IsWeaponDrawn(actor)) DoDrawWeapon(actor);
			data->state = WeaponState::Ready;
			return true;
		}
		
		// Start weapon switch sequence
		const char* actorName = CALL_MEMBER_FN(actor, GetReferenceName)();
		const char* reqStr = "UNKNOWN";
		if (request == WeaponRequest::Melee) reqStr = "MELEE";
		else if (request == WeaponRequest::Bow) reqStr = "BOW";
		else if (request == WeaponRequest::Glaive) reqStr = "GLAIVE";
		
		_MESSAGE("WeaponState: Actor %08X '%s' requesting %s switch", actor->formID, actorName ? actorName : "Unknown", reqStr);
		
		data->pendingRequest = request;
		data->state = WeaponState::Sheathing;
		data->stateStartTime = currentTime;
		DoSheatheWeapon(actor);
		
		return true;
	}
	
	bool RequestWeaponForDistance(Actor* actor, float distanceToTarget, bool targetIsMounted)
	{
		if (!actor) return false;
		
		// Use WeaponSwitchDistance for on-foot targets, WeaponSwitchDistanceMounted for mounted targets
		// These values come from config.h and are loaded from INI
		float switchDist = targetIsMounted ? WeaponSwitchDistanceMounted : WeaponSwitchDistance;
		bool hasBow = HasBowInInventory(actor);
		
		WeaponRequest request;
		bool forceMelee = false;  // Flag to bypass cooldown for critical melee switch
		
		if (distanceToTarget <= switchDist)
		{
			// Within melee range
			// ============================================
			// MOUNTED VS MOUNTED: PREFER GLAIVE
			// When fighting a mounted target in melee range, use glaive
			// ============================================
			if (targetIsMounted)
			{
				request = WeaponRequest::Glaive;
				_MESSAGE("WeaponState: %08X requesting GLAIVE (mounted vs mounted, dist: %.0f)", 
					actor->formID, distanceToTarget);
			}
			else
			{
				request = WeaponRequest::Melee;
			}
			
			// CRITICAL: If bow is currently equipped and we're in melee range, FORCE the switch
			// This bypasses cooldown because being stuck with a bow in melee is deadly
			if (IsBowEquipped(actor))
			{
				forceMelee = true;
				_MESSAGE("WeaponState: FORCE MELEE - %08X has bow but is at melee range (%.0f <= %.0f)", 
					actor->formID, distanceToTarget, switchDist);
			}
		}
		else if (hasBow)
		{
			// Beyond switch distance and has bow - use bow
			request = WeaponRequest::Bow;
		}
		else
		{
			// Beyond switch distance but no bow - use melee anyway
			// ============================================
			// MOUNTED VS MOUNTED: PREFER GLAIVE EVEN AT RANGE
			// If target is mounted and we're closing in, use glaive
			// ============================================
			if (targetIsMounted)
			{
				request = WeaponRequest::Glaive;
			}
			else
			{
				request = WeaponRequest::Melee;
			}
		}
		
		// If forcing melee, bypass the normal request and directly switch
		if (forceMelee)
		{
			// For mounted targets, force glaive instead of generic melee
			WeaponRequest forceRequest = targetIsMounted ? WeaponRequest::Glaive : WeaponRequest::Melee;
			return ForceWeaponSwitch(actor, forceRequest);
		}
		
		return RequestWeaponSwitch(actor, request);
	}
	
	// ============================================
	// FORCE WEAPON SWITCH - Bypasses cooldown
	// Used when actor MUST switch (e.g., bow in melee range)
	// ============================================
	bool ForceWeaponSwitch(Actor* actor, WeaponRequest request)
	{
		if (!actor || request == WeaponRequest::None) return false;
		
		WeaponStateData* data = GetOrCreateWeaponStateData(actor->formID);
		if (!data) return false;
		
		// Don't interrupt ongoing transitions (animation would look bad)
		if (data->state == WeaponState::Sheathing || data->state == WeaponState::Equipping || data->state == WeaponState::Drawing)
		{
			return false;
		}
		
		// Check if we already have what's requested
		// For Glaive request, check if glaive is specifically equipped
		if (request == WeaponRequest::Glaive)
		{
			TESForm* equippedWeapon = actor->GetEquippedObject(false);
			if (equippedWeapon)
			{
				UInt32 glaiveFormID = GetFullFormIdMine(WEAPON_ESP_NAME, MOUNTED_GLAIVE_BASE_FORMID);
				if (glaiveFormID != 0 && equippedWeapon->formID == glaiveFormID)
				{
					// Glaive already equipped
					if (!IsWeaponDrawn(actor)) DoDrawWeapon(actor);
					data->state = WeaponState::Ready;
					return true;
				}
			}
		}
		else if (request == WeaponRequest::Melee && IsMeleeEquipped(actor))
		{
			if (!IsWeaponDrawn(actor)) DoDrawWeapon(actor);
			data->state = WeaponState::Ready;
			return true;
		}
		else if (request == WeaponRequest::Bow && IsBowEquipped(actor))
		{
			if (!IsWeaponDrawn(actor)) DoDrawWeapon(actor);
			data->state = WeaponState::Ready;
			return true;
		}
		
		// FORCE the switch - NO COOLDOWN CHECK
		const char* actorName = CALL_MEMBER_FN(actor, GetReferenceName)();
		const char* reqStr = "UNKNOWN";
		if (request == WeaponRequest::Melee) reqStr = "MELEE";
		else if (request == WeaponRequest::Bow) reqStr = "BOW";
		else if (request == WeaponRequest::Glaive) reqStr = "GLAIVE";
		
		_MESSAGE("WeaponState: Actor %08X '%s' FORCING %s switch (bypassing cooldown)", 
			actor->formID, actorName ? actorName : "Unknown", reqStr);
		
		float currentTime = GetGameTime();
		data->pendingRequest = request;
		data->state = WeaponState::Sheathing;
		data->stateStartTime = currentTime;
		DoSheatheWeapon(actor);
		
		return true;
	}
	
	bool RequestWeaponDraw(Actor* actor)
	{
		if (!actor) return false;
		
		WeaponStateData* data = GetOrCreateWeaponStateData(actor->formID);
		if (!data) return false;
		
		if (data->state != WeaponState::Idle && data->state != WeaponState::Ready)
		{
			return false;
		}
		
		if (!IsWeaponDrawn(actor))
		{
			DoDrawWeapon(actor);
		}
		data->state = WeaponState::Ready;
		return true;
	}
	
	bool RequestWeaponSheathe(Actor* actor)
	{
		if (!actor) return false;
		
		WeaponStateData* data = GetOrCreateWeaponStateData(actor->formID);
		if (!data) return false;
		
		DoSheatheWeapon(actor);
		data->state = WeaponState::Idle;
		data->pendingRequest = WeaponRequest::None;
		return true;
	}
	
	WeaponState GetWeaponState(UInt32 actorFormID)
	{
		for (int i = 0; i < g_weaponStateCount; i++)
		{
			if (g_weaponStateData[i].isValid && g_weaponStateData[i].actorFormID == actorFormID)
			{
				return g_weaponStateData[i].state;
			}
		}
		return WeaponState::Idle;
	}
	
	bool IsWeaponReady(Actor* actor)
	{
		if (!actor) return false;
		return GetWeaponState(actor->formID) == WeaponState::Ready;
	}
	
	bool IsWeaponTransitioning(Actor* actor)
	{
		if (!actor) return false;
		WeaponState state = GetWeaponState(actor->formID);
		return (state == WeaponState::Sheathing || state == WeaponState::Equipping || state == WeaponState::Drawing);
	}
	
	bool CanSwitchWeapon(Actor* actor)
	{
		if (!actor) return false;
		
		for (int i = 0; i < g_weaponStateCount; i++)
		{
			if (g_weaponStateData[i].isValid && g_weaponStateData[i].actorFormID == actor->formID)
			{
				if (g_weaponStateData[i].state != WeaponState::Idle && g_weaponStateData[i].state != WeaponState::Ready)
					return false;
				
				float currentTime = GetGameTime();
				return (currentTime - g_weaponStateData[i].lastSwitchTime) >= WeaponSwitchCooldown;
			}
		}
		return true;
	}
	
	void ClearWeaponStateData(UInt32 actorFormID)
	{
		for (int i = 0; i < g_weaponStateCount; i++)
		{
			if (g_weaponStateData[i].isValid && g_weaponStateData[i].actorFormID == actorFormID)
			{
				g_weaponStateData[i].isValid = false;
				_MESSAGE("WeaponState: Cleared data for actor %08X", actorFormID);
				return;
			}
		}
	}
	
	// ============================================
	// ORIGINAL WEAPON DETECTION CODE BELOW
	// ============================================
	
	// (FormID constants moved to top of namespace)
	
	// ============================================
	// Inventory Add Functions
	// ============================================
	
	bool AddArrowsToInventory(Actor* actor, UInt32 count)
	{
		if (!actor) return false;
		
		TESForm* arrowForm = LookupFormByID(IRON_ARROW_FORMID);
		if (!arrowForm)
		{
			_MESSAGE("WeaponDetection: Failed to find Iron Arrow (FormID: %08X)", IRON_ARROW_FORMID);
			return false;
		}
		
		AddItem_Native(nullptr, 0, actor, arrowForm, count, true);
		return true;
	}
	
	bool AddAmmoToInventory(Actor* actor, UInt32 ammoFormID, UInt32 count)
	{
		if (!actor) return false;
		
		TESForm* ammoForm = LookupFormByID(ammoFormID);
		if (!ammoForm) return false;
		
		TESAmmo* ammo = DYNAMIC_CAST(ammoForm, TESForm, TESAmmo);
		if (!ammo) return false;
		
		AddItem_Native(nullptr, 0, actor, ammoForm, count, true);
		return true;
	}
	
	TESAmmo* FindAmmoInInventory(Actor* actor)
	{
		if (!actor) return nullptr;
		
		ExtraContainerChanges* containerChanges = static_cast<ExtraContainerChanges*>
			(actor->extraData.GetByType(kExtraData_ContainerChanges));
		
		if (!containerChanges || !containerChanges->data || !containerChanges->data->objList)
			return nullptr;
		
		tList<InventoryEntryData>* objList = containerChanges->data->objList;
		
		for (tList<InventoryEntryData>::Iterator it = objList->Begin(); !it.End(); ++it)
		{
			InventoryEntryData* entry = it.Get();
			if (!entry || !entry->type) continue;
			
			TESAmmo* ammo = DYNAMIC_CAST(entry->type, TESForm, TESAmmo);
			if (ammo && entry->countDelta > 0)
			{
				return ammo;
			}
		}
		
		return nullptr;
	}
	
	UInt32 CountArrowsInInventory(Actor* actor)
	{
		if (!actor) return 0;
		
		ExtraContainerChanges* containerChanges = static_cast<ExtraContainerChanges*>
			(actor->extraData.GetByType(kExtraData_ContainerChanges));
		
		if (!containerChanges || !containerChanges->data || !containerChanges->data->objList)
			return 0;
		
		UInt32 totalArrows = 0;
		tList<InventoryEntryData>* objList = containerChanges->data->objList;
		
		for (tList<InventoryEntryData>::Iterator it = objList->Begin(); !it.End(); ++it)
		{
			InventoryEntryData* entry = it.Get();
			if (!entry || !entry->type) continue;
			
			TESAmmo* ammo = DYNAMIC_CAST(entry->type, TESForm, TESAmmo);
			if (ammo)
			{
				SInt32 count = entry->countDelta;
				if (count > 0)
				{
					totalArrows += count;
				}
			}
		}
		
		return totalArrows;
	}
	
	bool EquipArrows(Actor* actor)
	{
		if (!actor) return false;
		
		UInt32 existingArrows = CountArrowsInInventory(actor);
		
		if (existingArrows < 5)
		{
			UInt32 arrowsToAdd = 5 - existingArrows;
			AddArrowsToInventory(actor, arrowsToAdd);
		}
		
		TESAmmo* ammoToEquip = FindAmmoInInventory(actor);
		
		if (!ammoToEquip)
		{
			TESForm* arrowForm = LookupFormByID(IRON_ARROW_FORMID);
			if (arrowForm)
			{
				ammoToEquip = DYNAMIC_CAST(arrowForm, TESForm, TESAmmo);
			}
		}
		
		if (!ammoToEquip) return false;
		
		EquipManager* equipManager = EquipManager::GetSingleton();
		if (equipManager)
		{
			CALL_MEMBER_FN(equipManager, EquipItem)(actor, ammoToEquip, nullptr, 1, nullptr, true, false, false, nullptr);
			return true;
		}
		
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
		info.hasBowInInventory = HasBowInInventory(actor);
		info.hasMeleeInInventory = HasMeleeWeaponInInventory(actor);
		
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
		
		EquipManager* equipManager = EquipManager::GetSingleton();
		if (equipManager)
		{
			BGSEquipSlot* rightSlot = GetRightHandSlot();
			CALL_MEMBER_FN(equipManager, EquipItem)(actor, bow, nullptr, 1, rightSlot, true, false, false, nullptr);
			
			// ============================================
			// ANIMATION FIX: Send weapon equip animation event
			// Some horse behavior graphs need explicit animation notifications
			// to properly show the weapon model
			// ============================================
			BSFixedString weaponDrawEvent("WeaponDraw");
			get_vfunc<bool (*)(IAnimationGraphManagerHolder*, const BSFixedString&)>(&actor->animGraphHolder, 0x1)(&actor->animGraphHolder, weaponDrawEvent);
			
			return true;
		}
		return false;
	}
	
	bool EquipBestMeleeWeapon(Actor* actor)
	{
		if (!actor) return false;
		
		TESObjectWEAP* melee = FindBestMeleeInInventory(actor);
		if (!melee) return false;
		
		EquipManager* equipManager = EquipManager::GetSingleton();
		if (equipManager)
		{
			BGSEquipSlot* rightSlot = GetRightHandSlot();
			CALL_MEMBER_FN(equipManager, EquipItem)(actor, melee, nullptr, 1, rightSlot, true, false, false, nullptr);
			
			// ============================================
			// ANIMATION FIX: Send weapon equip animation event
			// Some horse behavior graphs need explicit animation notifications
			// to properly show the weapon model
			// ============================================
			BSFixedString weaponDrawEvent("WeaponDraw");
			get_vfunc<bool (*)(IAnimationGraphManagerHolder*, const BSFixedString&)>(&actor->animGraphHolder, 0x1)(&actor->animGraphHolder, weaponDrawEvent);
			
			return true;
		}
		return false;
	}
	
	bool GiveDefaultMountedWeapon(Actor* actor)
	{
		if (!actor) return false;
		
		// ============================================
		// CHECK IF ACTOR ALREADY HAS A GLAIVE
		// If so, just equip it - don't add a new one
		// ============================================
		TESObjectWEAP* existingGlaive = GetExistingGlaiveFromInventory(actor);
		if (existingGlaive)
		{
			EquipManager* equipManager = EquipManager::GetSingleton();
			if (equipManager)
			{
				BGSEquipSlot* rightSlot = GetRightHandSlot();
				CALL_MEMBER_FN(equipManager, EquipItem)(actor, existingGlaive, nullptr, 1, rightSlot, true, false, false, nullptr);
				
				BSFixedString weaponDrawEvent("WeaponDraw");
				get_vfunc<bool (*)(IAnimationGraphManagerHolder*, const BSFixedString&)>(&actor->animGraphHolder, 0x1)(&actor->animGraphHolder, weaponDrawEvent);
				
				const char* weaponName = existingGlaive->fullName.name.data;
				_MESSAGE("WeaponDetection: Equipped existing '%s' on actor %08X", weaponName ? weaponName : "Glaive", actor->formID);
				return true;
			}
		}
		
		// ============================================
		// No existing glaive - try GlaiveDanger.esp first for variety
		// Falls back to MountedNPCCombat.esp if not available
		// ============================================
		
		// Try GlaiveDanger.esp first
		TESObjectWEAP* glaive = GetRandomGlaiveFromGlaiveDanger();
		
		if (glaive)
		{
			AddItem_Native(nullptr, 0, actor, glaive, 1, true);
			
			EquipManager* equipManager = EquipManager::GetSingleton();
			if (equipManager)
			{
				BGSEquipSlot* rightSlot = GetRightHandSlot();
				CALL_MEMBER_FN(equipManager, EquipItem)(actor, glaive, nullptr, 1, rightSlot, true, false, false, nullptr);
				
				BSFixedString weaponDrawEvent("WeaponDraw");
				get_vfunc<bool (*)(IAnimationGraphManagerHolder*, const BSFixedString&)>(&actor->animGraphHolder, 0x1)(&actor->animGraphHolder, weaponDrawEvent);
				
				const char* weaponName = glaive->fullName.name.data;
				_MESSAGE("WeaponDetection: Gave NEW '%s' to actor %08X", weaponName ? weaponName : "Glaive", actor->formID);
				return true;
			}
		}
		
		// Fallback to MountedNPCCombat.esp glaive
		UInt32 glaiveFormID = GetFullFormIdMine(WEAPON_ESP_NAME, MOUNTED_GLAIVE_BASE_FORMID);
		if (glaiveFormID == 0)
		{
			_MESSAGE("WeaponDetection: ERROR - Could not resolve glaive FormID from %s", WEAPON_ESP_NAME);
			return false;
		}
		
		TESForm* glaiveForm = LookupFormByID(glaiveFormID);
		if (!glaiveForm)
		{
			_MESSAGE("WeaponDetection: ERROR - Could not find glaive form %08X", glaiveFormID);
			return false;
		}
		
		TESObjectWEAP* fallbackGlaive = DYNAMIC_CAST(glaiveForm, TESForm, TESObjectWEAP);
		if (!fallbackGlaive)
		{
			_MESSAGE("WeaponDetection: ERROR - Glaive form %08X is not a weapon!", glaiveFormID);
			return false;
		}
		
		// Add to inventory (won't duplicate if already owned)
		AddItem_Native(nullptr, 0, actor, glaiveForm, 1, true);
		
		EquipManager* equipManager = EquipManager::GetSingleton();
		if (equipManager)
		{
			BGSEquipSlot* rightSlot = GetRightHandSlot();
			CALL_MEMBER_FN(equipManager, EquipItem)(actor, fallbackGlaive, nullptr, 1, rightSlot, true, false, false, nullptr);
			
			BSFixedString weaponDrawEvent("WeaponDraw");
			get_vfunc<bool (*)(IAnimationGraphManagerHolder*, const BSFixedString&)>(&actor->animGraphHolder, 0x1)(&actor->animGraphHolder, weaponDrawEvent);
			
			_MESSAGE("WeaponDetection: Gave default glaive to actor %08X (FormID: %08X)", actor->formID, glaiveFormID);
			return true;
		}
		
		return false;
	}
	
	bool SheatheCurrentWeapon(Actor* actor)
	{
		if (!actor) return false;
		
		// Check if weapon is drawn
		if (!IsWeaponDrawn(actor)) return false;
		
		// Sheathe the weapon
		actor->DrawSheatheWeapon(false);  // false = sheathe
		
		_MESSAGE("WeaponDetection: Sheathed weapon for actor %08X", actor->formID);
		return true;
	}
	
	bool GiveDefaultBow(Actor* actor)
	{
		if (!actor) return false;
		
		if (HasBowInInventory(actor)) return false;
		
		TESForm* bowForm = LookupFormByID(HUNTING_BOW_FORMID);
		if (!bowForm) return false;
		
		TESObjectWEAP* bow = DYNAMIC_CAST(bowForm, TESForm, TESObjectWEAP);
		if (!bow) return false;
		
		AddItem_Native(nullptr, 0, actor, bowForm, 1, true);
		return true;
	}
	
	bool RemoveDefaultBow(Actor* actor)
	{
		if (!actor) return false;
		
		TESForm* bowForm = LookupFormByID(HUNTING_BOW_FORMID);
		if (!bowForm) return false;
		
		TESObjectWEAP* bow = DYNAMIC_CAST(bowForm, TESForm, TESObjectWEAP);
		if (!bow) return false;
		
		// Unequip the bow if it's equipped
		if (IsBowEquipped(actor))
		{
			EquipManager* equipManager = EquipManager::GetSingleton();
			if (equipManager)
			{
				CALL_MEMBER_FN(equipManager, UnequipItem)(actor, bow, nullptr, 1, nullptr, false, false, true, false, nullptr);
				_MESSAGE("WeaponDetection: Unequipped Hunting Bow from actor %08X", actor->formID);
			}
		}
		
		// Note: We don't actually remove the bow from inventory since there's no easy
		// RemoveItem native function available. The bow will stay in inventory but
		// won't be equipped. This is acceptable behavior.
		
		return true;
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
				_MESSAGE("MountedCombat: NPC %08X Right Hand: '%s' (%s)", 
					formID, weaponName ? weaponName : "Unknown", GetWeaponTypeName(type));
			}
		}
	}
	
	void LogInventoryWeapons(Actor* actor, UInt32 formID)
	{
		if (!actor) return;
		// Minimal logging
	}
	
	// ============================================
	// Spell Detection
	// ============================================
	
	void LogEquippedSpells(Actor* actor, UInt32 formID) { }
	
	bool HasSpellsAvailable(Actor* actor)
	{
		if (!actor) return false;
		return actor->addedSpells.Length() > 0;
	}
	
	void LogAvailableSpells(Actor* actor, UInt32 formID) { }
	
	// ============================================
	// Weapon Node / Hitbox Detection
	// ============================================
	
	static const char* WEAPON_BONE_NAMES[] = {
		"WEAPON", "Weapon", "NPC R Hand [RHnd]", "NPC R Forearm [RLar]"
	};
	
	NiAVObject* GetWeaponBoneNode(Actor* actor)
	{
		if (!actor) return nullptr;
		
		NiNode* root = actor->GetNiNode();
		if (!root) return nullptr;
		
		for (int i = 0; i < sizeof(WEAPON_BONE_NAMES) / sizeof(WEAPON_BONE_NAMES[0]); i++)
		{
			const char* boneName = WEAPON_BONE_NAMES[i];
			NiAVObject* node = root->GetObjectByName(&boneName);
			if (node) return node;
		}
		
		return nullptr;
	}
	
	bool GetWeaponWorldPosition(Actor* actor, NiPoint3* outPosition)
	{
		if (!actor || !outPosition) return false;
		
		NiAVObject* weaponNode = GetWeaponBoneNode(actor);
		if (!weaponNode) 
		{
			outPosition->x = actor->pos.x;
			outPosition->y = actor->pos.y;
			outPosition->z = actor->pos.z + 100.0f;
			return false;
		}
		
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
	
	// ============================================
	// CHECK IF WEAPON IS TWO-HANDED
	// Returns true if the equipped weapon is a two-handed melee weapon
	// ============================================
	
	bool IsTwoHandedWeaponEquipped(Actor* actor)
	{
		if (!actor) return false;
		WeaponType type = GetEquippedWeaponType(actor, false);
		return (type == WeaponType::TwoHandSword || type == WeaponType::TwoHandAxe);
	}
	
	// ============================================
	// MELEE HIT DETECTION - SIMPLE DISTANCE BASED
	// Replaces complex collision system with reliable distance check
	// ============================================
	
	bool CheckMountedAttackHit(Actor* rider, Actor* target, float* outDistance)
	{
		if (!rider || !target) return false;
		
		// Simple distance-based check for mounted combat
		float dx = rider->pos.x - target->pos.x;
		float dy = rider->pos.y - target->pos.y;
		float dz = (rider->pos.z + 100.0f) - (target->pos.z + 80.0f);  // Approximate weapon height
		float distance = sqrt(dx * dx + dy * dy + dz * dz);
		
		if (outDistance) *outDistance = distance;
		
		float weaponReach = GetWeaponReach(rider);
		const float MOUNTED_REACH_BONUS = 100.0f;  // Mounted riders have extended reach
		const float HIT_THRESHOLD_PLAYER = 180.0f; // Base hit distance vs player
		const float HIT_THRESHOLD_NPC = 280.0f;    // Larger hit distance vs NPCs (both moving)
		
		// Determine if target is the player or an NPC
		bool targetIsPlayer = (g_thePlayer && (*g_thePlayer) && target == (*g_thePlayer));
		float baseThreshold = targetIsPlayer ? HIT_THRESHOLD_PLAYER : HIT_THRESHOLD_NPC;
		
		float effectiveThreshold = baseThreshold + (weaponReach * 0.5f) + MOUNTED_REACH_BONUS;
		
		// ============================================
		// TWO-HANDED WEAPON REACH BONUS
		// 2H weapons (greatswords, battleaxes) have longer reach when mounted
		// Uses TwoHandedReachBonus from config (default 80.0)
		// ============================================
		if (IsTwoHandedWeaponEquipped(rider))
		{
			effectiveThreshold += TwoHandedReachBonus;
		}
		
		return (distance <= effectiveThreshold);
	}
	
	// ============================================
	// Check if target would block the hit
	// Simple check - is target blocking?
	// ============================================
	
	bool WouldTargetBlockHit(Actor* rider, Actor* target)
	{
		if (!rider || !target) return false;
		
		// Check if target is in blocking state via animation graph
		static BSFixedString isBlockingVar("IsBlocking");
		bool isBlocking = false;
		
		typedef bool (*_GetGraphVariableBool)(IAnimationGraphManagerHolder* holder, const BSFixedString& varName, bool& out);
		_GetGraphVariableBool getGraphVarBool = get_vfunc<_GetGraphVariableBool>(&target->animGraphHolder, 0x12);
		
		if (getGraphVarBool)
		{
			getGraphVarBool(&target->animGraphHolder, isBlockingVar, isBlocking);
		}
		
		return isBlocking;
	}
}
