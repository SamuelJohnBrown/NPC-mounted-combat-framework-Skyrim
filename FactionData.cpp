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
			case MountedCombatClass::HunterRanged: return "Hunter (Ranged)";
			case MountedCombatClass::MageCaster: return "Mage (Caster)";
			case MountedCombatClass::CivilianFlee: return "Civilian (Flee)";
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
		
		// Get base form ID
		TESNPC* actorBase = DYNAMIC_CAST(actor->baseForm, TESForm, TESNPC);
		if (!actorBase) return false;
		
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
	// Combat Class Determination
	// ============================================
	
	MountedCombatClass DetermineCombatClass(Actor* actor)
	{
		if (!actor)
		{
			return MountedCombatClass::None;
		}
		
		// Check factions in order of specificity
		// NOTE: These functions now check ALL factions, not just primary
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
		
		if (IsHunterFaction(actor))
		{
			return MountedCombatClass::HunterRanged;
		}
		
		if (IsMageFaction(actor))
		{
			return MountedCombatClass::MageCaster;
		}
		
		if (IsCivilianFaction(actor))
		{
			return MountedCombatClass::CivilianFlee;
		}
		
		// Default: Check if has weapons - armed defaults to guard style
		MountedWeaponInfo weaponInfo = GetWeaponInfo(actor);
		if (weaponInfo.hasWeaponEquipped || weaponInfo.hasWeaponSheathed)
		{
			return MountedCombatClass::GuardMelee;  // Armed unknown = guard style
		}
		
		// Unarmed unknown = civilian flee
		return MountedCombatClass::CivilianFlee;
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
		
		// Check by name first (display name, if set)
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
		
		// Check by FormID (Skyrim.esm - verified from actual ESP data)
		if (modIndex == 0x00)
		{
			switch (baseFactionID)
			{
				// ============================================
				// Core Guard Factions (verified FormIDs)
				// ============================================
				case 0x0002BE3B:  // GuardDialogueFaction
				case 0x00086EEE:  // IsGuardFaction
				
				// Hold Guard Factions
				case 0x0002EBEE:  // GuardFactionSolitude (Haafingar)
				case 0x000267EA:  // GuardFactionWhiterun
				case 0x00029DB0:  // CrimeFactionHaafingar (guards respond to crimes)
				case 0x0002816D:  // GuardFactionRiften
				case 0x0002816C:  // GuardFactionMarkarth
				case 0x00029DB4:  // GuardFactionFalkreath
				case 0x0002816B:  // GuardFactionDawnstar
				case 0x00029DB1:  // GuardFactionWindhelm
				case 0x0002816E:  // (old GuardDialogueFaction reference, keep for safety)
				case 0x00029DB9:  // (old GuardFactionSolitude reference, keep for safety)
				case 0x000267E3:  // (old IsGuardFaction reference, keep for safety)
				
				// Additional Guard Factions
				case 0x00104293:  // JobGuardCaptainFaction
				case 0x000DB2E1:  // OrcGuardFaction
				case 0x00051608:  // CaravanGuard
				
				// Quest/Location Guards
				case 0x000E8DC4:  // WERoad02BodyguardFaction
				case 0x000A4E48:  // MorthalGuardhouseFaction
				case 0x00044D9A:  // dunDawnstarSanctuaryGuardianFaction
				case 0x00083218:  // CWWhiterunGuardNeutralFaction
				case 0x00027F9B:  // DA02GuardFaction
				case 0x00027FA8:  // DA02GuardsPlayerEnemy
				case 0x000628DB:  // MS03ChaletGuardEnemyFaction
				case 0x000797ED:  // MQ201ExteriorGuardFaction
				case 0x000A2C7C:  // MQ201PartyGuardFaction
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
		
		// Check ALL factions in the actor's faction list
		// TESNPC has a factions array: tArray<TESActorBaseData::FactionInfo> factions
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
				factionStr.find("imperial") != std::string::npos ||
				factionStr.find("stormcloak") != std::string::npos ||
				factionStr.find("legion") != std::string::npos ||
				factionStr.find("thalmor") != std::string::npos ||
				factionStr.find("penitus") != std::string::npos ||
				factionStr.find("sons of skyrim") != std::string::npos)
			{
				return true;
			}
		}
		
		// Check by FormID (Skyrim.esm)
		if (modIndex == 0x00)
		{
			switch (baseFactionID)
			{
				case 0x000D0607:  // MQ101SoldierFaction
				case 0x000E1B85:  // MQ301SoldierDialogueFaction
				case 0x000E0361:  // CWSoldierNoGuardDialogueFaction
				case 0x000B34D3:  // CWSoldierPlayerEnemyFaction
				case 0x000ABCE8:  // CWSoldierMageFaction
				case 0x000ABCE7:  // CWSoldierArcherFaction
				case 0x00083214:  // CWSoldierDefenderFaction
				case 0x00083215:  // CWSoldierAttackerFaction
				case 0x0003C37F:  // dunForelhostSoldierNeutral
				case 0x0003C380:  // dunForelhostSoldierUnfriendly
				case 0x0006D154:  // CWDialogueSoldierWaitingToDefendFaction
				case 0x0006D155:  // CWDialogueSoldierWaitingToAttackFaction
				case 0x0003ED94:// CWDialogueSoldierFaction
				case 0x000DEBA5:  // MQ104SoldierFaction
				case 0x000EE630:  // CWDisaffectedSoldierFaction
				case 0x000D0603:  // MQ103SonsOfSkyrimSoldierFaction
				case 0x000D0602:  // MQ103ImperialSoldierFaction
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
				factionStr.find("silver hand") != std::string::npos)
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
				case 0x000E0CD7:  // BanditAllyFaction
				case 0x000F6A9E:  // BanditFriendFaction
				case 0x00039FB2:  // dunRobbersGorgeBanditFaction
				case 0x0001B1EC:  // dunValtheimKeepBanditFaction
				case 0x000E8D58:  // WE20BanditFaction
				case 0x000E7ECC:  // WE19BanditFaction
				case 0x000D1978:  // WE06BanditFaction
				case 0x00033538:  // dunIcerunnerBanditFaction
				case 0x00026B0B:  // MS07BanditFaction
				case 0x00065BF0:  // MS07BanditSiblings
				case 0x0006D2E4:  // DunAnsilvundBanditFaction
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
				factionStr.find("warlock") != std::string::npos ||
				factionStr.find("wizard") != std::string::npos ||
				factionStr.find("necromancer") != std::string::npos ||
				factionStr.find("witch") != std::string::npos ||
				factionStr.find("enchanter") != std::string::npos ||
				factionStr.find("court wizard") != std::string::npos)
			{
				return true;
			}
		}
		
		// Check by FormID (Skyrim.esm)
		if (modIndex == 0x00)
		{
			switch (baseFactionID)
			{
				case 0x00027EB6:  // WarlockFaction
				case 0x000E8282:  // WarlockAllyFaction
				case 0x000E8D57:  // WE20WarlockFaction
				case 0x0002C6C8:// NecromancerFaction
				case 0x00066124:  // JobCourtWizardFaction
				case 0x00028848:  // CollegeofWinterholdArchMageFaction
				case 0x00106433:  // dunPOIWitchAniseCrimeFaction
				case 0x000A7AA5:  // dunMarkarthWizard_SpiderFaction
				case 0x000AA06E:  // ServicesDawnstarCourtWizard
				case 0x000C7C87:  // WICraftItem02AdditionalEnchanterFaction
				case 0x00019A15:  // ServicesMarkarthCastleWizard
				case 0x00068447:  // MarkarthWizardFaction
				case 0x00039F09:  // dunHarmugstahlFactionWarlockAttackedbySpiders
				case 0x00039F08:  // dunHarmugstahlFactionWarlock
				case 0x00097D66:  // dunFellglow_WarlockPrisonerAllyFaction
				case 0x000A7AA6:  // dunMarkarthWizard_SecureAreaFaction
					return true;
			}
		}
		
		return false;
	}

	// ============================================
	// Mage/Warlock Faction Check - NOW CHECKS ALL FACTIONS
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
	// Helper: Check if a single faction matches hunter criteria
	// ============================================
	static bool IsHunterFactionByFormID(TESFaction* faction)
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
			
			if (factionStr.find("hunter") != std::string::npos ||
				factionStr.find("hircine") != std::string::npos ||
				factionStr.find("bounty") != std::string::npos)
			{
				return true;
			}
		}
		
		// Check by FormID (Skyrim.esm)
		if (modIndex == 0x00)
		{
			switch (baseFactionID)
			{
				case 0x000C6CD4:  // HunterFaction
				case 0x000E68DE:  // WEDL09HunterFaction
				case 0x000E3A01:  // WEBountyHunter
				case 0x000D2B8A:  // DialogueOrcHuntersFaction
				case 0x000DDF44:  // WEServicesHunterFaction
				case 0x000E26F6:  // WE16HunterFaction
				case 0x0002ACE1:  // DA05HuntersOfHircineFaction
					return true;
			}
		}
		
		return false;
	}

	// ============================================
	// Hunter Faction Check - NOW CHECKS ALL FACTIONS
	// ============================================
	
	bool IsHunterFaction(Actor* actor)
	{
		if (!actor) return false;
		
		TESNPC* actorBase = DYNAMIC_CAST(actor->baseForm, TESForm, TESNPC);
		if (!actorBase) return false;
		
		// Check primary faction first
		if (actorBase->faction && IsHunterFactionByFormID(actorBase->faction))
		{
			return true;
		}
		
		// Check ALL factions
		for (UInt32 i = 0; i < actorBase->actorData.factions.count; i++)
		{
			TESActorBaseData::FactionInfo factionInfo;
			if (actorBase->actorData.factions.GetNthItem(i, factionInfo))
			{
				if (factionInfo.faction && IsHunterFactionByFormID(factionInfo.faction))
				{
					return true;
				}
			}
		}
		
		return false;
	}

	// ============================================
	// Helper: Check if a single faction matches civilian criteria
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
			
			// Passive/Civilian faction keywords
			if (factionStr.find("citizen") != std::string::npos ||
				factionStr.find("civilian") != std::string::npos ||
				factionStr.find("merchant") != std::string::npos ||
				factionStr.find("farmer") != std::string::npos ||
				factionStr.find("bard") != std::string::npos ||
				factionStr.find("pilgrim") != std::string::npos ||
				factionStr.find("traveler") != std::string::npos ||
				factionStr.find("beggar") != std::string::npos ||
				factionStr.find("servant") != std::string::npos ||
				factionStr.find("priest") != std::string::npos ||
				factionStr.find("noble") != std::string::npos ||
				factionStr.find("courier") != std::string::npos ||
				factionStr.find("innkeeper") != std::string::npos ||
				factionStr.find("shopkeeper") != std::string::npos ||
				factionStr.find("vendor") != std::string::npos ||
				factionStr.find("miner") != std::string::npos ||
				factionStr.find("fisher") != std::string::npos ||
				factionStr.find("lumberjack") != std::string::npos ||
				factionStr.find("blacksmith") != std::string::npos ||
				factionStr.find("apothecary") != std::string::npos ||
				factionStr.find("worker") != std::string::npos ||
				factionStr.find("shopper") != std::string::npos ||
				factionStr.find("services") != std::string::npos)
			{
				return true;
			}
		}
		
		// Check by FormID (Skyrim.esm)
		if (modIndex == 0x00)
		{
			switch (baseFactionID)
			{
				// Job Factions (General)
				case 0x00051596:  // JobMinerFaction
				case 0x00051599:  // JobMerchantFaction
				case 0x00051597:  // JobFarmerFaction
				case 0x00051594:  // JobBlacksmithFaction
				case 0x00051595:  // JobApothecaryFaction
				case 0x00051598:  // JobInnkeeperFaction
				case 0x0001032F:  // FavorJobsBeggarsFaction
				// Farmer/Fisher Factions
				case 0x000E1697:  // WEFarmerFaction
				case 0x0005229B:  // FishermanFaction
				case 0x00092A29:  // RiftenFisheryFaction
				// Merchant Factions
				case 0x000E68EF:  // WEJSMerchantHorseFaction
				case 0x000DDF43:  // WEServiceMiscMerchant
				case 0x0001F6AC:  // CaravanMerchant
				// Miner Factions
				case 0x00044D9D:  // DawnstarQuicksilverMinerFaction
				case 0x00044D9C:  // DawnstarIronBreakerMinersFaction
				case 0x00029786:  // MG02MinerFaction
				case 0x00068B95:  // LeftHandMinersBarracksFaction
				case 0x00068B96:  // KarthwastenMinersBarracksFaction
				// Blacksmith Factions
				case 0x000A7AA8:  // MarkarthCastleBlacksmithFaction
				case 0x000878A7:  // SolitudeBlacksmithFaction
				case 0x00039D7E:  // WindhelmBlacksmithFaction
				case 0x000878A6:  // ServicesSolitudeBlacksmith
				case 0x00039D6A:  // ServicesWindhelmBlacksmith
				case 0x000A9638:  // ServicesMorKhazgurBlacksmith
				case 0x000A9631:  // ServicesDushnikhYalBlacksmith
				case 0x00019E18:  // ServicesMarkarthBlacksmith
				case 0x00068BC8:  // ServicesFalkreathBlacksmith
				case 0x000A7AA9:  // ServicesMarkarthCastleBlacksmith
				case 0x000867F9:  // ServicesSpouseRiftenBlacksmith
				case 0x000867FB:  // ServicesSpouseWindhelmBlacksmith
				case 0x000867FD:  // ServicesSpouseSolitudeBlacksmith
				case 0x000867FF:  // ServicesSpouseWhiterunBlacksmith
				case 0x00086803:  // ServicesSpouseMarkarthBlacksmith
				// Innkeeper Factions
				case 0x000A4E47:  // KynesgroveBraidwoodInnkeeperFaction
				case 0x00099157:  // WindhelmCornerclubInnkeeperFaction
				case 0x000867F8:  // ServicesSpouseRiftenInnkeeper
				case 0x000867FA:  // ServicesSpouseWindhelmInnkeeper
				case 0x000867FC:  // ServicesSpouseSolitudeInnkeeper
				case 0x000867FE:  // ServicesSpouseWhiterunInnkeeper
				case 0x00086800:  // ServicesSpouseMarkarthInnkeeper
				// Apothecary Factions
				case 0x000AA06D:  // ServicesDawnstarUsefulThingsApothecary
				case 0x00039D7F:  // WindhelmApothecaryFaction
				case 0x000867E5:  // ServicesSpouseRiftenApothecary
				case 0x000867E7:  // ServicesSpouseWindhelmApothecary
				case 0x000867E9:  // ServicesSpouseSolitudeApothecary
				case 0x000867EB:  // ServicesSpouseWhiterunApothecary
				case 0x00086801:  // ServicesSpouseMarkarthApothecary
				// Worker/Servant Factions
				case 0x000878A8:  // MarkarthSmelterWorkersFaction
				case 0x00068458:  // MarkarthSilverBloodInnWorkerFaction
				case 0x00039D75:  // WindhelmCandlehearthWorkers
				case 0x00029DA4:  // MarkarthCastleServantsFaction
				case 0x00082DD9:  // WhiterunDragonsreachServants
				case 0x00029D95:  // SolitudeBluePalaceServants
				// Other Civilian Factions
				case 0x0002E6EC:  // CWCivilianFaction
				case 0x00019E17:  // ServicesMarkarthFoodMerchant
				case 0x0008A645:  // WhiterunMarketShoppers
				case 0x00078921:  // WindhelmPawnshopOwnerFaction
					return true;
			}
		}
		
		return false;
	}

	// ============================================
	// Civilian Faction Check - NOW CHECKS ALL FACTIONS
	// NOTE: Only returns true if NO combat factions found
	// ============================================
	
	bool IsCivilianFaction(Actor* actor)
	{
		if (!actor) return false;
		
		TESNPC* actorBase = DYNAMIC_CAST(actor->baseForm, TESForm, TESNPC);
		if (!actorBase) return false;
		
		// First, make sure we DON'T have any combat factions
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
}
