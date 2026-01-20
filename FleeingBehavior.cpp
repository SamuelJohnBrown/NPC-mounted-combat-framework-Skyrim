#include "FleeingBehavior.h"
#include "DynamicPackages.h"
#include "CombatStyles.h"
#include "CompanionCombat.h"
#include "Helper.h"
#include "config.h"
#include "FactionData.h"
#include "skse64/GameRTTI.h"
#include "skse64/GameData.h"
#include <cstdlib>
#include <ctime>

namespace MountedNPCCombatVR
{
	// ============================================
	// CIVILIAN FLEE CONFIGURATION
	// ============================================
	
	const float CIVILIAN_FLEE_SAFE_DISTANCE = 2000.0f;  // Distance to flee before resetting AI
	const float CIVILIAN_FLEE_CHECK_INTERVAL = 1.0f;    // Check distance every 1 second
	
	// ============================================
	// CIVILIAN FLEE STATE TRACKING
	// ============================================
	
	const int MAX_FLEEING_CIVILIANS = 5;
	
	struct CivilianFleeData
	{
		UInt32 riderFormID;
		UInt32 horseFormID;
		UInt32 threatFormID;       // The actor they're fleeing from (not just player)
		float fleeStartTime;
		float lastCheckTime;
		bool isFleeing;
		bool fleePackageInjected;
		bool isValid;
		
		void Reset()
		{
			riderFormID = 0;
			horseFormID = 0;
			threatFormID = 0;
			fleeStartTime = 0;
			lastCheckTime = 0;
			isFleeing = false;
			fleePackageInjected = false;
			isValid = false;
		}
	};
	
	static CivilianFleeData g_fleeingCivilians[MAX_FLEEING_CIVILIANS];
	static int g_fleeingCivilianCount = 0;
	static bool g_civilianFleeInitialized = false;

	// ============================================
	// TACTICAL FLEE CONFIGURATION
	// ============================================
	
	const float FLEE_HEALTH_THRESHOLD = 0.30f;
	const float FLEE_CHECK_INTERVAL = 5.0f;
	const float FLEE_CHANCE = 0.15f;
	const float FLEE_MIN_DURATION = 4.0f;
	const float FLEE_MAX_DURATION = 10.0f;
	
	// ============================================
	// FLEE STATE TRACKING
	// ============================================
	
	struct TacticalFleeData
	{
		UInt32 riderFormID;
		UInt32 horseFormID;
		UInt32 targetFormID;
		float fleeStartTime;
		float fleeDuration;
		float lastFleeCheckTime;
		bool isFleeing;
		bool isValid;
		
		void Reset()
		{
			riderFormID = 0;
			horseFormID = 0;
			targetFormID = 0;
			fleeStartTime = 0;
			fleeDuration = 0;
			lastFleeCheckTime = 0;
			isFleeing = false;
			isValid = false;
		}
	};
	
	static TacticalFleeData g_currentFleeingRider;
	static bool g_fleeSystemInitialized = false;
	static bool g_randomSeeded = false;
	
	// ============================================
	// UTILITY FUNCTIONS
	// ============================================
	
	static void EnsureRandomSeeded()
	{
		if (!g_randomSeeded)
		{
			srand((unsigned int)time(nullptr));
			g_randomSeeded = true;
		}
	}
	
	static float GetRandomFleeDuration()
	{
		EnsureRandomSeeded();
		float range = FLEE_MAX_DURATION - FLEE_MIN_DURATION;
		float randomValue = (float)rand() / (float)RAND_MAX;
		return FLEE_MIN_DURATION + (randomValue * range);
	}
	
	static bool RollFleeChance()
	{
		EnsureRandomSeeded();
		float roll = (float)rand() / (float)RAND_MAX;
		return roll < FLEE_CHANCE;
	}
	
	static float GetActorHealthPercent(Actor* actor)
	{
		if (!actor) return 1.0f;
		
		ActorValueOwner* avOwner = &actor->actorValueOwner;
		if (!avOwner) return 1.0f;
		
		float currentHealth = avOwner->GetCurrent(24);
		float maxHealth = avOwner->GetMaximum(24);
		
		if (maxHealth <= 0) return 1.0f;
		return currentHealth / maxHealth;
	}
	
