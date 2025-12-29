#include "SpecialDismount.h"
#include "Engine.h"
#include "NPCProtection.h"
#include "skse64/GameReferences.h"
#include "skse64/GameRTTI.h"
#include "skse64/GameData.h"
#include "skse64/NiNodes.h"
#include "skse64/PapyrusVM.h"
#include <chrono>
#include <thread>

namespace MountedNPCCombatVR
{
	// ============================================
	// Forward declarations
	// ============================================
	static bool IsActorMounted(Actor* actor);
	static bool IsActorBeingRidden(Actor* actor);
	
	// PushActorAway native function address (SKSEVR offset)
	RelocAddr<_PushActorAway> PushActorAway(0x009D0E60);
	
	// Task delegate implementation - uses FormIDs for safety
	taskPushActorAway::taskPushActorAway(UInt32 sourceFormID, UInt32 targetFormID, float afKnockbackForce)
	{
		m_sourceFormID = sourceFormID;
		m_targetFormID = targetFormID;
		m_afKnockbackForce = afKnockbackForce;
	}

	void taskPushActorAway::Run()
	{
		// Re-lookup actors from FormIDs to ensure they're still valid
		TESForm* sourceForm = LookupFormByID(m_sourceFormID);
		TESForm* targetForm = LookupFormByID(m_targetFormID);
		
		if (!sourceForm || !targetForm)
		{
			_MESSAGE("SpecialDismount: PushActorAway skipped - source or target no longer valid");
			return;
		}
		
		TESObjectREFR* source = DYNAMIC_CAST(sourceForm, TESForm, TESObjectREFR);
		Actor* target = DYNAMIC_CAST(targetForm, TESForm, Actor);
		
		if (!source || !target)
		{
			_MESSAGE("SpecialDismount: PushActorAway skipped - invalid cast");
			return;
		}
		
		// Check if target is still alive
		if (target->IsDead(1))
		{
			_MESSAGE("SpecialDismount: PushActorAway skipped - target is dead");
			return;
		}
		
		PushActorAway((*g_skyrimVM)->GetClassRegistry(), 0, source, target, m_afKnockbackForce);
	}

	void taskPushActorAway::Dispose()
	{
		delete this;
	}

	static const int MAX_GRABS = 8;
	static GrabInfo g_grabs[MAX_GRABS];
	static int g_grabCount = 0;

	static HiggsPluginAPI::IHiggsInterface001* s_higgs = nullptr;
	
	// Controller Z tracking
	static const int CONTROLLER_Z_TRACK_INTERVAL_MS = 50;   // 50ms polling
	static const float PULL_DOWN_THRESHOLD = 30.0f;  // Z drop threshold in units
	static const float RAGDOLL_FORCE = 5.0f;// Knockback force for ragdoll
	static const int RAGDOLL_DELAY_MS = 200;  // Delay after stagger before ragdoll (milliseconds)
	static std::thread g_controllerTrackThread;
	static bool g_trackingActive = false;
	
	// Previous Z position for delta detection
	static float g_lastControllerZ = 0.0f;
	static bool g_hasLastZ = false;

	// Hand node names for VR
	static const char* kLeftHandName = "NPC L Hand [LHnd]";
	static const char* kRightHandName = "NPC R Hand [RHnd]";
	
	// Stagger spell from MountedNPCCombat.esp (ESL-flagged)
	static const char* STAGGER_SPELL_ESP = "MountedNPCCombat.esp";
	static const UInt32 STAGGER_SPELL_BASE_FORMID = 0x08ED;  // Base form ID without ESL prefix
	static SpellItem* g_staggerSpell = nullptr;
	static bool g_staggerSpellInitialized = false;

	static double NowSeconds()
	{
		using namespace std::chrono;
		auto t = high_resolution_clock::now();
		return duration_cast<duration<double>>(t.time_since_epoch()).count();
	}

