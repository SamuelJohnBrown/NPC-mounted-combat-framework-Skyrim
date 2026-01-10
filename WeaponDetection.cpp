#include "WeaponDetection.h"
#include "DynamicPackages.h"  // For get_vfunc
#include "skse64/GameRTTI.h"
#include "skse64/GameData.h"
#include <ctime>
#include <cmath>
#include <algorithm>

namespace MountedNPCCombatVR
{
	// Iron Arrow FormID from Skyrim.esm
	const UInt32 IRON_ARROW_FORMID = 0x0001397D;
	
	// Iron Mace FormID from Skyrim.esm - default weapon for unarmed mounted NPCs
	const UInt32 IRON_MACE_FORMID = 0x00013982;
	
	// Hunting Bow FormID from Skyrim.esm
	const UInt32 HUNTING_BOW_FORMID = 0x00013985;
	
	// ============================================
	// WEAPON COLLISION SYSTEM
	// Based on WeaponCollisionVR's approach
	// Uses line segment collision for accurate hit detection
	// ============================================
	
	// Collision configuration - INCREASED for mounted combat (longer range)
	const float WEAPON_COLLISION_DIST_THRESHOLD = 100.0f;  // Distance threshold for collision (was 50)
	const float BODY_CAPSULE_RADIUS = 50.0f;        // Approximate body collision radius (was 30)
	const float MOUNTED_HIT_RANGE_BONUS = 80.0f;           // Extra range for mounted combat
	
	// Weapon bone names to search for
	static const char* WEAPON_BONE_RIGHT[] = {
		"WEAPON", "Weapon", "NPC R Hand [RHnd]", "NPC R Forearm [RLar]"
	};
	
	static const char* WEAPON_BONE_LEFT[] = {
		"SHIELD", "Shield", "NPC L Hand [LHnd]", "NPC L Forearm [LLar]"
	};
	
	// Math helper: Lerp between two points
	static NiPoint3 Lerp(const NiPoint3& A, const NiPoint3& B, float k)
	{
		return NiPoint3(
			A.x + (B.x - A.x) * k,
			A.y + (B.y - A.y) * k,
			A.z + (B.z - A.z) * k
		);
	}
	
	// Math helper: Clamp value to 0-1 range
	static float Clamp01(float t)
	{
		return (std::max)(0.0f, (std::min)(1.0f, t));
	}
	
	// Math helper: Dot product
	static float DotProduct(const NiPoint3& A, const NiPoint3& B)
	{
		return A.x * B.x + A.y * B.y + A.z * B.z;
	}
	
	// Math helper: Get distance between two points
	static float GetPointDistance(const NiPoint3& A, const NiPoint3& B)
	{
		float dx = B.x - A.x;
		float dy = B.y - A.y;
		float dz = B.z - A.z;
		return sqrt(dx * dx + dy * dy + dz * dz);
	}
	
	// Math helper: Find closest point on a line segment to a given point
	// Same algorithm as WeaponCollisionVR
	static NiPoint3 ConstrainToSegment(const NiPoint3& position, const NiPoint3& segStart, const NiPoint3& segEnd)
	{
		NiPoint3 ba(segEnd.x - segStart.x, segEnd.y - segStart.y, segEnd.z - segStart.z);
		NiPoint3 pa(position.x - segStart.x, position.y - segStart.y, position.z - segStart.z);
		
		float baDotBa = DotProduct(ba, ba);
		if (baDotBa < 0.0001f) return segStart;  // Degenerate segment
		
		float t = DotProduct(ba, pa) / baDotBa;
		return Lerp(segStart, segEnd, Clamp01(t));
	}
	
	// Distance from a point to a line segment
	static float DistPointToSegment(const NiPoint3& point, const NiPoint3& segStart, const NiPoint3& segEnd)
	{
		NiPoint3 closestPoint = ConstrainToSegment(point, segStart, segEnd);
		return GetPointDistance(point, closestPoint);
	}
	
	// Closest distance between two line segments
	// Returns the distance and optionally the contact point
	static float DistSegmentToSegment(
		const NiPoint3& seg1Start, const NiPoint3& seg1End,
		const NiPoint3& seg2Start, const NiPoint3& seg2End,
		NiPoint3* outContactPoint)
	{
		// Sample points along segment 1 and find closest to segment 2
		float minDist = 999999.0f;
		NiPoint3 closestPoint;
		
		const int SAMPLES = 10;
		for (int i = 0; i <= SAMPLES; i++)
		{
			float t = (float)i / (float)SAMPLES;
			NiPoint3 p1 = Lerp(seg1Start, seg1End, t);
			
			NiPoint3 closestOnSeg2 = ConstrainToSegment(p1, seg2Start, seg2End);
			float dist = GetPointDistance(p1, closestOnSeg2);
			
			if (dist < minDist)
			{
				minDist = dist;
				closestPoint = Lerp(p1, closestOnSeg2, 0.5f);  // Midpoint as contact
			}
		}
		
		// Also sample from segment 2 to segment 1
		for (int i = 0; i <= SAMPLES; i++)
		{
			float t = (float)i / (float)SAMPLES;
			NiPoint3 p2 = Lerp(seg2Start, seg2End, t);
			
			NiPoint3 closestOnSeg1 = ConstrainToSegment(p2, seg1Start, seg1End);
			float dist = GetPointDistance(p2, closestOnSeg1);
			
			if (dist < minDist)
			{
				minDist = dist;
				closestPoint = Lerp(p2, closestOnSeg1, 0.5f);
			}
		}
		
		if (outContactPoint) *outContactPoint = closestPoint;
		return minDist;
	}
	
