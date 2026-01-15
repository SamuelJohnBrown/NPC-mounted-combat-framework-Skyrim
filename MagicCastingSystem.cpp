#include "MagicCastingSystem.h"
#include "WeaponDetection.h"
#include "DynamicPackages.h"
#include "Helper.h"
#include "config.h"
#include "skse64/GameRTTI.h"
#include "skse64/GameData.h"
#include "skse64/GameObjects.h"
#include "skse64/GameReferences.h"
#include "skse64/GameThreads.h"
#include "skse64/PapyrusVM.h"
#include "skse64_common/Relocation.h"
#include "skse64_common/SafeWrite.h"
#include <cmath>
#include <cstdlib>
#include <mutex>
#include <vector>
#include <unordered_map>

namespace MountedNPCCombatVR
{
	// ============================================
	// EXTERNAL REFERENCES
	// ============================================
	extern SKSETaskInterface* g_task;
	
	// ============================================
	// SPELL CASTING NATIVE FUNCTION
	// Same as ArrowSystem - define our own local copy
	// ============================================
	typedef bool (*_RemoteCast)(VMClassRegistry* registry, UInt32 stackId, SpellItem* spell, TESObjectREFR* akSource, Actor* blameActor, TESObjectREFR* akTarget);
	RelocAddr<_RemoteCast> MageRemoteCast(0x009BB7F0);
	
	// ============================================
	// PROJECTILE CLASS DEFINITION
	// Based on SpellAutoAimVR's reverse engineering
	// ============================================
	
	class MageProjectile : public TESObjectREFR
	{
	public:
		UInt8 pad98[0xF0 - 0x98];		// 098
		NiPoint3 unk0F0;				// 0F0 - point/origin
		NiPoint3 velocity;				// 0FC - velocity vector
		UInt8 pad108[0x120 - 0x108];	// 108
		UInt32 shooter;					// 120 - shooter handle/formID
		UInt32 desiredTarget;			// 124 - target handle
	};
	
	// ============================================
	// MISSILE PROJECTILE HOOK SYSTEM
	// For Fire-and-Forget spells (Firebolt, Fireball, Ice Spike)
	// ============================================
	
	// Pending spell projectile aim data
	struct PendingSpellAim
	{
		UInt32 shooterFormID;
		UInt32 targetFormID;
		NiPoint3 targetAimPos;
		float registeredTime;
	};
	
	// Spell projectile redirect tracking
	static std::vector<PendingSpellAim> g_pendingSpellAims;
	static std::unordered_map<UInt32, bool> g_redirectedSpellProjectiles;
	static std::mutex g_spellAimMutex;
	static bool g_missileHookInstalled = false;
	static bool g_flameHookInstalled = false;
	
	// Original function pointers
	typedef void (*_UpdateMissileProjectile)(MageProjectile* proj, float deltaTime);
	_UpdateMissileProjectile g_originalUpdateMissile = nullptr;
	
	typedef void (*_UpdateFlameProjectile)(MageProjectile* proj, float deltaTime);
	_UpdateFlameProjectile g_originalUpdateFlame = nullptr;
	
	// VTable offsets from SpellAutoAimVR
	// MissileProjectile - for fire-and-forget spells (Firebolt, Fireball, Ice Spike)
	const uintptr_t MissileProjectileVtbl_Offset = 0x016FDEF8;
	const int MissileUpdateFunctionIndex = 0xAC;
	RelocPtr<_UpdateMissileProjectile> UpdateMissileProjectile_vtbl(MissileProjectileVtbl_Offset + MissileUpdateFunctionIndex * 8);
	
	// FlameProjectile - for concentration spells (Flames, Frostbite, Sparks)
	const uintptr_t FlameProjectileVtbl_Offset = 0x016FC440;
	const int FlameUpdateFunctionIndex = 0xAC;
	RelocPtr<_UpdateFlameProjectile> UpdateFlameProjectile_vtbl(FlameProjectileVtbl_Offset + FlameUpdateFunctionIndex * 8);
	
	// ============================================
	// SPELL PROJECTILE UPDATE HOOK - MISSILE
	// ============================================
	
