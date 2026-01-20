#include "CompanionCombat.h"
#include "MountedCombat.h"
#include "FactionData.h"
#include "DynamicPackages.h"
#include "WeaponDetection.h"
#include "NPCProtection.h"
#include "CombatStyles.h"  // For ClearNPCFollowTarget
#include "ArrowSystem.h"  // For ResetBowAttackState
#include "SpecialMovesets.h"  // For ClearAllMovesetData

#include "Helper.h"  // For GetGameTime
#include "config.h"
#include "skse64/GameRTTI.h"
#include <cmath>

namespace MountedNPCCombatVR
{
	// ============================================
	// SYSTEM STATE
	// ============================================
	
	static bool g_companionCombatInitialized = false;
	static MountedCompanionData g_trackedCompanions[MAX_TRACKED_COMPANIONS];
	static int g_trackedCompanionCount = 0;
	
	// Scan interval tracking - declared early so ResetCompanionCombat can use it
	static float g_lastCompanionScanTime = 0;
	const float COMPANION_SCAN_INTERVAL = 2.0f;  // Scan every 2 seconds
	
	// ============================================
	// INITIALIZATION
	// ============================================
	
	void InitCompanionCombat()
	{
		if (g_companionCombatInitialized) return;
		
		_MESSAGE("CompanionCombat: Initializing mounted companion combat system...");
		_MESSAGE("CompanionCombat: CompanionCombatEnabled = %s", CompanionCombatEnabled ? "TRUE" : "FALSE");
		
		for (int i = 0; i < MAX_TRACKED_COMPANIONS; i++)
		{
			g_trackedCompanions[i].Reset();
		}
		g_trackedCompanionCount = 0;
		
		g_companionCombatInitialized = true;
		_MESSAGE("CompanionCombat: System initialized (max %d companions, config limit: %d)", 
			MAX_TRACKED_COMPANIONS, MaxTrackedCompanions);
	}
	
	void ShutdownCompanionCombat()
	{
		if (!g_companionCombatInitialized) return;
		
		_MESSAGE("CompanionCombat: Shutting down...");
		ResetCompanionCombat();
		g_companionCombatInitialized = false;
	}
	
	void ResetCompanionCombat()
	{
		_MESSAGE("CompanionCombat: Resetting all companion tracking (data only - no form lookups)...");
		
		// Reset scan timer so companions are detected fresh
		g_lastCompanionScanTime = 0;
		
		// ============================================
		// CRITICAL: Do NOT call LookupFormByID during reset!
		// During game load/death/transition, forms may be invalid
		// Just clear the tracking data - let game handle actual actor cleanup
		// ============================================
		for (int i = 0; i < MAX_TRACKED_COMPANIONS; i++)
		{
			g_trackedCompanions[i].Reset();
		}
		g_trackedCompanionCount = 0;
		
		_MESSAGE("CompanionCombat: Reset complete");
	}
	
	// ============================================
	// COMPANION DETECTION
	// ============================================
	
	bool IsPlayerTeammate(Actor* actor)
	{
		if (!actor) return false;
		
		// Check the IsPlayerTeammate flag in flags1
		// kFlags_IsPlayerTeammate = 0x4000000
		return (actor->flags1 & Actor::kFlags_IsPlayerTeammate) != 0;
	}
	
	// Check if actor should be treated as a companion
	// Uses both vanilla teammate flag AND configurable name list
	bool IsCompanion(Actor* actor)
	{
		if (!actor) return false;
		
		// First check vanilla teammate flag
		if (IsPlayerTeammate(actor))
		{
			return true;
		}
		
		// Then check against the configurable name list
		const char* actorName = CALL_MEMBER_FN(actor, GetReferenceName)();
		if (actorName && IsInCompanionNameList(actorName))
		{
			return true;
		}
		
		return false;
	}
	
	bool IsMountedCompanion(Actor* actor)
	{
		if (!actor) return false;
		
		// Must be a companion (teammate OR in name list)
		if (!IsCompanion(actor)) return false;
		
		// Must be mounted
		NiPointer<Actor> mount;
		bool isMounted = CALL_MEMBER_FN(actor, GetMount)(mount);
		
		return isMounted && mount;
	}
	