	// Get weapon segment (hand position to weapon tip) for an actor
	bool GetWeaponSegment(Actor* actor, NiPoint3& outBottom, NiPoint3& outTop, bool leftHand)
	{
		if (!actor) return false;
		
		NiNode* root = actor->GetNiNode();
		if (!root) 
		{
			_MESSAGE("WeaponDetection: GetWeaponSegment - No root node for actor %08X", actor->formID);
			return false;
		}
		
		// Find weapon bone
		NiAVObject* weaponNode = nullptr;
		const char** boneNames = leftHand ? WEAPON_BONE_LEFT : WEAPON_BONE_RIGHT;
		int boneCount = 4;
		const char* foundBoneName = nullptr;
		
		for (int i = 0; i < boneCount; i++)
		{
			const char* boneName = boneNames[i];
			weaponNode = root->GetObjectByName(&boneName);
			if (weaponNode) 
			{
				foundBoneName = boneName;
				break;
			}
		}
		
		if (!weaponNode)
		{
			// Debug: Log that we couldn't find weapon bone
			static int logCount = 0;
			if (++logCount % 60 == 1)  // Only log occasionally
			{
				_MESSAGE("WeaponDetection: GetWeaponSegment - No weapon bone found for actor %08X (tried WEAPON, NPC R Hand, etc.)", actor->formID);
			}
			
			// Fallback: use actor position
			outBottom.x = actor->pos.x;
			outBottom.y = actor->pos.y;
			outBottom.z = actor->pos.z + 100.0f;
			outTop = outBottom;
			return false;
		}
		
		// Get hand position (weapon bottom)
		outBottom.x = weaponNode->m_worldTransform.pos.x;
		outBottom.y = weaponNode->m_worldTransform.pos.y;
		outBottom.z = weaponNode->m_worldTransform.pos.z;
		
		// Calculate weapon tip using rotation matrix and reach
		float reach = GetWeaponReach(actor);
		if (reach < 50.0f) reach = 70.0f;  // Minimum reach for melee
		
		// Get weapon direction from rotation matrix (Y-axis in bone space typically)
		// WeaponCollisionVR uses: rotate.entry[0][1], rotate.entry[1][1], rotate.entry[2][1]
		NiMatrix33& rot = weaponNode->m_worldTransform.rot;
		NiPoint3 weaponDir(rot.data[0][1], rot.data[1][1], rot.data[2][1]);
		
		// Normalize
		float len = sqrt(weaponDir.x * weaponDir.x + weaponDir.y * weaponDir.y + weaponDir.z * weaponDir.z);
		if (len > 0.001f)
		{
			weaponDir.x /= len;
			weaponDir.y /= len;
			weaponDir.z /= len;
		}
		
		// Calculate weapon tip
		outTop.x = outBottom.x + weaponDir.x * reach;
		outTop.y = outBottom.y + weaponDir.y * reach;
		outTop.z = outBottom.z + weaponDir.z * reach;
		
		// Debug: Log successful weapon segment (occasionally)
		static int successLog = 0;
		if (++successLog % 120 == 1)
		{
			_MESSAGE("WeaponDetection: Got weapon segment for %08X - bone '%s', bottom(%.0f,%.0f,%.0f) top(%.0f,%.0f,%.0f)", 
				actor->formID, foundBoneName,
				outBottom.x, outBottom.y, outBottom.z,
				outTop.x, outTop.y, outTop.z);
		}
		
		return true;
	}
	
	// Get body capsule (vertical line segment representing actor's body)
	void GetBodyCapsule(Actor* actor, NiPoint3& outBottom, NiPoint3& outTop)
	{
		if (!actor)
		{
			outBottom = NiPoint3(0, 0, 0);
			outTop = NiPoint3(0, 0, 0);
			return;
		}
		
		// Body capsule from feet to head
		outBottom.x = actor->pos.x;
		outBottom.y = actor->pos.y;
		outBottom.z = actor->pos.z + 20.0f;  // Slightly above ground
		
		outTop.x = actor->pos.x;
		outTop.y = actor->pos.y;
		outTop.z = actor->pos.z + 150.0f;// Approximate head height
	}
	
	// ============================================
	// Main Weapon Collision Check
	// Checks if rider's weapon collides with target's body or weapon
	// Note: WeaponCollisionResult struct is defined in WeaponDetection.h
	// ============================================
	
