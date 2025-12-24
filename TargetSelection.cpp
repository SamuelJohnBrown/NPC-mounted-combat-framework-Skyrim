#include "TargetSelection.h"
#include "skse64/GameRTTI.h"
#include "skse64/GameData.h"
#include "skse64/GameReferences.h"
#include <cmath>

namespace MountedNPCCombatVR
{
	// ============================================
	// CONFIGURATION
	// ============================================
	
	const float DEFAULT_MAX_COMBAT_RANGE = 4096.0f;  // Max range to consider a target valid
	const float MIN_TARGET_DISTANCE = 50.0f; // Minimum distance (too close = ignore)
	
	// ============================================
	// CORE TARGET FUNCTIONS
	// ============================================
	
	Actor* GetRiderCombatTarget(Actor* rider)
	{
		if (!rider) return nullptr;
		
		// Check if rider is in combat
		if (!rider->IsInCombat()) return nullptr;
		
		// ============================================
		// PLAYER PRIORITY CHECK
		// If player is a valid combat target, ALWAYS use player
		// This ensures mounted NPCs prioritize the player over other targets
		// ============================================
		if (g_thePlayer && (*g_thePlayer))
		{
			Actor* player = *g_thePlayer;
			
			// Check if player is in combat and within range
			if (player->IsInCombat() && IsValidCombatTarget(rider, player))
			{
				// Player is valid target - prioritize them
				return player;
			}
		}
		
		// ============================================
		// FALLBACK: Use stored combat target
		// Only if player is NOT a valid target
		// ============================================
		
		// Get the combat target FormID from the actor
		UInt32 targetFormID = rider->currentCombatTarget;
		
		if (targetFormID == 0)
		{
			return nullptr;
		}
		
		// Look up the target actor
		TESForm* form = LookupFormByID(targetFormID);
		if (!form) return nullptr;
		
		Actor* target = DYNAMIC_CAST(form, TESForm, Actor);
		if (!target) return nullptr;
		
		// Validate the target
		if (!IsValidCombatTarget(rider, target))
		{
			return nullptr;
		}
		
		return target;
	}
	
	bool IsValidCombatTarget(Actor* rider, Actor* target)
	{
		if (!rider || !target) return false;
		
		// Must be alive
		if (!IsTargetAlive(target)) return false;
		
		// Must be loaded (3D present)
		if (!IsTargetLoaded(target)) return false;
		
		// Must be within range
		if (!IsTargetInRange(rider, target)) return false;
		
		// Must be hostile (or at least not friendly)
		// Note: We check this loosely - if in combat, they're probably hostile
		// This allows the system to work even if faction data is complex
		
		return true;
	}
	
	CombatTargetInfo GetCombatTargetInfo(Actor* rider, Actor* target)
	{
		CombatTargetInfo info;
		info.target = target;
		info.type = TargetType::None;
		info.distance = 999999.0f;
		info.isValid = false;
		info.isHostile = false;
		info.isAlive = false;
		info.isLoaded = false;
		
		if (!target) return info;
		
		info.type = GetTargetType(target);
		info.isAlive = IsTargetAlive(target);
		info.isLoaded = IsTargetLoaded(target);
		info.isHostile = rider ? IsTargetHostile(rider, target) : false;
		
		if (rider)
		{
			info.distance = GetDistanceToTarget(rider, target);
			info.isValid = IsValidCombatTarget(rider, target);
		}
		
		return info;
	}
	
	// ============================================
	// TARGET VALIDATION
	// ============================================
	
	bool IsTargetAlive(Actor* target)
	{
		if (!target) return false;
		
		// IsDead(1) returns true if the actor is dead
		if (target->IsDead(1)) return false;
		
		// Check for bleedout state (flags2 has bleedout flag)
		// kFlag_kIsInBleedoutAnimation = 0x00800000
		if (target->flags2 & 0x00800000) return false;
		
		return true;
	}
	
	bool IsTargetLoaded(Actor* target)
	{
		if (!target) return false;
		
		// Check if 3D node is loaded
		if (!target->GetNiNode()) return false;
		
		// Check if process manager exists (AI is active)
		if (!target->processManager) return false;
		
		return true;
	}
	
	bool IsTargetHostile(Actor* rider, Actor* target)
	{
		if (!rider || !target) return false;
		
		// If target is in combat, assume hostility for simplicity
		// The game's combat system already determined they should fight
		if (rider->IsInCombat() && target->IsInCombat())
		{
			return true;
		}
		
		// Check kFlag_kAttackOnSight flag
		if (target->flags2 & Actor::kFlag_kAttackOnSight)
		{
			return true;
		}
		
		// Could add faction-based checks here in the future
		// For now, rely on combat state
		
		return false;
	}
	