	void UpdateMissileProjectile_Hook(MageProjectile* proj, float deltaTime)
	{
		// Call original first
		if (g_originalUpdateMissile)
		{
			g_originalUpdateMissile(proj, deltaTime);
		}
		
		// Safety checks
		if (!proj) return;
		if (proj->formID == 0 || proj->formID == 0xFFFFFFFF) return;
		
		try
		{
			// Check if we've already redirected this projectile
			{
				std::lock_guard<std::mutex> lock(g_spellAimMutex);
				if (g_redirectedSpellProjectiles.find(proj->formID) != g_redirectedSpellProjectiles.end())
				{
					return;
				}
				
				if (g_pendingSpellAims.empty())
				{
					return;
				}
			}
			
			// Resolve shooter handle to actual FormID
			UInt32 shooterFormID = 0;
			if (proj->shooter != 0)
			{
				NiPointer<TESObjectREFR> shooterRef;
				LookupREFRByHandle(proj->shooter, shooterRef);
				if (shooterRef)
				{
					shooterFormID = shooterRef->formID;
				}
			}
			
			if (shooterFormID == 0)
			{
				return;
			}
			
			std::lock_guard<std::mutex> lock(g_spellAimMutex);
			
			for (auto it = g_pendingSpellAims.begin(); it != g_pendingSpellAims.end(); )
			{
				float currentTime = (float)clock() / CLOCKS_PER_SEC;
				
				// Remove stale entries (older than 2 seconds)
				if (currentTime - it->registeredTime > 2.0f)
				{
					it = g_pendingSpellAims.erase(it);
					continue;
				}
				
				bool match = (shooterFormID == it->shooterFormID);
				
				if (match)
				{
					// Calculate direction from projectile to target aim position
					NiPoint3 projPos = proj->pos;
					NiPoint3 targetPos = it->targetAimPos;
					
					NiPoint3 direction;
					direction.x = targetPos.x - projPos.x;
					direction.y = targetPos.y - projPos.y;
					direction.z = targetPos.z - projPos.z;
					
					// Get current speed
					float speed = sqrt(proj->velocity.x * proj->velocity.x + 
									   proj->velocity.y * proj->velocity.y + 
									   proj->velocity.z * proj->velocity.z);
					
					if (speed < 100.0f) speed = 2000.0f;  // Default spell speed
					
					float dirLen = sqrt(direction.x * direction.x + direction.y * direction.y + direction.z * direction.z);
					if (dirLen > 0.0001f)
					{
						// Normalize and set velocity
						proj->velocity.x = (direction.x / dirLen) * speed;
						proj->velocity.y = (direction.y / dirLen) * speed;
						proj->velocity.z = (direction.z / dirLen) * speed;
						
						// Update rotation to match velocity direction
						float normalizedZ = proj->velocity.z / speed;
						proj->rot.x = asin(normalizedZ);
						proj->rot.z = atan2(proj->velocity.x, proj->velocity.y);
						
						if (proj->rot.z < 0.0f)
							proj->rot.z += 3.14159265f;
						if (proj->velocity.x < 0.0f)
							proj->rot.z += 3.14159265f;
						
						_MESSAGE("MagicCastingSystem: Redirected MISSILE spell %08X from %08X", proj->formID, shooterFormID);
					}
					
					g_redirectedSpellProjectiles[proj->formID] = true;
					it = g_pendingSpellAims.erase(it);
					
					// Clean up old entries
					if (g_redirectedSpellProjectiles.size() > 100)
						g_redirectedSpellProjectiles.clear();
					
					return;
				}
				
				++it;
			}
		}
		catch (...)
		{
			// Silent catch
		}
	}
	
	// ============================================
	// SPELL PROJECTILE UPDATE HOOK - FLAME
	// ============================================
	
	void UpdateFlameProjectile_Hook(MageProjectile* proj, float deltaTime)
	{
		// Call original first
		if (g_originalUpdateFlame)
		{
			g_originalUpdateFlame(proj, deltaTime);
		}
		
		// Safety checks
		if (!proj) return;
		if (proj->formID == 0 || proj->formID == 0xFFFFFFFF) return;
		
		try
		{
			// Check if we've already redirected this projectile
			{
				std::lock_guard<std::mutex> lock(g_spellAimMutex);
				if (g_redirectedSpellProjectiles.find(proj->formID) != g_redirectedSpellProjectiles.end())
				{
					return;
				}
				
				if (g_pendingSpellAims.empty())
				{
					return;
				}
			}
			
			// Resolve shooter handle to actual FormID
			UInt32 shooterFormID = 0;
			if (proj->shooter != 0)
			{
				NiPointer<TESObjectREFR> shooterRef;
				LookupREFRByHandle(proj->shooter, shooterRef);
				if (shooterRef)
				{
					shooterFormID = shooterRef->formID;
				}
			}
			
			if (shooterFormID == 0)
			{
				return;
			}
			
			std::lock_guard<std::mutex> lock(g_spellAimMutex);
			
			for (auto it = g_pendingSpellAims.begin(); it != g_pendingSpellAims.end(); )
			{
				float currentTime = (float)clock() / CLOCKS_PER_SEC;
				
				if (currentTime - it->registeredTime > 2.0f)
				{
					it = g_pendingSpellAims.erase(it);
					continue;
				}
				
				bool match = (shooterFormID == it->shooterFormID);
				
				if (match)
				{
					NiPoint3 projPos = proj->pos;
					NiPoint3 targetPos = it->targetAimPos;
					
					NiPoint3 direction;
					direction.x = targetPos.x - projPos.x;
					direction.y = targetPos.y - projPos.y;
					direction.z = targetPos.z - projPos.z;
					
					float speed = sqrt(proj->velocity.x * proj->velocity.x + 
									   proj->velocity.y * proj->velocity.y + 
									   proj->velocity.z * proj->velocity.z);
					
					if (speed < 100.0f) speed = 1500.0f;  // Flame spell speed
					
					float dirLen = sqrt(direction.x * direction.x + direction.y * direction.y + direction.z * direction.z);
					if (dirLen > 0.0001f)
					{
						proj->velocity.x = (direction.x / dirLen) * speed;
						proj->velocity.y = (direction.y / dirLen) * speed;
						proj->velocity.z = (direction.z / dirLen) * speed;
						
						float normalizedZ = proj->velocity.z / speed;
						proj->rot.x = asin(normalizedZ);
						proj->rot.z = atan2(proj->velocity.x, proj->velocity.y);
						
						if (proj->rot.z < 0.0f)
							proj->rot.z += 3.14159265f;
						if (proj->velocity.x < 0.0f)
							proj->rot.z += 3.14159265f;
						
						// Don't log every flame redirect - too verbose
					}
					
					g_redirectedSpellProjectiles[proj->formID] = true;
					it = g_pendingSpellAims.erase(it);
					
					if (g_redirectedSpellProjectiles.size() > 100)
						g_redirectedSpellProjectiles.clear();
					
					return;
				}
				
				++it;
			}
		}
		catch (...)
		{
			// Silent catch
		}
	}
	