	WeaponCollisionResult CheckWeaponCollision(Actor* attacker, Actor* target)
	{
		WeaponCollisionResult result;
		result.hasCollision = false;
		result.distance = 999999.0f;
		result.contactPoint = NiPoint3(0, 0, 0);
		result.hitWeapon = false;
		
		if (!attacker || !target) return result;
		
		// Get attacker's weapon segment
		NiPoint3 attackWeapBottom, attackWeapTop;
		bool gotAttackerWeapon = GetWeaponSegment(attacker, attackWeapBottom, attackWeapTop, false);
		
		if (!gotAttackerWeapon)
		{
			// Debug log - weapon segment failed
			static int failLog = 0;
			if (++failLog % 30 == 1)
			{
				_MESSAGE("WeaponDetection: CheckWeaponCollision - Failed to get attacker weapon segment");
			}
			return result;
		}
		
		// Get target's body capsule
		NiPoint3 bodyBottom, bodyTop;
		GetBodyCapsule(target, bodyBottom, bodyTop);
		
		// Check collision with body
		NiPoint3 bodyContactPoint;
		float bodyDist = DistSegmentToSegment(attackWeapBottom, attackWeapTop, bodyBottom, bodyTop, &bodyContactPoint);
		
		// Add body radius to threshold
		float bodyThreshold = WEAPON_COLLISION_DIST_THRESHOLD + BODY_CAPSULE_RADIUS;
		
		// Debug: Log collision distances occasionally
		static int distLog = 0;
		if (++distLog % 30 == 1)
		{
			_MESSAGE("WeaponDetection: Collision dist=%.1f, threshold=%.1f (weap: %.0f,%.0f,%.0f -> %.0f,%.0f,%.0f)",
				bodyDist, bodyThreshold,
				attackWeapBottom.x, attackWeapBottom.y, attackWeapBottom.z,
				attackWeapTop.x, attackWeapTop.y, attackWeapTop.z);
		}
		
		if (bodyDist < bodyThreshold)
		{
			result.hasCollision = true;
			result.distance = bodyDist;
			result.contactPoint = bodyContactPoint;
			result.hitWeapon = false;
		}
		
		// Check if target is blocking with weapon
		NiPoint3 targetWeapBottom, targetWeapTop;
		if (GetWeaponSegment(target, targetWeapBottom, targetWeapTop, false))
		{
			NiPoint3 weapContactPoint;
			float weapDist = DistSegmentToSegment(attackWeapBottom, attackWeapTop, targetWeapBottom, targetWeapTop, &weapContactPoint);
			
			// Weapon-to-weapon collision has tighter threshold
			float weapThreshold = WEAPON_COLLISION_DIST_THRESHOLD * 0.7f;
			
			if (weapDist < weapThreshold && weapDist < bodyDist)
			{
				result.hasCollision = true;
				result.distance = weapDist;
				result.contactPoint = weapContactPoint;
				result.hitWeapon = true;  // Hit their weapon (potential parry)
			}
		}
		
		// Also check shield if target has one
		TESForm* leftHandItem = target->GetEquippedObject(true);
		if (leftHandItem && leftHandItem->formType == kFormType_Armor)
		{
			NiPoint3 shieldBottom, shieldTop;
			if (GetWeaponSegment(target, shieldBottom, shieldTop, true))
			{
				NiPoint3 shieldContactPoint;
				float shieldDist = DistSegmentToSegment(attackWeapBottom, attackWeapTop, shieldBottom, shieldTop, &shieldContactPoint);
				
				// Shield has larger collision area
				float shieldThreshold = WEAPON_COLLISION_DIST_THRESHOLD * 1.5f;
				
				if (shieldDist < shieldThreshold && shieldDist < result.distance)
				{
					result.hasCollision = true;
					result.distance = shieldDist;
					result.contactPoint = shieldContactPoint;
					result.hitWeapon = true;  // Shield counts as blocking
				}
			}
		}
		
		return result;
	}
	
	// ============================================
	// Inventory Add Functions
	// ============================================
	
	bool AddArrowsToInventory(Actor* actor, UInt32 count)
	{
		if (!actor) return false;
		
		TESForm* arrowForm = LookupFormByID(IRON_ARROW_FORMID);
		if (!arrowForm)
		{
			_MESSAGE("WeaponDetection: Failed to find Iron Arrow (FormID: %08X)", IRON_ARROW_FORMID);
			return false;
		}
		
		AddItem_Native(nullptr, 0, actor, arrowForm, count, true);
		return true;
	}
	
	bool AddAmmoToInventory(Actor* actor, UInt32 ammoFormID, UInt32 count)
	{
		if (!actor) return false;
		
		TESForm* ammoForm = LookupFormByID(ammoFormID);
		if (!ammoForm) return false;
		
		TESAmmo* ammo = DYNAMIC_CAST(ammoForm, TESForm, TESAmmo);
		if (!ammo) return false;
		
		AddItem_Native(nullptr, 0, actor, ammoForm, count, true);
		return true;
	}
	
