#include "CompanionCombat.h"
#include "MountedCombat.h"
#include "FactionData.h"
#include "DynamicPackages.h"
#include "WeaponDetection.h"
#include "NPCProtection.h"
#include "CombatStyles.h"  // For ClearNPCFollowTarget
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
		_MESSAGE("CompanionCombat: Resetting all companion tracking...");
		
		for (int i = 0; i < MAX_TRACKED_COMPANIONS; i++)
		{
			if (g_trackedCompanions[i].isValid)
			{
				// Clear protection and follow packages from companions
				TESForm* form = LookupFormByID(g_trackedCompanions[i].companionFormID);
				if (form)
				{
					Actor* companion = DYNAMIC_CAST(form, TESForm, Actor);
					if (companion)
					{
						RemoveMountedProtection(companion);
						ClearNPCFollowTarget(companion);
					}
				}
				
				// Clear horse packages
				if (g_trackedCompanions[i].mountFormID != 0)
				{
					TESForm* mountForm = LookupFormByID(g_trackedCompanions[i].mountFormID);
					if (mountForm)
					{
						Actor* mount = DYNAMIC_CAST(mountForm, TESForm, Actor);
						if (mount)
						{
							Actor_ClearKeepOffsetFromActor(mount);
							Actor_EvaluatePackage(mount, false, false);
						}
					}
				}
			}
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
						
						const char* name = CALL_MEMBER_FN(companion, GetReferenceName)();
						_MESSAGE("CompanionCombat: Unregistered companion '%s' (%08X)", 
							name ? name : "Unknown", companionFormID);
					}
				}
				
				// Clear mount packages
				if (g_trackedCompanions[i].mountFormID != 0)
				{
					TESForm* mountForm = LookupFormByID(g_trackedCompanions[i].mountFormID);
					if (mountForm)
					{
						Actor* mount = DYNAMIC_CAST(mountForm, TESForm, Actor);
						if (mount)
						{
							Actor_ClearKeepOffsetFromActor(mount);
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
	
	void UpdateMountedCompanionCombat()
	{
		if (!g_companionCombatInitialized) return;
		if (!CompanionCombatEnabled) return;
		if (g_playerIsDead || !g_playerInExterior) return;
		
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