	// Initialize stagger spell from ESP - MUST be called after DATA LOADED
	static bool InitStaggerSpell()
	{
		// If already successfully initialized, return true
		if (g_staggerSpell != nullptr) return true;
		
		_MESSAGE("SpecialDismount: Attempting to load stagger spell from %s, base FormID: %08X", 
			STAGGER_SPELL_ESP, STAGGER_SPELL_BASE_FORMID);
		
		UInt32 fullFormID = GetFullFormIdMine(STAGGER_SPELL_ESP, STAGGER_SPELL_BASE_FORMID);
		_MESSAGE("SpecialDismount: GetFullFormIdMine returned: %08X", fullFormID);
		
		if (fullFormID == 0)
		{
			_MESSAGE("SpecialDismount: Failed to get full form ID for stagger spell from %s", STAGGER_SPELL_ESP);
			return false;
		}
		
		TESForm* form = LookupFormByID(fullFormID);
		if (!form)
		{
			_MESSAGE("SpecialDismount: Failed to lookup stagger spell form (FormID: %08X)", fullFormID);
			return false;
		}
		
		_MESSAGE("SpecialDismount: Found form, type: %d", form->formType);
		
		g_staggerSpell = DYNAMIC_CAST(form, TESForm, SpellItem);
		if (!g_staggerSpell)
		{
			_MESSAGE("SpecialDismount: Form %08X is not a SpellItem (formType: %d)", fullFormID, form->formType);
			return false;
		}
		
		_MESSAGE("SpecialDismount: Successfully loaded stagger spell from %s (FormID: %08X)", STAGGER_SPELL_ESP, fullFormID);
		g_staggerSpellInitialized = true;
		return true;
	}

	// Apply ragdoll (PushActorAway) to an actor - queued via task for thread safety
	static void ApplyRagdollToActor(Actor* target, float force)
	{
		if (!target) return;
		if (!g_thePlayer || !(*g_thePlayer)) return;
		if (!g_task) return;
		
		Actor* player = *g_thePlayer;
		
		const char* targetName = CALL_MEMBER_FN(target, GetReferenceName)();
		_MESSAGE("SpecialDismount: Applying ragdoll (force: %.1f) to '%s' (FormID: %08X)", 
			force, targetName ? targetName : "Unknown", target->formID);
		
		// Queue the push via task interface for thread safety
		// Pass FormIDs instead of raw pointers for safety
		g_task->AddTask(new taskPushActorAway(player->formID, target->formID, force));
	}

	// Apply stagger spell to a mounted NPC, then ragdoll after a short delay
	static void ApplyStaggerAndRagdollToActor(Actor* target)
	{
		if (!target) return;
		
		// CRITICAL: Only apply ragdoll if the target is STILL MOUNTED
		// Don't ragdoll them repeatedly after they've been dismounted
		if (!IsActorMounted(target))
		{
			_MESSAGE("SpecialDismount: Target is no longer mounted - skipping ragdoll");
			return;
		}
		
		const char* targetName = CALL_MEMBER_FN(target, GetReferenceName)();
		_MESSAGE("SpecialDismount: Applying ragdoll to '%s' (FormID: %08X)", 
			targetName ? targetName : "Unknown", target->formID);
		
		// Note: Stagger spell casting via ActorMagicCaster->CastSpellImmediate is not available
		// in this SKSE version. Just apply ragdoll directly instead.
		
		// Apply ragdoll immediately
		ApplyRagdollToActor(target, RAGDOLL_FORCE);
	}

	// Recursive node search by name
	static NiAVObject* FindNodeByName(NiAVObject* root, const char* name)
	{
		if (!root || !name) return nullptr;
		
		// Compare exact name (case-insensitive)
		if (root->m_name && _stricmp(root->m_name, name) == 0)
			return root;
		
		NiNode* node = root->GetAsNiNode();
		if (!node) return nullptr;
		
		for (UInt32 i = 0; i < node->m_children.m_size; ++i)
		{
			NiAVObject* child = node->m_children.m_data[i];
			if (!child) continue;
			if (NiAVObject* found = FindNodeByName(child, name))
				return found;
		}
		return nullptr;
	}