	bool IsTargetInRange(Actor* rider, Actor* target, float maxRange)
	{
		if (!rider || !target) return false;
		
		float distance = GetDistanceToTarget(rider, target);
		
		// Too far
		if (distance > maxRange) return false;
		
		// Too close (probably clipping or weird state)
		if (distance < MIN_TARGET_DISTANCE) return false;
		
		return true;
	}
	
	// ============================================
	// TARGET TYPE DETECTION
	// ============================================
	
	TargetType GetTargetType(Actor* target)
	{
		if (!target) return TargetType::None;
		
		if (IsTargetPlayer(target)) return TargetType::Player;
		if (IsTargetNPC(target)) return TargetType::NPC;
		if (IsTargetCreature(target)) return TargetType::Creature;
		
		return TargetType::None;
	}
	
	bool IsTargetPlayer(Actor* target)
	{
		if (!target) return false;
		if (!g_thePlayer || !(*g_thePlayer)) return false;
		
		return target->formID == (*g_thePlayer)->formID;
	}
	
	bool IsTargetNPC(Actor* target)
	{
		if (!target) return false;
		
		// Check if it's a Character (humanoid NPC)
		// Form type 62 = Character
		if (target->formType == kFormType_Character)
		{
			// Make sure it's not the player
			if (!IsTargetPlayer(target))
			{
				return true;
			}
		}
		
		return false;
	}
	
	bool IsTargetCreature(Actor* target)
	{
		if (!target) return false;
		
		// If it's an actor but not a Character and not the player, it's a creature
		// This includes animals, monsters, dragons, etc.
		if (!IsTargetPlayer(target) && !IsTargetNPC(target))
		{
			// It's an Actor but not a Character - must be creature
			return true;
		}
		
		return false;
	}
	
	// ============================================
	// DISTANCE FUNCTIONS
	// ============================================
	
	float GetDistanceToTarget(Actor* rider, Actor* target)
	{
		if (!rider || !target) return 999999.0f;
		
		float dx = rider->pos.x - target->pos.x;
		float dy = rider->pos.y - target->pos.y;
		float dz = rider->pos.z - target->pos.z;
		
		return sqrt(dx * dx + dy * dy + dz * dz);
	}
	
	float GetDistanceToTarget2D(Actor* rider, Actor* target)
	{
		if (!rider || !target) return 999999.0f;
		
		float dx = rider->pos.x - target->pos.x;
		float dy = rider->pos.y - target->pos.y;
		
		return sqrt(dx * dx + dy * dy);
	}
	
	// ============================================
	// TARGET PRIORITY
	// ============================================
	
	float CalculateTargetPriority(Actor* rider, Actor* target)
	{
		if (!rider || !target) return 0.0f;
		
		float priority = 100.0f;  // Base priority
		
		// Distance factor - closer = higher priority
		float distance = GetDistanceToTarget(rider, target);
		if (distance > 0)
		{
			priority += (4096.0f - distance) / 40.96f;  // +100 at 0 distance, +0 at max range
		}
		
		// Player gets priority bonus (they're the main threat usually)
		if (IsTargetPlayer(target))
		{
			priority += 50.0f;
		}
		
		// Target that's actively attacking gets priority
		if (target->IsInCombat())
		{
			priority += 25.0f;
		}
		
		// Wounded targets might be easier to finish off
		// Could add health-based priority here
		
		return priority;
	}
	
	// ============================================
	// LOGGING
	// ============================================
	
	void LogTargetInfo(Actor* rider, Actor* target)
	{
		if (!target)
		{
			_MESSAGE("TargetSelection: No target");
			return;
		}
		
		const char* targetName = CALL_MEMBER_FN(target, GetReferenceName)();
		CombatTargetInfo info = GetCombatTargetInfo(rider, target);
		
		const char* typeStr = "Unknown";
		switch (info.type)
		{
			case TargetType::Player: typeStr = "Player"; break;
			case TargetType::NPC: typeStr = "NPC"; break;
			case TargetType::Creature: typeStr = "Creature"; break;
			default: typeStr = "None"; break;
		}
		
		_MESSAGE("TargetSelection: ========================================");
		_MESSAGE("TargetSelection: Target: '%s' (FormID: %08X)", 
			targetName ? targetName : "Unknown", target->formID);
		_MESSAGE("TargetSelection: Type: %s", typeStr);
		_MESSAGE("TargetSelection: Distance: %.1f units", info.distance);
		_MESSAGE("TargetSelection: Valid: %s | Alive: %s | Loaded: %s | Hostile: %s",
			info.isValid ? "YES" : "NO",
			info.isAlive ? "YES" : "NO",
			info.isLoaded ? "YES" : "NO",
			info.isHostile ? "YES" : "NO");
		_MESSAGE("TargetSelection: ========================================");
	}
}
