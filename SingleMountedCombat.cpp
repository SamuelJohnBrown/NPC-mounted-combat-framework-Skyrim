#include "SingleMountedCombat.h"
#include "CombatStyles.h"
#include "WeaponDetection.h"
#include "DynamicPackages.h"
#include "Helper.h"
#include "skse64/GameRTTI.h"
#include "skse64/GameData.h"
#include "skse64/GameObjects.h"
#include "skse64/PapyrusVM.h"
#include "skse64_common/Relocation.h"
#include <cmath>
#include <cstdlib>
#include <ctime>

namespace MountedNPCCombatVR
{
	// ============================================
	// CONFIGURATION
	// ============================================
	
	// Horse sprint animation FormIDs from Skyrim.esm
	const UInt32 HORSE_SPRINT_START_FORMID = 0x0004408B;
	const UInt32 HORSE_SPRINT_STOP_FORMID = 0x0004408C;
	const UInt32 HORSE_REAR_UP_FORMID = 0x000DCD7C;  // Horse rear up animation from Skyrim.esm
	
	// Horse jump animation from MountedNPCCombat.esp
	const UInt32 HORSE_JUMP_BASE_FORMID = 0x0008E6;
	const char* JUMP_ESP_NAME = "MountedNPCCombat.esp";
	
	// Horse jump cooldown
	const float HORSE_JUMP_COOLDOWN = 4.0f;  // Only attempt jump every 4 seconds
	
	// Bow attack animation FormIDs from MountedNPCCombat.esp
	const UInt32 BOW_ATTACK_CHARGE_BASE_FORMID = 0x0008EA;  // Bow draw/charge
	const UInt32 BOW_ATTACK_RELEASE_BASE_FORMID = 0x0008EB;  // Bow release
	const char* BOW_ESP_NAME = "MountedNPCCombat.esp";
	
	// Iron Arrow FormID from Skyrim.esm (for projectile)
	const UInt32 IRON_ARROW_FORMID = 0x0001397D;
	
	// Bow attack configuration
	const float BOW_EQUIP_DELAY = 1.5f;        // Must have bow equipped for 1.5 seconds before drawing
	const float BOW_DRAW_MIN_TIME = 2.0f;      // Minimum draw hold time
	const float BOW_DRAW_MAX_TIME = 3.5f;      // Maximum draw hold time
	const float ARROW_SPEED = 3600.0f;         // Arrow projectile speed
	
	// ============================================
	// NATIVE PAPYRUS FIRE FUNCTION
	// This is the Weapon.Fire() Papyrus function exposed via native code
	// Same approach as WeaponThrowVR mod
	// ============================================
	
	typedef void (*_Fire)(VMClassRegistry* registry, UInt32 stackId, TESObjectWEAP* akWeapon, TESObjectREFR* akSource, TESAmmo* akAmmo);
	RelocAddr<_Fire> Fire(0x009EA9F0);  // Skyrim VR 1.4.15 - Papyrus Weapon.Fire native
	
	// ============================================
	// PROJECTILE LAUNCH DATA STRUCTURE
	// Based on Skyrim VR 1.4.15 reverse engineering
	// ============================================
	
	struct ProjectileLaunchData
	{
		void* vtbl;     // 00
		NiPoint3 origin;            // 08
		NiPoint3 contactNormal;     // 14
		void* projectileBase;       // 20 - BGSProjectile*
		TESObjectREFR* shooter;     // 28
		void* combatController;     // 30
		TESObjectWEAP* weaponSource;// 38
		TESAmmo* ammoSource;    // 40
		float angleZ;           // 48
		float angleX;    // 4C
		void* unk50;     // 50
		TESObjectREFR* desiredTarget; // 58
		float unk60;           // 60
		float unk64;    // 64
		TESObjectCELL* parentCell;  // 68
		void* spell;                // 70
		UInt32 castingSource;       // 78
		UInt32 pad7C;    // 7C
		void* enchantItem;          // 80
		void* poison;     // 88
		SInt32 area;        // 90
		float power;    // 94
		float scale;         // 98
		bool alwaysHit;             // 9C
		bool noDamageOutsideCombat; // 9D
		bool autoAim;   // 9E
		bool chainShatter;          // 9F
		bool useOrigin;          // A0
		bool deferInitialization;   // A1
		bool forceConeOfFire;  // A2
	};
	