	// ============================================
	// INSTALL SPELL PROJECTILE HOOKS
	// ============================================
	
	static void InstallMissileProjectileHook()
	{
		if (g_missileHookInstalled) return;
		
		g_originalUpdateMissile = *UpdateMissileProjectile_vtbl;
		SafeWrite64(UpdateMissileProjectile_vtbl.GetUIntPtr(), (uintptr_t)&UpdateMissileProjectile_Hook);
		
		g_missileHookInstalled = true;
		_MESSAGE("MagicCastingSystem: Missile projectile hook installed");
	}
	
	static void InstallFlameProjectileHook()
	{
		if (g_flameHookInstalled) return;
		
		g_originalUpdateFlame = *UpdateFlameProjectile_vtbl;
		SafeWrite64(UpdateFlameProjectile_vtbl.GetUIntPtr(), (uintptr_t)&UpdateFlameProjectile_Hook);
		
		g_flameHookInstalled = true;
		_MESSAGE("MagicCastingSystem: Flame projectile hook installed");
	}
	
	// ============================================
	// REGISTER SPELL PROJECTILE FOR REDIRECT
	// ============================================
	
	static void RegisterSpellProjectileForRedirect(UInt32 shooterFormID, UInt32 targetFormID, const NiPoint3& targetAimPos)
	{
		std::lock_guard<std::mutex> lock(g_spellAimMutex);
		
		PendingSpellAim aim;
		aim.shooterFormID = shooterFormID;
		aim.targetFormID = targetFormID;
		aim.targetAimPos = targetAimPos;
		aim.registeredTime = (float)clock() / CLOCKS_PER_SEC;
		
		g_pendingSpellAims.push_back(aim);
	}
	
	// ============================================
	// MAGIC CASTING SYSTEM
	// ============================================
	// Handles spell casting for mounted mage NPCs.
	// Used exclusively by the MageCaster combat class.
	// ============================================
	
	// ============================================
	// SPELL FORMIDS FROM Skyrim.esm
	// ============================================
	
	// Long Range - Fire and Forget Spells (Skyrim.esm base game only)
	const UInt32 SPELL_FIREBOLT = 0x0012FCD0;      // Firebolt - fire projectile
	const UInt32 SPELL_FIREBALL = 0x0001C789;     // Fireball - fire AoE projectile
	const UInt32 SPELL_ICE_SPIKE = 0x0002B96C;    // Ice Spike - frost projectile
	
	// Array of fire-and-forget spell FormIDs for random selection (base game only)
	static const UInt32 FIRE_AND_FORGET_SPELLS[] = {
		SPELL_FIREBOLT,
		SPELL_FIREBALL,
		SPELL_ICE_SPIKE
	};
	static const int FIRE_AND_FORGET_SPELL_COUNT = sizeof(FIRE_AND_FORGET_SPELLS) / sizeof(FIRE_AND_FORGET_SPELLS[0]);
	
	// Short Range - Concentration Spells (Skyrim.esm)
	const UInt32 SPELL_FLAMES = 0x00012FCD;     // Flames - fire stream
	const UInt32 SPELL_FROSTBITE = 0x0002B96B;    // Frostbite - frost stream
	const UInt32 SPELL_SPARKS = 0x0002DD2A;       // Sparks - lightning stream
	
	// Array of concentration spell FormIDs for random selection
	static const UInt32 CONCENTRATION_SPELLS[] = {
		SPELL_FLAMES,
		SPELL_FROSTBITE,
		SPELL_SPARKS
	};
	static const int CONCENTRATION_SPELL_COUNT = sizeof(CONCENTRATION_SPELLS) / sizeof(CONCENTRATION_SPELLS[0]);
	
	// ============================================
	// SYSTEM STATE
	// ============================================
	
	static bool g_magicSystemInitialized = false;
	
	// Cached spell forms - Fire and Forget
	static SpellItem* g_cachedSpells[FIRE_AND_FORGET_SPELL_COUNT] = { nullptr };
	static bool g_spellsCached = false;
	
	// Cached spell forms - Concentration
	static SpellItem* g_cachedConcentrationSpells[CONCENTRATION_SPELL_COUNT] = { nullptr };
	static bool g_concentrationSpellsCached = false;
	
	// ============================================
	// MAGE SPELL CASTING STATE MACHINE (Fire and Forget)
	// ============================================
	
	enum class MageSpellState
	{
		None = 0,
		Charging,    // Charging up spell (2.5-3.5 seconds)
		Casting,    // Spell being cast (brief)
		Cooldown  // Brief cooldown before next spell
	};
	
	struct MageSpellCastData
	{
		UInt32 casterFormID;
		UInt32 targetFormID;
		MageSpellState state;
		float stateStartTime;
		float chargeDuration;
		int selectedSpellIndex;
		bool isValid;
		
