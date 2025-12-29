#include "NPCProtection.h"
#include <set>
#include <mutex>

namespace MountedNPCCombatVR
{
	// Track which actors have protection applied (by FormID)
	static std::set<UInt32> g_protectedActors;
	static std::mutex g_protectionMutex;  // Thread safety for protection tracking
	
	// Actor Value IDs
	static const UInt32 AV_Mass = 36;
	
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
	
	// Internal function to do the actual work (uses SEH, no C++ objects)
	static bool DoApplyProtection(Actor* actor, UInt32 formID)
	{
		__try
		{
			actor->flags2 |= Actor::kFlag_kNoBleedoutRecovery;
			float originalMass = actor->actorValueOwner.GetBase(AV_Mass);
			actor->actorValueOwner.SetBase(AV_Mass, 1000.0f);
			
			const char* actorName = CALL_MEMBER_FN(actor, GetReferenceName)();
			_MESSAGE("MountedCombat: Applied mounted protection to '%s' (FormID: %08X) - Original mass: %.1f", 
				actorName ? actorName : "Unknown", formID, originalMass);
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
			actor->flags2 &= ~Actor::kFlag_kNoBleedoutRecovery;
			actor->actorValueOwner.SetBase(AV_Mass, 70.0f);
			
			const char* actorName = CALL_MEMBER_FN(actor, GetReferenceName)();
			_MESSAGE("MountedCombat: Removed mounted protection from '%s' (FormID: %08X)", 
				actorName ? actorName : "Unknown", formID);
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
		_MESSAGE("MountedCombat: Cleared all mounted protection tracking");
	}
}
