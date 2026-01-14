#include "Helper.h"
#include "MountedCombat.h"
#include "CombatStyles.h"
#include "DynamicPackages.h"
#include "SpecialDismount.h"
#include "MultiMountedCombat.h"
#include "SpecialMovesets.h"
#include "ArrowSystem.h"
#include "CompanionCombat.h"
#include "WeaponDetection.h"
#include "config.h"

namespace MountedNPCCombatVR
{
	// ============================================
	// Shared Utility Functions
	// ============================================
	
	// Shared time function - returns seconds since mod initialized
	// All files should use this instead of their own static time functions
	float GetGameTime()
	{
		static auto startTime = clock();
		return (float)(clock() - startTime) / CLOCKS_PER_SEC;
	}
	
	// Shared random seeding function - ensures srand() is called once
	// All files should use this instead of their own EnsureRandomSeeded functions
	static bool g_sharedRandomSeeded = false;
	
	void EnsureRandomSeeded()
	{
		if (!g_sharedRandomSeeded)
		{
			unsigned int seed = (unsigned int)time(nullptr) ^ (unsigned int)clock();
			srand(seed);
			rand(); rand(); rand();  // Discard first few values for better randomness
			g_sharedRandomSeeded = true;
		}
	}
	
	// ============================================
	// Mod State Control
	// ============================================
	
	// Global flag to enable/disable the mod's dismount prevention
	// Set to false during game transitions to prevent CTD
	bool g_modActive = false;
	
	// Timestamp when the mod should activate (after delay)
	std::chrono::steady_clock::time_point g_activationTime;
	
	// Delay in seconds before mod activates after game load
	// 1 second to ensure all actors are fully loaded
	const int ACTIVATION_DELAY_SECONDS = 1;
	
	// Maximum distance from player to check NPCs (in game units)
	// 4000 units = roughly 1 cell distance
	const float MAX_DISTANCE_FROM_PLAYER = 2000.0f;

	// ============================================
	// Player World Position Tracking
	// ============================================
	// Updated periodically - use these globals instead of 
	// constantly accessing g_thePlayer->pos
	
	float g_playerWorldPosX = 0.0f;
	float g_playerWorldPosY = 0.0f;
	float g_playerWorldPosZ = 0.0f;
	
	void UpdatePlayerWorldPosition()
	{
		if (!g_thePlayer || !(*g_thePlayer))
		{
			return;
		}
		
		Actor* player = *g_thePlayer;
		
		g_playerWorldPosX = player->pos.x;
		g_playerWorldPosY = player->pos.y;
		g_playerWorldPosZ = player->pos.z;
	}
	
	NiPoint3 GetPlayerWorldPosition()
	{
		NiPoint3 pos;
		pos.x = g_playerWorldPosX;
		pos.y = g_playerWorldPosY;
		pos.z = g_playerWorldPosZ;
		return pos;
	}

	// ============================================
	// NPC Dismount Prevention - Address Definitions
	// ============================================
	
	// The actual game function that performs dismount on an Actor
	// Offset: 0x0060E780 for Skyrim VR
	RelocAddr<_Dismount> OriginalDismountFunc(0x0060E780);
	
	_Dismount OriginalDismount = nullptr;

	// Note: PreventNPCDismountOnAttack is defined in config.cpp