	static bool IsEligibleForFlee(Actor* rider, Actor* horse)
	{
		if (!rider || !horse) return false;
		
		if (rider->IsDead(1) || horse->IsDead(1)) return false;
		if (!rider->IsInCombat()) return false;
		
		NiPointer<Actor> currentMount;
		if (!CALL_MEMBER_FN(rider, GetMount)(currentMount) || !currentMount) return false;
		if (currentMount->formID != horse->formID) return false;
		
		if (IsCompanion(rider)) return false;
		
		const char* riderName = CALL_MEMBER_FN(rider, GetReferenceName)();
		if (riderName && strlen(riderName) > 0)
		{
			if (strstr(riderName, "Captain") != nullptr) return false;
			if (strstr(riderName, "Leader") != nullptr) return false;
		}
		
		MountedCombatClass combatClass = DetermineCombatClass(rider);
		if (combatClass == MountedCombatClass::MageCaster) return false;
		
		return true;
	}
	
	// ============================================
	// INITIALIZATION
	// ============================================
	
	void InitTacticalFlee()
	{
		if (g_fleeSystemInitialized) return;
		
		_MESSAGE("TacticalFlee: Initializing tactical flee system...");
		g_currentFleeingRider.Reset();
		g_fleeSystemInitialized = true;
		_MESSAGE("TacticalFlee: System initialized");
	}
	
	void ShutdownTacticalFlee()
	{
		if (!g_fleeSystemInitialized) return;
		
		_MESSAGE("TacticalFlee: Shutting down...");
		
		if (g_currentFleeingRider.isFleeing)
		{
			StopTacticalFlee(g_currentFleeingRider.riderFormID);
		}
		
		g_currentFleeingRider.Reset();
		g_fleeSystemInitialized = false;
	}
	
	void ResetTacticalFlee()
	{
		_MESSAGE("TacticalFlee: Resetting all state (data only - no form lookups)...");
		
		// ============================================
		// CRITICAL: Do NOT call LookupFormByID during reset!
		// During game load/death/transition, forms may be invalid
		// Just clear the tracking data - let game handle actual actor cleanup
		// ============================================
		
		g_currentFleeingRider.Reset();
		_MESSAGE("TacticalFlee: State reset complete");
	}
	
	// ============================================
	// START TACTICAL FLEE
	// ============================================
	
