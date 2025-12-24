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
	// Combat Class Determination
	// ============================================
	
	MountedCombatClass DetermineCombatClass(Actor* actor)
	{
		if (!actor)
		{
			return MountedCombatClass::None;
		}
		
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
	// Guard Faction Check
	// ============================================
	
	bool IsGuardFaction(Actor* actor)
	{
		if (!actor) return false;
		
		TESNPC* actorBase = DYNAMIC_CAST(actor->baseForm, TESForm, TESNPC);
		if (!actorBase || !actorBase->faction) return false;
		
		TESFaction* faction = actorBase->faction;
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
				// ============================================
				// Guard Factions
				// ============================================
				case 0x0002816E:  // GuardDialogueFaction
				case 0x000267EA:  // GuardFactionWhiterun
				case 0x00029DB0:  // GuardFactionWindhelm
				case 0x0002816D:  // GuardFactionRiften
				case 0x00029DB9:  // GuardFactionSolitude / Haafingar
				case 0x0002816C:  // GuardFactionMarkarth
				case 0x00029DB4:  // GuardFactionFalkreath
				case 0x0002816B:  // GuardFactionDawnstar
				// Additional Guard Factions
				case 0x000267E3:  // IsGuardFaction
				case 0x00104293:  // JobGuardCaptainFaction
				case 0x000DB2E1:  // OrcGuardFaction
				case 0x00051608:  // CaravanGuard
				case 0x00029DB1:  // DragonsreachBasementGuards
				// Quest/Location Guards
				case 0x000E8DC4:  // WERoad02BodyguardFaction
				case 0x000A4E48:  // MorthalGuardhouseFaction
				case 0x00044D9A:  // dunDawnstarSanctuaryGuardianFaction
				case 0x00083218:  // CWWhiterunGuardNeutralFaction
				case 0x000E0361:  // CWSoldierNoGuardDialogueFaction
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
	// Soldier Faction Check
	// ============================================
	
	bool IsSoldierFaction(Actor* actor)
	{
		if (!actor) return false;
		
		TESNPC* actorBase = DYNAMIC_CAST(actor->baseForm, TESForm, TESNPC);
		if (!actorBase || !actorBase->faction) return false;
		
		TESFaction* faction = actorBase->faction;
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
				// ============================================
				// Soldier Factions
				// ============================================
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
				case 0x0006D154:// CWDialogueSoldierWaitingToDefendFaction
				case 0x0006D155:  // CWDialogueSoldierWaitingToAttackFaction
				case 0x0003ED94:  // CWDialogueSoldierFaction
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
	// Bandit Faction Check
	// ============================================
	
	bool IsBanditFaction(Actor* actor)
	{
		if (!actor) return false;
		
		TESNPC* actorBase = DYNAMIC_CAST(actor->baseForm, TESForm, TESNPC);
		if (!actorBase || !actorBase->faction) return false;
		
		TESFaction* faction = actorBase->faction;
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
				// ============================================
				// Bandit Factions
				// ============================================
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
				case 0x0006D2E4:// DunAnsilvundBanditFaction
					return true;
			}
		}
		
		return false;
	}

	// ============================================
	// Mage/Warlock Faction Check
	// ============================================
	
	bool IsMageFaction(Actor* actor)
	{
		if (!actor) return false;
		
		TESNPC* actorBase = DYNAMIC_CAST(actor->baseForm, TESForm, TESNPC);
		if (!actorBase || !actorBase->faction) return false;
		
		TESFaction* faction = actorBase->faction;
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
				// ============================================
				// Mage/Warlock Factions
				// ============================================
				case 0x00027EB6:  // WarlockFaction
				case 0x000E8282:  // WarlockAllyFaction
				case 0x000E8D57:// WE20WarlockFaction
				case 0x0002C6C8:  // NecromancerFaction
				case 0x00066124:  // JobCourtWizardFaction
				case 0x00028848:  // CollegeofWinterholdArchMageFaction
				case 0x00106433:// dunPOIWitchAniseCrimeFaction
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
	// Hunter Faction Check
	// ============================================
	
	bool IsHunterFaction(Actor* actor)
	{
		if (!actor) return false;
		
		TESNPC* actorBase = DYNAMIC_CAST(actor->baseForm, TESForm, TESNPC);
		if (!actorBase || !actorBase->faction) return false;
		
		TESFaction* faction = actorBase->faction;
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
				// ============================================
				// Hunter Factions
				// ============================================
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
	// Civilian Faction Check
	// ============================================
	
	bool IsCivilianFaction(Actor* actor)
	{
		if (!actor) return false;
		
		TESNPC* actorBase = DYNAMIC_CAST(actor->baseForm, TESForm, TESNPC);
		if (!actorBase || !actorBase->faction) return false;
		
		TESFaction* faction = actorBase->faction;
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
				// ============================================
				// Job Factions (General)
				// ============================================
				case 0x00051596:  // JobMinerFaction
				case 0x00051599:  // JobMerchantFaction
				case 0x00051597:  // JobFarmerFaction
				case 0x00051594:  // JobBlacksmithFaction
				case 0x00051595:  // JobApothecaryFaction
				case 0x00051598:  // JobInnkeeperFaction
				case 0x0001032F:  // FavorJobsBeggarsFaction
				
				// ============================================
				// Farmer/Fisher Factions
				// ============================================
				case 0x000E1697:  // WEFarmerFaction
				case 0x0005229B:  // FishermanFaction
				case 0x00092A29:  // RiftenFisheryFaction
				
				// ============================================
				// Merchant Factions
				// ============================================
				case 0x000E68EF:  // WEJSMerchantHorseFaction
				case 0x000DDF43:  // WEServiceMiscMerchant
				case 0x0001F6AC:  // CaravanMerchant
				
				// ============================================
				// Miner Factions
				// ============================================
				case 0x00044D9D:  // DawnstarQuicksilverMinerFaction
				case 0x00044D9C:  // DawnstarIronBreakerMinersFaction
				case 0x00029786:  // MG02MinerFaction
				case 0x00068B95:  // LeftHandMinersBarracksFaction
				case 0x00068B96:  // KarthwastenMinersBarracksFaction
				
				// ============================================
				// Blacksmith Factions
				// ============================================
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
				
				// ============================================
				// Innkeeper Factions
				// ============================================
				case 0x000A4E47:  // KynesgroveBraidwoodInnkeeperFaction
				case 0x00099157:  // WindhelmCornerclubInnkeeperFaction
				case 0x000867F8:  // ServicesSpouseRiftenInnkeeper
				case 0x000867FA:  // ServicesSpouseWindhelmInnkeeper
				case 0x000867FC:  // ServicesSpouseSolitudeInnkeeper
				case 0x000867FE:  // ServicesSpouseWhiterunInnkeeper
				case 0x00086800:  // ServicesSpouseMarkarthInnkeeper
				
				// ============================================
				// Apothecary Factions
				// ============================================
				case 0x000AA06D:  // ServicesDawnstarUsefulThingsApothecary
				case 0x00039D7F:  // WindhelmApothecaryFaction
				case 0x000867E5:  // ServicesSpouseRiftenApothecary
				case 0x000867E7:  // ServicesSpouseWindhelmApothecary
				case 0x000867E9:  // ServicesSpouseSolitudeApothecary
				case 0x000867EB:  // ServicesSpouseWhiterunApothecary
				case 0x00086801:  // ServicesSpouseMarkarthApothecary
				
				// ============================================
				// Worker/Servant Factions
				// ============================================
				case 0x000878A8:  // MarkarthSmelterWorkersFaction
				case 0x00068458:  // MarkarthSilverBloodInnWorkerFaction
				case 0x00039D75:  // WindhelmCandlehearthWorkers
				case 0x00029DA4:  // MarkarthCastleServantsFaction
				case 0x00082DD9:  // WhiterunDragonsreachServants
				case 0x00029D95:  // SolitudeBluePalaceServants
				
				// ============================================
				// Other Civilian Factions
				// ============================================
				case 0x0002E6EC:  // CWCivilianFaction
				case 0x00019E17:  // ServicesMarkarthFoodMerchant
				case 0x0008A645:  // WhiterunMarketShoppers
				case 0x00078921:  // WindhelmPawnshopOwnerFaction
					return true;
			}
		}
		
		return false;
	}
}