	// ============================================
	// Helper function to check if actor is a humanoid NPC
	// ============================================
	bool IsHumanoidNPC(Actor* actor)
	{
		if (!actor) return false;
		
		// Skip player
		if (!g_thePlayer || !(*g_thePlayer)) return false;
		if (actor == (*g_thePlayer)) return false;
		
		// Get the actor's race
		TESRace* race = actor->race;
		if (!race) return false;
		
		// Get race name for logging
		const char* raceName = race->fullName.name.data;
		
		// Simple approach: Check if race name contains common creature types
		if (raceName)
		{
			std::string raceStr = raceName;
			std::transform(raceStr.begin(), raceStr.end(), raceStr.begin(), ::tolower);
			
			// Skip if it's clearly an animal/creature/monster
			if (raceStr.find("fox") != std::string::npos ||
				raceStr.find("wolf") != std::string::npos ||
				raceStr.find("bear") != std::string::npos ||
				raceStr.find("deer") != std::string::npos ||
				raceStr.find("elk") != std::string::npos ||
				raceStr.find("goat") != std::string::npos ||
				raceStr.find("horse") != std::string::npos ||
				raceStr.find("dog") != std::string::npos ||
				raceStr.find("skeever") != std::string::npos ||
				raceStr.find("rabbit") != std::string::npos ||
				raceStr.find("chicken") != std::string::npos ||
				raceStr.find("cow") != std::string::npos ||
				raceStr.find("mudcrab") != std::string::npos ||
				raceStr.find("spider") != std::string::npos ||
				raceStr.find("dragon") != std::string::npos ||
				raceStr.find("troll") != std::string::npos ||
				raceStr.find("giant") != std::string::npos ||
				raceStr.find("mammoth") != std::string::npos ||
				raceStr.find("sabrecat") != std::string::npos ||
				raceStr.find("horker") != std::string::npos ||
				raceStr.find("slaughterfish") != std::string::npos ||
				raceStr.find("hagraven") != std::string::npos ||
				raceStr.find("spriggan") != std::string::npos ||
				raceStr.find("wisp") != std::string::npos ||
				raceStr.find("atronach") != std::string::npos ||
				raceStr.find("dwarven") != std::string::npos ||
				raceStr.find("centurion") != std::string::npos ||
				raceStr.find("sphere") != std::string::npos ||
				raceStr.find("falmer") != std::string::npos ||
				raceStr.find("chaurus") != std::string::npos ||
				raceStr.find("draugr") != std::string::npos ||
				raceStr.find("skeleton") != std::string::npos ||
				raceStr.find("ghost") != std::string::npos ||
				raceStr.find("vampire") != std::string::npos ||
				raceStr.find("werewolf") != std::string::npos ||
				raceStr.find("frostbite") != std::string::npos ||
				raceStr.find("ice wraith") != std::string::npos ||
				raceStr.find("gargoyle") != std::string::npos ||
				raceStr.find("lurker") != std::string::npos ||
				raceStr.find("seeker") != std::string::npos ||
				raceStr.find("riekling") != std::string::npos ||
				raceStr.find("netch") != std::string::npos ||
				raceStr.find("ash") != std::string::npos)
			{
				return false;
			}
		}
		
		return true; // Assume humanoid if not in creature list
	}
	
	// ============================================
	// Check if mod is ready to run
	// ============================================
	bool IsModReady()
	{
		// Check if mod is globally active
		if (!g_modActive)
		{
			return false;
		}
		
		// Check if we've passed the activation delay
		auto now = std::chrono::steady_clock::now();
		if (now < g_activationTime)
		{
			return false;
		}
		
		// Safety: Check if player exists and is valid
		if (!g_thePlayer || !(*g_thePlayer))
		{
			return false;
		}
		
		return true;
	}
	
	// ============================================
	// Check if actor is within range of player
	// ============================================
	bool IsWithinPlayerRange(Actor* actor)
	{
		if (!actor) return false;
		
		// Get player
		if (!g_thePlayer || !(*g_thePlayer)) return false;
		Actor* player = *g_thePlayer;
		
		// Get positions
		NiPoint3 actorPos = actor->pos;
		NiPoint3 playerPos = player->pos;
		
		// Calculate distance squared (faster than sqrt)
		float dx = actorPos.x - playerPos.x;
		float dy = actorPos.y - playerPos.y;
		float dz = actorPos.z - playerPos.z;
		float distSq = dx*dx + dy*dy + dz*dz;
		
		// Compare with max distance squared
		float maxDistSq = MAX_DISTANCE_FROM_PLAYER * MAX_DISTANCE_FROM_PLAYER;
		
		return distSq <= maxDistSq;
	}
	
	// ============================================
	// Safe actor validation - VERY IMPORTANT
	// ============================================
	bool IsActorValid(Actor* actor)
	{
		__try
		{
			if (!actor) return false;
			
			// Check if the actor's form is valid
			if (actor->formID == 0) return false;
			
			// Check formType to make sure it's actually an Actor
			if (actor->formType != kFormType_Character) return false;
			
			// Check if actor has 3D loaded (meaning it's fully loaded in an active cell)
			NiNode* node = actor->GetNiNode();
			if (!node) return false;
			
			// Check if actor has loaded state (extra safety for mounting NPCs)
			if (!actor->loadedState) return false;
			
			// Check if actor's process is valid (ensures AI is initialized)
			if (!actor->processManager) return false;
			
			return true;
		}
		__except(EXCEPTION_EXECUTE_HANDLER)
		{
			// If any access violation occurs, actor is invalid
			return false;
		}
	}