	TESAmmo* FindAmmoInInventory(Actor* actor)
	{
		if (!actor) return nullptr;
		
		ExtraContainerChanges* containerChanges = static_cast<ExtraContainerChanges*>
			(actor->extraData.GetByType(kExtraData_ContainerChanges));
		
		if (!containerChanges || !containerChanges->data || !containerChanges->data->objList)
			return nullptr;
		
		tList<InventoryEntryData>* objList = containerChanges->data->objList;
		
		for (tList<InventoryEntryData>::Iterator it = objList->Begin(); !it.End(); ++it)
		{
			InventoryEntryData* entry = it.Get();
			if (!entry || !entry->type) continue;
			
			TESAmmo* ammo = DYNAMIC_CAST(entry->type, TESForm, TESAmmo);
			if (ammo && entry->countDelta > 0)
			{
				return ammo;
			}
		}
		
		return nullptr;
	}
	
	UInt32 CountArrowsInInventory(Actor* actor)
	{
		if (!actor) return 0;
		
		ExtraContainerChanges* containerChanges = static_cast<ExtraContainerChanges*>
			(actor->extraData.GetByType(kExtraData_ContainerChanges));
		
		if (!containerChanges || !containerChanges->data || !containerChanges->data->objList)
			return 0;
		
		UInt32 totalArrows = 0;
		tList<InventoryEntryData>* objList = containerChanges->data->objList;
		
		for (tList<InventoryEntryData>::Iterator it = objList->Begin(); !it.End(); ++it)
		{
			InventoryEntryData* entry = it.Get();
			if (!entry || !entry->type) continue;
			
			TESAmmo* ammo = DYNAMIC_CAST(entry->type, TESForm, TESAmmo);
			if (ammo)
			{
				SInt32 count = entry->countDelta;
				if (count > 0)
				{
					totalArrows += count;
				}
			}
		}
		
		return totalArrows;
	}
	
	bool EquipArrows(Actor* actor)
	{
		if (!actor) return false;
		
		UInt32 existingArrows = CountArrowsInInventory(actor);
		
		if (existingArrows < 5)
		{
			UInt32 arrowsToAdd = 5 - existingArrows;
			AddArrowsToInventory(actor, arrowsToAdd);
		}
		
		TESAmmo* ammoToEquip = FindAmmoInInventory(actor);
		
		if (!ammoToEquip)
		{
			TESForm* arrowForm = LookupFormByID(IRON_ARROW_FORMID);
			if (arrowForm)
			{
				ammoToEquip = DYNAMIC_CAST(arrowForm, TESForm, TESAmmo);
			}
		}
		
		if (!ammoToEquip) return false;
		
		EquipManager* equipManager = EquipManager::GetSingleton();
		if (equipManager)
		{
			CALL_MEMBER_FN(equipManager, EquipItem)(actor, ammoToEquip, nullptr, 1, nullptr, true, false, false, nullptr);
			return true;
		}
		
		return false;
	}
	
	// ============================================
	// Weapon Detection
	// ============================================
	
	MountedWeaponInfo GetWeaponInfo(Actor* actor)
	{
		MountedWeaponInfo info;
		
		if (!actor) return info;
		
		info.hasWeaponEquipped = IsWeaponDrawn(actor);
		info.mainHandType = GetEquippedWeaponType(actor, false);
		info.offHandType = GetEquippedWeaponType(actor, true);
		info.isBow = (info.mainHandType == WeaponType::Bow || info.mainHandType == WeaponType::Crossbow);
		info.isShieldEquipped = (info.offHandType == WeaponType::Shield);
		info.weaponReach = GetWeaponReach(actor);
		info.hasWeaponSheathed = HasWeaponAvailable(actor);
		info.hasBowInInventory = HasBowInInventory(actor);
		info.hasMeleeInInventory = HasMeleeWeaponInInventory(actor);
		
		return info;
	}
	
	bool IsWeaponDrawn(Actor* actor)
	{
		if (!actor) return false;
		return actor->actorState.IsWeaponDrawn();
	}
	
	bool HasWeaponAvailable(Actor* actor)
	{
		if (!actor) return false;
		
		TESForm* rightHand = actor->GetEquippedObject(false);
		if (rightHand)
		{
			TESObjectWEAP* weapon = DYNAMIC_CAST(rightHand, TESForm, TESObjectWEAP);
			if (weapon) return true;
		}
		
		TESForm* leftHand = actor->GetEquippedObject(true);
		if (leftHand)
		{
			TESObjectWEAP* weapon = DYNAMIC_CAST(leftHand, TESForm, TESObjectWEAP);
			if (weapon) return true;
		}
		
		return false;
	}
	
	const char* GetWeaponTypeName(WeaponType type)
	{
		switch (type)
		{
			case WeaponType::None: return "None";
			case WeaponType::OneHandSword: return "One-Hand Sword";
			case WeaponType::OneHandAxe: return "One-Hand Axe";
			case WeaponType::OneHandMace: return "One-Hand Mace";
			case WeaponType::OneHandDagger: return "Dagger";
			case WeaponType::TwoHandSword: return "Two-Hand Sword";
			case WeaponType::TwoHandAxe: return "Two-Hand Axe/Hammer";
			case WeaponType::Bow: return "Bow";
			case WeaponType::Crossbow: return "Crossbow";
			case WeaponType::Staff: return "Staff";
			case WeaponType::Shield: return "Shield";
			default: return "Unknown";
		}
	}
	