	Actor* GetCompanionMount(Actor* companion)
	{
		if (!companion) return nullptr;
		
		NiPointer<Actor> mount;
		if (CALL_MEMBER_FN(companion, GetMount)(mount) && mount)
		{
			return mount.get();
		}
		return nullptr;
	}
	
	// ============================================
	// COMPANION TRACKING
	// ============================================
	
	MountedCompanionData* RegisterMountedCompanion(Actor* companion, Actor* mount)
	{
		if (!companion || !mount) return nullptr;
		
		// Check if already registered
		for (int i = 0; i < MAX_TRACKED_COMPANIONS; i++)
		{
			if (g_trackedCompanions[i].isValid && 
				g_trackedCompanions[i].companionFormID == companion->formID)
			{
				// Update mount if changed
				g_trackedCompanions[i].mountFormID = mount->formID;
				return &g_trackedCompanions[i];
			}
		}
		
		// Check against config limit
		if (g_trackedCompanionCount >= MaxTrackedCompanions)
		{
			_MESSAGE("CompanionCombat: WARNING - Config limit reached (%d), cannot track new companion", 
				MaxTrackedCompanions);
			return nullptr;
		}
		
		// Find empty slot (limited by config)
		for (int i = 0; i < MaxTrackedCompanions && i < MAX_TRACKED_COMPANIONS; i++)
		{
			if (!g_trackedCompanions[i].isValid)
			{
				g_trackedCompanions[i].companionFormID = companion->formID;
				g_trackedCompanions[i].mountFormID = mount->formID;
				g_trackedCompanions[i].targetFormID = 0;
				g_trackedCompanions[i].lastUpdateTime = 0;
				g_trackedCompanions[i].combatStartTime = 0;
				g_trackedCompanions[i].weaponDrawn = false;
				g_trackedCompanions[i].isValid = true;
				g_trackedCompanionCount++;
				
				LogCompanionDetection(companion, mount);
				
				return &g_trackedCompanions[i];
			}
		}
		
		_MESSAGE("CompanionCombat: WARNING - No empty slots available");
		return nullptr;
	}
	
	void UnregisterMountedCompanion(UInt32 companionFormID)
	{
		for (int i = 0; i < MAX_TRACKED_COMPANIONS; i++)
		{
			if (g_trackedCompanions[i].isValid && 
				g_trackedCompanions[i].companionFormID == companionFormID)
			{
				// Clear protection and packages
				TESForm* form = LookupFormByID(companionFormID);
				if (form)
				{
					Actor* companion = DYNAMIC_CAST(form, TESForm, Actor);
					if (companion)
					{
						RemoveMountedProtection(companion);
						ClearNPCFollowTarget(companion);
						
						// ============================================
						// RESET ALL COMBAT STATE ON DISMOUNT
						// This prevents stuck animations and invalid states
						// SKIP if companion is dead or in bleedout to prevent CTD
						// ============================================
						
						// Only reset combat state if companion is alive and not in bleedout
						bool companionAlive = !companion->IsDead(1);
						bool safeToModify = companionAlive && companion->loadedState;
						
						if (safeToModify)
						{
							// Reset bow attack state (fixes stuck bow draw animation)
							ResetBowAttackState(companion->formID);
							
							// Clear weapon switch tracking (allows clean re-equip)
							ClearWeaponSwitchData(companion->formID);
							
							// NOTE: Removed Actor_IdleStop and DrawSheatheWeapon calls
							// These can cause CTD if actor is in bleedout or invalid state
						}
						
						const char* name = CALL_MEMBER_FN(companion, GetReferenceName)();
						_MESSAGE("CompanionCombat: Unregistered companion '%s' (%08X) - %s", 
							name ? name : "Unknown", companionFormID,
							safeToModify ? "reset combat state" : "skipped reset (dead/bleedout)");
					}
				}
				
				// Clear mount packages and special movesets
				if (g_trackedCompanions[i].mountFormID != 0)
				{
					TESForm* mountForm = LookupFormByID(g_trackedCompanions[i].mountFormID);
					if (mountForm)
					{
						Actor* mount = DYNAMIC_CAST(mountForm, TESForm, Actor);
						if (mount)
						{
							// Clear all special moveset data for the mount
							ClearAllMovesetData(mount->formID);
							
							Actor_ClearKeepOffsetFromActor(mount);
							Actor_EvaluatePackage(mount, false, false);
						}
					}
				}
				
				g_trackedCompanions[i].Reset();
				g_trackedCompanionCount--;
				return;
			}
		}
	}
	
