#include "SpecialDismount.h"
#include "Helper.h"
#include "NPCProtection.h"
#include "MountedCombat.h"
#include "DynamicPackages.h"
#include "AILogging.h"
#include "HorseMountScanner.h"
#include "WeaponDetection.h"
#include "ArrowSystem.h"
#include "SpecialMovesets.h"
#include "CombatStyles.h" // For ClearRangedRoleForRider
#include "MagicCastingSystem.h" // For resetting mage state on dismount
#include "skse64/GameReferences.h"
#include "skse64/GameRTTI.h"
#include "skse64/GameData.h"
#include "skse64/NiNodes.h"
#include "skse64/PapyrusVM.h"
#include <chrono>
#include <thread>
#include <string>

namespace MountedNPCCombatVR
{
	// ===========================================
	// Forward declarations
	// ===========================================
	static bool IsActorMounted(Actor* actor);
	static bool IsActorBeingRidden(Actor* actor);
	static void StopHorseMovementOnGrab(Actor* horse);
	static void RestoreHorseMovementOnRelease(Actor* horse);
	static void TriggerAggressionOnPulledRider(Actor* pulledRider);
	
	// PushActorAway native function address (SKSEVR offset)
	RelocAddr<_PushActorAway> PushActorAway(0x009D0E60);
	
	// SendAssaultAlarm - triggers crime/aggression response from NPC
	// Signature: void Actor_SendAssaultAlarm(UInt64 a1, UInt64 a2, Actor* actor)
	typedef void (*_Actor_SendAssaultAlarm)(UInt64 a1, UInt64 a2, Actor* actor);
	RelocAddr<_Actor_SendAssaultAlarm> Actor_SendAssaultAlarm(0x986530);
	
	// StartCombat - forces an actor into combat with a target
	// From Engine.h, but we may need a different approach
	
	// ============================================
	// CRIME/AGGRESSION CONFIGURATION
	// ============================================
	static const float ALLY_ALERT_RADIUS = 2000.0f;  // How far nearby allies are alerted
	static const int MAX_ALLIES_TO_ALERT = 3;     // Max allies to alert at once
	
	// Task delegate implementation - uses FormIDs for safety
	taskPushActorAway::taskPushActorAway(UInt32 sourceFormID, UInt32 targetFormID, float afKnockbackForce)
	{
		m_sourceFormID = sourceFormID;
		m_targetFormID = targetFormID;
		m_afKnockbackForce = afKnockbackForce;
	}

	void taskPushActorAway::Run()
	{
		TESForm* sourceForm = LookupFormByID(m_sourceFormID);
		TESForm* targetForm = LookupFormByID(m_targetFormID);
		
		if (!sourceForm || !targetForm) return;
		
		TESObjectREFR* source = DYNAMIC_CAST(sourceForm, TESForm, TESObjectREFR);
		Actor* target = DYNAMIC_CAST(targetForm, TESForm, Actor);
		
		if (!source || !target) return;
		if (target->IsDead(1)) return;
		
		PushActorAway((*g_skyrimVM)->GetClassRegistry(), 0, source, target, m_afKnockbackForce);
	}

	void taskPushActorAway::Dispose()
	{
		delete this;
	}
	
	// ============================================
	// Task to trigger aggression on main thread
	// ============================================
	class taskTriggerAggression : public TaskDelegate
	{
	public:
		taskTriggerAggression(UInt32 pulledRiderFormID) : m_pulledRiderFormID(pulledRiderFormID) {}
		
		virtual void Run()
		{
			TESForm* form = LookupFormByID(m_pulledRiderFormID);
			if (!form) return;
			
			Actor* pulledRider = DYNAMIC_CAST(form, TESForm, Actor);
			if (!pulledRider) return;
			
			TriggerAggressionOnPulledRider(pulledRider);
		}
		
		virtual void Dispose() { delete this; }
		
	private:
		UInt32 m_pulledRiderFormID;
	};
	
	// ============================================
	// Task to restore actor from ragdoll state
	// ============================================
	class taskRestoreFromRagdoll : public TaskDelegate
	{
	public:
		taskRestoreFromRagdoll(UInt32 actorFormID) : m_actorFormID(actorFormID) {}
		