		void Reset()
		{
			casterFormID = 0;
			targetFormID = 0;
			state = MageSpellState::None;
			stateStartTime = 0;
			chargeDuration = 0;
			selectedSpellIndex = 0;
			isValid = false;
		}
	};
	
	const int MAX_TRACKED_MAGES = 5;
	static MageSpellCastData g_mageSpellData[MAX_TRACKED_MAGES];
	static int g_mageSpellCount = 0;
	
	// ============================================
	// CONCENTRATION SPELL STATE MACHINE (Close Range)
	// ============================================
	
	enum class ConcentrationState
	{
		None = 0,
		Casting,
		Cooldown
	};
	
	struct ConcentrationCastData
	{
		UInt32 casterFormID;
		UInt32 targetFormID;
		ConcentrationState state;
		float stateStartTime;
		float burstDuration;
		int selectedSpellIndex;
		float lastCastTime;
		bool isValid;
		
		void Reset()
		{
			casterFormID = 0;
			targetFormID = 0;
			state = ConcentrationState::None;
			stateStartTime = 0;
			burstDuration = 0;
			selectedSpellIndex = 0;
			lastCastTime = 0;
			isValid = false;
		}
	};
	
	static ConcentrationCastData g_concentrationData[MAX_TRACKED_MAGES];
	static int g_concentrationCount = 0;
	
	const float CONCENTRATION_RECAST_INTERVAL = 0.5f;
	
	// ============================================
	// SPELL CACHING - Fire and Forget
	// ============================================
	
	static void CacheSpells()
	{
		if (g_spellsCached) return;
		
		_MESSAGE("MagicCastingSystem: Caching fire-and-forget spells from Skyrim.esm...");
		
		for (int i = 0; i < FIRE_AND_FORGET_SPELL_COUNT; i++)
		{
			UInt32 spellFormID = FIRE_AND_FORGET_SPELLS[i];
			TESForm* form = LookupFormByID(spellFormID);
			if (form)
			{
				g_cachedSpells[i] = DYNAMIC_CAST(form, TESForm, SpellItem);
				if (g_cachedSpells[i])
				{
					const char* spellName = g_cachedSpells[i]->fullName.name.data;
					_MESSAGE("MagicCastingSystem: Cached spell %d '%s' (FormID: %08X)", 
						i, spellName ? spellName : "Unknown", spellFormID);
				}
			}
		}
		
		g_spellsCached = true;
	}
	
	// ============================================
	// SPELL CACHING - Concentration
	// ============================================
	
	static void CacheConcentrationSpells()
	{
		if (g_concentrationSpellsCached) return;
		
		_MESSAGE("MagicCastingSystem: Caching concentration spells from Skyrim.esm...");
		
		for (int i = 0; i < CONCENTRATION_SPELL_COUNT; i++)
		{
			UInt32 spellFormID = CONCENTRATION_SPELLS[i];
			TESForm* form = LookupFormByID(spellFormID);
			if (form)
			{
				g_cachedConcentrationSpells[i] = DYNAMIC_CAST(form, TESForm, SpellItem);
				if (g_cachedConcentrationSpells[i])
				{
					const char* spellName = g_cachedConcentrationSpells[i]->fullName.name.data;
					_MESSAGE("MagicCastingSystem: Cached concentration spell %d '%s' (FormID: %08X)", 
						i, spellName ? spellName : "Unknown", spellFormID);
				}
			}
		}
		
		g_concentrationSpellsCached = true;
	}
	
	static int GetRandomSpellIndex()
	{
		CacheSpells();
		
		int availableCount = 0;
		int availableIndices[FIRE_AND_FORGET_SPELL_COUNT];
		
		for (int i = 0; i < FIRE_AND_FORGET_SPELL_COUNT; i++)
		{
			if (g_cachedSpells[i] != nullptr)
			{
				availableIndices[availableCount] = i;
				availableCount++;
			}
		}
		
		if (availableCount == 0) return -1;
		
		EnsureRandomSeeded();
		int randomIndex = rand() % availableCount;
		return availableIndices[randomIndex];
	}
	
	static int GetRandomConcentrationSpellIndex()
	{
		CacheConcentrationSpells();
		
		int availableCount = 0;
		int availableIndices[CONCENTRATION_SPELL_COUNT];
		
		for (int i = 0; i < CONCENTRATION_SPELL_COUNT; i++)
		{
			if (g_cachedConcentrationSpells[i] != nullptr)
			{
				availableIndices[availableCount] = i;
				availableCount++;
			}
		}
		
		if (availableCount == 0) return -1;
		
		EnsureRandomSeeded();
		int randomIndex = rand() % availableCount;
		return availableIndices[randomIndex];
	}
	
	static SpellItem* GetRandomFireAndForgetSpell()
	{
		int index = GetRandomSpellIndex();
		if (index >= 0 && index < FIRE_AND_FORGET_SPELL_COUNT)
			return g_cachedSpells[index];
		return nullptr;
	}
	
	// ============================================
	// MAGE STATE MANAGEMENT - Fire and Forget
	// ============================================
	