	MountedCompanionData* GetCompanionData(UInt32 companionFormID)
	{
		for (int i = 0; i < MAX_TRACKED_COMPANIONS; i++)
		{
			if (g_trackedCompanions[i].isValid && 
				g_trackedCompanions[i].companionFormID == companionFormID)
			{
				return &g_trackedCompanions[i];
			}
		}
		return nullptr;
	}
	
	int GetMountedCompanionCount()
	{
		return g_trackedCompanionCount;
	}
	
	// ============================================
	// COMPANION TARGET VALIDATION (Friendly Fire Prevention)
	// ============================================
	
	bool IsValidCompanionTarget(Actor* companion, Actor* potentialTarget)
	{
		if (!companion || !potentialTarget) return false;
		
		// Skip dead actors
		if (potentialTarget->IsDead(1)) return false;
		
		// Never target self
		if (potentialTarget->formID == companion->formID) return false;
		
		// Never target the player
		if (g_thePlayer && (*g_thePlayer))
		{
			if (potentialTarget->formID == (*g_thePlayer)->formID) return false;
		}
		
		// Never target other companions (same team)
		if (IsCompanion(potentialTarget))
		{
			return false;
		}
		
		// Never target a companion's mount (horse)
		for (int i = 0; i < MAX_TRACKED_COMPANIONS; i++)
		{
			if (g_trackedCompanions[i].isValid)
			{
				if (g_trackedCompanions[i].mountFormID == potentialTarget->formID)
				{
					return false;
				}
			}
		}
		
		// Also check player's mount
		if (g_thePlayer && (*g_thePlayer))
		{
			NiPointer<Actor> playerMount;
			if (CALL_MEMBER_FN(*g_thePlayer, GetMount)(playerMount) && playerMount)
			{
				if (playerMount->formID == potentialTarget->formID)
				{
					return false;
				}
			}
		}
		
		return true;
	}
	
	// ============================================
	// COMPANION COMBAT UPDATE
	// ============================================
	
	// Scan for new mounted companions to register
	static void ScanForMountedCompanions()
	{
		if (!g_thePlayer || !(*g_thePlayer)) return;
		
		Actor* player = *g_thePlayer;
		TESObjectCELL* cell = player->parentCell;
		if (!cell) return;
		
		// Only scan if player is in combat
		if (!player->IsInCombat()) return;
		
		for (UInt32 i = 0; i < cell->objectList.count; i++)
		{
			TESObjectREFR* ref = nullptr;
			cell->objectList.GetNthItem(i, ref);
			
			if (!ref) continue;
			if (ref->formType != kFormType_Character) continue;
			
			Actor* actor = static_cast<Actor*>(ref);
			
			// Skip player
			if (actor->formID == player->formID) continue;
			
			// Skip dead
			if (actor->IsDead(1)) continue;
			
			// Check if it's a mounted companion
			if (!IsMountedCompanion(actor)) continue;
			
			// Check if already tracked
			if (GetCompanionData(actor->formID) != nullptr) continue;
			
			// Get their mount
			Actor* mount = GetCompanionMount(actor);
			if (!mount) continue;
			
			// Check distance
			float dx = actor->pos.x - player->pos.x;
			float dy = actor->pos.y - player->pos.y;
			float dz = actor->pos.z - player->pos.z;
			float dist = sqrt(dx * dx + dy * dy + dz * dz);
			
			if (dist > CompanionScanRange) continue;
			
			// Register this companion!
			_MESSAGE("CompanionCombat: SCAN DETECTED new mounted companion near player in combat");
			MountedCompanionData* data = RegisterMountedCompanion(actor, mount);
			
			if (data)
			{
				// Find companion's combat target (usually whoever is attacking the player)
				Actor* target = nullptr;
				
				// First check if companion is already in combat with a target
				UInt32 companionCombatHandle = actor->currentCombatTarget;
				if (companionCombatHandle != 0)
				{
					NiPointer<TESObjectREFR> targetRef;
					LookupREFRByHandle(companionCombatHandle, targetRef);
					if (targetRef)
					{
						target = DYNAMIC_CAST(targetRef.get(), TESObjectREFR, Actor);
					}
				}
				
				// If no target, use player's combat target
				if (!target && player->IsInCombat())
				{
					UInt32 playerCombatHandle = player->currentCombatTarget;
					if (playerCombatHandle != 0)
					{
						NiPointer<TESObjectREFR> targetRef;
						LookupREFRByHandle(playerCombatHandle, targetRef);
						if (targetRef)
						{
							target = DYNAMIC_CAST(targetRef.get(), TESObjectREFR, Actor);
						}
					}
				}
				
				// CRITICAL: Never let companions target the player!
				bool targetIsPlayer = (g_thePlayer && (*g_thePlayer) && target == (*g_thePlayer));
				if (targetIsPlayer)
				{
					_MESSAGE("CompanionCombat: WARNING - Blocked companion from targeting PLAYER!");
					target = nullptr;  // Clear invalid target
				}
				
				if (target && !target->IsDead(1))
				{
					// Set up companion combat through CombatStyles (same as all other riders)
					SetNPCFollowTarget(actor, target);
					
					const char* companionName = CALL_MEMBER_FN(actor, GetReferenceName)();
					const char* targetName = CALL_MEMBER_FN(target, GetReferenceName)();
					_MESSAGE("CompanionCombat: Companion '%s' set to follow '%s'",
						companionName ? companionName : "Unknown",
						targetName ? targetName : "Unknown");
				}
			}
		}
	}
	
