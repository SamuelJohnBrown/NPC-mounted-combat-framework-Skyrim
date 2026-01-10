#pragma once

#include "skse64/GameReferences.h"
#include "skse64/GameThreads.h"
#include "skse64/PapyrusVM.h"
#include "skse64/PluginAPI.h"
#include "higgsinterface001.h"

namespace MountedNPCCombatVR
{
	// External reference to task interface (defined in main.cpp)
	extern SKSETaskInterface* g_task;
	
	// Initialize/Shutdown the special dismount Grab monitoring
	void InitSpecialDismount();
	void InitSpecialDismountSpells();  // Call after DATA LOADED to load spells from ESPs
	void ShutdownSpecialDismount();

	// Struct used to track active grabs
	struct GrabInfo
	{
		UInt32 grabbedFormID; // formID of grabbed reference
		bool isLeftHand;
		bool isMount;       // true = grabbed the mount (horse), false = grabbed the rider
		double startTime;
		bool isValid;
	};

	// Query helpers
	bool IsActorGrabbedByPlayer(UInt32 actorFormID);
	GrabInfo* GetActiveGrabInfo(UInt32 actorFormID);
	
	// Check if a horse is currently grabbed by player (stops movement while grabbed)
	bool IsHorseGrabbedByPlayer(UInt32 horseFormID);
	
	// PushActorAway native function
	typedef void(*_PushActorAway)(VMClassRegistry* registry, UInt32 stackId, TESObjectREFR* akSource, Actor* akActor, float afKnockbackForce);
	extern RelocAddr<_PushActorAway> PushActorAway;
	
	// Task delegate for thread-safe PushActorAway
	// Stores FormIDs instead of raw pointers for safety
	class taskPushActorAway : public TaskDelegate
	{
	public:
		virtual void Run();
		virtual void Dispose();

		taskPushActorAway(UInt32 sourceFormID, UInt32 targetFormID, float afKnockbackForce);
		
		UInt32 m_sourceFormID;
		UInt32 m_targetFormID;
		float m_afKnockbackForce;
	};
}