		virtual void Run()
		{
			TESForm* form = LookupFormByID(m_actorFormID);
			if (!form) return;
			
			Actor* actor = DYNAMIC_CAST(form, TESForm, Actor);
			if (!actor) return;
			if (actor->IsDead(1)) return;
			
			// Reset mass back to default (50)
			const float DEFAULT_MASS = 50.0f;
			SetActorMass(actor, DEFAULT_MASS);
			
			// Force actor to get up / exit ragdoll by evaluating package
			Actor_EvaluatePackage(actor, false, false);
			
			_MESSAGE("SpecialDismount: Restored actor %08X from ragdoll (mass reset to %.0f)", m_actorFormID, DEFAULT_MASS);
		}
		
		virtual void Dispose() { delete this; }
		
	private:
		UInt32 m_actorFormID;
	};

	static const int MAX_GRABS = 8;
	static GrabInfo g_grabs[MAX_GRABS];
	static int g_grabCount = 0;

	static HiggsPluginAPI::IHiggsInterface001* s_higgs = nullptr;
	
	// Controller Z tracking - INSTANT response
	static const int CONTROLLER_Z_TRACK_INTERVAL_MS = 8;    // ~120fps polling
	static const float PULL_DOWN_THRESHOLD = 15.0f;     // Lower threshold for faster detection
	static const float RAGDOLL_FORCE = 1.0f;      // Very gentle force to prevent floor clipping
	static const int RAGDOLL_DURATION_MS = 1750;   // 1.75 seconds ragdoll duration
	static std::thread g_controllerTrackThread;
	static bool g_trackingActive = false;
	
	// Previous Z position for delta detection
	static float g_lastControllerZ = 0.0f;
	static bool g_hasLastZ = false;

	// Hand node names for VR
	static const char* kLeftHandName = "NPC L Hand [LHnd]";
	static const char* kRightHandName = "NPC R Hand [RHnd]";
	
	// ============================================
	// GRABBED HORSE TRACKING
	// ============================================
	
	struct GrabbedHorseData
	{
		UInt32 horseFormID;
		UInt32 riderFormID;
		UInt32 targetFormID;
		bool wasInCombat;
		bool isValid;
		
		void Reset()
		{
			horseFormID = 0;
			riderFormID = 0;
			targetFormID = 0;
			wasInCombat = false;
			isValid = false;
		}
	};
	
	static const int MAX_GRABBED_HORSES = 4;
	static GrabbedHorseData g_grabbedHorses[MAX_GRABBED_HORSES];
	static int g_grabbedHorseCount = 0;

	static double NowSeconds()
	{
		using namespace std::chrono;
		auto t = high_resolution_clock::now();
		return duration_cast<duration<double>>(t.time_since_epoch()).count();
	}

	// ============================================
	// CRIME/AGGRESSION SYSTEM
	// ============================================
	
	// Check if two actors share a faction (are allies) - simplified version
	// Uses combat state and faction data instead of IsHostileToActor
	static bool AreActorsAllies(Actor* a, Actor* b)
	{
		if (!a || !b) return false;
		
		// Check if they're in combat with each other - if so, not allies
		UInt32 aCombatTarget = a->currentCombatTarget;
		UInt32 bCombatTarget = b->currentCombatTarget;
		
		if (aCombatTarget != 0)
		{
			NiPointer<TESObjectREFR> aTargetRef;
			LookupREFRByHandle(aCombatTarget, aTargetRef);
			if (aTargetRef && aTargetRef->formID == b->formID)
			{
				return false;  // a is fighting b
			}
		}
		
		if (bCombatTarget != 0)
		{
			NiPointer<TESObjectREFR> bTargetRef;
			LookupREFRByHandle(bCombatTarget, bTargetRef);
			if (bTargetRef && bTargetRef->formID == a->formID)
			{
				return false;  // b is fighting a
			}
		}
		
		// If neither is fighting the other, consider them potential allies
		return true;
	}
	