	bool StartTacticalFlee(Actor* rider, Actor* horse, Actor* target)
	{
		if (!rider || !horse || !target) return false;
		
		if (g_currentFleeingRider.isFleeing)
		{
			return false;
		}
		
		const char* riderName = CALL_MEMBER_FN(rider, GetReferenceName)();
		float fleeDuration = GetRandomFleeDuration();
		
		_MESSAGE("TacticalFlee: ========================================");
		_MESSAGE("TacticalFlee: '%s' (%08X) STARTING TACTICAL RETREAT!", 
			riderName ? riderName : "Unknown", rider->formID);
		_MESSAGE("TacticalFlee: Health: %.0f%% | Duration: %.1f seconds",
			GetActorHealthPercent(rider) * 100.0f, fleeDuration);
		_MESSAGE("TacticalFlee: ========================================");
		
		StopHorseSprint(horse);
		Actor_ClearKeepOffsetFromActor(horse);
		ClearInjectedPackages(horse);
		ClearNPCFollowTarget(rider);
		
		SetWeaponDrawn(rider, false);
		
		TESPackage* fleePackage = CreatePackageByType(TESPackage::kPackageType_Flee);
		if (fleePackage)
		{
			fleePackage->packageFlags |= 6;
			
			PackageTarget packageTarget;
			PackageTarget_CTOR(&packageTarget);
			TESPackage_SetPackageTarget(fleePackage, &packageTarget);
			PackageTarget_ResetValueByTargetType((PackageTarget*)fleePackage->unk40, 0);
			PackageTarget_SetFromReference((PackageTarget*)fleePackage->unk40, target);
			
			TESPackage_sub_140439BE0(fleePackage, 0);
			
			ActorProcessManager* process = horse->processManager;
			if (process && process->unk18.package)
			{
				TESPackage_CopyFlagsFromOtherPackage(fleePackage, process->unk18.package);
			}
			
			get_vfunc<_Actor_PutCreatedPackage>(horse, 0xE1)(horse, fleePackage, true, 1);
			
			_MESSAGE("TacticalFlee: Injected Flee package to horse %08X", horse->formID);
		}
		else
		{
			_MESSAGE("TacticalFlee: WARNING - Failed to create Flee package, using fallback");
			
			float dx = horse->pos.x - target->pos.x;
			float dy = horse->pos.y - target->pos.y;
			float dist = sqrt(dx * dx + dy * dy);
			
			if (dist > 0)
			{
				dx = (dx / dist) * 1500.0f;
			 dy = (dy / dist) * 1500.0f;
				
				NiPoint3 offset;
				offset.x = dx;
				offset.y = dy;
				offset.z = 0;
				
				NiPoint3 offsetAngle;
				offsetAngle.x = 0;
				offsetAngle.y = 0;
				offsetAngle.z = 0;
				
				UInt32 targetHandle = target->CreateRefHandle();
				if (targetHandle != 0 && targetHandle != *g_invalidRefHandle)
				{
					Actor_KeepOffsetFromActor(horse, targetHandle, offset, offsetAngle, 2000.0f, 500.0f);
					Actor_EvaluatePackage(horse, false, false);
				}
			}
			
			StartHorseSprint(horse);
		}
		
		g_currentFleeingRider.riderFormID = rider->formID;
		g_currentFleeingRider.horseFormID = horse->formID;
		g_currentFleeingRider.targetFormID = target->formID;
		g_currentFleeingRider.fleeStartTime = GetGameTime();
		g_currentFleeingRider.fleeDuration = fleeDuration;
		g_currentFleeingRider.lastFleeCheckTime = GetGameTime();
		g_currentFleeingRider.isFleeing = true;
		g_currentFleeingRider.isValid = true;
		
		return true;
	}
	
	// ============================================
	// STOP TACTICAL FLEE - Return to combat
	// ============================================
	
	void StopTacticalFlee(UInt32 riderFormID)
	{
		if (!g_currentFleeingRider.isFleeing) return;
		if (g_currentFleeingRider.riderFormID != riderFormID) return;
		
		TESForm* riderForm = LookupFormByID(g_currentFleeingRider.riderFormID);
		TESForm* horseForm = LookupFormByID(g_currentFleeingRider.horseFormID);
		TESForm* targetForm = LookupFormByID(g_currentFleeingRider.targetFormID);
		
		if (!riderForm || !horseForm)
		{
			g_currentFleeingRider.Reset();
			return;
		}
		
		Actor* rider = DYNAMIC_CAST(riderForm, TESForm, Actor);
		Actor* horse = DYNAMIC_CAST(horseForm, TESForm, Actor);
		Actor* target = targetForm ? DYNAMIC_CAST(targetForm, TESForm, Actor) : nullptr;
		
		if (!rider || !horse)
		{
			g_currentFleeingRider.Reset();
			return;
		}
		
		const char* riderName = CALL_MEMBER_FN(rider, GetReferenceName)();
		float fleeTime = GetGameTime() - g_currentFleeingRider.fleeStartTime;
		
		_MESSAGE("TacticalFlee: ========================================");
		_MESSAGE("TacticalFlee: '%s' (%08X) ENDING TACTICAL RETREAT",
			riderName ? riderName : "Unknown", rider->formID);
		_MESSAGE("TacticalFlee: Fled for %.1f seconds - RETURNING TO COMBAT!",
			fleeTime);
		_MESSAGE("TacticalFlee: ========================================");
		
		StopHorseSprint(horse);
		Actor_ClearKeepOffsetFromActor(horse);
		ClearInjectedPackages(horse);
		
		Actor_EvaluatePackage(rider, false, false);
		Actor_EvaluatePackage(horse, false, false);
		
		if (target && !target->IsDead(1))
		{
			float distance = GetDistanceBetween(rider, target);
			
			if (distance <= MaxCombatDistance)
			{
				SetWeaponDrawn(rider, true);
				SetNPCFollowTarget(rider, target);
				ForceHorseCombatWithTarget(horse, target);
				
				_MESSAGE("TacticalFlee: Re-engaged target at %.0f units", distance);
			}
			else
			{
				_MESSAGE("TacticalFlee: Target too far (%.0f > %.0f) - not re-engaging",
					distance, MaxCombatDistance);
			}
		}
		else
		{
			_MESSAGE("TacticalFlee: No valid target to re-engage");
		}
		
		g_currentFleeingRider.Reset();
	}
	