	static MageSpellCastData* GetOrCreateMageData(UInt32 casterFormID)
	{
		for (int i = 0; i < MAX_TRACKED_MAGES; i++)
		{
			if (g_mageSpellData[i].isValid && g_mageSpellData[i].casterFormID == casterFormID)
				return &g_mageSpellData[i];
		}
		
		for (int i = 0; i < MAX_TRACKED_MAGES; i++)
		{
			if (!g_mageSpellData[i].isValid)
			{
				g_mageSpellData[i].Reset();
				g_mageSpellData[i].casterFormID = casterFormID;
				g_mageSpellData[i].isValid = true;
				g_mageSpellCount++;
				return &g_mageSpellData[i];
			}
		}
		
		return nullptr;
	}
	
	// ============================================
	// CONCENTRATION STATE MANAGEMENT
	// ============================================
	
	static ConcentrationCastData* GetOrCreateConcentrationData(UInt32 casterFormID)
	{
		for (int i = 0; i < MAX_TRACKED_MAGES; i++)
		{
			if (g_concentrationData[i].isValid && g_concentrationData[i].casterFormID == casterFormID)
				return &g_concentrationData[i];
		}
		
		for (int i = 0; i < MAX_TRACKED_MAGES; i++)
		{
			if (!g_concentrationData[i].isValid)
			{
				g_concentrationData[i].Reset();
				g_concentrationData[i].casterFormID = casterFormID;
				g_concentrationData[i].isValid = true;
				g_concentrationCount++;
				return &g_concentrationData[i];
			}
		}
		
		return nullptr;
	}
	
	// ============================================
	// TASK CLASS FOR CASTING SPELL ON GAME THREAD
	// ============================================
	
	class TaskCastMageSpell : public TaskDelegate
	{
	public:
		UInt32 m_casterFormID;
		UInt32 m_targetFormID;
		UInt32 m_spellFormID;
		float m_targetX;
		float m_targetY;
		float m_targetZ;
		bool m_isConcentration;
		
		TaskCastMageSpell(UInt32 casterFormID, UInt32 targetFormID, UInt32 spellFormID, 
						  float aimX, float aimY, float aimZ, bool isConcentration = false)
			: m_casterFormID(casterFormID)
			, m_targetFormID(targetFormID)
			, m_spellFormID(spellFormID)
			, m_targetX(aimX)
			, m_targetY(aimY)
			, m_targetZ(aimZ)
			, m_isConcentration(isConcentration) {}
		
		virtual void Run() override
		{
			if (m_casterFormID == 0 || m_targetFormID == 0 || m_spellFormID == 0)
				return;
			
			TESForm* casterForm = LookupFormByID(m_casterFormID);
			TESForm* targetForm = LookupFormByID(m_targetFormID);
			
			if (!casterForm || !targetForm) return;
			
			Actor* caster = DYNAMIC_CAST(casterForm, TESForm, Actor);
			Actor* target = DYNAMIC_CAST(targetForm, TESForm, Actor);
			
			if (!caster || !target) return;
			if (caster->IsDead(1) || target->IsDead(1)) return;
			
			TESForm* spellForm = LookupFormByID(m_spellFormID);
			if (!spellForm) return;
			
			SpellItem* spell = DYNAMIC_CAST(spellForm, TESForm, SpellItem);
			if (!spell) return;
			
			VMClassRegistry* registry = (*g_skyrimVM) ? (*g_skyrimVM)->GetClassRegistry() : nullptr;
			if (!registry) return;
			
			// Register projectile for redirection using our spell-specific system
			NiPoint3 aimPos;
			aimPos.x = m_targetX;
			aimPos.y = m_targetY;
			aimPos.z = m_targetZ;
			RegisterSpellProjectileForRedirect(m_casterFormID, m_targetFormID, aimPos);
			
			const char* casterName = CALL_MEMBER_FN(caster, GetReferenceName)();
			const char* spellName = spell->fullName.name.data;
			
			if (!m_isConcentration)
			{
				_MESSAGE("MagicCastingSystem: Casting '%s' from '%s' (%08X)",
					spellName ? spellName : "Unknown",
					casterName ? casterName : "Unknown", m_casterFormID);
			}
			
			MageRemoteCast(registry, 0, spell, caster, caster, target);
		}
		
		virtual void Dispose() override
		{
			delete this;
		}
	};
	
	// ============================================
	// FIRE SPELL AT TARGET (Fire and Forget)
	// ============================================
	
	static bool FireSpellAtTarget(Actor* caster, Actor* target, int spellIndex)
	{
		if (!caster || !target || !g_task) return false;
		
		if (spellIndex < 0 || spellIndex >= FIRE_AND_FORGET_SPELL_COUNT)
			return false;
		
		UInt32 spellFormID = FIRE_AND_FORGET_SPELLS[spellIndex];
		
		// Install missile projectile hook for fire-and-forget spells
		InstallMissileProjectileHook();
		
		// Calculate spell origin offset
		float casterAngle = caster->rot.z;
		float forwardX = sin(casterAngle);
		float forwardY = cos(casterAngle);
		float rightX = cos(casterAngle);
		float rightY = -sin(casterAngle);
		
		float originX = caster->pos.x 
			+ (forwardX * SpellOriginForwardOffset) 
			+ (rightX * SpellOriginRightOffset);
		float originY = caster->pos.y 
			+ (forwardY * SpellOriginForwardOffset) 
			+ (rightY * SpellOriginRightOffset);
		float originZ = caster->pos.z + SpellOriginUpOffset;
		
		// Calculate target aim position
		NiPoint3 targetPos = target->pos;
		float targetAimZ;
		
		NiPointer<Actor> targetMount;
		if (CALL_MEMBER_FN(target, GetMount)(targetMount) && targetMount)
			targetAimZ = targetPos.z + SpellTargetMountedHeight;
		else
			targetAimZ = targetPos.z + SpellTargetFootHeight;
		
		g_task->AddTask(new TaskCastMageSpell(
			caster->formID, target->formID, spellFormID,
			targetPos.x, targetPos.y, targetAimZ, false
		));
		
		return true;
	}
	