	WeaponType GetEquippedWeaponType(Actor* actor, bool leftHand)
	{
		if (!actor) return WeaponType::None;
		
		TESForm* equippedItem = actor->GetEquippedObject(leftHand);
		if (!equippedItem) return WeaponType::None;
		
		TESObjectWEAP* weapon = DYNAMIC_CAST(equippedItem, TESForm, TESObjectWEAP);
		if (weapon)
		{
			UInt8 type = weapon->type();
			switch (type)
			{
				case TESObjectWEAP::GameData::kType_OneHandSword: return WeaponType::OneHandSword;
				case TESObjectWEAP::GameData::kType_OneHandDagger: return WeaponType::OneHandDagger;
				case TESObjectWEAP::GameData::kType_OneHandAxe: return WeaponType::OneHandAxe;
				case TESObjectWEAP::GameData::kType_OneHandMace: return WeaponType::OneHandMace;
				case TESObjectWEAP::GameData::kType_TwoHandSword: return WeaponType::TwoHandSword;
				case TESObjectWEAP::GameData::kType_TwoHandAxe: return WeaponType::TwoHandAxe;
				case TESObjectWEAP::GameData::kType_Bow: return WeaponType::Bow;
				case TESObjectWEAP::GameData::kType_Staff: return WeaponType::Staff;
				case TESObjectWEAP::GameData::kType_CrossBow: return WeaponType::Crossbow;
				default: return WeaponType::Unknown;
			}
		}
		
		TESObjectARMO* armor = DYNAMIC_CAST(equippedItem, TESForm, TESObjectARMO);
		if (armor && leftHand)
		{
			return WeaponType::Shield;
		}
		
		return WeaponType::None;
	}
	
	float GetWeaponReach(Actor* actor)
	{
		if (!actor) return 0.0f;
		
		const float DEFAULT_UNARMED_REACH = 64.0f;
		const float DEFAULT_MELEE_REACH = 96.0f;
		const float DEFAULT_BOW_REACH = 4096.0f;
		
		TESForm* rightHand = actor->GetEquippedObject(false);
		if (!rightHand) return DEFAULT_UNARMED_REACH;
		
		TESObjectWEAP* weapon = DYNAMIC_CAST(rightHand, TESForm, TESObjectWEAP);
		if (!weapon) return DEFAULT_UNARMED_REACH;
		
		UInt8 type = weapon->type();
		if (type == TESObjectWEAP::GameData::kType_Bow || 
			type == TESObjectWEAP::GameData::kType_CrossBow ||
			type == TESObjectWEAP::GameData::kType_Staff)
		{
			return DEFAULT_BOW_REACH;
		}
		
		float reach = weapon->reach();
		if (reach > 0.0f)
		{
			return DEFAULT_MELEE_REACH * reach;
		}
		
		return DEFAULT_MELEE_REACH;
	}
	
	// ============================================
	// Weapon Equip/Switch Functions
	// ============================================
	
	bool IsBowEquipped(Actor* actor)
	{
		if (!actor) return false;
		WeaponType type = GetEquippedWeaponType(actor, false);
		return (type == WeaponType::Bow || type == WeaponType::Crossbow);
	}
	
	bool IsMeleeEquipped(Actor* actor)
	{
		if (!actor) return false;
		WeaponType type = GetEquippedWeaponType(actor, false);
		return (type == WeaponType::OneHandSword || 
				type == WeaponType::OneHandAxe || 
				type == WeaponType::OneHandMace ||
				type == WeaponType::TwoHandSword ||
				type == WeaponType::TwoHandAxe);
	}
	
	bool HasBowInInventory(Actor* actor)
	{
		if (!actor) return false;
		
		ExtraContainerChanges* containerChanges = static_cast<ExtraContainerChanges*>(
			actor->extraData.GetByType(kExtraData_ContainerChanges));
		
		if (!containerChanges || !containerChanges->data || !containerChanges->data->objList)
			return false;
		
		tList<InventoryEntryData>* objList = containerChanges->data->objList;
		
		for (tList<InventoryEntryData>::Iterator it = objList->Begin(); !it.End(); ++it)
		{
			InventoryEntryData* entry = it.Get();
			if (!entry || !entry->type) continue;
			
			TESObjectWEAP* weapon = DYNAMIC_CAST(entry->type, TESForm, TESObjectWEAP);
			if (weapon)
			{
				UInt8 type = weapon->type();
				if (type == TESObjectWEAP::GameData::kType_Bow || 
					type == TESObjectWEAP::GameData::kType_CrossBow)
				{
					return true;
				}
			}
		}
		return false;
	}
	