	// Projectile::Launch function address for Skyrim VR 1.4.15
	// RELOCATION_ID(42928, 44108) in CommonLibSSE maps to VR address
	typedef UInt32* (*_Projectile_Launch)(UInt32* result, ProjectileLaunchData* launchData);
	RelocAddr<_Projectile_Launch> Projectile_Launch(0x4327B0);  // Skyrim VR 1.4.15
	
	// ============================================
	// SYSTEM STATE
	// ============================================
	
	static bool g_singleCombatInitialized = false;
	static bool g_randomSeeded = false;
	static float g_combatStartTime = 0.0f;
	
	// Cached sprint idles
	static TESIdleForm* g_horseSprintStart = nullptr;
	static TESIdleForm* g_horseSprintStop = nullptr;
	static TESIdleForm* g_horseRearUp = nullptr;
	static bool g_sprintIdlesInitialized = false;
	
	// Cached horse jump idle
	static TESIdleForm* g_horseJump = nullptr;
	static bool g_jumpIdleInitialized = false;
	
	// Horse jump cooldown tracking
	struct HorseJumpData
	{
		UInt32 horseFormID;
		float lastJumpTime;
		bool isValid;
	};
	
	static HorseJumpData g_horseJumpData[5];
	static int g_horseJumpCount = 0;
	
	// Cached bow attack idles
	static TESIdleForm* g_bowAttackCharge = nullptr;
	static TESIdleForm* g_bowAttackRelease = nullptr;
	static bool g_bowIdlesInitialized = false;
	
	// Bow attack state tracking per rider
	enum class BowAttackState
	{
		None = 0,
		WaitingToEquip,   // Bow equipped, waiting 1.5 seconds
		Drawing,        // Bow being drawn
		Holding,          // Bow drawn, holding for 2-3.5 seconds
		Released     // Arrow released
	};
	
	struct RiderBowAttackData
	{
		UInt32 riderFormID;
		BowAttackState state;
		float bowEquipTime;      // When bow was equipped
		float drawStartTime;     // When draw animation started
		float holdDuration;       // Random hold time (2-3.5 seconds)
		bool isValid;
	};
	
	static RiderBowAttackData g_riderBowData[5];
	static int g_riderBowCount = 0;
	
	// ============================================
	// UTILITY FUNCTIONS
	// ============================================
	
	void EnsureRandomSeeded()
	{
		if (!g_randomSeeded)
		{
			unsigned int seed = (unsigned int)time(nullptr) ^ (unsigned int)clock();
			srand(seed);
			rand(); rand(); rand();
			g_randomSeeded = true;
		}
	}
	
	static float GetGameTimeSeconds()
	{
		static auto startTime = clock();
		return (float)(clock() - startTime) / CLOCKS_PER_SEC;
	}
	
	// ============================================
	// SPRINT ANIMATION FUNCTIONS
	// ============================================
	
	void InitSprintIdles()
	{
		if (g_sprintIdlesInitialized) return;
		
		_MESSAGE("SingleMountedCombat: Loading horse animations...");
		
		TESForm* sprintStartForm = LookupFormByID(HORSE_SPRINT_START_FORMID);
		if (sprintStartForm)
		{
			g_horseSprintStart = DYNAMIC_CAST(sprintStartForm, TESForm, TESIdleForm);
			if (g_horseSprintStart)
			{
				_MESSAGE("SingleMountedCombat: Found HORSE_SPRINT_START (FormID: %08X)", HORSE_SPRINT_START_FORMID);
			}
		}
		
		TESForm* sprintStopForm = LookupFormByID(HORSE_SPRINT_STOP_FORMID);
		if (sprintStopForm)
		{
			g_horseSprintStop = DYNAMIC_CAST(sprintStopForm, TESForm, TESIdleForm);
			if (g_horseSprintStop)
			{
				_MESSAGE("SingleMountedCombat: Found HORSE_SPRINT_STOP (FormID: %08X)", HORSE_SPRINT_STOP_FORMID);
			}
		}
		
		TESForm* rearUpForm = LookupFormByID(HORSE_REAR_UP_FORMID);
		if (rearUpForm)
		{
			g_horseRearUp = DYNAMIC_CAST(rearUpForm, TESForm, TESIdleForm);
			if (g_horseRearUp)
			{
				_MESSAGE("SingleMountedCombat: Found HORSE_REAR_UP (FormID: %08X)", HORSE_REAR_UP_FORMID);
			}
		}
		
		g_sprintIdlesInitialized = true;
	}
	