	void UpdateMountedCompanionCombat()
	{
		if (!g_companionCombatInitialized) return;
		if (!CompanionCombatEnabled) return;
		if (g_playerIsDead || !g_playerInExterior) return;
		
		// Periodic scan for new mounted companions when player is in combat
		float currentTime = GetGameTime();
		if ((currentTime - g_lastCompanionScanTime) >= COMPANION_SCAN_INTERVAL)
		{
			g_lastCompanionScanTime = currentTime;
			ScanForMountedCompanions();
		}
		
		// Monitor each tracked companion for state changes (death, dismount)
		for (int i = 0; i < MAX_TRACKED_COMPANIONS; i++)
		{
			MountedCompanionData* data = &g_trackedCompanions[i];
			if (!data->isValid) continue;
			
			// Look up companion
			TESForm* companionForm = LookupFormByID(data->companionFormID);
			if (!companionForm)
			{
				data->Reset();
				g_trackedCompanionCount--;
				continue;
			}
			
			Actor* companion = DYNAMIC_CAST(companionForm, TESForm, Actor);
			if (!companion)
			{
				data->Reset();
				g_trackedCompanionCount--;
				continue;
			}
			
			// Check if companion died
			if (companion->IsDead(1))
			{
				LogCompanionCombatState(companion, "DIED - removing from tracking");
				UnregisterMountedCompanion(companion->formID);
				continue;
			}
			
			// Check if companion dismounted
			NiPointer<Actor> currentMount;
			bool stillMounted = CALL_MEMBER_FN(companion, GetMount)(currentMount);
			if (!stillMounted || !currentMount || currentMount->formID != data->mountFormID)
			{
				LogCompanionCombatState(companion, "DISMOUNTED - removing from tracking");
				UnregisterMountedCompanion(companion->formID);
				continue;
			}
			
			// Check if no longer a companion (dismissed)
			if (!IsCompanion(companion))
			{
				LogCompanionCombatState(companion, "NO LONGER COMPANION - removing from tracking");
				UnregisterMountedCompanion(companion->formID);
				continue;
			}
		}
	}
	
	// ============================================
	// LOGGING
	// ============================================
	