	// ============================================
	// FIRE CONCENTRATION SPELL AT TARGET
	// ============================================
	
	static bool FireConcentrationSpellAtTarget(Actor* caster, Actor* target, int spellIndex)
	{
		if (!caster || !target || !g_task) return false;
		
		if (spellIndex < 0 || spellIndex >= CONCENTRATION_SPELL_COUNT)
			return false;
		
		UInt32 spellFormID = CONCENTRATION_SPELLS[spellIndex];
		
		// Install flame projectile hook for concentration spells
		InstallFlameProjectileHook();
		
		// Calculate spell origin offset
		float casterAngle = caster->rot.z;
		float forwardX = sin(casterAngle);
		float forwardY = cos(casterAngle);
		float rightX = cos(casterAngle);
		float rightY = -sin(casterAngle);
		
		float originX = caster->pos.x 
			+ (forwardX * SpellOriginForwardOffset) 
			+ (rightX * SpellOriginRightOffset);
		float originY = caster->pos.y 
			+ (forwardY * SpellOriginForwardOffset) 
			+ (rightY * SpellOriginRightOffset);
		float originZ = caster->pos.z + SpellOriginUpOffset;
		
		// Calculate target aim position
		NiPoint3 targetPos = target->pos;
		float targetAimZ;
		
		NiPointer<Actor> targetMount;
		if (CALL_MEMBER_FN(target, GetMount)(targetMount) && targetMount)
			targetAimZ = targetPos.z + SpellTargetMountedHeight;
		else
			targetAimZ = targetPos.z + SpellTargetFootHeight;
		
		g_task->AddTask(new TaskCastMageSpell(
			caster->formID, target->formID, spellFormID,
			targetPos.x, targetPos.y, targetAimZ, true
		));
		
		return true;
	}
	
	// ============================================
	// SYSTEM INITIALIZATION
	// ============================================
	
	void InitMagicCastingSystem()
	{
		if (g_magicSystemInitialized) return;
		
		_MESSAGE("MagicCastingSystem: Initializing...");
		
		// Reset fire-and-forget tracking
		for (int i = 0; i < MAX_TRACKED_MAGES; i++)
			g_mageSpellData[i].Reset();
		g_mageSpellCount = 0;
		
		// Reset concentration tracking
		for (int i = 0; i < MAX_TRACKED_MAGES; i++)
			g_concentrationData[i].Reset();
		g_concentrationCount = 0;
		
		// Cache all spells
		CacheSpells();
		CacheConcentrationSpells();
		
		g_magicSystemInitialized = true;
		_MESSAGE("MagicCastingSystem: Initialized (max %d mages, %d fire-and-forget, %d concentration)", 
			MAX_TRACKED_MAGES, FIRE_AND_FORGET_SPELL_COUNT, CONCENTRATION_SPELL_COUNT);
	}
	
	void ShutdownMagicCastingSystem()
	{
		if (!g_magicSystemInitialized) return;
		_MESSAGE("MagicCastingSystem: Shutting down...");
		ResetMagicCastingSystem();
		g_magicSystemInitialized = false;
	}
	
	void ResetMagicCastingSystemCache()
	{
		_MESSAGE("MagicCastingSystem: Resetting cache...");
		
		// Clear fire-and-forget cached spells
		for (int i = 0; i < FIRE_AND_FORGET_SPELL_COUNT; i++)
			g_cachedSpells[i] = nullptr;
		g_spellsCached = false;
		
		// Clear concentration cached spells
		for (int i = 0; i < CONCENTRATION_SPELL_COUNT; i++)
			g_cachedConcentrationSpells[i] = nullptr;
		g_concentrationSpellsCached = false;
		
		// Clear state data
		for (int i = 0; i < MAX_TRACKED_MAGES; i++)
		{
			g_mageSpellData[i].Reset();
			g_concentrationData[i].Reset();
		}
		g_mageSpellCount = 0;
		g_concentrationCount = 0;
		
		// Clear pending spell aims
		{
			std::lock_guard<std::mutex> lock(g_spellAimMutex);
			g_pendingSpellAims.clear();
			g_redirectedSpellProjectiles.clear();
		}
		
		_MESSAGE("MagicCastingSystem: Cache reset complete");
	}
	