	// ============================================
	// NPC Dismount Prevention - Hook Function
	// ============================================
	
	int64_t __fastcall DismountHook(Actor* actor)
	{
		// ALWAYS pass through if mod not ready - no exceptions
		if (!g_modActive)
		{
			return OriginalDismount(actor);
		}
		
		// Check activation delay
		auto now = std::chrono::steady_clock::now();
		if (now < g_activationTime)
		{
			// Still waiting for activation delay
			return OriginalDismount(actor);
		}
		
		// Update mounted combat system
		UpdateMountedCombat();
		
		// Validate actor with SEH protection
		if (!IsActorValid(actor))
		{
			return OriginalDismount(actor);
		}

		// ============================================
		// PLAYER: ALWAYS ALLOW DISMOUNT - NO EXCEPTIONS
		// ============================================
		if (g_thePlayer && (*g_thePlayer) && actor == (*g_thePlayer))
		{
			return OriginalDismount(actor);
		}
		
		// ============================================
		// FROM HERE ON: NPC LOGIC ONLY
		// ============================================
		
		// Only apply in exterior cells
		if (!IsPlayerInExteriorCell())
		{
			return OriginalDismount(actor);
		}
		
		// Check if this is a humanoid NPC we care about
		if (!IsHumanoidNPC(actor))
		{
			return OriginalDismount(actor);
		}
		
		// ============================================
		// GRABBED BY PLAYER: ALLOW DISMOUNT
		// If the player is physically grabbing this mounted rider, allow dismount
		// ============================================
		if (IsActorGrabbedByPlayer(actor->formID))
		{
			// Double-check they're still mounted (the grab was registered when they were mounted)
			NiPointer<Actor> currentMount;
			bool stillMounted = CALL_MEMBER_FN(actor, GetMount)(currentMount);
			
			if (stillMounted && currentMount)
			{
				const char* actorName = CALL_MEMBER_FN(actor, GetReferenceName)();
				_MESSAGE("DismountHook: Mounted rider '%s' (FormID: %08X) is GRABBED by player - allowing dismount", 
					actorName ? actorName : "Unknown", actor->formID);
				
				// Clean up tracking since they're dismounting
				// RemoveNPCFromTracking handles all cleanup internally
				RemoveNPCFromTracking(actor->formID);
				return OriginalDismount(actor);
			}
		}
		
		// If NPC is dead, allow dismount
		if (actor->IsDead(1))
		{
			if (IsNPCTracked(actor->formID))
			{
				const char* actorName = CALL_MEMBER_FN(actor, GetReferenceName)();
				_MESSAGE("DismountHook: NPC '%s' (FormID: %08X) DIED - allowing dismount", 
					actorName ? actorName : "Unknown", actor->formID);
				
				// RemoveNPCFromTracking handles all cleanup internally
				RemoveNPCFromTracking(actor->formID);
			}
			return OriginalDismount(actor);
		}

		// Check if the NPC is currently mounted
		NiPointer<Actor> mount;
		bool isMounted = CALL_MEMBER_FN(actor, GetMount)(mount);
		
		if (isMounted && mount)
		{
			bool inCombat = actor->IsInCombat();
			
			// ============================================
			// RATE-LIMITED LOGGING FOR MOUNTED NPCs
			// Uses a simple circular buffer to track recently logged NPCs
			// ============================================
			static UInt32 recentlyLoggedNPCs[8] = {0};
			static float recentLogTimes[8] = {0};
			static int logIndex = 0;
			float currentTime = (float)clock() / CLOCKS_PER_SEC;
			const float LOG_COOLDOWN = 10.0f;  // Only log each NPC once per 10 seconds
			
			// Check if this NPC was recently logged
			bool shouldLog = true;
			for (int i = 0; i < 8; i++)
			{
				if (recentlyLoggedNPCs[i] == actor->formID && 
					(currentTime - recentLogTimes[i]) < LOG_COOLDOWN)
				{
					shouldLog = false;
					break;
				}
			}
			
			// Only log new NPCs in combat (not routine dismount checks)
			if (shouldLog && inCombat && !IsNPCTracked(actor->formID))
			{
				const char* actorName = CALL_MEMBER_FN(actor, GetReferenceName)();
				_MESSAGE("DismountHook: Checking mounted NPC '%s' (FormID: %08X) - InCombat: %s", 
					actorName ? actorName : "Unknown", actor->formID, inCombat ? "YES" : "NO");
				
				// Store in circular buffer
				recentlyLoggedNPCs[logIndex] = actor->formID;
				recentLogTimes[logIndex] = currentTime;
				logIndex = (logIndex + 1) % 8;
			}
			
			// ============================================
			// NPC IS MOUNTED AND IN COMBAT: BLOCK DISMOUNT
			// ============================================
			if (inCombat)
			{
				// Check if this is a companion (uses CompanionCombat tracking)
				bool isTrackedCompanion = (GetCompanionData(actor->formID) != nullptr);
				
				// Track new combat NPCs (not yet tracked by either system)
				if (!IsNPCTracked(actor->formID) && !isTrackedCompanion)
				{
					const char* actorName = CALL_MEMBER_FN(actor, GetReferenceName)();
					
					// ============================================
					// COMPANION CHECK: Companions get FULL combat capability
					// Route them through the EXACT SAME system as guards
					// The game's AI handles targeting - we just enable the combat
					// ============================================
					if (CompanionCombatEnabled && IsCompanion(actor))
					{
						Actor* mountActor = mount.get();
						if (mountActor)
						{
							// Rate limit logging - only log first block per NPC
							static UInt32 lastLoggedCompanion = 0;
							if (lastLoggedCompanion != actor->formID)
							{
								lastLoggedCompanion = actor->formID;
								_MESSAGE("DismountHook: BLOCKING COMPANION '%s' (FormID: %08X) - SAME AS GUARD", 
									actorName ? actorName : "Unknown", actor->formID);
							}
							
							// Register with companion tracking system (for friendly fire prevention)
							RegisterMountedCompanion(actor, mountActor);
							
							// ============================================
							// COMPANION WEAPON SETUP
							// Give companions a bow if they don't have one
							// ============================================
							if (!HasBowInInventory(actor))
							{
								GiveDefaultBow(actor);
								_MESSAGE("DismountHook: Gave default bow to companion '%s'", 
									actorName ? actorName : "Unknown");
							}
							EquipArrows(actor);
							
							// ============================================
							// ROUTE THROUGH STANDARD GUARD COMBAT SYSTEM
							// Exact same as any other mounted NPC
							// Game AI handles targeting
							// ============================================
							OnDismountBlocked(actor, mount);
						}
					}
					else
					{
						// Regular NPC - use standard MountedCombat system
						// Rate limit logging - only log first block per NPC
						static UInt32 lastLoggedNPC = 0;
						if (lastLoggedNPC != actor->formID)
						{
							lastLoggedNPC = actor->formID;
							_MESSAGE("DismountHook: BLOCKING '%s' (FormID: %08X) - in combat, preventing dismount", 
								actorName ? actorName : "Unknown", actor->formID);
						}
						
						OnDismountBlocked(actor, mount);
					}
				}
				
				// BLOCK DISMOUNT - NPC is in combat
				// Disengage is handled by CombatStyles when player gets too far (MAX_COMBAT_DISTANCE)
				return 0;
			}
			else
			{
				// ============================================
				// NPC IS MOUNTED BUT NOT IN COMBAT: ALLOW DISMOUNT
				// ============================================
				if (IsNPCTracked(actor->formID))
				{
					const char* actorName = CALL_MEMBER_FN(actor, GetReferenceName)();
					_MESSAGE("DismountHook: '%s' (FormID: %08X) left combat - allowing dismount", 
						actorName ? actorName : "Unknown", actor->formID);
					
					// RemoveNPCFromTracking handles all cleanup internally
					// (ClearNPCFollowTarget, RemoveMountedProtection, and Reset)
					RemoveNPCFromTracking(actor->formID);
				}
				
				// ALLOW DISMOUNT - NPC is not in combat
				return OriginalDismount(actor);
			}
		}

		// Not mounted - allow
		return OriginalDismount(actor);
	}