	void InitBowIdles()
	{
		if (g_bowIdlesInitialized) return;
		
		_MESSAGE("SingleMountedCombat: Loading bow attack animations from %s...", BOW_ESP_NAME);
		
		// Get full FormIDs using ESP lookup
		UInt32 chargeFormID = GetFullFormIdMine(BOW_ESP_NAME, BOW_ATTACK_CHARGE_BASE_FORMID);
		UInt32 releaseFormID = GetFullFormIdMine(BOW_ESP_NAME, BOW_ATTACK_RELEASE_BASE_FORMID);
		
		_MESSAGE("SingleMountedCombat: Looking up BOW_CHARGE - Base: %08X, Full: %08X", 
			BOW_ATTACK_CHARGE_BASE_FORMID, chargeFormID);
		_MESSAGE("SingleMountedCombat: Looking up BOW_RELEASE - Base: %08X, Full: %08X", 
			BOW_ATTACK_RELEASE_BASE_FORMID, releaseFormID);
		
		if (chargeFormID != 0)
		{
			TESForm* chargeForm = LookupFormByID(chargeFormID);
			if (chargeForm)
			{
				g_bowAttackCharge = DYNAMIC_CAST(chargeForm, TESForm, TESIdleForm);
				if (g_bowAttackCharge)
				{
					_MESSAGE("SingleMountedCombat: Found BOW_ATTACK_CHARGE (FormID: %08X)", chargeFormID);
				}
			}
		}
		
		if (releaseFormID != 0)
		{
			TESForm* releaseForm = LookupFormByID(releaseFormID);
			if (releaseForm)
			{
				g_bowAttackRelease = DYNAMIC_CAST(releaseForm, TESForm, TESIdleForm);
				if (g_bowAttackRelease)
				{
					_MESSAGE("SingleMountedCombat: Found BOW_ATTACK_RELEASE (FormID: %08X)", releaseFormID);
				}
			}
		}
		
		g_bowIdlesInitialized = true;
		_MESSAGE("SingleMountedCombat: Bow animations initialized - Charge: %s, Release: %s",
			g_bowAttackCharge ? "SUCCESS" : "FAILED",
			g_bowAttackRelease ? "SUCCESS" : "FAILED");
	}

	bool SendHorseAnimationEvent(Actor* horse, const char* eventName)
	{
		if (!horse) return false;
		
		BSFixedString event(eventName);
		
		// Use vtable call to NotifyAnimationGraph
		typedef bool (*_IAnimationGraphManagerHolder_NotifyAnimationGraph)(IAnimationGraphManagerHolder* _this, const BSFixedString& eventName);
		return get_vfunc<_IAnimationGraphManagerHolder_NotifyAnimationGraph>(&horse->animGraphHolder, 0x1)(&horse->animGraphHolder, event);
	}
	
	void StartHorseSprint(Actor* horse)
	{
		if (!horse) return;
		
		InitSprintIdles();
		
		if (g_horseSprintStart)
		{
			const char* eventName = g_horseSprintStart->animationEvent.c_str();
			if (eventName && strlen(eventName) > 0)
			{
				if (SendHorseAnimationEvent(horse, eventName))
				{
					_MESSAGE("SingleMountedCombat: Horse %08X started sprinting (event: %s)", horse->formID, eventName);
				}
			}
		}
	}
	
