#include "FactionData.h"
#include <algorithm>
#include <string>

namespace MountedNPCCombatVR
{
	// ============================================
	// Combat Class Name Helper
	// ============================================
	
	const char* GetCombatClassName(MountedCombatClass combatClass)
	{
		switch (combatClass)
		{
			case MountedCombatClass::None: return "None";
			case MountedCombatClass::GuardMelee: return "Guard (Melee/Ranged)";
			case MountedCombatClass::SoldierMelee: return "Soldier (Melee/Ranged)";
			case MountedCombatClass::BanditRanged: return "Bandit (Ranged/Melee)";
			case MountedCombatClass::MageCaster: return "Mage (Caster)";
			case MountedCombatClass::CivilianFlee: return "Civilian (Flee)";
			case MountedCombatClass::Other: return "Other (Unknown Faction - Aggressive)";
			default: return "Unknown";
		}
	}
	
	// ============================================
	// HOSTILE NPC LISTS
	// ============================================
	// These are NPCs that Guards and Soldiers should
	// be hostile towards and will follow/attack.
	// Organized by category for easy maintenance.
	// ============================================
	
	// ============================================
	// BANDIT NPCs (Skyrim.esm - Mod Index 0x00)
	// ============================================
	static const UInt32 HOSTILE_BANDITS[] = {
		// Bandit Base Types
		0x0003DEE4,  // EncBandit02Boss2HNordM
		0x0003DEED,  // EncBandit03Boss2HNordM
		0x0003DEF8,  // EncBandit04Boss2HNordM
		0x0003DF02,  // EncBandit05Boss2HNordM
		0x0003DF0C,  // EncBandit06Boss2HNordM
		
		// Bandit Magic Users
		0x00039D60,  // SubCharBandit02Magic
		0x00039D61,  // SubCharBandit03Magic
		0x00039D62,  // SubCharBandit04Magic
		0x00039D63,  // SubCharBandit05Magic
		0x00039D64,  // SubCharBandit06Magic
	};
	static const int HOSTILE_BANDITS_COUNT = sizeof(HOSTILE_BANDITS) / sizeof(HOSTILE_BANDITS[0]);
	
	// ============================================
	// WARLOCK/NECROMANCER NPCs (Skyrim.esm)
	// ============================================
	static const UInt32 HOSTILE_WARLOCKS[] = {
		// Necromancer Bosses Female
		0x000E1035,  // EncWarlockNecro02BossBretonF
		0x000E1039,  // EncWarlockNecro03BossBretonF
		0x000E103D,  // EncWarlockNecro04BossBretonF
		0x000E1041,  // EncWarlockNecro05BossBretonF
		0x000E1045,  // EncWarlockNecro06BossBretonF
		
		// Necromancer Bosses Male
		0x000E1036,// EncWarlockNecro02BossBretonM
		0x000E103A,  // EncWarlockNecro03BossBretonM
		0x000E103E,  // EncWarlockNecro04BossBretonM
		0x000E1042,  // EncWarlockNecro05BossBretonM
		0x000E1046,  // EncWarlockNecro06BossBretonM
		
		// Storm Warlock Bosses
		0x000E1051,  // EncWarlockStorm02BossBretonF
		0x000E1052,  // EncWarlockStorm02BossBretonM
		0x000E1053,  // EncWarlockStorm02BossHighElfF
		0x000E1054,  // EncWarlockStorm02BossHighElfM
		0x000E1055,  // EncWarlockStorm03BossBretonF
		0x000E1056,  // EncWarlockStorm03BossBretonM
		0x000E1057,  // EncWarlockStorm03BossHighElfF
		0x000E1058,  // EncWarlockStorm03BossHighElfM
		0x000E1059,  // EncWarlockStorm04BossBretonF
		0x000E105A,  // EncWarlockStorm04BossBretonM
		0x000E105B,  // EncWarlockStorm04BossHighElfF
		0x000E105C,  // EncWarlockStorm04BossHighElfM
		0x000E105D,  // EncWarlockStorm05BossBretonF
		0x000E105E,  // EncWarlockStorm05BossBretonM
		0x000E105F,// EncWarlockStorm05BossHighElfF
		0x000E1060,  // EncWarlockStorm05BossHighElfM
		0x000E1061,  // EncWarlockStorm06BossBretonF
		0x000E1062,  // EncWarlockStorm06BossBretonM
		0x000E1063,  // EncWarlockStorm06BossHighElfF
		0x000E1064,  // EncWarlockStorm06BossHighElfM
		
		// Level 07 Warlocks
		0x001091B3,  // EncWarlockFire07HighElfM
		0x001091B4,  // EncWarlockIce07HighElfM
		0x001091B5,  // EncWarlockNecro07HighElfM
		0x001091B6,  // EncWarlockStorm07HighElfM
		0x001091B9,  // EncWarlockFire07BretonF
		0x001091BA,  // EncWarlockIce07BretonF
		0x001091BB,  // EncWarlockNecro07BretonF
		0x001091BC,  // EncWarlockStorm07BretonF
		0x001091BE,  // EncWarlockFire07BossHighElfM
		0x001091BF,  // EncWarlockFire07BossDarkElfF
		0x001091C0,  // EncWarlockIce07BossHighElfM
		0x001091C1,  // EncWarlockIce07BossNordF
		0x001091C4,  // EncWarlockStorm07BossHighElfM
		0x001091C5,  // EncWarlockStorm07BossBretonF
	};
	static const int HOSTILE_WARLOCKS_COUNT = sizeof(HOSTILE_WARLOCKS) / sizeof(HOSTILE_WARLOCKS[0]);
	