	// ============================================
	// Mod State Control Functions
	// ============================================
	
	void DeactivateMod()
	{
		_MESSAGE("MountedNPCCombatVR: === DEACTIVATING MOD ===");
		g_modActive = false;
		
		// Release all controlled mounts (restore their AI movement)
		ReleaseAllMountControl();
		
		// Clear all follow tracking
		ClearAllFollowingNPCs();
		
		// Reset mounted combat tracking to clear any stored FormIDs
		ResetAllMountedNPCs();
		
		// Reset SingleMountedCombat cached forms (arrow spell, bow idles, etc.)
		ResetSingleMountedCombatCache();
		
		// Reset CombatStyles cached forms (attack animations, etc.)
		ResetCombatStylesCache();
		
		// Reset MultiMountedCombat system (ranged riders, Captain tracking, etc.)
		ClearAllMultiRiders();
		
		// Reset SpecialMovesets system (charge, rapid fire, turn tracking, etc.)
		ResetAllSpecialMovesets();
		
		// Reset Arrow system
		ResetArrowSystem();
		
		// Reset DynamicPackage state (weapon switch tracking, horse movement, etc.)
		ResetDynamicPackageState();
		
		// Reset CompanionCombat system (mounted teammates/followers)
		ResetCompanionCombat();
		
		_MESSAGE("MountedNPCCombatVR: Mod DEACTIVATED - all state reset");
	}
	