	void ResetMagicCastingSystem()
	{
		_MESSAGE("MagicCastingSystem: Resetting all state...");
		
		for (int i = 0; i < MAX_TRACKED_MAGES; i++)
		{
			g_mageSpellData[i].Reset();
			g_concentrationData[i].Reset();
		}
		g_mageSpellCount = 0;
		g_concentrationCount = 0;
		
		for (int i = 0; i < FIRE_AND_FORGET_SPELL_COUNT; i++)
			g_cachedSpells[i] = nullptr;
		g_spellsCached = false;
		
		for (int i = 0; i < CONCENTRATION_SPELL_COUNT; i++)
			g_cachedConcentrationSpells[i] = nullptr;
		g_concentrationSpellsCached = false;
		
		// Clear pending spell aims
		{
			std::lock_guard<std::mutex> lock(g_spellAimMutex);
			g_pendingSpellAims.clear();
			g_redirectedSpellProjectiles.clear();
		}
		
		g_magicSystemInitialized = false;
		
		_MESSAGE("MagicCastingSystem: All state reset complete");
	}
	
	// ============================================
	// CONCENTRATION SPELL UPDATE (Close Range)
	// ============================================
	
	bool UpdateConcentrationSpellCasting(Actor* caster, Actor* target, float distanceToTarget)
	{
		if (!g_magicSystemInitialized)
			InitMagicCastingSystem();
		
		if (!caster || !target) return false;
		
		if (distanceToTarget > ConcentrationRangeMax)
			return false;
		
		ConcentrationCastData* data = GetOrCreateConcentrationData(caster->formID);
		if (!data) return false;
		
		data->targetFormID = target->formID;
		
		float currentTime = GetGameTime();
		
		switch (data->state)
		{
			case ConcentrationState::None:
			{
				int spellIndex = GetRandomConcentrationSpellIndex();
				if (spellIndex < 0) return false;
				
				EnsureRandomSeeded();
				float burstRange = ConcentrationBurstMaxTime - ConcentrationBurstMinTime;
				float randomBurst = ConcentrationBurstMinTime + (((float)(rand() % 100)) / 100.0f * burstRange);
				
				data->state = ConcentrationState::Casting;
				data->stateStartTime = currentTime;
				data->burstDuration = randomBurst;
				data->selectedSpellIndex = spellIndex;
				data->lastCastTime = 0;
				
				const char* casterName = CALL_MEMBER_FN(caster, GetReferenceName)();
				_MESSAGE("MagicCastingSystem: Mage '%s' (%08X) starting CONCENTRATION burst for %.2fs",
					casterName ? casterName : "Unknown", caster->formID, randomBurst);
				
				return true;
			}
			
			case ConcentrationState::Casting:
			{
				float timeInState = currentTime - data->stateStartTime;
				
				if (distanceToTarget > ConcentrationRangeMax)
				{
					data->state = ConcentrationState::Cooldown;
					data->stateStartTime = currentTime;
					return false;
				}
				
				if (timeInState >= data->burstDuration)
				{
					data->state = ConcentrationState::Cooldown;
					data->stateStartTime = currentTime;
					return false;
				}
				
				if ((currentTime - data->lastCastTime) >= CONCENTRATION_RECAST_INTERVAL)
				{
					TESForm* targetForm = LookupFormByID(data->targetFormID);
					if (!targetForm)
					{
						data->state = ConcentrationState::Cooldown;
						data->stateStartTime = currentTime;
						return false;
					}
					
					Actor* currentTarget = DYNAMIC_CAST(targetForm, TESForm, Actor);
					if (!currentTarget || currentTarget->IsDead(1))
					{
						data->state = ConcentrationState::Cooldown;
						data->stateStartTime = currentTime;
						return false;
					}
					
					FireConcentrationSpellAtTarget(caster, currentTarget, data->selectedSpellIndex);
					data->lastCastTime = currentTime;
				}
				
				return true;
			}
			
			case ConcentrationState::Cooldown:
			{
				float timeInState = currentTime - data->stateStartTime;
				
				if (timeInState >= ConcentrationCooldownTime)
					data->state = ConcentrationState::None;
				
				return false;
			}
			
			default:
				return false;
		}
	}
	
	// ============================================
	// MAIN UPDATE FUNCTION - FIRE AND FORGET
	// ============================================
	