	bool HasMeleeWeaponInInventory(Actor* actor)
	{
		if (!actor) return false;
		
		ExtraContainerChanges* containerChanges = static_cast<ExtraContainerChanges*>(
			actor->extraData.GetByType(kExtraData_ContainerChanges));
		
		if (!containerChanges || !containerChanges->data || !containerChanges->data->objList)
			return false;
		
		tList<InventoryEntryData>* objList = containerChanges->data->objList;
		
		for (tList<InventoryEntryData>::Iterator it = objList->Begin(); !it.End(); ++it)
		{
			InventoryEntryData* entry = it.Get();
			if (!entry || !entry->type) continue;
			
			TESObjectWEAP* weapon = DYNAMIC_CAST(entry->type, TESForm, TESObjectWEAP);
			if (weapon)
			{
				UInt8 type = weapon->type();
				if (type == TESObjectWEAP::GameData::kType_OneHandSword ||
					type == TESObjectWEAP::GameData::kType_OneHandAxe ||
					type == TESObjectWEAP::GameData::kType_OneHandMace ||
					type == TESObjectWEAP::GameData::kType_TwoHandSword ||
					type == TESObjectWEAP::GameData::kType_TwoHandAxe)
				{
					return true;
				}
			}
		}
		return false;
	}
	
	TESObjectWEAP* FindBestBowInInventory(Actor* actor)
	{
		if (!actor) return nullptr;
		
		ExtraContainerChanges* containerChanges = static_cast<ExtraContainerChanges*>(
			actor->extraData.GetByType(kExtraData_ContainerChanges));
		
		if (!containerChanges || !containerChanges->data || !containerChanges->data->objList)
			return nullptr;
		
		TESObjectWEAP* bestBow = nullptr;
		int bestDamage = 0;
		
		tList<InventoryEntryData>* objList = containerChanges->data->objList;
		
		for (tList<InventoryEntryData>::Iterator it = objList->Begin(); !it.End(); ++it)
		{
			InventoryEntryData* entry = it.Get();
			if (!entry || !entry->type) continue;
			
			TESObjectWEAP* weapon = DYNAMIC_CAST(entry->type, TESForm, TESObjectWEAP);
			if (weapon)
			{
				UInt8 type = weapon->type();
				if (type == TESObjectWEAP::GameData::kType_Bow || 
					type == TESObjectWEAP::GameData::kType_CrossBow)
				{
					int damage = weapon->damage.GetAttackDamage();
					if (damage > bestDamage || bestBow == nullptr)
					{
						bestBow = weapon;
						bestDamage = damage;
					}
				}
			}
		}
		return bestBow;
	}
	
	TESObjectWEAP* FindBestMeleeInInventory(Actor* actor)
	{
		if (!actor) return nullptr;
		
		ExtraContainerChanges* containerChanges = static_cast<ExtraContainerChanges*>(
			actor->extraData.GetByType(kExtraData_ContainerChanges));
		
		if (!containerChanges || !containerChanges->data || !containerChanges->data->objList)
			return nullptr;
		
		TESObjectWEAP* bestMelee = nullptr;
		int bestDamage = 0;
		
		tList<InventoryEntryData>* objList = containerChanges->data->objList;
		
		for (tList<InventoryEntryData>::Iterator it = objList->Begin(); !it.End(); ++it)
		{
			InventoryEntryData* entry = it.Get();
			if (!entry || !entry->type) continue;
			
			TESObjectWEAP* weapon = DYNAMIC_CAST(entry->type, TESForm, TESObjectWEAP);
			if (weapon)
			{
				UInt8 type = weapon->type();
				if (type == TESObjectWEAP::GameData::kType_OneHandSword ||
					type == TESObjectWEAP::GameData::kType_OneHandAxe ||
					type == TESObjectWEAP::GameData::kType_OneHandMace ||
					type == TESObjectWEAP::GameData::kType_TwoHandSword ||
					type == TESObjectWEAP::GameData::kType_TwoHandAxe)
				{
					int damage = weapon->damage.GetAttackDamage();
					if (damage > bestDamage || bestMelee == nullptr)
					{
						bestMelee = weapon;
						bestDamage = damage;
					}
				}
			}
		}
		return bestMelee;
	}
	
	bool EquipBestBow(Actor* actor)
	{
		if (!actor) return false;
		
		TESObjectWEAP* bow = FindBestBowInInventory(actor);
		if (!bow) return false;
		
		EquipManager* equipManager = EquipManager::GetSingleton();
		if (equipManager)
		{
			BGSEquipSlot* rightSlot = GetRightHandSlot();
			CALL_MEMBER_FN(equipManager, EquipItem)(actor, bow, nullptr, 1, rightSlot, true, false, false, nullptr);
			
			// ============================================
			// ANIMATION FIX: Send weapon equip animation event
			// Some horse behavior graphs need explicit animation notifications
			// to properly show the weapon model
			// ============================================
			BSFixedString weaponDrawEvent("WeaponDraw");
			get_vfunc<bool (*)(IAnimationGraphManagerHolder*, const BSFixedString&)>(&actor->animGraphHolder, 0x1)(&actor->animGraphHolder, weaponDrawEvent);
			
			return true;
		}
		return false;
	}
	