	// ============================================
	// VAMPIRE NPCs (Skyrim.esm)
	// ============================================
	static const UInt32 HOSTILE_VAMPIRES[] = {
		0x00107A9B,  // EncVampire00BretonF
		0x00107A9C,  // EncVampire00DarkElfF
		0x00107A9D,  // EncVampire00HighElfF
		0x00107A9E,  // EncVampire00ImperialF
		0x00107A9F,  // EncVampire00NordF
	};
	static const int HOSTILE_VAMPIRES_COUNT = sizeof(HOSTILE_VAMPIRES) / sizeof(HOSTILE_VAMPIRES[0]);
	
	// ============================================
	// DWARVEN AUTOMATONS (Skyrim.esm)
	// ============================================
	static const UInt32 HOSTILE_DWARVEN[] = {
		0x0010F9B9,  // EncDwarvenCenturion01
		0x0010E753,// EncDwarvenCenturion02
		0x00023A96,  // EncDwarvenCenturion03
		0x0010EC86,  // EncDwarvenSpider01
		0x00023A98,  // EncDwarvenSpider02
		0x0010EC87,  // EncDwarvenSpider03
		0x0010EC89,  // EncDwarvenSphere01
		0x00023A97,  // EncDwarvenSphere02
		0x0010EC8E,  // EncDwarvenSphere03
	};
	static const int HOSTILE_DWARVEN_COUNT = sizeof(HOSTILE_DWARVEN) / sizeof(HOSTILE_DWARVEN[0]);
	
	// ============================================
	// GIANTS (Skyrim.esm)
	// ============================================
	static const UInt32 HOSTILE_GIANTS[] = {
		0x00023AAE,  // EncGiant01
		0x00030437,  // EncGiant02
		0x00030438,  // EncGiant03
	};
	static const int HOSTILE_GIANTS_COUNT = sizeof(HOSTILE_GIANTS) / sizeof(HOSTILE_GIANTS[0]);
	
	// ============================================
	// HAGRAVENS (Skyrim.esm)
	// ============================================
	static const UInt32 HOSTILE_HAGRAVENS[] = {
		0x00023AB0,  // EncHagraven
	};
	static const int HOSTILE_HAGRAVENS_COUNT = sizeof(HOSTILE_HAGRAVENS) / sizeof(HOSTILE_HAGRAVENS[0]);
	
	// ============================================
	// DRAUGR (Skyrim.esm)
	// ============================================
	static const UInt32 HOSTILE_DRAUGR[] = {
		// Draugr 01
		0x0002D1DE,  // EncSkeleton01Melee1H (used in draugr lists)
		
		// Draugr 02
		0x0001FE86,  // EncDraugr02Melee1HHeadM00
		0x0001FE87,// EncDraugr02Melee1HHeadM01
		0x0001FE88,  // EncDraugr02Melee1HHeadM02
		0x0001FE89,  // EncDraugr02Melee1HHeadM03
		0x0001FE8A,  // EncDraugr02Melee1HHeadM04
		0x0001FE8B,  // EncDraugr02Melee1HHeadM05
		
		// Draugr 03
		0x00023BC4,  // EncDraugr03Melee1HHeadM00
		0x000388EE,  // EncDraugr03Melee1HHeadM01
		0x000388EF,  // EncDraugr03Melee1HHeadM02
		0x000388E4,  // EncDraugr03Melee1HHeadF00
		
		// Draugr 04
		0x00023BF5,  // EncDraugr04Melee1HHeadM00
		0x00038946,  // EncDraugr04Melee1HHeadM01
		0x00038940,  // EncDraugr04Melee1HHeadF01
		
		// Draugr 05
		0x00023BCB,  // EncDraugr05Melee1HHeadM00
		0x00038A0D,  // EncDraugr05Melee1HHeadM01
		0x0003B543,  // EncDraugr05Melee1HHeadF00
		
		// Draugr 05 Ebony
		0x00038A0B,  // EncDraugr05Melee1HEbonyHeadM01
		0x00038A0C,  // EncDraugr05Melee1HEbonyHeadM02
		0x0003B53F,  // EncDraugr05Melee1HEbonyHeadM00
		0x0003B540,  // EncDraugr05Melee1HEbonyHeadF00
	};
	static const int HOSTILE_DRAUGR_COUNT = sizeof(HOSTILE_DRAUGR) / sizeof(HOSTILE_DRAUGR[0]);
	
	// ============================================
	// FALMER (Skyrim.esm)
	// ============================================
	static const UInt32 HOSTILE_FALMER[] = {
		0x00063224,  // EncFalmer01SpellswordA
		0x00063225,  // EncFalmer01SpellswordB
		0x00063226,  // EncFalmer02Spellsword
		0x00063227,  // EncFalmer03Spellsword
		0x0006322A,  // EncFalmer04Spellsword
		0x0006322B,  // EncFalmer05Spellsword
	};
	static const int HOSTILE_FALMER_COUNT = sizeof(HOSTILE_FALMER) / sizeof(HOSTILE_FALMER[0]);
	