	void LogCompanionSpells(Actor* companion)
	{
		if (!companion) return;
		
		const char* companionName = CALL_MEMBER_FN(companion, GetReferenceName)();
		
		_MESSAGE("CompanionCombat: === SPELL LIST for '%s' (%08X) ===",
			companionName ? companionName : "Unknown", companion->formID);
		
		// Log added spells
		int addedSpellCount = companion->addedSpells.Length();
		if (addedSpellCount > 0)
		{
			_MESSAGE("CompanionCombat:   [Added Spells: %d]", addedSpellCount);
			for (int i = 0; i < addedSpellCount; i++)
			{
				SpellItem* spell = companion->addedSpells.Get(i);
				if (spell)
				{
					const char* spellName = spell->fullName.name.data;
					_MESSAGE("CompanionCombat:     - %s (FormID: %08X, SpellType: %d)", 
						spellName ? spellName : "Unknown Spell", 
						spell->formID,
						(int)spell->data.spellType);
				}
			}
		}
		else
		{
			_MESSAGE("CompanionCombat:   [Added Spells: None]");
		}
		
		// Log equipped spells
		SpellItem* leftSpell = companion->leftHandSpell;
		SpellItem* rightSpell = companion->rightHandSpell;
		
		if (leftSpell || rightSpell)
		{
			_MESSAGE("CompanionCombat:   [Equipped Spells]");
			if (leftSpell)
			{
				const char* spellName = leftSpell->fullName.name.data;
				_MESSAGE("CompanionCombat:     Left Hand: %s (FormID: %08X)", 
					spellName ? spellName : "Unknown", leftSpell->formID);
			}
			if (rightSpell)
			{
				const char* spellName = rightSpell->fullName.name.data;
				_MESSAGE("CompanionCombat:Right Hand: %s (FormID: %08X)", 
					spellName ? spellName : "Unknown", rightSpell->formID);
			}
		}
		
		// Log equipped shout
		TESForm* equippedShout = companion->equippedShout;
		if (equippedShout)
		{
			_MESSAGE("CompanionCombat:   [Equipped Shout]");
			_MESSAGE("CompanionCombat:     Shout FormID: %08X", equippedShout->formID);
		}
		
		// Try to get base NPC's spell list
		TESNPC* baseNPC = DYNAMIC_CAST(companion->baseForm, TESForm, TESNPC);
		if (baseNPC)
		{
			int baseSpellCount = baseNPC->spellList.GetSpellCount();
			if (baseSpellCount > 0)
			{
				_MESSAGE("CompanionCombat:   [Base NPC Spells: %d]", baseSpellCount);
				for (int i = 0; i < baseSpellCount; i++)
				{
					SpellItem* spell = baseNPC->spellList.GetNthSpell(i);
					if (spell)
					{
						const char* spellName = spell->fullName.name.data;
						_MESSAGE("CompanionCombat:     - %s (FormID: %08X)", 
							spellName ? spellName : "Unknown Spell", spell->formID);
					}
				}
			}
			
			int baseShoutCount = baseNPC->spellList.GetShoutCount();
			if (baseShoutCount > 0)
			{
				_MESSAGE("CompanionCombat:   [Base NPC Shouts: %d]", baseShoutCount);
				for (int i = 0; i < baseShoutCount; i++)
				{
					TESShout* shout = baseNPC->spellList.GetNthShout(i);
					if (shout)
					{
						const char* shoutName = shout->fullName.name.data;
						_MESSAGE("CompanionCombat:     - %s (FormID: %08X)", 
							shoutName ? shoutName : "Unknown Shout", shout->formID);
					}
				}
			}
		}
		
		_MESSAGE("CompanionCombat: === END SPELL LIST ===");
	}
	
	void LogCompanionDetection(Actor* companion, Actor* mount)
	{
		if (!companion || !mount) return;
		
		const char* companionName = CALL_MEMBER_FN(companion, GetReferenceName)();
		const char* mountName = CALL_MEMBER_FN(mount, GetReferenceName)();
		
		// Determine how the companion was detected
		bool byTeammateFlag = IsPlayerTeammate(companion);
		bool byNameList = companionName && IsInCompanionNameList(companionName);
		
		const char* detectionMethod = "Unknown";
		if (byTeammateFlag && byNameList)
		{
			detectionMethod = "TeammateFlag + NameList";
		}
		else if (byTeammateFlag)
		{
			detectionMethod = "TeammateFlag";
		}
		else if (byNameList)
		{
			detectionMethod = "NameList";
		}
		
		_MESSAGE("CompanionCombat: COMPANION DETECTED - '%s' (%08X) on '%s' (%08X) [%s]", 
			companionName ? companionName : "Unknown", companion->formID,
			mountName ? mountName : "Horse", mount->formID,
			detectionMethod);
		
		// Log the companion's spells
		LogCompanionSpells(companion);
	}
	
	void LogCompanionCombatState(Actor* companion, const char* state)
	{
		if (!companion) return;
		
		const char* companionName = CALL_MEMBER_FN(companion, GetReferenceName)();
		_MESSAGE("CompanionCombat: '%s' (%08X) - %s",
			companionName ? companionName : "Companion",
			companion->formID,
			state ? state : "Unknown State");
	}
}