	// Get VR controller node (left or right hand)
	static NiAVObject* GetVRControllerNode(bool isLeft)
	{
		if (!g_thePlayer || !(*g_thePlayer)) return nullptr;
		
		Actor* player = *g_thePlayer;
		NiNode* root = player->GetNiNode();
		if (!root) return nullptr;
		
		const char* nodeName = isLeft ? kLeftHandName : kRightHandName;
		return FindNodeByName(root, nodeName);
	}

	// Get controller world Z position
	static float GetControllerWorldZ(bool isLeft)
	{
		NiAVObject* node = GetVRControllerNode(isLeft);
		if (node)
		{
			return node->m_worldTransform.pos.z;
		}
		return 0.0f;
	}

	// Controller tracking thread function
	static void ControllerTrackingThread()
	{
		g_hasLastZ = false;
		g_lastControllerZ = 0.0f;
		
		while (g_trackingActive)
		{
			// Check all active rider grabs
			for (int i = 0; i < g_grabCount; i++)
			{
				if (g_grabs[i].isValid && !g_grabs[i].isMount)
				{
					// This is a rider grab - first check if they're still mounted
					TESForm* form = LookupFormByID(g_grabs[i].grabbedFormID);
					if (!form)
					{
						g_grabs[i].isValid = false;
						continue;
					}
					
					Actor* grabbedActor = DYNAMIC_CAST(form, TESForm, Actor);
					if (!grabbedActor)
					{
						g_grabs[i].isValid = false;
						continue;
					}
					
					// If rider is no longer mounted, stop tracking this grab
					if (!IsActorMounted(grabbedActor))
					{
						_MESSAGE("SpecialDismount: Rider %08X dismounted - stopping pull detection", g_grabs[i].grabbedFormID);
						g_grabs[i].isValid = false;
						continue;
					}
					
					// Get controller Z
					float currentZ = GetControllerWorldZ(g_grabs[i].isLeftHand);
					
					if (g_hasLastZ)
					{
						// Calculate delta (negative = pulling down)
						float deltaZ = currentZ - g_lastControllerZ;
						
						// Check if there's a sharp drop (deltaZ is negative and exceeds threshold)
						if (deltaZ < -PULL_DOWN_THRESHOLD)
						{
							_MESSAGE("SpecialDismount: [PULL DETECTED] Rider %08X - %s hand pulled DOWN by %.2f units (Z: %.2f -> %.2f)",
								g_grabs[i].grabbedFormID,
								g_grabs[i].isLeftHand ? "LEFT" : "RIGHT",
								-deltaZ,
								g_lastControllerZ,
								currentZ);
							
							// Apply ragdoll (function will check if still mounted)
							ApplyStaggerAndRagdollToActor(grabbedActor);
							
							// Mark this grab as processed so we don't ragdoll again
							// The rider will either dismount or stay mounted, but we only ragdoll once per grab
							g_grabs[i].isValid = false;
							_MESSAGE("SpecialDismount: Grab processed - will not ragdoll again for this grab");
						}
					}
					
					g_lastControllerZ = currentZ;
					g_hasLastZ = true;
				}
			}
			
			std::this_thread::sleep_for(std::chrono::milliseconds(CONTROLLER_Z_TRACK_INTERVAL_MS));
		}
	}

	// Start tracking controller Z
	static void StartControllerTracking()
	{
		if (g_trackingActive) return;
		
		g_trackingActive = true;
		g_hasLastZ = false;
		g_controllerTrackThread = std::thread(ControllerTrackingThread);
		_MESSAGE("SpecialDismount: Started controller tracking (interval: %dms, threshold: %.0f units)", 
			CONTROLLER_Z_TRACK_INTERVAL_MS, PULL_DOWN_THRESHOLD);
	}

	// Stop tracking controller Z
	static void StopControllerTracking()
	{
		if (!g_trackingActive) return;
		
		g_trackingActive = false;
		if (g_controllerTrackThread.joinable())
		{
			g_controllerTrackThread.join();
		}
		g_hasLastZ = false;
		_MESSAGE("SpecialDismount: Stopped controller tracking");
	}