	bool UpdateMageSpellCasting(Actor* caster, Actor* target, float distanceToTarget)
	{
		if (!g_magicSystemInitialized)
			InitMagicCastingSystem();
		
		if (!caster || !target) return false;
		
		// Check if target is in close range - use concentration spells
		if (distanceToTarget <= ConcentrationRangeMax)
			return UpdateConcentrationSpellCasting(caster, target, distanceToTarget);
		
		// Only fire-and-forget in valid range
		if (distanceToTarget < SpellRangeMin || distanceToTarget > SpellRangeMax)
			return false;
		
		MageSpellCastData* data = GetOrCreateMageData(caster->formID);
		if (!data) return false;
		
		data->targetFormID = target->formID;
		
		float currentTime = GetGameTime();
		
		switch (data->state)
		{
			case MageSpellState::None:
			{
				int spellIndex = GetRandomSpellIndex();
				if (spellIndex < 0) return false;
				
				EnsureRandomSeeded();
				float chargeRange = SpellChargeMaxTime - SpellChargeMinTime;
				float randomCharge = SpellChargeMinTime + (((float)(rand() % 100)) / 100.0f * chargeRange);
				
				data->state = MageSpellState::Charging;
				data->stateStartTime = currentTime;
				data->chargeDuration = randomCharge;
				data->selectedSpellIndex = spellIndex;
				
				return true;
			}
			
			case MageSpellState::Charging:
			{
				float timeInState = currentTime - data->stateStartTime;
				
				// Check if target moved into close range - switch to concentration
				if (distanceToTarget <= ConcentrationRangeMax)
				{
					data->state = MageSpellState::None;
					return UpdateConcentrationSpellCasting(caster, target, distanceToTarget);
				}
				
				if (distanceToTarget < SpellRangeMin || distanceToTarget > SpellRangeMax)
				{
					data->state = MageSpellState::None;
					return false;
				}
				
				if (timeInState >= data->chargeDuration)
				{
					TESForm* targetForm = LookupFormByID(data->targetFormID);
					if (!targetForm)
					{
						data->state = MageSpellState::None;
						return false;
					}
					
					Actor* currentTarget = DYNAMIC_CAST(targetForm, TESForm, Actor);
					if (!currentTarget || currentTarget->IsDead(1))
					{
						data->state = MageSpellState::None;
						return false;
					}
					
					if (FireSpellAtTarget(caster, currentTarget, data->selectedSpellIndex))
					{
						data->state = MageSpellState::Casting;
						data->stateStartTime = currentTime;
						
						const char* casterName = CALL_MEMBER_FN(caster, GetReferenceName)();
						_MESSAGE("MagicCastingSystem: Mage '%s' (%08X) CAST spell %d",
							casterName ? casterName : "Unknown", caster->formID, data->selectedSpellIndex);
					}
					else
					{
						data->state = MageSpellState::None;
					}
				}
				
				return true;
			}
			
			case MageSpellState::Casting:
			{
				data->state = MageSpellState::Cooldown;
				data->stateStartTime = currentTime;
				return false;
			}
			
			case MageSpellState::Cooldown:
			{
				float timeInState = currentTime - data->stateStartTime;
				
				if (timeInState >= SpellCooldownTime)
					data->state = MageSpellState::None;
				
				return false;
			}
			
			default:
				return false;
		}
	}
	
	// ============================================
	// CHECK IF MAGE IS CURRENTLY CASTING
	// ============================================
	
	bool IsMageCharging(UInt32 casterFormID)
	{
		for (int i = 0; i < MAX_TRACKED_MAGES; i++)
		{
			if (g_mageSpellData[i].isValid && g_mageSpellData[i].casterFormID == casterFormID)
			{
				if (g_mageSpellData[i].state == MageSpellState::Charging)
					return true;
			}
		}
		
		for (int i = 0; i < MAX_TRACKED_MAGES; i++)
		{
			if (g_concentrationData[i].isValid && g_concentrationData[i].casterFormID == casterFormID)
			{
				if (g_concentrationData[i].state == ConcentrationState::Casting)
					return true;
			}
		}
		
		return false;
	}
	
	bool IsMageCastingConcentration(UInt32 casterFormID)
	{
		for (int i = 0; i < MAX_TRACKED_MAGES; i++)
		{
			if (g_concentrationData[i].isValid && g_concentrationData[i].casterFormID == casterFormID)
				return g_concentrationData[i].state == ConcentrationState::Casting;
		}
		return false;
	}
	
	// ============================================
	// RESET MAGE SPELL STATE
	// ============================================
	
	void ResetMageSpellState(UInt32 casterFormID)
	{
		for (int i = 0; i < MAX_TRACKED_MAGES; i++)
		{
			if (g_mageSpellData[i].isValid && g_mageSpellData[i].casterFormID == casterFormID)
			{
				g_mageSpellData[i].Reset();
				g_mageSpellCount--;
				break;
			}
		}
		
		for (int i = 0; i < MAX_TRACKED_MAGES; i++)
		{
			if (g_concentrationData[i].isValid && g_concentrationData[i].casterFormID == casterFormID)
			{
				g_concentrationData[i].Reset();
				g_concentrationCount--;
				break;
			}
		}
	}
	
	// ============================================
	// LEGACY FUNCTIONS
	// ============================================
	
	bool UpdateSpellCasting(Actor* caster, Actor* target)
	{
		if (!caster || !target) return false;
		
		float dx = target->pos.x - caster->pos.x;
		float dy = target->pos.y - caster->pos.y;
		float distance = sqrt(dx * dx + dy * dy);
		
		return UpdateMageSpellCasting(caster, target, distance);
	}
	
	void ResetSpellCastingState(UInt32 casterFormID)
	{
		ResetMageSpellState(casterFormID);
	}
	
	bool IsSpellCastingActive(UInt32 casterFormID)
	{
		return IsMageCharging(casterFormID);
	}
	
	bool HasOffensiveSpellEquipped(Actor* actor)
	{
		return true;
	}
	
	SpellItem* GetBestOffensiveSpell(Actor* caster, Actor* target)
	{
		return GetRandomFireAndForgetSpell();
	}
	
	bool CastSpellAtTarget(Actor* caster, Actor* target)
	{
		if (!caster || !target) return false;
		
		int spellIndex = GetRandomSpellIndex();
		if (spellIndex < 0) return false;
		
		return FireSpellAtTarget(caster, target, spellIndex);
	}
	
	void ScheduleDelayedSpellCast(Actor* caster, Actor* target)
	{
		UpdateSpellCasting(caster, target);
	}
	
	void UpdateDelayedSpellCasts() {}
	void ClearDelayedSpellCasts() {}
}