	bool EquipBestMeleeWeapon(Actor* actor)
	{
		if (!actor) return false;
		
		TESObjectWEAP* melee = FindBestMeleeInInventory(actor);
		if (!melee) return false;
		
		EquipManager* equipManager = EquipManager::GetSingleton();
		if (equipManager)
		{
			BGSEquipSlot* rightSlot = GetRightHandSlot();
			CALL_MEMBER_FN(equipManager, EquipItem)(actor, melee, nullptr, 1, rightSlot, true, false, false, nullptr);
			
			// ============================================
			// ANIMATION FIX: Send weapon equip animation event
			// Some horse behavior graphs need explicit animation notifications
			// to properly show the weapon model
			// ============================================
			BSFixedString weaponDrawEvent("WeaponDraw");
			get_vfunc<bool (*)(IAnimationGraphManagerHolder*, const BSFixedString&)>(&actor->animGraphHolder, 0x1)(&actor->animGraphHolder, weaponDrawEvent);
			
			return true;
		}
		return false;
	}
	
	bool GiveDefaultMountedWeapon(Actor* actor)
	{
		if (!actor) return false;
		
		if (HasMeleeWeaponInInventory(actor)) return false;
		
		TESForm* maceForm = LookupFormByID(IRON_MACE_FORMID);
		if (!maceForm) return false;
		
		TESObjectWEAP* mace = DYNAMIC_CAST(maceForm, TESForm, TESObjectWEAP);
		if (!mace) return false;
		
		AddItem_Native(nullptr, 0, actor, maceForm, 1, true);
		
		EquipManager* equipManager = EquipManager::GetSingleton();
		if (equipManager)
		{
			BGSEquipSlot* rightSlot = GetRightHandSlot();
			CALL_MEMBER_FN(equipManager, EquipItem)(actor, mace, nullptr, 1, rightSlot, true, false, false, nullptr);
			
			// ============================================
			// ANIMATION FIX: Send weapon equip animation event
			// Some horse behavior graphs need explicit animation notifications
			// to properly show the weapon model
			// ============================================
			BSFixedString weaponDrawEvent("WeaponDraw");
			get_vfunc<bool (*)(IAnimationGraphManagerHolder*, const BSFixedString&)>(&actor->animGraphHolder, 0x1)(&actor->animGraphHolder, weaponDrawEvent);
			
			return true;
		}
		
		return false;
	}
	
	bool SheatheCurrentWeapon(Actor* actor)
	{
		if (!actor) return false;
		
		// Check if weapon is drawn
		if (!IsWeaponDrawn(actor)) return false;
		
		// Sheathe the weapon
		actor->DrawSheatheWeapon(false);  // false = sheathe
		
		_MESSAGE("WeaponDetection: Sheathed weapon for actor %08X", actor->formID);
		return true;
	}
	
	bool GiveDefaultBow(Actor* actor)
	{
		if (!actor) return false;
		
		if (HasBowInInventory(actor)) return false;
		
		TESForm* bowForm = LookupFormByID(HUNTING_BOW_FORMID);
		if (!bowForm) return false;
		
		TESObjectWEAP* bow = DYNAMIC_CAST(bowForm, TESForm, TESObjectWEAP);
		if (!bow) return false;
		
		AddItem_Native(nullptr, 0, actor, bowForm, 1, true);
		return true;
	}
	
	bool RemoveDefaultBow(Actor* actor)
	{
		if (!actor) return false;
		
		TESForm* bowForm = LookupFormByID(HUNTING_BOW_FORMID);
		if (!bowForm) return false;
		
		TESObjectWEAP* bow = DYNAMIC_CAST(bowForm, TESForm, TESObjectWEAP);
		if (!bow) return false;
		
		// Unequip the bow if it's equipped
		if (IsBowEquipped(actor))
		{
			EquipManager* equipManager = EquipManager::GetSingleton();
			if (equipManager)
			{
				CALL_MEMBER_FN(equipManager, UnequipItem)(actor, bow, nullptr, 1, nullptr, false, false, true, false, nullptr);
				_MESSAGE("WeaponDetection: Unequipped Hunting Bow from actor %08X", actor->formID);
			}
		}
		
		// Note: We don't actually remove the bow from inventory since there's no easy
		// RemoveItem native function available. The bow will stay in inventory but
		// won't be equipped. This is acceptable behavior.
		
		return true;
	}

	// ============================================
	// Weapon Logging
	// ============================================
	
	void LogEquippedWeapons(Actor* actor, UInt32 formID)
	{
		if (!actor) return;
		
		TESForm* rightHand = actor->GetEquippedObject(false);
		if (rightHand)
		{
			TESObjectWEAP* weapon = DYNAMIC_CAST(rightHand, TESForm, TESObjectWEAP);
			if (weapon)
			{
				const char* weaponName = weapon->fullName.name.data;
				WeaponType type = GetEquippedWeaponType(actor, false);
				_MESSAGE("MountedCombat: NPC %08X Right Hand: '%s' (%s)", 
					formID, weaponName ? weaponName : "Unknown", GetWeaponTypeName(type));
			}
		}
	}
	
	void LogInventoryWeapons(Actor* actor, UInt32 formID)
	{
		if (!actor) return;
		// Minimal logging
	}
	
	// ============================================
	// Spell Detection
	// ============================================
	
	void LogEquippedSpells(Actor* actor, UInt32 formID) { }
	
	bool HasSpellsAvailable(Actor* actor)
	{
		if (!actor) return false;
		return actor->addedSpells.Length() > 0;
	}
	
