#include "NPCProtection.h"
#include "Helper.h"
#include <set>
#include <map>
#include <mutex>
#include <ctime>

namespace MountedNPCCombatVR
{
	// Track which actors have protection applied (by FormID)
	static std::set<UInt32> g_protectedActors;
	static std::mutex g_protectionMutex;  // Thread safety for protection tracking
	
	// Track temporary stagger allowance
	struct TemporaryStaggerData
	{
		UInt32 actorFormID;
		float endTime;       // When to restore protection
		bool isValid;
	};
	
	static TemporaryStaggerData g_tempStaggerData[10];
	static int g_tempStaggerCount = 0;
	
	// Actor Value IDs
	static const UInt32 AV_Mass = 36;
	
	// Default mass to restore when protection is removed
	static const float DEFAULT_MASS = 50.0f;
	
	// Protected mass (prevents stagger)
	static const float PROTECTED_MASS = 1000.0f;
	
	// Use shared GetGameTime() from Helper.h instead of local function
	static float GetCurrentTimeSeconds()
	{
		return GetGameTime();
	}
	
	// Safe actor validation before modifying (uses SEH, no C++ objects)
	static bool IsActorSafeToModify(Actor* actor)
	{
		__try
		{
			if (!actor) return false;
			if (actor->formID == 0) return false;
			if (actor->formType != kFormType_Character) return false;
			
			// Try to access a field to verify pointer is valid
			volatile UInt32 testFlags = actor->flags2;
			(void)testFlags;
			
			return true;
		}
		__except(EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}
	
	// Track which NPCs we've already logged protection for to reduce spam
	static UInt32 g_lastProtectionAppliedNPC = 0;
	static UInt32 g_lastProtectionRemovedNPC = 0;
	
	// Internal function to do the actual work (uses SEH, no C++ objects)
	static bool DoApplyProtection(Actor* actor, UInt32 formID)
	{
		__try
		{
			// NOTE: Removed kFlag_kNoBleedoutRecovery - was causing CTD
			// Just set mass for stagger resistance
			float originalMass = actor->actorValueOwner.GetBase(AV_Mass);
			actor->actorValueOwner.SetBase(AV_Mass, PROTECTED_MASS);
			
			// Rate limit logging - only log first apply per NPC
			if (g_lastProtectionAppliedNPC != formID)
			{
				g_lastProtectionAppliedNPC = formID;
				const char* actorName = CALL_MEMBER_FN(actor, GetReferenceName)();
				_MESSAGE("MountedCombat: Applied mounted protection to '%s' (FormID: %08X) - Original mass: %.1f", 
					actorName ? actorName : "Unknown", formID, originalMass);
			}
			return true;
		}
		__except(EXCEPTION_EXECUTE_HANDLER)
		{
			_MESSAGE("NPCProtection: EXCEPTION in DoApplyProtection for FormID %08X", formID);
			return false;
		}
	}
	
	// Internal function to do the actual work (uses SEH, no C++ objects)
	static bool DoRemoveProtection(Actor* actor, UInt32 formID)
	{
		__try
		{
			// NOTE: Removed kFlag_kNoBleedoutRecovery - was causing CTD
			// Just reset mass
			actor->actorValueOwner.SetBase(AV_Mass, DEFAULT_MASS);
			
			// Rate limit logging - only log first remove per NPC
			if (g_lastProtectionRemovedNPC != formID)
			{
				g_lastProtectionRemovedNPC = formID;
				const char* actorName = CALL_MEMBER_FN(actor, GetReferenceName)();
				_MESSAGE("MountedCombat: Removed mounted protection from '%s' (FormID: %08X)", 
					actorName ? actorName : "Unknown", formID);
			}
			return true;
		}
		__except(EXCEPTION_EXECUTE_HANDLER)
		{
			_MESSAGE("NPCProtection: EXCEPTION in DoRemoveProtection for FormID %08X", formID);
			return false;
		}
	}
	
	void ApplyMountedProtection(Actor* actor)
	{
		if (!actor) return;
		
		// Validate actor pointer before doing anything
		if (!IsActorSafeToModify(actor))
		{
			_MESSAGE("NPCProtection: ApplyMountedProtection - actor pointer invalid, skipping");
			return;
		}
		
		UInt32 formID = actor->formID;
		
		std::lock_guard<std::mutex> lock(g_protectionMutex);
		
		// Already protected?
		if (g_protectedActors.find(formID) != g_protectedActors.end())
		{
			return;
		}
		
		if (DoApplyProtection(actor, formID))
		{
			g_protectedActors.insert(formID);
		}
	}
	
	void RemoveMountedProtection(Actor* actor)
	{
		if (!actor) return;
		
		// Validate actor pointer before doing anything
		if (!IsActorSafeToModify(actor))
		{
			_MESSAGE("NPCProtection: RemoveMountedProtection - actor pointer invalid, skipping");
			return;
		}
		
		UInt32 formID = actor->formID;
		
		std::lock_guard<std::mutex> lock(g_protectionMutex);
		
		// Not protected?
		if (g_protectedActors.find(formID) == g_protectedActors.end())
		{
			return;
		}
		
		// Always remove from tracking, even if the actor modification fails
		g_protectedActors.erase(formID);
		DoRemoveProtection(actor, formID);
	}
	
	bool HasMountedProtection(Actor* actor)
	{
		if (!actor) return false;
		
		std::lock_guard<std::mutex> lock(g_protectionMutex);
		return g_protectedActors.find(actor->formID) != g_protectedActors.end();
	}
	
	void ClearAllMountedProtection()
	{
		std::lock_guard<std::mutex> lock(g_protectionMutex);
		g_protectedActors.clear();
		
		// Also clear temporary stagger data
		for (int i = 0; i < 10; i++)
		{
			g_tempStaggerData[i].isValid = false;
		}
		g_tempStaggerCount = 0;
		
		_MESSAGE("MountedCombat: Cleared all mounted protection tracking");
	}
	
	void SetActorMass(Actor* actor, float mass)
	{
		if (!actor) return;
		if (!IsActorSafeToModify(actor)) return;
		
		__try
		{
			actor->actorValueOwner.SetBase(AV_Mass, mass);
		}
		__except(EXCEPTION_EXECUTE_HANDLER)
		{
			_MESSAGE("NPCProtection: EXCEPTION in SetActorMass for FormID %08X", actor->formID);
		}
	}
	
	// ============================================
	// Temporary Stagger Allow System
	// ============================================
	
	// Helper to set mass (SEH protected, no C++ objects)
	static bool DoSetMassForStagger(Actor* actor, UInt32 formID, float mass)
	{
		__try
		{
			actor->actorValueOwner.SetBase(AV_Mass, mass);
			return true;
		}
		__except(EXCEPTION_EXECUTE_HANDLER)
		{
			_MESSAGE("NPCProtection: EXCEPTION setting mass for FormID %08X", formID);
			return false;
		}
	}
	
	void AllowTemporaryStagger(Actor* actor, float duration)
	{
		if (!actor) return;
		if (!IsActorSafeToModify(actor)) return;
		
		UInt32 formID = actor->formID;
		float currentTime = GetCurrentTimeSeconds();
		float endTime = currentTime + duration;
		
		// First, handle array operations under lock
		bool needsNewEntry = true;
		bool shouldSetMass = false;
		
		{
			std::lock_guard<std::mutex> lock(g_protectionMutex);
			
			// Check if already has temp stagger allowed - extend duration
			for (int i = 0; i < g_tempStaggerCount; i++)
			{
				if (g_tempStaggerData[i].isValid && g_tempStaggerData[i].actorFormID == formID)
				{
					g_tempStaggerData[i].endTime = endTime;
					needsNewEntry = false;
					break;
				}
			}
			
			// Add new entry if needed
			if (needsNewEntry && g_tempStaggerCount < 10)
			{
				g_tempStaggerData[g_tempStaggerCount].actorFormID = formID;
				g_tempStaggerData[g_tempStaggerCount].endTime = endTime;
				g_tempStaggerData[g_tempStaggerCount].isValid = true;
				g_tempStaggerCount++;
				shouldSetMass = true;
			}
		}
		
		// Set mass OUTSIDE the lock (SEH-protected)
		if (shouldSetMass)
		{
			if (DoSetMassForStagger(actor, formID, DEFAULT_MASS))
			{
				const char* actorName = CALL_MEMBER_FN(actor, GetReferenceName)();
				_MESSAGE("NPCProtection: Temporarily allowing stagger for '%s' (%08X) for %.1f seconds", 
					actorName ? actorName : "Unknown", formID, duration);
			}
		}
	}
	
	bool HasTemporaryStaggerAllowed(Actor* actor)
	{
		if (!actor) return false;
		
		UInt32 formID = actor->formID;
		
		std::lock_guard<std::mutex> lock(g_protectionMutex);
		
		for (int i = 0; i < g_tempStaggerCount; i++)
		{
			if (g_tempStaggerData[i].isValid && g_tempStaggerData[i].actorFormID == formID)
			{
				return true;
			}
		}
		
		return false;
	}
	
	void UpdateTemporaryStaggerTimers()
	{
		float currentTime = GetCurrentTimeSeconds();
		
		// Collect expired entries first (under lock)
		struct ExpiredEntry
		{
			UInt32 formID;
			bool shouldRestore;
		};
		ExpiredEntry expiredEntries[10];
		int expiredCount = 0;
		
		{
			std::lock_guard<std::mutex> lock(g_protectionMutex);
			
			for (int i = g_tempStaggerCount - 1; i >= 0; i--)
			{
				if (!g_tempStaggerData[i].isValid) continue;
				
				if (currentTime >= g_tempStaggerData[i].endTime)
				{
					UInt32 formID = g_tempStaggerData[i].actorFormID;
					
					// Check if still protected (in the protected set)
					bool shouldRestore = (g_protectedActors.find(formID) != g_protectedActors.end());
					
					// Add to expired list
					if (expiredCount < 10)
					{
						expiredEntries[expiredCount].formID = formID;
						expiredEntries[expiredCount].shouldRestore = shouldRestore;
						expiredCount++;
					}
					
					// Remove from temp stagger list
					g_tempStaggerData[i].isValid = false;
					
					// Compact the array
					for (int j = i; j < g_tempStaggerCount - 1; j++)
					{
						g_tempStaggerData[j] = g_tempStaggerData[j + 1];
					}
					g_tempStaggerCount--;
				}
			}
		}
		
		// Now process expired entries OUTSIDE the lock (SEH-protected)
		for (int i = 0; i < expiredCount; i++)
		{
			UInt32 formID = expiredEntries[i].formID;
			
			if (!expiredEntries[i].shouldRestore) continue;
			
			// Look up actor and restore mass
			TESForm* form = LookupFormByID(formID);
			if (form && form->formType == kFormType_Character)
			{
				Actor* actor = static_cast<Actor*>(form);
				
				if (DoSetMassForStagger(actor, formID, PROTECTED_MASS))
				{
					const char* actorName = CALL_MEMBER_FN(actor, GetReferenceName)();
					_MESSAGE("NPCProtection: Restored stagger protection for '%s' (%08X)", 
						actorName ? actorName : "Unknown", formID);
				}
			}
		}
	}
}