	void StopHorseSprint(Actor* horse)
	{
		if (!horse) return;
		
		InitSprintIdles();
		
		if (g_horseSprintStop)
		{
			const char* eventName = g_horseSprintStop->animationEvent.c_str();
			if (eventName && strlen(eventName) > 0)
			{
				if (SendHorseAnimationEvent(horse, eventName))
				{
					_MESSAGE("SingleMountedCombat: Horse %08X stopped sprinting (event: %s)", horse->formID, eventName);
				}
			}
		}
	}
	
	// ============================================
	// HORSE REAR UP ANIMATION
	// ============================================
	
	bool PlayHorseRearUpAnimation(Actor* horse)
	{
		if (!horse) return false;
		
		InitSprintIdles();
		
		if (g_horseRearUp)
		{
			const char* eventName = g_horseRearUp->animationEvent.c_str();
			if (eventName && strlen(eventName) > 0)
			{
				if (SendHorseAnimationEvent(horse, eventName))
				{
					_MESSAGE("SingleMountedCombat: Horse %08X playing rear up animation (event: %s)", 
						horse->formID, eventName);
					return true;
				}
			}
		}
		
		return false;
	}
	
	// ============================================
	// BOW ATTACK FUNCTIONS
	// ============================================
	
	RiderBowAttackData* GetOrCreateBowAttackData(UInt32 riderFormID)
	{
		// Check if already tracked
		for (int i = 0; i < g_riderBowCount; i++)
		{
			if (g_riderBowData[i].isValid && g_riderBowData[i].riderFormID == riderFormID)
			{
				return &g_riderBowData[i];
			}
		}
		
		// Create new entry
		if (g_riderBowCount < 5)
		{
			RiderBowAttackData* data = &g_riderBowData[g_riderBowCount];
			data->riderFormID = riderFormID;
			data->state = BowAttackState::None;
			data->bowEquipTime = 0;
			data->drawStartTime = 0;
			data->holdDuration = 0;
			data->isValid = true;
			g_riderBowCount++;
			return data;
		}
		
		return nullptr;
	}
	
	void ResetBowAttackState(UInt32 riderFormID)
	{
		for (int i = 0; i < g_riderBowCount; i++)
		{
			if (g_riderBowData[i].isValid && g_riderBowData[i].riderFormID == riderFormID)
			{
				g_riderBowData[i].state = BowAttackState::None;
				g_riderBowData[i].bowEquipTime = 0;
				g_riderBowData[i].drawStartTime = 0;
				g_riderBowData[i].holdDuration = 0;
				return;
			}
		}
	}
	
	bool PlayBowDrawAnimation(Actor* rider)
	{
		if (!rider) return false;
		
		InitBowIdles();
		
		if (g_bowAttackCharge)
		{
			const char* eventName = g_bowAttackCharge->animationEvent.c_str();
			if (eventName && strlen(eventName) > 0)
			{
				if (SendHorseAnimationEvent(rider, eventName))
				{
					_MESSAGE("SingleMountedCombat: Rider %08X drawing bow (event: %s)", rider->formID, eventName);
					return true;
				}
			}
		}
		
		return false;
	}
	
	bool PlayBowReleaseAnimation(Actor* rider, Actor* target)
	{
		if (!rider) return false;
		
		InitBowIdles();
		
		if (g_bowAttackRelease)
		{
			const char* eventName = g_bowAttackRelease->animationEvent.c_str();
			if (eventName && strlen(eventName) > 0)
			{
				if (SendHorseAnimationEvent(rider, eventName))
				{
					_MESSAGE("SingleMountedCombat: Rider %08X releasing bow (event: %s)", rider->formID, eventName);
					
					// Fire the actual arrow projectile at the target
					if (target)
					{
						FireArrowAtTarget(rider, target);
					}
					else
					{
						_MESSAGE("SingleMountedCombat: WARNING - No target provided for arrow fire!");
					}
					
					return true;
				}
			}
		}
		
		return false;
	}
	