	// ============================================
	// CHAURUS (Skyrim.esm)
	// ============================================
	static const UInt32 HOSTILE_CHAURUS[] = {
		0x000A5600,  // EncChaurus
		0x00023A8F,  // EncChaurusReaper
	};
	static const int HOSTILE_CHAURUS_COUNT = sizeof(HOSTILE_CHAURUS) / sizeof(HOSTILE_CHAURUS[0]);
	
	// ============================================
	// SKELETONS (Skyrim.esm)
	// ============================================
	static const UInt32 HOSTILE_SKELETONS[] = {
		0x0002D1DE,  // EncSkeleton01Melee1H
		0x0002D1E0,  // EncSkeleton01Melee2H
		0x0002D1FC,  // EncSkeleton01Missile
		0x0002D1FD,// EncSkeleton01Melee1Hshield
	};
	static const int HOSTILE_SKELETONS_COUNT = sizeof(HOSTILE_SKELETONS) / sizeof(HOSTILE_SKELETONS[0]);
	
	// ============================================
	// DREMORA (Skyrim.esm)
	// ============================================
	static const UInt32 HOSTILE_DREMORA[] = {
		0x00025D1D,  // EncDremoraWarlock01
		0x00016F04,// EncDremoraWarlock02
		0x00016F69,  // EncDremoraWarlock03
		0x00016FF3,  // EncDremoraWarlock04
		0x00016FF7,  // EncDremoraWarlock05
		0x00016FFA,  // EncDremoraWarlock06
	};
	static const int HOSTILE_DREMORA_COUNT = sizeof(HOSTILE_DREMORA) / sizeof(HOSTILE_DREMORA[0]);
	
	// ============================================
	// WEREWOLVES/WEREBEARS (Skyrim.esm)
	// ============================================
	static const UInt32 HOSTILE_WEREWOLVES[] = {
		0x000A1970,  // EncWerewolf01Boss
		0x000A1971,  // EncWerewolf02Boss
		0x000A1972,  // EncWerewolf03Boss
		0x000A1973,  // EncWerewolf04Boss
		0x000A1974,  // EncWerewolf05Boss
		0x000A1975,  // EncWerewolf05Boss (alt level)
		0x000A1976,  // EncWerewolf06Boss
	};
	static const int HOSTILE_WEREWOLVES_COUNT = sizeof(HOSTILE_WEREWOLVES) / sizeof(HOSTILE_WEREWOLVES[0]);
	
	// ============================================
	// SPIDERS (Skyrim.esm)
	// ============================================
	static const UInt32 HOSTILE_SPIDERS[] = {
		0x00023AAA,  // EncFrostbiteSpider
		0x00041FB4,  // EncFrostbiteSpiderLarge
		0x00023AAB,  // EncFrostbiteSpiderGiant
		0x00023AAC,  // EncFrostbiteSpiderSnow
		0x0004203F,  // EncFrostbiteSpiderSnowLarge
		0x00023AAD,  // EncFrostbiteSpiderSnowGiant
	};
	static const int HOSTILE_SPIDERS_COUNT = sizeof(HOSTILE_SPIDERS) / sizeof(HOSTILE_SPIDERS[0]);
	
	// ============================================
	// HOSTILE CREATURES (Skyrim.esm)
	// ============================================
	static const UInt32 HOSTILE_CREATURES[] = {
		// Wolves
		0x00023ABE,// EncWolf
		0x00023ABF,  // EncWolfIce
		
		// Trolls
		0x00023ABA,  // EncTroll
		0x00023ABB,  // EncTrollFrost
		
		// Bears
		0x00023A8A,  // EncBear
		0x00023A8B,  // EncBearCave
		
		// Sabrecats
		0x00023AB5,// EncSabreCat
		0x00023AB6,  // EncSabreCatSnow
		
		// Spriggans
		0x00023AB9,  // EncSpriggan
		
		// Ice Wraiths
		0x00023AB3,  // EncIceWraith
		
		// Mudcrabs
		0x000E4010,  // EncMudcrabMedium
		0x000E4011,  // EncMudcrabLarge
		0x00021875,  // EncMudcrabGiant
		
		// Spriggan Companions (hostile variants)
		0x000C96C0,  // EncSabreCatSnowSprigganCompanion
		0x000C96C1,  // EncWolfIceSprigganCompanion
		0x000C96C3,  // EncBearCaveSprigganCompanion
		0x000C96C4,  // EncBearSnowSprigganCompanion
	};
	static const int HOSTILE_CREATURES_COUNT = sizeof(HOSTILE_CREATURES) / sizeof(HOSTILE_CREATURES[0]);
	
	// ============================================
	// Check if NPC is in hostile list
	// ============================================
	
	bool IsHostileBandit(UInt32 baseFormID)
	{
		UInt32 baseID = baseFormID & 0x00FFFFFF;
		for (int i = 0; i < HOSTILE_BANDITS_COUNT; i++)
		{
			if (baseID == HOSTILE_BANDITS[i]) return true;
		}
		return false;
	}
	
