#include "NPCProtection.h"
#include <set>

namespace MountedNPCCombatVR
{
	// Track which actors have protection applied (by FormID)
	static std::set<UInt32> g_protectedActors;
	
	// Actor Value IDs
	static const UInt32 AV_Mass = 36;
	
	void ApplyMountedProtection(Actor* actor)
	{
		if (!actor) return;
		
		UInt32 formID = actor->formID;
		
		// Already protected?
		if (g_protectedActors.find(formID) != g_protectedActors.end())
		{
			return;
		}
		
		// Set the NoBleedoutRecovery flag to prevent bleedout
		actor->flags2 |= Actor::kFlag_kNoBleedoutRecovery;
		
		// Save original mass, then set high value to prevent stagger
		float originalMass = actor->actorValueOwner.GetBase(AV_Mass);
		actor->actorValueOwner.SetBase(AV_Mass, 1000.0f);
		
		g_protectedActors.insert(formID);
		
		const char* actorName = CALL_MEMBER_FN(actor, GetReferenceName)();
		_MESSAGE("MountedCombat: Applied mounted protection to '%s' (FormID: %08X) - Original mass: %.1f", 
			actorName ? actorName : "Unknown", formID, originalMass);
	}
	
	void RemoveMountedProtection(Actor* actor)
	{
		if (!actor) return;
		
		UInt32 formID = actor->formID;
		
		// Not protected?
		if (g_protectedActors.find(formID) == g_protectedActors.end())
		{
			return;
		}
		
		// Clear the NoBleedoutRecovery flag
		actor->flags2 &= ~Actor::kFlag_kNoBleedoutRecovery;
		
		// Restore reasonable mass (default NPC mass is around 75-100)
		actor->actorValueOwner.SetBase(AV_Mass, 100.0f);
		
		g_protectedActors.erase(formID);
		
		const char* actorName = CALL_MEMBER_FN(actor, GetReferenceName)();
		_MESSAGE("MountedCombat: Removed mounted protection from '%s' (FormID: %08X)", 
			actorName ? actorName : "Unknown", formID);
	}
	
	bool HasMountedProtection(Actor* actor)
	{
		if (!actor) return false;
		return g_protectedActors.find(actor->formID) != g_protectedActors.end();
	}
	
	void ClearAllMountedProtection()
	{
		g_protectedActors.clear();
		_MESSAGE("MountedCombat: Cleared all mounted protection tracking");
	}
}