	// ============================================
	// CHECK IF RIDER SHOULD START FLEEING
	// ============================================
	
	bool CheckAndTriggerTacticalFlee(Actor* rider, Actor* horse, Actor* target)
	{
		if (!g_fleeSystemInitialized) return false;
		if (!rider || !horse || !target) return false;
		
		if (g_currentFleeingRider.isFleeing) return false;
		
		if (!IsEligibleForFlee(rider, horse)) return false;
		
		float healthPercent = GetActorHealthPercent(rider);
		if (healthPercent > FLEE_HEALTH_THRESHOLD) return false;
		
		static UInt32 s_lastCheckedRider = 0;
		static float s_lastCheckTime = 0;
		
		float currentTime = GetGameTime();
		
		if (rider->formID == s_lastCheckedRider)
		{
			if ((currentTime - s_lastCheckTime) < FLEE_CHECK_INTERVAL)
			{
				return false;
			}
		}
		
		s_lastCheckedRider = rider->formID;
		s_lastCheckTime = currentTime;
		
		if (!RollFleeChance())
		{
			return false;
		}
		
		return StartTacticalFlee(rider, horse, target);
	}
	
	// ============================================
	// UPDATE TACTICAL FLEE - Call every frame
	// ============================================
	
	void UpdateTacticalFlee()
	{
		if (!g_fleeSystemInitialized) return;
		if (!g_currentFleeingRider.isFleeing) return;
		
		float currentTime = GetGameTime();
		float elapsedTime = currentTime - g_currentFleeingRider.fleeStartTime;
		
		if (elapsedTime >= g_currentFleeingRider.fleeDuration)
		{
			StopTacticalFlee(g_currentFleeingRider.riderFormID);
			return;
		}
		
		TESForm* riderForm = LookupFormByID(g_currentFleeingRider.riderFormID);
		TESForm* horseForm = LookupFormByID(g_currentFleeingRider.horseFormID);
		
		if (!riderForm || !horseForm)
		{
			_MESSAGE("TacticalFlee: Rider or horse form invalid - stopping flee");
			g_currentFleeingRider.Reset();
			return;
		}
		
		Actor* rider = DYNAMIC_CAST(riderForm, TESForm, Actor);
		Actor* horse = DYNAMIC_CAST(horseForm, TESForm, Actor);
		
		if (!rider || !horse)
		{
			_MESSAGE("TacticalFlee: Rider or horse cast failed - stopping flee");
			g_currentFleeingRider.Reset();
			return;
		}
		
		if (rider->IsDead(1))
		{
			_MESSAGE("TacticalFlee: Rider died during flee - stopping");
			g_currentFleeingRider.Reset();
			return;
		}
		
		NiPointer<Actor> currentMount;
		if (!CALL_MEMBER_FN(rider, GetMount)(currentMount) || !currentMount)
		{
			_MESSAGE("TacticalFlee: Rider dismounted during flee - stopping");
			g_currentFleeingRider.Reset();
			return;
		}
		
		static float s_lastProgressLog = 0;
		if ((currentTime - s_lastProgressLog) >= 2.0f)
		{
			s_lastProgressLog = currentTime;
			const char* riderName = CALL_MEMBER_FN(rider, GetReferenceName)();
			_MESSAGE("TacticalFlee: '%s' fleeing - %.1f / %.1f seconds",
				riderName ? riderName : "Unknown", elapsedTime, g_currentFleeingRider.fleeDuration);
		}
	}
	
