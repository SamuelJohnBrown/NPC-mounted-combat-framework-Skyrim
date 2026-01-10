#pragma once

#include "MountedCombat.h"

namespace MountedNPCCombatVR
{
	// ============================================
	// Faction Data and Classification
	// ============================================
	// All faction lists and classification logic
	// for determining NPC combat behavior types.
	// 
	// Function declarations are in MountedCombat.h
	// Implementations are in FactionData.cpp
	// ============================================
	
	// ============================================
	// HOSTILE NPC DETECTION
	// ============================================
	// These functions check if an NPC should be treated
	// as hostile by guards/soldiers (valid follow/attack target)
	// ============================================
	
	// Master hostile check - returns true if guards/soldiers should attack this NPC
	bool IsHostileNPC(Actor* actor);
	
	// Get hostile type name for logging
	const char* GetHostileTypeName(Actor* actor);
	
	// Individual hostile category checks
	bool IsHostileBandit(UInt32 baseFormID);
	bool IsHostileWarlock(UInt32 baseFormID);
	bool IsHostileVampire(UInt32 baseFormID);
	bool IsHostileDwarven(UInt32 baseFormID);
	bool IsHostileGiant(UInt32 baseFormID);
	bool IsHostileHagraven(UInt32 baseFormID);
	bool IsHostileDraugr(UInt32 baseFormID);
	bool IsHostileFalmer(UInt32 baseFormID);
	bool IsHostileChaurus(UInt32 baseFormID);
	bool IsHostileSkeleton(UInt32 baseFormID);
	bool IsHostileDremora(UInt32 baseFormID);
	bool IsHostileWerewolf(UInt32 baseFormID);
	bool IsHostileSpider(UInt32 baseFormID);
	bool IsHostileCreature(UInt32 baseFormID);
	
	// ============================================
	// DRAGON DETECTION (By Race)
	// ============================================
	
	bool IsDragon(Actor* actor);
	
	// ============================================
	// ACTOR HOSTILITY CHECK
	// ============================================
	
	// Check if one actor is hostile to another (combat target, attack on sight, etc.)
	bool IsActorHostileToActor(Actor* actor, Actor* target);
	
	// ============================================
	// MASTER HOSTILE CHECK
	// ============================================
}