	void LogAvailableSpells(Actor* actor, UInt32 formID) { }
	
	// ============================================
	// Weapon Node / Hitbox Detection
	// ============================================
	
	static const char* WEAPON_BONE_NAMES[] = {
		"WEAPON", "Weapon", "NPC R Hand [RHnd]", "NPC R Forearm [RLar]"
	};
	
	NiAVObject* GetWeaponBoneNode(Actor* actor)
	{
		if (!actor) return nullptr;
		
		NiNode* root = actor->GetNiNode();
		if (!root) return nullptr;
		
		for (int i = 0; i < sizeof(WEAPON_BONE_NAMES) / sizeof(WEAPON_BONE_NAMES[0]); i++)
		{
			const char* boneName = WEAPON_BONE_NAMES[i];
			NiAVObject* node = root->GetObjectByName(&boneName);
			if (node) return node;
		}
		
		return nullptr;
	}
	
	bool GetWeaponWorldPosition(Actor* actor, NiPoint3* outPosition)
	{
		if (!actor || !outPosition) return false;
		
		NiAVObject* weaponNode = GetWeaponBoneNode(actor);
		if (!weaponNode) 
		{
			outPosition->x = actor->pos.x;
			outPosition->y = actor->pos.y;
			outPosition->z = actor->pos.z + 100.0f;
			return false;
		}
		
		outPosition->x = weaponNode->m_worldTransform.pos.x;
		outPosition->y = weaponNode->m_worldTransform.pos.y;
		outPosition->z = weaponNode->m_worldTransform.pos.z;
		
		return true;
	}
	
	float GetDistanceToPlayer(NiPoint3* position)
	{
		if (!position) return 999999.0f;
		if (!g_thePlayer || !(*g_thePlayer)) return 999999.0f;
		
		Actor* player = *g_thePlayer;
		
		float dx = position->x - player->pos.x;
		float dy = position->y - player->pos.y;
		float dz = position->z - player->pos.z;
		
		return sqrt(dx * dx + dy * dy + dz * dz);
	}
	
	// ============================================
	// MELEE HIT DETECTION - SIMPLE DISTANCE BASED
	// Replaces complex collision system with reliable distance check
	// ============================================
	
	bool CheckMountedAttackHit(Actor* rider, Actor* target, float* outDistance)
	{
		if (!rider || !target) return false;
		
		// Simple distance-based check for mounted combat
		float dx = rider->pos.x - target->pos.x;
		float dy = rider->pos.y - target->pos.y;
		float dz = (rider->pos.z + 100.0f) - (target->pos.z + 80.0f);  // Approximate weapon height
		float distance = sqrt(dx * dx + dy * dy + dz * dz);
		
		if (outDistance) *outDistance = distance;
		
		float weaponReach = GetWeaponReach(rider);
		const float MOUNTED_REACH_BONUS = 100.0f;  // Mounted riders have extended reach
		const float HIT_THRESHOLD_PLAYER = 180.0f; // Base hit distance vs player
		const float HIT_THRESHOLD_NPC = 280.0f;    // Larger hit distance vs NPCs (both moving)
		
		// Determine if target is the player or an NPC
		bool targetIsPlayer = (g_thePlayer && (*g_thePlayer) && target == (*g_thePlayer));
		float baseThreshold = targetIsPlayer ? HIT_THRESHOLD_PLAYER : HIT_THRESHOLD_NPC;
		
		float effectiveThreshold = baseThreshold + (weaponReach * 0.5f) + MOUNTED_REACH_BONUS;
		
		// Log hit checks for debugging NPC vs NPC combat
		if (!targetIsPlayer)
		{
			static int hitCheckLogCounter = 0;
			hitCheckLogCounter++;
			// Log every 30th check to avoid spam
			if (hitCheckLogCounter % 30 == 0)
			{
				const char* targetName = CALL_MEMBER_FN(target, GetReferenceName)();
				_MESSAGE("WeaponDetection: Hit check vs NPC '%s' - dist: %.0f, threshold: %.0f, inRange: %s",
					targetName ? targetName : "Unknown",
					distance, effectiveThreshold,
					(distance <= effectiveThreshold) ? "YES" : "NO");
			}
		}
		
		return (distance <= effectiveThreshold);
	}
	
	// ============================================
	// Check if target would block the hit
	// Simple check - is target blocking?
	// ============================================
	
	bool WouldTargetBlockHit(Actor* rider, Actor* target)
	{
		if (!rider || !target) return false;
		
		// Check if target is in blocking state via animation graph
		static BSFixedString isBlockingVar("IsBlocking");
		bool isBlocking = false;
		
		typedef bool (*_GetGraphVariableBool)(IAnimationGraphManagerHolder* holder, const BSFixedString& varName, bool& out);
		_GetGraphVariableBool getGraphVarBool = get_vfunc<_GetGraphVariableBool>(&target->animGraphHolder, 0x12);
		
		if (getGraphVarBool)
		{
			getGraphVarBool(&target->animGraphHolder, isBlockingVar, isBlocking);
		}
		
		return isBlocking;
	}
}