	// ============================================
	// QUERY FUNCTIONS
	// ============================================
	
	bool IsRiderFleeing(UInt32 riderFormID)
	{
		return g_currentFleeingRider.isFleeing && 
		       g_currentFleeingRider.riderFormID == riderFormID;
	}
	
	bool IsAnyRiderFleeing()
	{
		return g_currentFleeingRider.isFleeing;
	}
	
	UInt32 GetFleeingRiderFormID()
	{
		return g_currentFleeingRider.isFleeing ? g_currentFleeingRider.riderFormID : 0;
	}
	
	float GetFleeTimeRemaining(UInt32 riderFormID)
	{
		if (!g_currentFleeingRider.isFleeing) return 0;
		if (g_currentFleeingRider.riderFormID != riderFormID) return 0;
		
		float elapsed = GetGameTime() - g_currentFleeingRider.fleeStartTime;
		float remaining = g_currentFleeingRider.fleeDuration - elapsed;
		return (remaining > 0) ? remaining : 0;
	}
	
	bool IsHorseRiderFleeing(UInt32 horseFormID)
	{
		// Check tactical flee
		if (g_currentFleeingRider.isFleeing && g_currentFleeingRider.horseFormID == horseFormID)
		{
			return true;
		}
		
		// Check civilian flee
		for (int i = 0; i < MAX_FLEEING_CIVILIANS; i++)
		{
			if (g_fleeingCivilians[i].isValid && 
				g_fleeingCivilians[i].isFleeing && 
				g_fleeingCivilians[i].horseFormID == horseFormID)
			{
				return true;
			}
		}
		
		return false;
	}
	
	// ============================================
	// CIVILIAN FLEE SYSTEM
	// Mounted civilians flee from threats and reset AI at safe distance
	// ============================================
	
	static CivilianFleeData* GetOrCreateCivilianFleeData(UInt32 riderFormID)
	{
		// Check if already tracked
		for (int i = 0; i < MAX_FLEEING_CIVILIANS; i++)
		{
			if (g_fleeingCivilians[i].isValid && g_fleeingCivilians[i].riderFormID == riderFormID)
			{
				return &g_fleeingCivilians[i];
			}
		}
		
		// Find empty slot
		for (int i = 0; i < MAX_FLEEING_CIVILIANS; i++)
		{
			if (!g_fleeingCivilians[i].isValid)
			{
				g_fleeingCivilians[i].Reset();
				g_fleeingCivilians[i].riderFormID = riderFormID;
				g_fleeingCivilians[i].isValid = true;
				g_fleeingCivilianCount++;
				return &g_fleeingCivilians[i];
			}
		}
		
		return nullptr;
	}
	
	static void ClearCivilianFleeData(UInt32 riderFormID)
	{
		for (int i = 0; i < MAX_FLEEING_CIVILIANS; i++)
		{
			if (g_fleeingCivilians[i].isValid && g_fleeingCivilians[i].riderFormID == riderFormID)
			{
				g_fleeingCivilians[i].Reset();
				g_fleeingCivilianCount--;
				return;
			}
		}
	}
	
	bool IsCivilianFleeing(UInt32 riderFormID)
	{
		for (int i = 0; i < MAX_FLEEING_CIVILIANS; i++)
		{
			if (g_fleeingCivilians[i].isValid && 
				g_fleeingCivilians[i].isFleeing && 
				g_fleeingCivilians[i].riderFormID == riderFormID)
			{
				return true;
			}
		}
		return false;
	}
	