	// Update bow attack state for a rider
	// Returns true if bow attack is in progress
	// allowAttack: if false, only tracks equip time but doesn't start attacks
	// target: the combat target to fire at (required for actual firing)
	bool UpdateBowAttack(Actor* rider, bool allowAttack, Actor* target)
	{
		if (!rider) return false;
		
		// Check if bow is equipped
		if (!IsBowEquipped(rider))
		{
			// Bow not equipped - reset state
			ResetBowAttackState(rider->formID);
			return false;
		}
		
		RiderBowAttackData* data = GetOrCreateBowAttackData(rider->formID);
		if (!data) return false;
		
		float currentTime = GetGameTimeSeconds();
		
		switch (data->state)
		{
			case BowAttackState::None:
			{
				// Start tracking equip time
				data->state = BowAttackState::WaitingToEquip;
				data->bowEquipTime = currentTime;
				return false;
			}
			
			case BowAttackState::WaitingToEquip:
			{
				// Wait 1.5 seconds with bow equipped
				if ((currentTime - data->bowEquipTime) >= BOW_EQUIP_DELAY)
				{
					if (allowAttack)
					{
						// Equip arrows before drawing (only once per combat session)
						static UInt32 lastArrowEquipRider = 0;
						if (rider->formID != lastArrowEquipRider)
						{
							EquipArrows(rider);
							lastArrowEquipRider = rider->formID;
							_MESSAGE("SingleMountedCombat: Rider %08X equipped arrows for bow combat", rider->formID);
						}
						
						// Start drawing
						if (PlayBowDrawAnimation(rider))
						{
							data->state = BowAttackState::Drawing;
							data->drawStartTime = currentTime;
							
							// Generate random hold duration (2-3.5 seconds)
							EnsureRandomSeeded();
							float randomRange = BOW_DRAW_MAX_TIME - BOW_DRAW_MIN_TIME;
							data->holdDuration = BOW_DRAW_MIN_TIME + (((float)(rand() % 100)) / 100.0f * randomRange);
							
							_MESSAGE("SingleMountedCombat: Rider %08X starting bow draw, will hold for %.1f seconds",
								rider->formID, data->holdDuration);
						}
					}
				}
				return false;
			}
			
			case BowAttackState::Drawing:
			{
				// Transition to holding immediately (draw animation started)
				data->state = BowAttackState::Holding;
				return true;
			}
			
			case BowAttackState::Holding:
			{
				// Check if hold duration complete
				if ((currentTime - data->drawStartTime) >= data->holdDuration)
				{
					// Release! Pass the target for actual arrow firing
					if (PlayBowReleaseAnimation(rider, target))
					{
						data->state = BowAttackState::Released;
						_MESSAGE("SingleMountedCombat: Rider %08X released arrow after %.1f seconds",
							rider->formID, currentTime - data->drawStartTime);
					}
				}
				return true;
			}
			
			case BowAttackState::Released:
			{
				// Reset for next attack after a short delay
				data->state = BowAttackState::None;
				data->bowEquipTime = currentTime;  // Reset equip timer
				return false;
			}
			
			default:
				return false;
		}
	}
	
	// ============================================
	// ARROW FIRING FUNCTION
	// Uses the native Papyrus Weapon.Fire() function
	// Same approach as WeaponThrowVR mod
	// ============================================
	
	// Helper function to get the equipped ammo from an actor
	TESAmmo* GetEquippedAmmo(Actor* actor)
	{
		if (!actor) return nullptr;
		
		ExtraContainerChanges* containerChanges = static_cast<ExtraContainerChanges*>(
			actor->extraData.GetByType(kExtraData_ContainerChanges));
		
		if (!containerChanges || !containerChanges->data || !containerChanges->data->objList)
			return nullptr;
		
		tList<InventoryEntryData>* objList = containerChanges->data->objList;
		
		for (tList<InventoryEntryData>::Iterator it = objList->Begin(); !it.End(); ++it)
		{
			InventoryEntryData* entry = it.Get();
			if (!entry || !entry->type) continue;
			
			// Check if this is ammo
			TESAmmo* ammo = DYNAMIC_CAST(entry->type, TESForm, TESAmmo);
			if (!ammo) continue;
			
			// Check if it's equipped (worn)
			if (entry->extendDataList)
			{
				for (ExtendDataList::Iterator extIt = entry->extendDataList->Begin(); !extIt.End(); ++extIt)
				{
					BaseExtraList* extraList = extIt.Get();
					if (extraList && extraList->HasType(kExtraData_Worn))
					{
						return ammo;
					}
				}
			}
		}
		
		return nullptr;
	}
	