	void ActivateModWithDelay()
	{
		_MESSAGE("MountedNPCCombatVR: === ACTIVATING MOD ===");
		
		// First deactivate to clean up any stale state
		g_modActive = false;
		
		// Release any controlled mounts from previous session
		ReleaseAllMountControl();
		
		// Reset all mounted NPC tracking
		ResetAllMountedNPCs();
		
		// Re-initialize the mounted combat system
		InitMountedCombatSystem();
		
		// Initialize the dynamic package system
		InitDynamicPackageSystem();
		
		// Set activation time to now + delay
		g_activationTime = std::chrono::steady_clock::now() + std::chrono::seconds(ACTIVATION_DELAY_SECONDS);
		g_modActive = true;
		
		// Log player state for diagnostics
		if (g_thePlayer && (*g_thePlayer))
		{
			Actor* player = *g_thePlayer;
			_MESSAGE("MountedNPCCombatVR: Player valid - FormID: %08X, Pos: (%.0f, %.0f, %.0f)",
				player->formID, player->pos.x, player->pos.y, player->pos.z);
		}
		else
		{
			_MESSAGE("MountedNPCCombatVR: WARNING - Player pointer not yet valid (will be checked later)");
		}
		
		_MESSAGE("MountedNPCCombatVR: Mod will activate in %d seconds", ACTIVATION_DELAY_SECONDS);
	}

	// ============================================
	// NPC Dismount Prevention - Hook Setup
	// ============================================
	