	bool StartCivilianFlee(Actor* rider, Actor* horse, Actor* threat)
	{
		if (!rider || !horse || !threat) return false;
		
		// Verify this is actually a civilian
		MountedCombatClass combatClass = DetermineCombatClass(rider);
		if (combatClass != MountedCombatClass::CivilianFlee)
		{
			return false;
		}
		
		// Check if already fleeing
		if (IsCivilianFleeing(rider->formID))
		{
			return false;
		}
		
		CivilianFleeData* data = GetOrCreateCivilianFleeData(rider->formID);
		if (!data) return false;
		
		const char* riderName = CALL_MEMBER_FN(rider, GetReferenceName)();
		const char* threatName = CALL_MEMBER_FN(threat, GetReferenceName)();
		
		_MESSAGE("CivilianFlee: ========================================");
		_MESSAGE("CivilianFlee: '%s' (%08X) FLEEING from '%s'!", 
			riderName ? riderName : "Civilian", rider->formID,
			threatName ? threatName : "Threat");
		_MESSAGE("CivilianFlee: Will flee until %.0f units away", CIVILIAN_FLEE_SAFE_DISTANCE);
		_MESSAGE("CivilianFlee: ========================================");
		
		// Clear any combat state first
		StopHorseSprint(horse);
		Actor_ClearKeepOffsetFromActor(horse);
		ClearInjectedPackages(horse);
		
		// Sheathe weapon if drawn
		if (IsWeaponDrawn(rider))
		{
			SetWeaponDrawn(rider, false);
		}
		
		// Create and inject flee package targeting the threat
		TESPackage* fleePackage = CreatePackageByType(TESPackage::kPackageType_Flee);
		if (fleePackage)
		{
			fleePackage->packageFlags |= 6;  // Override flag
			
			PackageTarget packageTarget;
			PackageTarget_CTOR(&packageTarget);
			TESPackage_SetPackageTarget(fleePackage, &packageTarget);
			PackageTarget_ResetValueByTargetType((PackageTarget*)fleePackage->unk40, 0);
			PackageTarget_SetFromReference((PackageTarget*)fleePackage->unk40, threat);
			
			TESPackage_sub_140439BE0(fleePackage, 0);
			
			// Copy flags from current package if available
			ActorProcessManager* process = horse->processManager;
			if (process && process->unk18.package)
			{
				TESPackage_CopyFlagsFromOtherPackage(fleePackage, process->unk18.package);
			}
			
			// Inject the flee package to the horse
			get_vfunc<_Actor_PutCreatedPackage>(horse, 0xE1)(horse, fleePackage, true, 1);
			
			data->fleePackageInjected = true;
			_MESSAGE("CivilianFlee: Injected Flee package to horse %08X", horse->formID);
		}
		else
		{
			_MESSAGE("CivilianFlee: WARNING - Failed to create Flee package!");
			data->fleePackageInjected = false;
		}
		
		// Start sprint for faster flee
		StartHorseSprint(horse);
		
		// Store flee data
		data->horseFormID = horse->formID;
		data->threatFormID = threat->formID;
		data->fleeStartTime = GetGameTime();
		data->lastCheckTime = GetGameTime();
		data->isFleeing = true;
		
		return true;
	}
	