	// Find nearby allies of the pulled rider and alert them
	static void AlertNearbyAllies(Actor* pulledRider, Actor* player)
	{
		if (!pulledRider || !player) return;
		
		const char* riderName = CALL_MEMBER_FN(pulledRider, GetReferenceName)();
		_MESSAGE("SpecialDismount: Alerting nearby allies of '%s' within %.0f units",
			riderName ? riderName : "Rider", ALLY_ALERT_RADIUS);
		
		int alliesAlerted = 0;
		
		// Get current cell
		TESObjectCELL* cell = pulledRider->parentCell;
		if (!cell) return;
		
		// Iterate through actors in the cell
		for (UInt32 i = 0; i < cell->objectList.count && alliesAlerted < MAX_ALLIES_TO_ALERT; i++)
		{
			TESObjectREFR* ref = nullptr;
			cell->objectList.GetNthItem(i, ref);
			
			if (!ref) continue;
			if (ref->formType != kFormType_Character) continue;
			
			Actor* ally = static_cast<Actor*>(ref);
			
			// Skip the pulled rider themselves
			if (ally->formID == pulledRider->formID) continue;
			
			// Skip the player
			if (ally->formID == player->formID) continue;
			
			// Skip dead actors
			if (ally->IsDead(1)) continue;
			
			// Check distance
			float dx = ally->pos.x - pulledRider->pos.x;
			float dy = ally->pos.y - pulledRider->pos.y;
			float distance = sqrt(dx * dx + dy * dy);
			
			if (distance > ALLY_ALERT_RADIUS) continue;
			
			// Check if they're allies (not fighting each other)
			if (!AreActorsAllies(pulledRider, ally)) continue;
			
			const char* allyName = CALL_MEMBER_FN(ally, GetReferenceName)();
			_MESSAGE("SpecialDismount: Alerting ally '%s' (%08X) at distance %.0f",
				allyName ? allyName : "Unknown", ally->formID, distance);
			
			// Send assault alarm - this makes the ally hostile to player
			Actor_SendAssaultAlarm(0, 0, ally);
			
			// Set attack on sight flag
			ally->flags2 |= Actor::kFlag_kAttackOnSight;
			
			// Force AI re-evaluation
			Actor_EvaluatePackage(ally, false, false);
			
			alliesAlerted++;
		}
		
		_MESSAGE("SpecialDismount: Alerted %d nearby allies", alliesAlerted);
	}
	
	// Main function to trigger aggression when rider is pulled off
	static void TriggerAggressionOnPulledRider(Actor* pulledRider)
	{
		if (!pulledRider) return;
		if (!g_thePlayer || !(*g_thePlayer)) return;
		
		Actor* player = *g_thePlayer;
		
		const char* riderName = CALL_MEMBER_FN(pulledRider, GetReferenceName)();
		_MESSAGE("SpecialDismount: ========================================");
		_MESSAGE("SpecialDismount: TRIGGERING AGGRESSION - Pulled rider: '%s' (%08X)",
			riderName ? riderName : "Unknown", pulledRider->formID);
		_MESSAGE("SpecialDismount: ========================================");
		
		// Check if the rider was already in combat with player
		bool wasAlreadyInCombat = pulledRider->IsInCombat();
		_MESSAGE("SpecialDismount: Was already in combat: %s", wasAlreadyInCombat ? "YES" : "NO");
		
		// Send assault alarm on the pulled rider - this triggers crime/aggression
		_MESSAGE("SpecialDismount: Sending assault alarm to pulled rider...");
		Actor_SendAssaultAlarm(0, 0, pulledRider);
		
		// Set attack on sight flag
		pulledRider->flags2 |= Actor::kFlag_kAttackOnSight;
		
		// Force AI re-evaluation to make them attack
		Actor_EvaluatePackage(pulledRider, false, false);
		
		// Alert nearby allies
		AlertNearbyAllies(pulledRider, player);
		
		// Check post-aggression state
		bool isNowInCombat = pulledRider->IsInCombat();
		_MESSAGE("SpecialDismount: Post-aggression: InCombat=%s",
			isNowInCombat ? "YES" : "NO");
		
		_MESSAGE("SpecialDismount: Aggression triggered successfully");
	}

	// ============================================
	// HORSE GRAB MOVEMENT CONTROL
	// ============================================
	
	static GrabbedHorseData* GetGrabbedHorseData(UInt32 horseFormID)
	{
		for (int i = 0; i < MAX_GRABBED_HORSES; i++)
		{
			if (g_grabbedHorses[i].isValid && g_grabbedHorses[i].horseFormID == horseFormID)
			{
				return &g_grabbedHorses[i];
			}
		}
		return nullptr;
	}
	
	static GrabbedHorseData* CreateGrabbedHorseData(UInt32 horseFormID)
	{
		GrabbedHorseData* existing = GetGrabbedHorseData(horseFormID);
		if (existing) return existing;
		
		for (int i = 0; i < MAX_GRABBED_HORSES; i++)
		{
			if (!g_grabbedHorses[i].isValid)
			{
				g_grabbedHorses[i].Reset();
				g_grabbedHorses[i].horseFormID = horseFormID;
				g_grabbedHorses[i].isValid = true;
				g_grabbedHorseCount++;
				return &g_grabbedHorses[i];
			}
		}
		return nullptr;
	}
	