	bool IsHostileWarlock(UInt32 baseFormID)
	{
		UInt32 baseID = baseFormID & 0x00FFFFFF;
		for (int i = 0; i < HOSTILE_WARLOCKS_COUNT; i++)
		{
			if (baseID == HOSTILE_WARLOCKS[i]) return true;
		}
		return false;
	}
	
	bool IsHostileVampire(UInt32 baseFormID)
	{
		UInt32 baseID = baseFormID & 0x00FFFFFF;
		for (int i = 0; i < HOSTILE_VAMPIRES_COUNT; i++)
		{
			if (baseID == HOSTILE_VAMPIRES[i]) return true;
		}
		return false;
	}
	
	bool IsHostileDwarven(UInt32 baseFormID)
	{
		UInt32 baseID = baseFormID & 0x00FFFFFF;
		for (int i = 0; i < HOSTILE_DWARVEN_COUNT; i++)
		{
			if (baseID == HOSTILE_DWARVEN[i]) return true;
		}
		return false;
	}
	
	bool IsHostileGiant(UInt32 baseFormID)
	{
		UInt32 baseID = baseFormID & 0x00FFFFFF;
		for (int i = 0; i < HOSTILE_GIANTS_COUNT; i++)
		{
			if (baseID == HOSTILE_GIANTS[i]) return true;
		}
		return false;
	}
	
	bool IsHostileHagraven(UInt32 baseFormID)
	{
		UInt32 baseID = baseFormID & 0x00FFFFFF;
		for (int i = 0; i < HOSTILE_HAGRAVENS_COUNT; i++)
		{
			if (baseID == HOSTILE_HAGRAVENS[i]) return true;
		}
		return false;
	}
	
	bool IsHostileDraugr(UInt32 baseFormID)
	{
		UInt32 baseID = baseFormID & 0x00FFFFFF;
		for (int i = 0; i < HOSTILE_DRAUGR_COUNT; i++)
		{
			if (baseID == HOSTILE_DRAUGR[i]) return true;
		}
		return false;
	}
	
	bool IsHostileFalmer(UInt32 baseFormID)
	{
		UInt32 baseID = baseFormID & 0x00FFFFFF;
		for (int i = 0; i < HOSTILE_FALMER_COUNT; i++)
		{
			if (baseID == HOSTILE_FALMER[i]) return true;
		}
		return false;
	}
	
	bool IsHostileChaurus(UInt32 baseFormID)
	{
		UInt32 baseID = baseFormID & 0x00FFFFFF;
		for (int i = 0; i < HOSTILE_CHAURUS_COUNT; i++)
		{
			if (baseID == HOSTILE_CHAURUS[i]) return true;
		}
		return false;
	}
	
	bool IsHostileSkeleton(UInt32 baseFormID)
	{
		UInt32 baseID = baseFormID & 0x00FFFFFF;
		for (int i = 0; i < HOSTILE_SKELETONS_COUNT; i++)
		{
			if (baseID == HOSTILE_SKELETONS[i]) return true;
		}
		return false;
	}
	
	bool IsHostileDremora(UInt32 baseFormID)
	{
		UInt32 baseID = baseFormID & 0x00FFFFFF;
		for (int i = 0; i < HOSTILE_DREMORA_COUNT; i++)
		{
			if (baseID == HOSTILE_DREMORA[i]) return true;
		}
		return false;
	}
	
	bool IsHostileWerewolf(UInt32 baseFormID)
	{
		UInt32 baseID = baseFormID & 0x00FFFFFF;
		for (int i = 0; i < HOSTILE_WEREWOLVES_COUNT; i++)
		{
			if (baseID == HOSTILE_WEREWOLVES[i]) return true;
		}
		return false;
	}
	
	bool IsHostileSpider(UInt32 baseFormID)
	{
		UInt32 baseID = baseFormID & 0x00FFFFFF;
		for (int i = 0; i < HOSTILE_SPIDERS_COUNT; i++)
		{
			if (baseID == HOSTILE_SPIDERS[i]) return true;
		}
		return false;
	}
	
	bool IsHostileCreature(UInt32 baseFormID)
	{
		UInt32 baseID = baseFormID & 0x00FFFFFF;
		for (int i = 0; i < HOSTILE_CREATURES_COUNT; i++)
		{
			if (baseID == HOSTILE_CREATURES[i]) return true;
		}
		return false;
	}
	
	// ============================================
	// MASTER HOSTILE CHECK
	// Returns true if this NPC should be treated as
	// hostile by guards/soldiers (target for follow/attack)
	// ============================================
	