	// Check if any rider is currently grabbed
	static bool IsAnyRiderGrabbed()
	{
		for (int i = 0; i < g_grabCount; i++)
		{
			if (g_grabs[i].isValid && !g_grabs[i].isMount)
			{
				return true;
			}
		}
		return false;
	}

	// Check if actor is mounted (is a rider on a horse)
	static bool IsActorMounted(Actor* actor)
	{
		if (!actor) return false;
		NiPointer<Actor> mount;
		return CALL_MEMBER_FN(actor, GetMount)(mount) && mount;
	}

	// Check if actor is a mount with a rider (is a horse being ridden)
	static bool IsActorBeingRidden(Actor* actor)
	{
		if (!actor) return false;
		NiPointer<Actor> rider;
		return CALL_MEMBER_FN(actor, GetMountedBy)(rider) && rider;
	}

	static GrabInfo* CreateOrGetGrab(UInt32 formID, bool isLeft, bool isMount)
	{
		// Check if already tracked
		for (int i = 0; i < g_grabCount; i++)
		{
			if (g_grabs[i].isValid && g_grabs[i].grabbedFormID == formID)
			{
				g_grabs[i].isLeftHand = isLeft;
				return &g_grabs[i];
			}
		}
		
		// Create new entry
		if (g_grabCount < MAX_GRABS)
		{
			GrabInfo& g = g_grabs[g_grabCount++];
			g.grabbedFormID = formID;
			g.isLeftHand = isLeft;
			g.isMount = isMount;
			g.startTime = NowSeconds();
			g.isValid = true;
			return &g;
		}
		return nullptr;
	}

	static void RemoveGrab(UInt32 formID)
	{
		for (int i = 0; i < g_grabCount; i++)
		{
			if (g_grabs[i].isValid && g_grabs[i].grabbedFormID == formID)
			{
				g_grabs[i].isValid = false;
				// Compact array
				for (int j = i; j < g_grabCount - 1; j++)
				{
					g_grabs[j] = g_grabs[j + 1];
				}
				g_grabCount--;
				return;
			}
		}
	}

	// HIGGS Grabbed Callback
	static void HiggsGrabCallback(bool isLeft, TESObjectREFR* grabbed)
	{
		if (!grabbed) return;
		
		// Only care about Actors
		if (grabbed->formType != kFormType_Character) return;
		
		Actor* grabbedActor = DYNAMIC_CAST(grabbed, TESObjectREFR, Actor);
		if (!grabbedActor) return;
		
		// Check if this is a mounted rider OR a mount being ridden
		bool isMountedRider = IsActorMounted(grabbedActor);
		bool isBeingRidden = IsActorBeingRidden(grabbedActor);
		
		// Only track if part of a mounted pair
		if (!isMountedRider && !isBeingRidden) return;
		
		UInt32 formID = grabbed->formID;
		const char* actorName = CALL_MEMBER_FN(grabbedActor, GetReferenceName)();
		const char* handStr = isLeft ? "LEFT" : "RIGHT";
		const char* typeStr = isMountedRider ? "MOUNTED RIDER" : "MOUNT (with rider)";
		
		_MESSAGE("SpecialDismount: ========================================");
		_MESSAGE("SpecialDismount: PLAYER GRABBED %s", typeStr);
		_MESSAGE("SpecialDismount: Actor: '%s' (FormID: %08X)", actorName ? actorName : "Unknown", formID);
		_MESSAGE("SpecialDismount: Hand: %s", handStr);
		
		// If this is a rider, remove their protection so they can be staggered/dismounted
		if (isMountedRider)
		{
			_MESSAGE("SpecialDismount: Removing stagger/bleedout/dismount protection from rider");
			RemoveMountedProtection(grabbedActor);
		}
		
		_MESSAGE("SpecialDismount: ========================================");
		
		CreateOrGetGrab(formID, isLeft, isBeingRidden);
		
		// If rider grabbed, start tracking controller Z
		if (isMountedRider)
		{
			StartControllerTracking();
		}
	}