	static void RemoveGrabbedHorseData(UInt32 horseFormID)
	{
		for (int i = 0; i < MAX_GRABBED_HORSES; i++)
		{
			if (g_grabbedHorses[i].isValid && g_grabbedHorses[i].horseFormID == horseFormID)
			{
				g_grabbedHorses[i].Reset();
				g_grabbedHorseCount--;
				return;
			}
		}
	}
	
	static void StopHorseMovementOnGrab(Actor* horse)
	{
		if (!horse) return;
		
		_MESSAGE("SpecialDismount: STOPPING horse %08X", horse->formID);
		
		Actor_ClearKeepOffsetFromActor(horse);
		ClearInjectedPackages(horse);
		Actor_EvaluatePackage(horse, false, false);
		
		GrabbedHorseData* data = CreateGrabbedHorseData(horse->formID);
		if (data)
		{
			NiPointer<Actor> rider;
			if (CALL_MEMBER_FN(horse, GetMountedBy)(rider) && rider)
			{
				data->riderFormID = rider->formID;
				data->wasInCombat = rider->IsInCombat();
				
				UInt32 targetHandle = rider->currentCombatTarget;
				if (targetHandle != 0)
				{
					NiPointer<TESObjectREFR> targetRef;
					LookupREFRByHandle(targetHandle, targetRef);
					if (targetRef)
					{
						data->targetFormID = targetRef->formID;
					}
				}
			}
		}
	}
	
	static void RestoreHorseMovementOnRelease(Actor* horse)
	{
		if (!horse) return;
		
		GrabbedHorseData* data = GetGrabbedHorseData(horse->formID);
		if (!data) return;
		
		_MESSAGE("SpecialDismount: RESTORING horse %08X", horse->formID);
		
		if (data->wasInCombat && data->riderFormID != 0 && data->targetFormID != 0)
		{
			TESForm* riderForm = LookupFormByID(data->riderFormID);
			TESForm* targetForm = LookupFormByID(data->targetFormID);
			
			if (riderForm && targetForm)
			{
				Actor* rider = DYNAMIC_CAST(riderForm, TESForm, Actor);
				Actor* target = DYNAMIC_CAST(targetForm, TESForm, Actor);
				
				if (rider && target && rider->IsInCombat() && !target->IsDead(1))
				{
					UInt32 targetHandle = target->CreateRefHandle();
					if (targetHandle != 0 && targetHandle != *g_invalidRefHandle)
					{
						NiPoint3 offset = {0, -300.0f, 0};
						NiPoint3 offsetAngle = {0, 0, 0};
						Actor_KeepOffsetFromActor(horse, targetHandle, offset, offsetAngle, 1500.0f, 300.0f);
						Actor_EvaluatePackage(horse, false, false);
					}
				}
			}
		}
		
		Actor_EvaluatePackage(horse, false, false);
		RemoveGrabbedHorseData(horse->formID);
	}

	// ============================================
	// INSTANT RAGDOLL with timed recovery
	// ============================================
	static void ApplyInstantRagdoll(Actor* target)
	{
		if (!target) return;
		if (!IsActorMounted(target)) return;
		if (!g_thePlayer || !(*g_thePlayer)) return;
		if (!g_task) return;
		
		_MESSAGE("SpecialDismount: INSTANT RAGDOLL on %08X (force: %.1f, duration: %dms)", 
			target->formID, RAGDOLL_FORCE, RAGDOLL_DURATION_MS);
		
		// Get horse FormID BEFORE the ragdoll (while still mounted)
		UInt32 horseFormID = 0;
		NiPointer<Actor> mount;
		if (CALL_MEMBER_FN(target, GetMount)(mount) && mount)
		{
			horseFormID = mount->formID;
		}
		
		// ============================================
		// CRITICAL: Reset weapon state machine for pulled rider
		// This clears any pending weapon switches or equip states
		// ============================================
		ClearWeaponStateData(target->formID);
		
		// ============================================
		// CRITICAL: Clear any pending bow attack animations
		// Prevents stuck bow draw state after dismount
		// ============================================
		ResetBowAttackState(target->formID);
		
		// ============================================
		// CRITICAL: Clear ranged role assignment for pulled rider
		// They are no longer mounted so can't be in ranged role
		// ============================================
		ClearRangedRoleForRider(target->formID);
		
		// ============================================
		// CRITICAL: Clear mage-related state for pulled rider
		// Prevents lingering spell charge/retreat state while dismounted
		// Use per-mage resets (do not attempt to re-apply follow packages)
		// ============================================
		ResetMageSpellState(target->formID);
		ResetMageCombatMode(target->formID);
		ResetMageRetreat(target->formID);
		
		// ============================================
		// CRITICAL: Clear horse's special movesets
		// Clears charge, stand ground, rapid fire,90-deg turns, etc.
		// ============================================
		if (horseFormID !=0)
		{
			ClearAllMovesetData(horseFormID);
		}
		
		// Queue ragdoll on main thread
		Actor* player = *g_thePlayer;
		g_task->AddTask(new taskPushActorAway(player->formID, target->formID, RAGDOLL_FORCE));
		
		// Queue aggression trigger on main thread (must be done from main thread)
		UInt32 targetFormID = target->formID;
		g_task->AddTask(new taskTriggerAggression(targetFormID));
		
		// Notify scanner that this NPC was dismounted (pulled off by player)
		// This registers them for remount AI tracking
		OnNPCDismounted(targetFormID, horseFormID);
		
		// Schedule recovery after ragdoll duration
		std::thread([targetFormID]() {
			std::this_thread::sleep_for(std::chrono::milliseconds(RAGDOLL_DURATION_MS));
			
			if (g_task)
			{
				g_task->AddTask(new taskRestoreFromRagdoll(targetFormID));
			}
		}).detach();
	}