	void SetupDismountHook()
	{
		_MESSAGE("SetupDismountHook: Initializing NPC Dismount Prevention...");
		_MESSAGE("SetupDismountHook: PreventNPCDismountOnAttack = %s", PreventNPCDismountOnAttack ? "ENABLED" : "DISABLED");
		
		uintptr_t funcAddr = OriginalDismountFunc.GetUIntPtr();
		_MESSAGE("SetupDismountHook: Dismount function address: 0x%llX", funcAddr);
		
		// Read the first bytes of the dismount function
		unsigned char* funcStart = (unsigned char*)funcAddr;
		_MESSAGE("SetupDismountHook: First 20 bytes: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
			funcStart[0], funcStart[1], funcStart[2], funcStart[3],
			funcStart[4], funcStart[5], funcStart[6], funcStart[7],
			funcStart[8], funcStart[9], funcStart[10], funcStart[11],
			funcStart[12], funcStart[13], funcStart[14], funcStart[15],
			funcStart[16], funcStart[17], funcStart[18], funcStart[19]);
		
		// Store original function pointer directly
		OriginalDismount = (_Dismount)funcAddr;
		
		// Analyze the function prologue to determine how many bytes to copy
		// The function starts with: 40 55 56 57 41 54 41 55 41 56 41 57 48 8D 6C 24 D9 48 81 EC
		// 40 55 = push rbp (2 bytes, REX prefix)
		// 56 = push rsi (1 byte)
		// 57 = push rdi (1 byte)
		// 41 54 = push r12 (2 bytes)
		// 41 55 = push r13 (2 bytes)
		// 41 56 = push r14 (2 bytes)
		// 41 57 = push r15 (2 bytes)
		// 48 8D 6C 24 D9 = lea rbp, [rsp-0x27] (5 bytes)
		// Total: 17 bytes before we hit sub rsp
		
		int prologSize = 0;
		int i = 0;
		
		// Parse instructions until we have at least 5 bytes
		while (prologSize < 5 && i < 20)
		{
			unsigned char b = funcStart[i];
			
			// REX.W prefix (40-4F)
			if (b >= 0x40 && b <= 0x4F)
			{
				unsigned char nextB = funcStart[i + 1];
				
				// REX + push (50-57)
				if (nextB >= 0x50 && nextB <= 0x57)
				{
					prologSize += 2;
					i += 2;
					continue;
				}
				// REX + other instruction - need to analyze more
				else if (nextB == 0x8D) // lea
				{
					// lea with ModRM - typically 5+ bytes with REX
					// 48 8D 6C 24 XX = lea rbp, [rsp+disp8]
					prologSize += 5;
					i += 5;
					continue;
				}
				else if (nextB == 0x81 || nextB == 0x83) // sub/add with immediate
				{
					// sub rsp, imm - 4 or 7 bytes depending on immediate size
					if (nextB == 0x83) // imm8
					{
						prologSize += 4;
						i += 4;
					}
					else // imm32
					{
						prologSize += 7;
						i += 7;
					}
					continue;
				}
				else if (nextB == 0x89) // mov
				{
					// mov [rsp+disp8], reg = 5 bytes with REX
					prologSize += 5;
					i += 5;
					continue;
				}
				else
				{
					// Unknown REX instruction, assume 2 bytes minimum
					prologSize += 2;
					i += 2;
					continue;
				}
			}
			// Single byte push (50-57)
			else if (b >= 0x50 && b <= 0x57)
			{
				prologSize += 1;
				i += 1;
				continue;
			}
			// Unknown - break to avoid infinite loop
			else
			{
				break;
			}
		}
		
		_MESSAGE("SetupDismountHook: Detected prolog size: %d bytes", prologSize);
		
		// For this specific function (40 55 56 57 41 54 41 55 41 56 41 57...)
		// We need at least 12 bytes to not break mid-instruction
		// But 5 bytes minimum for our jump
		if (prologSize < 5)
		{
			_MESSAGE("SetupDismountHook: WARNING - Could not determine safe prolog size, using 12 bytes (safe for push sequence)");
			prologSize = 12; // 40 55 56 57 41 54 41 55 41 56 41 57 = 12 bytes of pushes
		}
		
		// Allocate trampoline memory
		void* trampMem = g_localTrampoline.Allocate(prologSize + 14);
		unsigned char* tramp = (unsigned char*)trampMem;
		
		// Copy the original prolog bytes
		memcpy(tramp, funcStart, prologSize);
		
		// Add jump back to original function + prologSize
		int offset = prologSize;
		tramp[offset++] = 0xFF;
		tramp[offset++] = 0x25;
		tramp[offset++] = 0x00;
		tramp[offset++] = 0x00;
		tramp[offset++] = 0x00;
		tramp[offset++] = 0x00;
		
		uintptr_t jumpBack = funcAddr + prologSize;
		memcpy(&tramp[offset], &jumpBack, 8);
		
		// Update OriginalDismount to point to trampoline
		OriginalDismount = (_Dismount)trampMem;
		
		_MESSAGE("SetupDismountHook: Trampoline at 0x%llX, jumps back to 0x%llX", (uintptr_t)trampMem, jumpBack);
		_MESSAGE("SetupDismountHook: Copied %d bytes to trampoline", prologSize);
		
		// Write jump at original function to our hook
		g_branchTrampoline.Write5Branch(funcAddr, (uintptr_t)DismountHook);
		
		_MESSAGE("SetupDismountHook: Hook installed successfully!");
		
		// Initialize mounted combat system
		InitMountedCombatSystem();
		
		// Mod starts deactivated - will be activated after game load with delay
		g_modActive = false;
	}

	std::uintptr_t Write5Call(std::uintptr_t a_src, std::uintptr_t a_dst)
	{
		const auto disp = reinterpret_cast<std::int32_t*>(a_src + 1);
		const auto nextOp = a_src + 5;
		const auto func = nextOp + *disp;
		g_branchTrampoline.Write5Call(a_src, a_dst);
		return func;
	}

	void LeftHandedModeChange()
	{
		const int value = vlibGetSetting("bLeftHandedMode:VRInput");
		if (value != leftHandedMode)
		{
			leftHandedMode = value;
			LOG("Left Handed Mode is %s.", leftHandedMode ? "ON" : "OFF");
		}
	}