	bool IsHostileNPC(Actor* actor)
	{
		if (!actor) return false;
		
		// ============================================
		// DRAGON CHECK (by race - highest priority)
		// Dragons are always hostile
		// ============================================
		if (IsDragon(actor)) return true;
		
		// Get base form ID for NPC checks
		TESNPC* actorBase = DYNAMIC_CAST(actor->baseForm, TESForm, TESNPC);
		if (!actorBase) 
		{
			// Not a humanoid NPC - could still be a hostile creature
			// Check if it's in combat and hostile
			return false;
		}
		
		UInt32 baseFormID = actorBase->formID;
		
		// Check all hostile categories
		if (IsHostileBandit(baseFormID)) return true;
		if (IsHostileWarlock(baseFormID)) return true;
		if (IsHostileVampire(baseFormID)) return true;
		if (IsHostileDwarven(baseFormID)) return true;
		if (IsHostileGiant(baseFormID)) return true;
		if (IsHostileHagraven(baseFormID)) return true;
		if (IsHostileDraugr(baseFormID)) return true;
		if (IsHostileFalmer(baseFormID)) return true;
		if (IsHostileChaurus(baseFormID)) return true;
		if (IsHostileSkeleton(baseFormID)) return true;
		if (IsHostileDremora(baseFormID)) return true;
		if (IsHostileWerewolf(baseFormID)) return true;
		if (IsHostileSpider(baseFormID)) return true;
		if (IsHostileCreature(baseFormID)) return true;
		
		// Also check faction-based hostility
		if (IsBanditFaction(actor)) return true;
		if (IsMageFaction(actor)) return true;
		
		return false;
	}
	
	// ============================================
	// Get hostile type name (for logging)
	// ============================================
	
	const char* GetHostileTypeName(Actor* actor)
	{
		if (!actor) return "Unknown";
		
		// Check dragon first (by race)
		if (IsDragon(actor)) return "Dragon";
		
		TESNPC* actorBase = DYNAMIC_CAST(actor->baseForm, TESForm, TESNPC);
		if (!actorBase) return "Unknown";
		
		UInt32 baseFormID = actorBase->formID;
		
		if (IsHostileBandit(baseFormID)) return "Bandit";
		if (IsHostileWarlock(baseFormID)) return "Warlock/Necromancer";
		if (IsHostileVampire(baseFormID)) return "Vampire";
		if (IsHostileDwarven(baseFormID)) return "Dwarven Automaton";
		if (IsHostileGiant(baseFormID)) return "Giant";
		if (IsHostileHagraven(baseFormID)) return "Hagraven";
		if (IsHostileDraugr(baseFormID)) return "Draugr";
		if (IsHostileFalmer(baseFormID)) return "Falmer";
		if (IsHostileChaurus(baseFormID)) return "Chaurus";
		if (IsHostileSkeleton(baseFormID)) return "Skeleton";
		if (IsHostileDremora(baseFormID)) return "Dremora";
		if (IsHostileWerewolf(baseFormID)) return "Werewolf";
		if (IsHostileSpider(baseFormID)) return "Frostbite Spider";
		if (IsHostileCreature(baseFormID)) return "Hostile Creature";
		if (IsBanditFaction(actor)) return "Bandit (Faction)";
		if (IsMageFaction(actor)) return "Mage (Faction)";
		
		return "Unknown Hostile";
	}

	// ============================================
	// Forward declaration for LogActorFactions
	// ============================================
	void LogActorFactions(Actor* actor);

	// ============================================
	// Combat Class Determination
	// ============================================
	
	MountedCombatClass DetermineCombatClass(Actor* actor)
	{
		if (!actor)
		{
			return MountedCombatClass::None;
		}
		
		// Log faction info (only at INFO level)
		LogActorFactions(actor);
		
		// Check factions in order of specificity
		if (IsGuardFaction(actor))
		{
			return MountedCombatClass::GuardMelee;
		}
		
		if (IsSoldierFaction(actor))
		{
			return MountedCombatClass::SoldierMelee;
		}
		
		if (IsBanditFaction(actor))
		{
			return MountedCombatClass::BanditRanged;
		}
		
		if (IsMageFaction(actor))
		{
			return MountedCombatClass::MageCaster;
		}
		
		if (IsCivilianFaction(actor))
		{
			return MountedCombatClass::CivilianFlee;
		}
		
		// No faction match - classify as "Other" (aggressive melee)
		return MountedCombatClass::Other;
	}

	// ============================================
	// LOG ALL FACTIONS FOR AN ACTOR
	// Only logs on first detection - controlled by logging level
	// ============================================
	
	void LogActorFactions(Actor* actor)
	{
		if (!actor) return;
		
		// Only log detailed faction info at INFO level (2)
		// At WARN level (1), just log the final classification
		if (logging < 2) return;
		
		const char* actorName = CALL_MEMBER_FN(actor, GetReferenceName)();
		
		_MESSAGE("FactionData: '%s' (%08X) - Scanning factions...", 
			actorName ? actorName : "Unknown", actor->formID);
		
		TESNPC* actorBase = DYNAMIC_CAST(actor->baseForm, TESForm, TESNPC);
		if (!actorBase)
		{
			_MESSAGE("FactionData: ERROR - Could not get actor base form");
			return;
		}
		
		// Log primary faction only
		if (actorBase->faction)
		{
			const char* factionName = actorBase->faction->fullName.name.data;
			_MESSAGE("FactionData:   Primary: '%s' (%08X)", 
				factionName ? factionName : "(unnamed)", actorBase->faction->formID);
		}
		
		// At INFO level, also log matched results (but not all factions)
		_MESSAGE("FactionData:   Guard:%s Soldier:%s Bandit:%s Mage:%s Civilian:%s", 
			IsGuardFaction(actor) ? "Y" : "N",
			IsSoldierFaction(actor) ? "Y" : "N",
			IsBanditFaction(actor) ? "Y" : "N",
			IsMageFaction(actor) ? "Y" : "N",
			IsCivilianFaction(actor) ? "Y" : "N");
	}