	// ============================================
	// Node finding
	// ============================================
	static NiAVObject* FindNodeByName(NiAVObject* root, const char* name)
	{
		if (!root || !name) return nullptr;
		
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

	static NiAVObject* GetVRControllerNode(bool isLeft)
	{
		if (!g_thePlayer || !(*g_thePlayer)) return nullptr;
		
		Actor* player = *g_thePlayer;
		NiNode* root = player->GetNiNode();
		if (!root) return nullptr;
		
		const char* nodeName = isLeft ? kLeftHandName : kRightHandName;
		return FindNodeByName(root, nodeName);
	}

	static float GetControllerWorldZ(bool isLeft)
	{
		NiAVObject* node = GetVRControllerNode(isLeft);
		if (node)
		{
			return node->m_worldTransform.pos.z;
		}
		return 0.0f;
	}

	// ============================================
	// Controller tracking thread - FAST polling
	// ============================================
	static void ControllerTrackingThread()
	{
		g_hasLastZ = false;
		g_lastControllerZ = 0.0f;
		
		while (g_trackingActive)
		{
			for (int i = 0; i < g_grabCount; i++)
			{
				if (g_grabs[i].isValid && !g_grabs[i].isMount)
				{
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
					
					if (!IsActorMounted(grabbedActor))
					{
						g_grabs[i].isValid = false;
						continue;
					}
					
					float currentZ = GetControllerWorldZ(g_grabs[i].isLeftHand);
					
					if (g_hasLastZ)
					{
						float deltaZ = currentZ - g_lastControllerZ;
						
						// Pull detected!
						if (deltaZ < -PULL_DOWN_THRESHOLD)
						{
							_MESSAGE("SpecialDismount: [PULL] DOWN %.1f units", -deltaZ);
							
							// INSTANT ragdoll with timed recovery AND aggression trigger
							ApplyInstantRagdoll(grabbedActor);
							
							g_grabs[i].isValid = false;
						}
					}
					
					g_lastControllerZ = currentZ;
					g_hasLastZ = true;
				}
			}
			
			std::this_thread::sleep_for(std::chrono::milliseconds(CONTROLLER_Z_TRACK_INTERVAL_MS));
		}
	}

	static void StartControllerTracking()
	{
		if (g_trackingActive) return;
		
		g_trackingActive = true;
		g_hasLastZ = false;
		g_controllerTrackThread = std::thread(ControllerTrackingThread);
	}

	static void StopControllerTracking()
	{
		if (!g_trackingActive) return;
		
		g_trackingActive = false;
		if (g_controllerTrackThread.joinable())
		{
			g_controllerTrackThread.join();
		}
		g_hasLastZ = false;
	}

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

	static bool IsActorMounted(Actor* actor)
	{
		if (!actor) return false;
		NiPointer<Actor> mount;
		return CALL_MEMBER_FN(actor, GetMount)(mount) && mount;
	}

	static bool IsActorBeingRidden(Actor* actor)
	{
		if (!actor) return false;
		NiPointer<Actor> rider;
		return CALL_MEMBER_FN(actor, GetMountedBy)(rider) && rider;
	}

	static GrabInfo* CreateOrGetGrab(UInt32 formID, bool isLeft, bool isMount)
	{
		for (int i = 0; i < g_grabCount; i++)
		{
			if (g_grabs[i].isValid && g_grabs[i].grabbedFormID == formID)
			{
				g_grabs[i].isLeftHand = isLeft;
				return &g_grabs[i];
			}
		}
		
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
				for (int j = i; j < g_grabCount - 1; j++)
				{
					g_grabs[j] = g_grabs[j + 1];
				}
				g_grabCount--;
				return;
			}
		}
	}

	// ============================================
	// HIGGS Callbacks
	// ============================================
	
	static void HiggsGrabCallback(bool isLeft, TESObjectREFR* grabbed)
	{
		if (!grabbed) return;
		if (grabbed->formType != kFormType_Character) return;
		
		Actor* grabbedActor = DYNAMIC_CAST(grabbed, TESObjectREFR, Actor);
		if (!grabbedActor) return;
		
		bool isMountedRider = IsActorMounted(grabbedActor);
		bool isBeingRidden = IsActorBeingRidden(grabbedActor);
		
		if (!isMountedRider && !isBeingRidden) return;
		
		UInt32 formID = grabbed->formID;
		
		_MESSAGE("SpecialDismount: GRABBED %s %08X", 
			isMountedRider ? "RIDER" : "HORSE", formID);
		
		// HORSE: Stop movement
		if (isBeingRidden)
		{
			StopHorseMovementOnGrab(grabbedActor);
		}
		
		// RIDER: Remove protection and capture initial Z for instant pull detection
		if (isMountedRider)
		{
			RemoveMountedProtection(grabbedActor);
			
			// Capture initial Z position IMMEDIATELY so first pull can be detected
			g_lastControllerZ = GetControllerWorldZ(isLeft);
			g_hasLastZ = true;
		}
		
		CreateOrGetGrab(formID, isLeft, isBeingRidden);
		
		if (isMountedRider)
		{
			StartControllerTracking();
		}
	}

	static void HiggsDroppedCallback(bool isLeft, TESObjectREFR* dropped)
	{
		if (!dropped) return;
		
		UInt32 formID = dropped->formID;
		
		GrabInfo* gi = GetActiveGrabInfo(formID);
		if (!gi) return;
		
		bool wasRider = !gi->isMount;
		bool wasHorse = gi->isMount;
		
		Actor* droppedActor = DYNAMIC_CAST(dropped, TESObjectREFR, Actor);
		
		if (wasHorse && droppedActor)
		{
			RestoreHorseMovementOnRelease(droppedActor);
		}
		
		if (wasRider && droppedActor)
		{
			if (IsActorMounted(droppedActor))
			{
				ApplyMountedProtection(droppedActor);
			}
		}
		
		RemoveGrab(formID);
		
		if (wasRider && !IsAnyRiderGrabbed())
		{
			StopControllerTracking();
		}
	}

	// ============================================
	// Init / Shutdown
	// ============================================
	
	void InitSpecialDismount()
	{
		_MESSAGE("SpecialDismount: Initializing...");
		
		for (int i = 0; i < MAX_GRABBED_HORSES; i++)
		{
			g_grabbedHorses[i].Reset();
		}
		g_grabbedHorseCount = 0;
		
		if (!higgsInterface)
		{
			_MESSAGE("SpecialDismount: HIGGS interface not available");
			return;
		}
		
		s_higgs = higgsInterface;
		s_higgs->AddGrabbedCallback(HiggsGrabCallback);
		s_higgs->AddDroppedCallback(HiggsDroppedCallback);
		
		_MESSAGE("SpecialDismount: Registered with HIGGS");
	}
	
	void InitSpecialDismountSpells()
	{
		// Not using spells anymore - instant ragdoll
	}
	
	void ShutdownSpecialDismount()
	{
		StopControllerTracking();
		
		for (int i = 0; i < g_grabCount; i++)
		{
			g_grabs[i].isValid = false;
		}
		g_grabCount = 0;
		
		for (int i = 0; i < MAX_GRABBED_HORSES; i++)
		{
			g_grabbedHorses[i].Reset();
		}
		g_grabbedHorseCount = 0;
	}

	bool IsActorGrabbedByPlayer(UInt32 actorFormID)
	{
		for (int i = 0; i < g_grabCount; i++)
		{
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
	
	bool IsHorseGrabbedByPlayer(UInt32 horseFormID)
	{
		return GetGrabbedHorseData(horseFormID) != nullptr;
	}
}
