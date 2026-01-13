#pragma once
#include "skse64/PapyrusSpell.h"
#include "skse64/PapyrusGame.h"
#include "skse64/PapyrusActor.h"
#include "skse64/PapyrusPotion.h"
#include "skse64/GameMenus.h"
#include "skse64_common/SafeWrite.h"
#include <thread>
#include <atomic>
#include <mutex>
#include <unordered_set>
#include <chrono>
#include <xbyak/xbyak.h>
#include "skse64/NiExtraData.h"
#include "skse64_common/BranchTrampoline.h"
#include <skse64/GameRTTI.h>
#include <skse64/GameData.h>
#include <skse64/NiTypes.h>
#include <skse64/NiGeometry.h>
#include <skse64/GameExtraData.h>
#include <skse64/GameHandlers.h>

#include "skse64/NiExtraData.h"
#include <skse64/NiControllers.h>
#include "skse64/InternalTasks.h"

#include <deque>
#include <queue>
#include <array>
#include "skse64\GameVR.h"
#include <skse64/PapyrusEvents.h>

#include "config.h"

namespace MountedNPCCombatVR
{
	// ============================================
	// Shared Utility Functions
	// ============================================
	
	// Get elapsed time in seconds since mod started
	// Use this instead of creating separate static time functions per file
	float GetGameTime();
	
	// Ensure random number generator is seeded (call before using rand())
	// Use this instead of creating separate EnsureRandomSeeded functions per file
	void EnsureRandomSeeded();
	
	// ============================================
	// Mod State Control
	// ============================================
	extern bool g_modActive;
	
	void DeactivateMod();
	void ActivateModWithDelay();
	bool IsModReady();
	
	// ============================================
	// Player World Position Tracking
	// ============================================
	// Updated every frame by the game loop
	// Use these instead of constantly accessing g_thePlayer->pos
	
	extern float g_playerWorldPosX;
	extern float g_playerWorldPosY;
	extern float g_playerWorldPosZ;
	
	// Update player position globals (call from main update loop)
	void UpdatePlayerWorldPosition();
	
	// Get player world position as NiPoint3
	NiPoint3 GetPlayerWorldPosition();
	
	// ============================================
	// Game Event Handlers
	// ============================================
	UInt32 GetFullFormIdMine(const char* espName, UInt32 baseFormId);
	void ShowErrorBoxAndTerminate(const char* errorString);
	void GameLoad();
	void PostLoadGame();
	void OnNewGame();
	void OnPreLoadGame();
	void OnMainMenu();

	// ============================================
	// NPC Dismount Prevention Hook
	// ============================================
	
	// Function signature for the game's Dismount function
	typedef int64_t(__fastcall* _Dismount)(Actor* actor);
	
	// Original dismount function pointer (set during hook setup)
	extern _Dismount OriginalDismount;

	// The hook function that intercepts ALL dismount calls
	int64_t __fastcall DismountHook(Actor* actor);

	// Sets up the dismount hook - call this during mod initialization
	void SetupDismountHook();
}