	// ============================================
	// Helper: Check if a single faction matches guard criteria
	// ============================================
	static bool IsGuardFactionByFormID(TESFaction* faction)
	{
		if (!faction) return false;
		
		UInt32 factionFormID = faction->formID;
		UInt8 modIndex = (factionFormID >> 24) & 0xFF;
		UInt32 baseFactionID = factionFormID & 0x00FFFFFF;
		
		// Check by name first
		const char* factionName = faction->fullName.name.data;
		if (factionName && strlen(factionName) > 0)
		{
			std::string factionStr = factionName;
			std::transform(factionStr.begin(), factionStr.end(), factionStr.begin(), ::tolower);
			
			if (factionStr.find("guard") != std::string::npos)
			{
				return true;
			}
		}
		
		// Check by FormID (Skyrim.esm)
		if (modIndex == 0x00)
		{
			switch (baseFactionID)
			{
				case 0x00028713:  // GuardDialogueFaction
				case 0x0002BE3B:  // CrimeFactionWhiterun
				case 0x00029DB0:  // CrimeFactionSolitude
				case 0x000267E3:  // CrimeFactionRiften
				case 0x00029DB4:  // CrimeFactionMarkarth
				case 0x0002816D:  // CrimeFactionWindhelm
				case 0x00028849:  // CrimeFactionHaafingar
				case 0x000267EA:  // CrimeFactionFalkreath
				case 0x00028170:  // CrimeFactionPale
				case 0x00028848:  // CrimeFactionWinterhold
				case 0x0002816E:  // CrimeFactionHjaalmarch
					return true;
			}
		}
		
		return false;
	}

	// ============================================
	// Guard Faction Check - NOW CHECKS ALL FACTIONS
	// ============================================
	
	bool IsGuardFaction(Actor* actor)
	{
		if (!actor) return false;
		
		TESNPC* actorBase = DYNAMIC_CAST(actor->baseForm, TESForm, TESNPC);
		if (!actorBase) return false;
		
		// Check primary faction first
		if (actorBase->faction && IsGuardFactionByFormID(actorBase->faction))
		{
			return true;
		}
		
		// Check ALL factions
		for (UInt32 i = 0; i < actorBase->actorData.factions.count; i++)
		{
			TESActorBaseData::FactionInfo factionInfo;
			if (actorBase->actorData.factions.GetNthItem(i, factionInfo))
			{
				if (factionInfo.faction && IsGuardFactionByFormID(factionInfo.faction))
				{
					return true;
				}
			}
		}
		
		return false;
	}

	// ============================================
	// Helper: Check if a single faction matches soldier criteria
	// ============================================
	static bool IsSoldierFactionByFormID(TESFaction* faction)
	{
		if (!faction) return false;
		
		UInt32 factionFormID = faction->formID;
		UInt8 modIndex = (factionFormID >> 24) & 0xFF;
		UInt32 baseFactionID = factionFormID & 0x00FFFFFF;
		
		// Check by name first
		const char* factionName = faction->fullName.name.data;
		if (factionName && strlen(factionName) > 0)
		{
			std::string factionStr = factionName;
			std::transform(factionStr.begin(), factionStr.end(), factionStr.begin(), ::tolower);
			
			if (factionStr.find("soldier") != std::string::npos ||
				factionStr.find("stormcloak") != std::string::npos ||
				factionStr.find("imperial") != std::string::npos ||
				factionStr.find("legion") != std::string::npos)
			{
				return true;
			}
		}
		
		// Check by FormID (Skyrim.esm)
		if (modIndex == 0x00)
		{
			switch (baseFactionID)
			{
				case 0x00028849:  // CWImperialFaction
				case 0x00028848:  // CWSonsFaction (Stormcloaks)
				case 0x0002BF9A:  // CWImperialSoldierFaction
				case 0x0002BF9B:  // CWSonsSoldierFaction
					return true;
			}
		}
		
		return false;
	}

	// ============================================
	// Soldier Faction Check - NOW CHECKS ALL FACTIONS
	// ============================================
	
	bool IsSoldierFaction(Actor* actor)
	{
		if (!actor) return false;
		
		TESNPC* actorBase = DYNAMIC_CAST(actor->baseForm, TESForm, TESNPC);
		if (!actorBase) return false;
		
		// Check primary faction first
		if (actorBase->faction && IsSoldierFactionByFormID(actorBase->faction))
		{
			return true;
		}
		
		// Check ALL factions
		for (UInt32 i = 0; i < actorBase->actorData.factions.count; i++)
		{
			TESActorBaseData::FactionInfo factionInfo;
			if (actorBase->actorData.factions.GetNthItem(i, factionInfo))
			{
				if (factionInfo.faction && IsSoldierFactionByFormID(factionInfo.faction))
				{
					return true;
				}
			}
		}
		
		return false;
	}