	bool FireArrowAtTarget(Actor* shooter, Actor* target)
	{
		if (!shooter || !target) return false;
		
		// Get the equipped bow
		TESForm* equippedWeapon = shooter->GetEquippedObject(false);
		if (!equippedWeapon)
		{
			_MESSAGE("SingleMountedCombat: FireArrowAtTarget - No weapon equipped on %08X", shooter->formID);
			return false;
		}
		
		TESObjectWEAP* bow = DYNAMIC_CAST(equippedWeapon, TESForm, TESObjectWEAP);
		if (!bow)
		{
			_MESSAGE("SingleMountedCombat: FireArrowAtTarget - Equipped item is not a weapon on %08X", shooter->formID);
			return false;
		}
		
		// Get the equipped ammo from the actor
		TESAmmo* ammo = GetEquippedAmmo(shooter);
		if (!ammo)
		{
			// Fallback to iron arrows if no ammo equipped
			TESForm* arrowForm = LookupFormByID(IRON_ARROW_FORMID);
			if (arrowForm)
			{
				ammo = DYNAMIC_CAST(arrowForm, TESForm, TESAmmo);
			}
			
			if (!ammo)
			{
				_MESSAGE("SingleMountedCombat: FireArrowAtTarget - No ammo equipped or found on %08X", shooter->formID);
				return false;
			}
			_MESSAGE("SingleMountedCombat: Using fallback Iron Arrows for %08X", shooter->formID);
		}
		else
		{
			_MESSAGE("SingleMountedCombat: Using equipped ammo '%s' for %08X", 
				ammo->fullName.name.data ? ammo->fullName.name.data : "Unknown", shooter->formID);
		}
		
		// Get the VM registry
		VMClassRegistry* registry = (*g_skyrimVM)->GetClassRegistry();
		if (!registry)
		{
			_MESSAGE("SingleMountedCombat: FireArrowAtTarget - Failed to get VM registry");
			return false;
		}
		
		// Calculate direction to target and set shooter's rotation to face target
		float dx = target->pos.x - shooter->pos.x;
		float dy = target->pos.y - shooter->pos.y;
		float dz = target->pos.z - shooter->pos.z;
		
		float horizontalDist = sqrt(dx * dx + dy * dy);
		
		// Calculate angles
		float angleZ = atan2(dx, dy);  // Horizontal angle (heading)
		float angleX = -atan2(dz, horizontalDist);  // Vertical angle (pitch)
		
		// Store original rotation
		float originalRotX = shooter->rot.x;
		float originalRotZ = shooter->rot.z;
		
		// Temporarily set shooter rotation to aim at target
		shooter->rot.z = angleZ;
		shooter->rot.x = angleX;
		
		// Fire the projectile using native Papyrus Fire function
		// Parameters: registry, stackId (0), weapon, source, ammo
		Fire(registry, 0, bow, shooter, ammo);
		
		// Restore original rotation
		shooter->rot.x = originalRotX;
		shooter->rot.z = originalRotZ;
		
		_MESSAGE("SingleMountedCombat: Fired arrow from %08X at target %08X (angleZ: %.2f, angleX: %.2f)", 
			shooter->formID, target->formID, angleZ, angleX);
		
		return true;
	}
	
	// ============================================
	// INITIALIZATION
	// ============================================
	
	void InitSingleMountedCombat()
	{
		if (g_singleCombatInitialized) return;
		
		_MESSAGE("SingleMountedCombat: Initializing...");
		EnsureRandomSeeded();
		g_combatStartTime = GetGameTimeSeconds();
		InitSprintIdles();
		g_singleCombatInitialized = true;
		_MESSAGE("SingleMountedCombat: Initialized successfully");
	}
	
	void NotifyCombatStarted()
	{
		g_combatStartTime = GetGameTimeSeconds();
		_MESSAGE("SingleMountedCombat: Combat started");
	}
}
