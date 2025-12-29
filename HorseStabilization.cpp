#include "HorseStabilization.h"
#include "Helper.h"
#include "config.h"
#include "skse64/GameRTTI.h"
#include "skse64/GameData.h"
#include "skse64/GameReferences.h"
#include "skse64/NiNodes.h"
#include "skse64/GameExtraData.h"
#include <vector>
#include <ctime>
#include <cstring>

namespace MountedNPCCombatVR
{
	// ============================================
	// CONFIGURATION
	// ============================================
	
	const float STABILIZATION_DELAY = 1.0f;        // Wait 1 second after registration before stabilizing
	const float MAX_FALL_DISTANCE = 500.0f;      // Max distance horse can fall before we intervene
	const float GROUND_CHECK_DISTANCE = 1000.0f;     // How far down to raycast for ground
	const float STABILIZATION_HEIGHT_OFFSET = 50.0f; // Height above ground to place horse
	const int MAX_PENDING_HORSES = 20;// Max horses to track for stabilization
	
	// ============================================
	// STATE
	// ============================================
	
	struct PendingStabilization
	{
		UInt32 horseFormID;
		NiPoint3 originalPosition;    // Position when registered
		float registrationTime;       // When the horse was registered
		bool isValid;
	};
	
	static PendingStabilization g_pendingStabilizations[MAX_PENDING_HORSES];
	static int g_pendingCount = 0;
	static bool g_stabilizationEnabled = true;
	static bool g_systemInitialized = false;
	static bool g_stabilizationActive = false;  // Only true when we have pending work
	static float g_stabilizationEndTime = 0;    // Auto-disable after this time
	static UInt32 g_lastCellFormID = 0;         // Track last cell to detect cell changes
	
	// ============================================
	// UTILITY FUNCTIONS
	// ============================================
	
	static float GetCurrentTime()
	{
		static auto startTime = clock();
		return (float)(clock() - startTime) / CLOCKS_PER_SEC;
	}
	
	bool IsHorse(Actor* actor)
	{
		if (!actor) return false;
		
		// Check race name/editor ID for "horse"
		TESNPC* actorBase = DYNAMIC_CAST(actor->baseForm, TESForm, TESNPC);
		if (actorBase && actorBase->race.race)
		{
			const char* raceName = actorBase->race.race->fullName.name.data;
			if (raceName)
			{
				// Case-insensitive check for "horse"
				if (strstr(raceName, "orse") || strstr(raceName, "ORSE"))
					return true;
			}
			
			// Also check editor ID
			const char* editorID = actorBase->race.race->editorId.data;
			if (editorID)
			{
				if (strstr(editorID, "orse") || strstr(editorID, "ORSE") || 
				    strstr(editorID, "Horse") || strstr(editorID, "horse"))
					return true;
			}
		}
		
		return false;
	}
	
	// ============================================
	// GROUND DETECTION
	// ============================================
	
	static float GetGroundHeight(float x, float y, float currentZ)
	{
		// For now, return a reasonable estimate
		// The engine will naturally settle the horse on ground
		// We just need to prevent extreme falls
		return currentZ - MAX_FALL_DISTANCE;
	}
	
	// ============================================
	// STABILIZATION CORE
	// ============================================
	
	bool StabilizeHorse(Actor* horse)
	{
		if (!horse) return false;
		if (!g_stabilizationEnabled) return false;
		
		// Check if horse is valid and loaded
		if (!horse->loadedState) return false;
		
		// Get current position
		NiPoint3 currentPos = horse->pos;
		
		// Get the ground height estimate
		float groundHeight = GetGroundHeight(currentPos.x, currentPos.y, currentPos.z);
		
		// If horse is way below where it should be, correct it
		if (currentPos.z < groundHeight - MAX_FALL_DISTANCE)
		{
			_MESSAGE("HorseStabilization: Horse %08X fell too far (Z: %.0f, Ground: %.0f) - correcting position",
				horse->formID, currentPos.z, groundHeight);
			
			// Move horse back up to a safe height
			NiPoint3 safePos;
			safePos.x = currentPos.x;
			safePos.y = currentPos.y;
			safePos.z = groundHeight + STABILIZATION_HEIGHT_OFFSET;
			
			// Set the position
			horse->pos = safePos;
			
			// Also update the NiNode position if available
			if (horse->GetNiNode())
			{
				horse->GetNiNode()->m_localTransform.pos = safePos;
			}
			
			_MESSAGE("HorseStabilization: Horse %08X repositioned to Z: %.0f", horse->formID, safePos.z);
			return true;
		}
		
		return false;
	}
	