	void StopCivilianFlee(UInt32 riderFormID, bool resetToDefaultAI)
	{
		CivilianFleeData* data = nullptr;
		for (int i = 0; i < MAX_FLEEING_CIVILIANS; i++)
		{
			if (g_fleeingCivilians[i].isValid && g_fleeingCivilians[i].riderFormID == riderFormID)
			{
				data = &g_fleeingCivilians[i];
				break;
			}
		}
		
		if (!data || !data->isFleeing) return;
		
		TESForm* riderForm = LookupFormByID(data->riderFormID);
		TESForm* horseForm = LookupFormByID(data->horseFormID);
		
		if (!riderForm || !horseForm)
		{
			ClearCivilianFleeData(riderFormID);
			return;
		}
		
		Actor* rider = DYNAMIC_CAST(riderForm, TESForm, Actor);
		Actor* horse = DYNAMIC_CAST(horseForm, TESForm, Actor);
		
		if (!rider || !horse)
		{
			ClearCivilianFleeData(riderFormID);
			return;
		}
		
		const char* riderName = CALL_MEMBER_FN(rider, GetReferenceName)();
		float fleeTime = GetGameTime() - data->fleeStartTime;
		
		_MESSAGE("CivilianFlee: ========================================");
		_MESSAGE("CivilianFlee: '%s' (%08X) STOPPED FLEEING",
			riderName ? riderName : "Civilian", rider->formID);
		_MESSAGE("CivilianFlee: Fled for %.1f seconds", fleeTime);
		_MESSAGE("CivilianFlee: ========================================");
		
		// Stop horse movement
		StopHorseSprint(horse);
		Actor_ClearKeepOffsetFromActor(horse);
		ClearInjectedPackages(horse);
		
		if (resetToDefaultAI)
		{
			// ============================================
			// RESET TO DEFAULT AI - Same as disengage logic
			// Uses StopActorCombatAlarm to properly clear combat state
			// ============================================
			
			// Stop combat alarm - this properly clears combat state
			StopActorCombatAlarm(rider);
			
			// Clear horse combat target and flags
			horse->currentCombatTarget = 0;
			horse->flags2 &= ~Actor::kFlag_kAttackOnSight;
			
			// Force AI re-evaluation to return to default behavior
			Actor_EvaluatePackage(rider, false, false);
			Actor_EvaluatePackage(horse, false, false);
			
			_MESSAGE("CivilianFlee: '%s' AI reset to default behavior", riderName ? riderName : "Civilian");
		}
		
		ClearCivilianFleeData(riderFormID);
	}
	
	void UpdateCivilianFlee()
	{
		if (!g_civilianFleeInitialized) return;
		
		float currentTime = GetGameTime();
		
		for (int i = 0; i < MAX_FLEEING_CIVILIANS; i++)
		{
			CivilianFleeData* data = &g_fleeingCivilians[i];
			if (!data->isValid || !data->isFleeing) continue;
			
			// Rate limit checks
			if ((currentTime - data->lastCheckTime) < CIVILIAN_FLEE_CHECK_INTERVAL)
			{
				continue;
			}
			data->lastCheckTime = currentTime;
			
			// Lookup actors
			TESForm* riderForm = LookupFormByID(data->riderFormID);
			TESForm* horseForm = LookupFormByID(data->horseFormID);
			TESForm* threatForm = LookupFormByID(data->threatFormID);
			
			if (!riderForm || !horseForm)
			{
				_MESSAGE("CivilianFlee: Rider or horse form invalid - stopping flee");
				data->Reset();
				g_fleeingCivilianCount--;
				continue;
			}
			
			Actor* rider = DYNAMIC_CAST(riderForm, TESForm, Actor);
			Actor* horse = DYNAMIC_CAST(horseForm, TESForm, Actor);
			Actor* threat = threatForm ? DYNAMIC_CAST(threatForm, TESForm, Actor) : nullptr;
			
			if (!rider || !horse)
			{
				_MESSAGE("CivilianFlee: Actor cast failed - stopping flee");
				data->Reset();
				g_fleeingCivilianCount--;
				continue;
			}
			
			// Check if rider died or dismounted
			if (rider->IsDead(1))
			{
				_MESSAGE("CivilianFlee: Civilian died - stopping flee");
				data->Reset();
				g_fleeingCivilianCount--;
				continue;
			}
			
			// Check if horse died
			if (horse->IsDead(1))
			{
				_MESSAGE("CivilianFlee: Horse died - stopping flee");
			 data->Reset();
				g_fleeingCivilianCount--;
				continue;
			}
			
			NiPointer<Actor> currentMount;
			if (!CALL_MEMBER_FN(rider, GetMount)(currentMount) || !currentMount)
			{
				_MESSAGE("CivilianFlee: Civilian dismounted - stopping flee");
			 data->Reset();
				g_fleeingCivilianCount--;
				continue;
			}
			
			// Calculate distance to threat
			float distanceToThreat = 99999.0f;
			if (threat && !threat->IsDead(1))
			{
				float dx = threat->pos.x - horse->pos.x;
				float dy = threat->pos.y - horse->pos.y;
				distanceToThreat = sqrt(dx * dx + dy * dy);
			}
			
			// Check if at safe distance
			if (distanceToThreat >= CIVILIAN_FLEE_SAFE_DISTANCE)
			{
				const char* riderName = CALL_MEMBER_FN(rider, GetReferenceName)();
				_MESSAGE("CivilianFlee: '%s' reached safe distance (%.0f >= %.0f) - resetting AI",
					riderName ? riderName : "Civilian", distanceToThreat, CIVILIAN_FLEE_SAFE_DISTANCE);
				
				// Stop fleeing and reset to default AI
				StopCivilianFlee(data->riderFormID, true);
				continue;
			}
			
			// Still fleeing - log progress periodically
			static float s_lastProgressLog = 0;
			if ((currentTime - s_lastProgressLog) >= 3.0f)
			{
				s_lastProgressLog = currentTime;
				const char* riderName = CALL_MEMBER_FN(rider, GetReferenceName)();
				_MESSAGE("CivilianFlee: '%s' fleeing - distance to threat: %.0f / %.0f",
					riderName ? riderName : "Civilian", distanceToThreat, CIVILIAN_FLEE_SAFE_DISTANCE);
			}
		}
	}
	
