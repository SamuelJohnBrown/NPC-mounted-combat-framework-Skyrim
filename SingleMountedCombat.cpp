#include "SingleMountedCombat.h"
#include "ArrowSystem.h"
#include "DynamicPackages.h"
#include "Helper.h"
#include "skse64/GameRTTI.h"
#include "skse64/GameData.h"
#include "skse64/GameObjects.h"
#include "skse64_common/Utilities.h"
#include <cmath>
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
	
	// ============================================
	// SYSTEM STATE
	// ============================================
	
	static bool g_singleCombatInitialized = false;
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
	
	// ============================================
	// UTILITY FUNCTIONS
	// ============================================
	
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
		
		// Silently load horse animations - only log errors
		TESForm* sprintStartForm = LookupFormByID(HORSE_SPRINT_START_FORMID);
		if (sprintStartForm)
		{
			g_horseSprintStart = DYNAMIC_CAST(sprintStartForm, TESForm, TESIdleForm);
		}
		else
		{
			_MESSAGE("SingleMountedCombat: ERROR - Could not find HORSE_SPRINT_START");
		}
		
		TESForm* sprintStopForm = LookupFormByID(HORSE_SPRINT_STOP_FORMID);
		if (sprintStopForm)
		{
			g_horseSprintStop = DYNAMIC_CAST(sprintStopForm, TESForm, TESIdleForm);
		}
		else
		{
			_MESSAGE("SingleMountedCombat: ERROR - Could not find HORSE_SPRINT_STOP");
		}
		
		TESForm* rearUpForm = LookupFormByID(HORSE_REAR_UP_FORMID);
		if (rearUpForm)
		{
			g_horseRearUp = DYNAMIC_CAST(rearUpForm, TESForm, TESIdleForm);
		}
		else
		{
			_MESSAGE("SingleMountedCombat: ERROR - Could not find HORSE_REAR_UP");
		}
		
		g_sprintIdlesInitialized = true;
	}

	bool SendHorseAnimationEvent(Actor* horse, const char* eventName)
	{
		if (!horse) return false;
		
		BSFixedString event(eventName);
		
		typedef bool (*_IAnimationGraphManagerHolder_NotifyAnimationGraph)(IAnimationGraphManagerHolder* _this, const BSFixedString& eventName);
		return get_vfunc<_IAnimationGraphManagerHolder_NotifyAnimationGraph>(&horse->animGraphHolder, 0x1)(&horse->animGraphHolder, event);
	}
	
	void StartHorseSprint(Actor* horse)
	{
		if (!horse)
		{
			_MESSAGE("SingleMountedCombat: StartHorseSprint - horse is null!");
			return;
		}
		
		_MESSAGE("SingleMountedCombat: StartHorseSprint called for horse %08X", horse->formID);
		
		InitSprintIdles();
		
		if (g_horseSprintStart)
		{
			const char* eventName = g_horseSprintStart->animationEvent.c_str();
			if (eventName && strlen(eventName) > 0)
			{
				_MESSAGE("SingleMountedCombat: Sending sprint event '%s' to horse %08X", eventName, horse->formID);
				if (SendHorseAnimationEvent(horse, eventName))
				{
					_MESSAGE("SingleMountedCombat: Horse %08X sprint animation ACCEPTED", horse->formID);
				}
				else
				{
					_MESSAGE("SingleMountedCombat: Horse %08X sprint animation REJECTED (graph busy?)", horse->formID);
				}
			}
			else
			{
				_MESSAGE("SingleMountedCombat: ERROR - Sprint idle has empty animation event name!");
			}
		}
		else
		{
			_MESSAGE("SingleMountedCombat: ERROR - g_horseSprintStart is null!");
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
				SendHorseAnimationEvent(horse, eventName);
				// No log needed for sprint stop - happens frequently
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
					// Log handled by caller (SpecialMovesets)
					return true;
				}
			}
		}
		
		return false;
	}
	
	// ============================================
	// RESET CACHED FORMS
	// ============================================
	
	void ResetSingleMountedCombatCache()
	{
		_MESSAGE("SingleMountedCombat: Resetting cached forms...");
		
		g_horseSprintStart = nullptr;
		g_horseSprintStop = nullptr;
		g_horseRearUp = nullptr;
		g_sprintIdlesInitialized = false;
		
		g_horseJump = nullptr;
		g_jumpIdleInitialized = false;
		
		ResetArrowSystemCache();
		
		g_horseJumpCount = 0;
		for (int i = 0; i < 5; i++)
		{
			g_horseJumpData[i].isValid = false;
			g_horseJumpData[i].horseFormID = 0;
		}
		
		g_singleCombatInitialized = false;
	}
	
	// ============================================
	// INITIALIZATION
	// ============================================
	
	void InitSingleMountedCombat()
	{
		if (g_singleCombatInitialized) return;
		
		g_combatStartTime = GetGameTimeSeconds();
		InitSprintIdles();
		InitArrowSystem();
		g_singleCombatInitialized = true;
	}
	
	void NotifyCombatStarted()
	{
		g_combatStartTime = GetGameTimeSeconds();
	}
	
	float GetCombatElapsedTime()
	{
		return GetGameTimeSeconds() - g_combatStartTime;
	}
}