	// ============================================
	// Helper: Check if a single faction matches bandit criteria
	// ============================================
	static bool IsBanditFactionByFormID(TESFaction* faction)
	{
		if (!faction) return false;
		
		UInt32 factionFormID = faction->formID;
		UInt8 modIndex = (factionFormID >> 24) & 0xFF;
		UInt32 baseFactionID = factionFormID & 0x00FFFFFF;
		
		// Check by name first
		const char* factionName = faction->fullName.name.data;
		if (factionName && strlen(factionName) > 0)
		{
			std::string factionStr = factionName;
			std::transform(factionStr.begin(), factionStr.end(), factionStr.begin(), ::tolower);
			
			if (factionStr.find("bandit") != std::string::npos ||
				factionStr.find("forsworn") != std::string::npos ||
				factionStr.find("outlaw") != std::string::npos ||
				factionStr.find("thief") != std::string::npos)
			{
				return true;
			}
		}
		
		// Check by FormID (Skyrim.esm)
		if (modIndex == 0x00)
		{
			switch (baseFactionID)
			{
				case 0x0001BCC0:  // BanditFaction
				case 0x00043599:  // ForswornFaction
				case 0x00105C20:  // BanditAllyFaction
					return true;
			}
		}
		
		return false;
	}

	// ============================================
	// Bandit Faction Check - NOW CHECKS ALL FACTIONS
	// ============================================
	
	bool IsBanditFaction(Actor* actor)
	{
		if (!actor) return false;
		
		TESNPC* actorBase = DYNAMIC_CAST(actor->baseForm, TESForm, TESNPC);
		if (!actorBase) return false;
		
		// Check primary faction first
		if (actorBase->faction && IsBanditFactionByFormID(actorBase->faction))
		{
			return true;
		}
		
		// Check ALL factions
		for (UInt32 i = 0; i < actorBase->actorData.factions.count; i++)
		{
			TESActorBaseData::FactionInfo factionInfo;
			if (actorBase->actorData.factions.GetNthItem(i, factionInfo))
			{
				if (factionInfo.faction && IsBanditFactionByFormID(factionInfo.faction))
				{
					return true;
				}
			}
		}
		
		return false;
	}

	// ============================================
	// Helper: Check if a single faction matches mage criteria
	// ============================================
	static bool IsMageFactionByFormID(TESFaction* faction)
	{
		if (!faction) return false;
		
		UInt32 factionFormID = faction->formID;
		UInt8 modIndex = (factionFormID >> 24) & 0xFF;
		UInt32 baseFactionID = factionFormID & 0x00FFFFFF;
		
		// Check by name first
		const char* factionName = faction->fullName.name.data;
		if (factionName && strlen(factionName) > 0)
		{
			std::string factionStr = factionName;
			std::transform(factionStr.begin(), factionStr.end(), factionStr.begin(), ::tolower);
			
			if (factionStr.find("mage") != std::string::npos ||
				factionStr.find("wizard") != std::string::npos ||
				factionStr.find("warlock") != std::string::npos ||
				factionStr.find("necromancer") != std::string::npos ||
				factionStr.find("college") != std::string::npos)
			{
				return true;
			}
		}
		
		// Check by FormID (Skyrim.esm)
		if (modIndex == 0x00)
		{
			switch (baseFactionID)
			{
				case 0x00028848:  // WarlockFaction (placeholder)
				case 0x00039F26:  // CollegeOfWinterholdFaction
					return true;
			}
		}
		
		return false;
	}

	// ============================================
	// Mage Faction Check - NOW CHECKS ALL FACTIONS
	// ============================================
	
	bool IsMageFaction(Actor* actor)
	{
		if (!actor) return false;
		
		TESNPC* actorBase = DYNAMIC_CAST(actor->baseForm, TESForm, TESNPC);
		if (!actorBase) return false;
		
		// Check primary faction first
		if (actorBase->faction && IsMageFactionByFormID(actorBase->faction))
		{
			return true;
		}
		
		// Check ALL factions
		for (UInt32 i = 0; i < actorBase->actorData.factions.count; i++)
		{
			TESActorBaseData::FactionInfo factionInfo;
			if (actorBase->actorData.factions.GetNthItem(i, factionInfo))
			{
				if (factionInfo.faction && IsMageFactionByFormID(factionInfo.faction))
				{
					return true;
				}
			}
		}
		
		return false;
	}

	// ============================================
	// Helper: Check if a single faction matches civilian criteria
	// (Now includes former Hunter faction types)
	// ============================================
	static bool IsCivilianFactionByFormID(TESFaction* faction)
	{
		if (!faction) return false;
		
		UInt32 factionFormID = faction->formID;
		UInt8 modIndex = (factionFormID >> 24) & 0xFF;
		UInt32 baseFactionID = factionFormID & 0x00FFFFFF;
		
		// Check by name first
		const char* factionName = faction->fullName.name.data;
		if (factionName && strlen(factionName) > 0)
		{
			std::string factionStr = factionName;
			std::transform(factionStr.begin(), factionStr.end(), factionStr.begin(), ::tolower);
			
			if (factionStr.find("citizen") != std::string::npos ||
				factionStr.find("civilian") != std::string::npos ||
				factionStr.find("townsfolk") != std::string::npos ||
				factionStr.find("merchant") != std::string::npos ||
				factionStr.find("farmer") != std::string::npos ||
				factionStr.find("hunter") != std::string::npos ||   // Hunters now civilian
				factionStr.find("hircine") != std::string::npos)    // Hunters of Hircine
			{
				return true;
			}
		}
		
		// Check by FormID (Skyrim.esm)
		if (modIndex == 0x00)
		{
			switch (baseFactionID)
			{
				case 0x00013793:  // TownWhiterunFaction
				case 0x00029DB1:  // TownSolitudeFaction  
				case 0x000267E4:  // TownRiftenFaction
				case 0x00029DB5:  // TownMarkarthFaction
				case 0x0002816F:  // TownWindhelmFaction
				case 0x000C6CD4:  // HunterFaction (now civilian)
				case 0x000E68DE:  // WEDL09HunterFaction (now civilian)
				case 0x000E3A01:  // WEBountyHunter (now civilian)
				case 0x000D2B8A:  // DialogueOrcHuntersFaction (now civilian)
				case 0x000DDF44:  // WEServicesHunterFaction (now civilian)
				case 0x000E26F6:  // WE16HunterFaction (now civilian)
				case 0x0002ACE1:  // DA05HuntersOfHircineFaction (now civilian)
					return true;
			}
		}
		
		return false;
	}