	// HIGGS Dropped Callback
	static void HiggsDroppedCallback(bool isLeft, TESObjectREFR* dropped)
	{
		if (!dropped) return;
		
		UInt32 formID = dropped->formID;
		
		// Check if we were tracking this grab
		GrabInfo* gi = GetActiveGrabInfo(formID);
		if (!gi) return;
		
		bool wasRider = !gi->isMount;
		
		double now = NowSeconds();
		double duration = now - gi->startTime;
		
		const char* handStr = gi->isLeftHand ? "LEFT" : "RIGHT";
		const char* typeStr = gi->isMount ? "MOUNT" : "MOUNTED RIDER";
		
		// Get actor
		Actor* droppedActor = DYNAMIC_CAST(dropped, TESObjectREFR, Actor);
		const char* actorName = "Unknown";
		if (droppedActor)
		{
			actorName = CALL_MEMBER_FN(droppedActor, GetReferenceName)();
		}
		
		_MESSAGE("SpecialDismount: ========================================");
		_MESSAGE("SpecialDismount: PLAYER RELEASED %s", typeStr);
		_MESSAGE("SpecialDismount: Actor: '%s' (FormID: %08X)", actorName ? actorName : "Unknown", formID);
		_MESSAGE("SpecialDismount: Hand: %s", handStr);
		_MESSAGE("SpecialDismount: Grab Duration: %.2f seconds", duration);
		
		// If this was a rider and they're still mounted, restore their protection
		if (wasRider && droppedActor)
		{
			if (IsActorMounted(droppedActor))
			{
				_MESSAGE("SpecialDismount: Rider still mounted - restoring stagger/bleedout/dismount protection");
				ApplyMountedProtection(droppedActor);
			}
			else
			{
				_MESSAGE("SpecialDismount: Rider is no longer mounted - not restoring protection");
			}
		}
		
		_MESSAGE("SpecialDismount: ========================================");
		
		RemoveGrab(formID);
		
		// If no more riders grabbed, stop tracking
		if (wasRider && !IsAnyRiderGrabbed())
		{
			StopControllerTracking();
		}
	}

	void InitSpecialDismount()
	{
		_MESSAGE("SpecialDismount: Initializing...");
		
		// Note: Don't call InitStaggerSpell() here - data isn't loaded yet
		// Call InitSpecialDismountSpells() from main.cpp after kMessage_DataLoaded
		
		if (!higgsInterface)
		{
			_MESSAGE("SpecialDismount: HIGGS interface not available - cannot register callbacks");
			return;
		}
		
		s_higgs = higgsInterface;
		
		// Register callbacks
		s_higgs->AddGrabbedCallback(HiggsGrabCallback);
		s_higgs->AddDroppedCallback(HiggsDroppedCallback);
		
		_MESSAGE("SpecialDismount: Registered with HIGGS (build: %d)", s_higgs->GetBuildNumber());
	}
	
	void InitSpecialDismountSpells()
	{
		_MESSAGE("SpecialDismount: Initializing spells (post DATA LOADED)...");
		InitStaggerSpell();
	}
	
	void ShutdownSpecialDismount()
	{
		StopControllerTracking();
		
		for (int i = 0; i < g_grabCount; i++)
		{
			g_grabs[i].isValid = false;
		}
		g_grabCount = 0;
		
		// Clear cached spell
		g_staggerSpell = nullptr;
		g_staggerSpellInitialized = false;
	}

	bool IsActorGrabbedByPlayer(UInt32 actorFormID)
	{
		for (int i = 0; i < g_grabCount; i++)
		{
			// Only return true for rider grabs (not mount grabs)
			if (g_grabs[i].isValid && g_grabs[i].grabbedFormID == actorFormID && !g_grabs[i].isMount)
			{
				return true;
			}
		}
		return false;
	}

	GrabInfo* GetActiveGrabInfo(UInt32 actorFormID)
	{
		for (int i = 0; i < g_grabCount; i++)
		{
			if (g_grabs[i].isValid && g_grabs[i].grabbedFormID == actorFormID)
			{
				return &g_grabs[i];
			}
		}
		return nullptr;
	}
}