	bool ProcessCivilianMountedNPC(Actor* rider, Actor* horse, Actor* threat)
	{
		if (!rider || !horse) return false;
		
		// Check if this is a civilian
		MountedCombatClass combatClass = DetermineCombatClass(rider);
		if (combatClass != MountedCombatClass::CivilianFlee)
		{
			return false;  // Not a civilian - use normal combat logic
		}
		
		// Already fleeing?
		if (IsCivilianFleeing(rider->formID))
		{
			return true;  // Already being handled
		}
		
		// Need a threat to flee from
		if (!threat || threat->IsDead(1))
		{
			return false;
		}
		
		// Start civilian flee!
		return StartCivilianFlee(rider, horse, threat);
	}
	
	void InitCivilianFlee()
	{
		if (g_civilianFleeInitialized) return;
		
		_MESSAGE("CivilianFlee: Initializing civilian flee system...");
		
		for (int i = 0; i < MAX_FLEEING_CIVILIANS; i++)
		{
			g_fleeingCivilians[i].Reset();
		}
		g_fleeingCivilianCount = 0;
		g_civilianFleeInitialized = true;
		
		_MESSAGE("CivilianFlee: System initialized (max %d civilians)", MAX_FLEEING_CIVILIANS);
	}
	
	void ShutdownCivilianFlee()
	{
		if (!g_civilianFleeInitialized) return;
		
		_MESSAGE("CivilianFlee: Shutting down...");
		
		// Stop all fleeing civilians
		for (int i = 0; i < MAX_FLEEING_CIVILIANS; i++)
		{
			if (g_fleeingCivilians[i].isValid && g_fleeingCivilians[i].isFleeing)
			{
				StopCivilianFlee(g_fleeingCivilians[i].riderFormID, false);
			}
		}
		
		g_civilianFleeInitialized = false;
	}
	
	void ResetCivilianFlee()
	{
		_MESSAGE("CivilianFlee: Resetting all state...");
		
		for (int i = 0; i < MAX_FLEEING_CIVILIANS; i++)
		{
			g_fleeingCivilians[i].Reset();
		}
		g_fleeingCivilianCount = 0;
		
		_MESSAGE("CivilianFlee: State reset complete");
	}
	
	// ============================================
	// LEGACY NAMESPACE - For compatibility
	// ============================================
	
	namespace CivilianFlee
	{
		void InitFleeingBehavior()
		{
			InitTacticalFlee();
			InitCivilianFlee();
		}
		
		void ShutdownFleeingBehavior()
		{
			ShutdownTacticalFlee();
			ShutdownCivilianFlee();
		}
	}
}