	void ShowErrorBox(const char* errorString)
	{
		int msgboxID = MessageBox(
			NULL,
			(LPCTSTR)errorString,
			(LPCTSTR)"Mounted NPC Combat VR Fatal Error",
			MB_ICONERROR | MB_OK | MB_TASKMODAL
		);
	}

	void ShowErrorBoxAndLog(const char* errorString)
	{
		_ERROR(errorString);
		ShowErrorBox(errorString);
	}

	void ShowErrorBoxAndTerminate(const char* errorString)
	{
		ShowErrorBoxAndLog(errorString);
		*((int*)0) = 0xDEADBEEF;
	}

	template<typename T>
	T* LoadFormAndLog(const std::string& pluginName, UInt32& fullFormId, UInt32 baseFormId, const char* formName) 
	{
		fullFormId = GetFullFormIdFromEspAndFormId(pluginName.c_str(), GetBaseFormID(baseFormId));
		if (fullFormId > 0) 
		{
			TESForm* form = LookupFormByID(fullFormId);
			if (form) 
			{
				T* castedForm = nullptr;
				if constexpr (std::is_same_v<T, BGSProjectile>) 
				{
					castedForm = DYNAMIC_CAST(form, TESForm, BGSProjectile);
				}
				else if constexpr (std::is_same_v<T, TESAmmo>) 
				{
					castedForm = DYNAMIC_CAST(form, TESForm, TESAmmo);
				}
				else if constexpr (std::is_same_v<T, TESObjectWEAP>) 
				{
					castedForm = DYNAMIC_CAST(form, TESForm, TESObjectWEAP);
				}
				else if constexpr (std::is_same_v<T, TESObjectREFR>) 
				{
					castedForm = DYNAMIC_CAST(form, TESForm, TESObjectREFR);
				}
				else if constexpr (std::is_same_v<T, BGSSoundDescriptorForm>) 
				{
					castedForm = DYNAMIC_CAST(form, TESForm, BGSSoundDescriptorForm);
				}

				if (castedForm) 
				{
					LOG_ERR("%s found. formid: %x", formName, fullFormId);
					return castedForm;
				}
				else 
				{
					LOG_ERR("%s null. formid: %x", formName, fullFormId);
				}
			}
			else 
			{
				LOG_ERR("%s not found. formid: %x", formName, fullFormId);
			}
		}
		return nullptr;
	}

	void GameLoad()
	{
		_MESSAGE("MountedNPCCombatVR: GameLoad - Deactivating mod for transition");
		DeactivateMod();
		LeftHandedModeChange();
	}

	void PostLoadGame()
	{
		_MESSAGE("MountedNPCCombatVR: PostLoadGame - Save game loaded");
		
		// Hot-reload config on every game load
		loadConfig();
		
		if (g_thePlayer && (*g_thePlayer) && (*g_thePlayer)->loadedState)
		{
			_MESSAGE("MountedNPCCombatVR: Player loaded successfully - activating mod with delay");
			ActivateModWithDelay();
		}
	}
	
	void OnNewGame()
	{
		_MESSAGE("MountedNPCCombatVR: OnNewGame - New game started, activating mod with delay");
		
		// Hot-reload config on new game
		loadConfig();
		
		ActivateModWithDelay();
	}
	
	void OnPreLoadGame()
	{
		_MESSAGE("MountedNPCCombatVR: OnPreLoadGame - Deactivating mod before load");
		DeactivateMod();
	}
	
	void OnMainMenu()
	{
		_MESSAGE("MountedNPCCombatVR: OnMainMenu - Deactivating mod");
		DeactivateMod();
	}

	UInt32 GetFullFormIdMine(const char* espName, UInt32 baseFormId)
	{
		UInt32 fullFormID = 0;

		std::string espNameStr = espName;
		std::transform(espNameStr.begin(), espNameStr.end(), espNameStr.begin(), ::tolower);

		if (espNameStr == "skyrim.esm")
		{
			fullFormID = baseFormId;
		}
		else
		{
			DataHandler* dataHandler = DataHandler::GetSingleton();

			if (dataHandler)
			{
				std::pair<const char*, UInt32> formIdPair = { espName, baseFormId };
				
				const ModInfo* modInfo = NEWLookupAllLoadedModByName(formIdPair.first);
				if (modInfo)
				{
					if (IsValidModIndex(modInfo->modIndex))
					{
						fullFormID = GetFullFormID(modInfo, GetBaseFormID(formIdPair.second));
					}
				}
			}
		}
		return fullFormID;
	}
}