	// ============================================
	// PENDING STABILIZATION MANAGEMENT
	// ============================================
	
	void RegisterHorseForStabilization(Actor* horse)
	{
		if (!horse) return;
		if (!g_stabilizationEnabled) return;
		if (!IsHorse(horse)) return;
		
		// Check if already registered
		for (int i = 0; i < g_pendingCount; i++)
		{
			if (g_pendingStabilizations[i].isValid && 
				g_pendingStabilizations[i].horseFormID == horse->formID)
			{
				return;  // Already registered
			}
		}
		
		// Find a free slot
		if (g_pendingCount >= MAX_PENDING_HORSES)
		{
			_MESSAGE("HorseStabilization: WARNING - Max pending stabilizations reached, cannot register horse %08X",
				horse->formID);
			return;
		}
		
		// Register the horse
		PendingStabilization* pending = &g_pendingStabilizations[g_pendingCount];
		pending->horseFormID = horse->formID;
		pending->originalPosition = horse->pos;
		pending->registrationTime = GetCurrentTime();
		pending->isValid = true;
		g_pendingCount++;
		
		_MESSAGE("HorseStabilization: Registered horse %08X for stabilization (pos: %.0f, %.0f, %.0f)",
			horse->formID, horse->pos.x, horse->pos.y, horse->pos.z);
	}
	
	void ProcessPendingStabilizations()
	{
		// Quick early exit if not active - costs almost nothing
		if (!g_stabilizationActive) return;
		if (!g_stabilizationEnabled) return;
		if (g_pendingCount == 0)
		{
			// No more pending - deactivate
			g_stabilizationActive = false;
			_MESSAGE("HorseStabilization: All horses processed, deactivating frame updates");
			return;
		}
		
		float currentTime = GetCurrentTime();
		
		// Safety timeout - auto-disable after 10 seconds regardless
		if (currentTime > g_stabilizationEndTime)
		{
			_MESSAGE("HorseStabilization: Timeout reached, deactivating (processed %d remaining)", g_pendingCount);
			ClearPendingStabilizations();
			g_stabilizationActive = false;
			return;
		}
		
		for (int i = 0; i < g_pendingCount; i++)
		{
			PendingStabilization* pending = &g_pendingStabilizations[i];
			if (!pending->isValid) continue;
			
			// Check if enough time has passed
			if ((currentTime - pending->registrationTime) < STABILIZATION_DELAY)
			{
				continue;  // Not ready yet
			}
			
			// Look up the horse
			TESForm* form = LookupFormByID(pending->horseFormID);
			if (!form)
			{
				pending->isValid = false;
				continue;
			}
			
			Actor* horse = DYNAMIC_CAST(form, TESForm, Actor);
			if (!horse)
			{
				pending->isValid = false;
				continue;
			}
			
			// Check if horse fell significantly from original position
			float fallDistance = pending->originalPosition.z - horse->pos.z;
			
			if (fallDistance > MAX_FALL_DISTANCE)
			{
				_MESSAGE("HorseStabilization: Horse %08X fell %.0f units since registration - stabilizing",
					horse->formID, fallDistance);
				
				// Restore to original position (slightly above to let physics settle)
				NiPoint3 safePos;
				safePos.x = pending->originalPosition.x;
				safePos.y = pending->originalPosition.y;
				safePos.z = pending->originalPosition.z + STABILIZATION_HEIGHT_OFFSET;
				
				horse->pos = safePos;
				
				if (horse->GetNiNode())
				{
					horse->GetNiNode()->m_localTransform.pos = safePos;
				}
				
				_MESSAGE("HorseStabilization: Horse %08X restored to safe position (%.0f, %.0f, %.0f)",
					horse->formID, safePos.x, safePos.y, safePos.z);
			}
			
			// Mark as processed
			pending->isValid = false;
		}
		
		// Compact the array (remove invalid entries)
		int writeIdx = 0;
		for (int readIdx = 0; readIdx < g_pendingCount; readIdx++)
		{
			if (g_pendingStabilizations[readIdx].isValid)
			{
				if (writeIdx != readIdx)
				{
					g_pendingStabilizations[writeIdx] = g_pendingStabilizations[readIdx];
				}
				writeIdx++;
			}
		}
		g_pendingCount = writeIdx;
	}
	