	// ============================================
	// Civilian Faction Check - NOW CHECKS ALL FACTIONS
	// ============================================
	
	bool IsCivilianFaction(Actor* actor)
	{
		if (!actor) return false;
		
		TESNPC* actorBase = DYNAMIC_CAST(actor->baseForm, TESForm, TESNPC);
		if (!actorBase) return false;
		
		// IMPORTANT: If actor belongs to a combat faction, they're NOT civilian
		// (This is called last in DetermineCombatClass, but double-check)
		// This prevents a guard captain from being marked civilian just because
		// they also belong to a civilian faction
		
		bool hasCivilianFaction = false;
		
		// Check primary faction
		if (actorBase->faction)
		{
			if (IsCivilianFactionByFormID(actorBase->faction))
			{
				hasCivilianFaction = true;
			}
		}
		
		// Check ALL factions
		for (UInt32 i = 0; i < actorBase->actorData.factions.count; i++)
		{
			TESActorBaseData::FactionInfo factionInfo;
			if (actorBase->actorData.factions.GetNthItem(i, factionInfo))
			{
				if (factionInfo.faction && IsCivilianFactionByFormID(factionInfo.faction))
				{
					hasCivilianFaction = true;
					break;  // Found at least one civilian faction
				}
			}
		}
		
		return hasCivilianFaction;
	}
	
	// ============================================
	// DRAGON DETECTION (By Race)
	// Dragons don't have a simple FormID list - detect by race name
	// ============================================
	
	bool IsDragon(Actor* actor)
	{
		if (!actor) return false;
		
		// Get the actor's race
		TESRace* race = actor->race;
		if (!race) return false;
		
		// Check race name
		const char* raceName = race->fullName.name.data;
		if (!raceName) return false;
		
		std::string raceStr = raceName;
		std::transform(raceStr.begin(), raceStr.end(), raceStr.begin(), ::tolower);
		
		// Check for dragon race names
		if (raceStr.find("dragon") != std::string::npos)
		{
			return true;
		}
		
		// Also check editor ID if full name doesn't match
		// Dragons have race FormIDs like DragonRace (0x000F6726)
		UInt32 raceFormID = race->formID;
		UInt8 modIndex = (raceFormID >> 24) & 0xFF;
		UInt32 baseRaceID = raceFormID & 0x00FFFFFF;
		
		// Skyrim.esm dragon races
		if (modIndex == 0x00)
		{
			switch (baseRaceID)
			{
				case 0x000F6726:  // DragonRace
				case 0x00012E82:  // DragonBlackRace (Alduin)
				case 0x0001CA03:  // DragonPriestRace (not a dragon but hostile)
				case 0x0010E9D1:  // UndeadDragonRace
					return true;
			}
		}
		
		return false;
	}
	
	// ============================================
	// ACTOR HOSTILITY CHECK
	// Check if one actor is hostile to another using game engine
	// This respects crime/bounty/faction relations
	// ============================================
	
	bool IsActorHostileToActor(Actor* actor, Actor* target)
	{
		if (!actor || !target) return false;
		
		// Check if actor is in combat with target
		UInt32 combatTargetHandle = actor->currentCombatTarget;
		if (combatTargetHandle != 0)
		{
			NiPointer<TESObjectREFR> targetRef;
			LookupREFRByHandle(combatTargetHandle, targetRef);
			if (targetRef && targetRef->formID == target->formID)
			{
				return true;  // Actor is actively targeting this target
			}
		}
		
		// Check attack on sight flag
		if (actor->flags2 & Actor::kFlag_kAttackOnSight)
		{
			// Actor has attack on sight - check if target is their combat target
			if (combatTargetHandle != 0)
			{
				NiPointer<TESObjectREFR> targetRef;
				LookupREFRByHandle(combatTargetHandle, targetRef);
				if (targetRef && targetRef->formID == target->formID)
				{
					return true;
				}
			}
		}
		
		// Check if actor is in combat and target is their enemy
		if (actor->IsInCombat())
		{
			// If in combat and combat target is this target, they're hostile
			if (combatTargetHandle != 0)
			{
				NiPointer<TESObjectREFR> targetRef;
				LookupREFRByHandle(combatTargetHandle, targetRef);
				if (targetRef && targetRef->formID == target->formID)
				{
					return true;
				}
			}
		}
		
		return false;
	}
}