	void ClearPendingStabilizations()
	{
		for (int i = 0; i < MAX_PENDING_HORSES; i++)
		{
			g_pendingStabilizations[i].isValid = false;
		}
		g_pendingCount = 0;
		g_stabilizationActive = false;
		_MESSAGE("HorseStabilization: Cleared all pending stabilizations");
	}
	
	// ============================================
	// CELL CHANGE DETECTION
	// ============================================
	
	void CheckCellChangeForStabilization()
	{
		if (!g_stabilizationEnabled) return;
		if (!g_thePlayer || !(*g_thePlayer)) return;
		
		TESObjectCELL* playerCell = (*g_thePlayer)->parentCell;
		if (!playerCell) return;
		
		UInt32 currentCellFormID = playerCell->formID;
		
		// Check if cell changed
		if (currentCellFormID == g_lastCellFormID)
		{
			return;  // Same cell, nothing to do
		}
		
		// Cell changed - update tracking
		g_lastCellFormID = currentCellFormID;
		
		// Skip interior cells
		if (playerCell->unk120 == nullptr)
		{
			return;  // Interior cell, no horses
		}
		
		// New outdoor cell - trigger stabilization
		_MESSAGE("HorseStabilization: Detected outdoor cell change (FormID: %08X) - scanning for horses", currentCellFormID);
		StabilizeAllHorses();
	}
	
	// ============================================
	// STABILIZE ALL HORSES IN LOADED CELLS
	// ============================================
	
	void StabilizeAllHorses()
	{
		if (!g_stabilizationEnabled) return;
		
		// Use the same actor iteration as Helper.cpp
		if (!g_thePlayer || !(*g_thePlayer)) return;
		
		TESObjectCELL* playerCell = (*g_thePlayer)->parentCell;
		if (!playerCell) return;
		
		// Skip interior cells - horses only relevant outdoors
		// Interior cells have null worldspace (unk120)
		if (playerCell->unk120 == nullptr)
		{
			_MESSAGE("HorseStabilization: Interior cell - skipping (horses not relevant)");
			return;
		}
		
		_MESSAGE("HorseStabilization: Scanning outdoor cell for horses to stabilize...");
		
		int horsesFound = 0;
		
		// Iterate through references in the player's cell
		for (UInt32 i = 0; i < playerCell->objectList.count; i++)
		{
			TESObjectREFR* ref = nullptr;
			playerCell->objectList.GetNthItem(i, ref);
			
			if (!ref) continue;
			
			Actor* actor = DYNAMIC_CAST(ref, TESObjectREFR, Actor);
			if (!actor) continue;
			
			if (IsHorse(actor))
			{
				horsesFound++;
				RegisterHorseForStabilization(actor);
			}
		}
		
		// Activate processing if we found horses
		if (horsesFound > 0)
		{
			g_stabilizationActive = true;
			g_stabilizationEndTime = GetCurrentTime() + 10.0f;  // 10 second max runtime
			_MESSAGE("HorseStabilization: Found %d horses, activated for max 10 seconds", horsesFound);
		}
		else
		{
			_MESSAGE("HorseStabilization: No horses found in player cell");
		}
	}
	
	// ============================================
	// INITIALIZATION & CONTROL
	// ============================================
	
	void InitHorseStabilization()
	{
		if (g_systemInitialized) return;
		
		_MESSAGE("HorseStabilization: Initializing horse stabilization system...");
		
		ClearPendingStabilizations();
		
		// Use config setting
		g_stabilizationEnabled = EnableHorseStabilization;
		g_systemInitialized = true;
		
		_MESSAGE("HorseStabilization: System initialized - %s", 
			g_stabilizationEnabled ? "ENABLED" : "DISABLED (via config)");
	}
	
	void SetStabilizationEnabled(bool enabled)
	{
		g_stabilizationEnabled = enabled;
		_MESSAGE("HorseStabilization: System %s", enabled ? "ENABLED" : "DISABLED");
	}
	
	bool IsStabilizationEnabled()
	{
		return g_stabilizationEnabled;
	}
}
