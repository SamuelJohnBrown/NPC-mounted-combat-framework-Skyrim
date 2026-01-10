#include "Engine.h"

#include <skse64/PapyrusActor.cpp>

namespace MountedNPCCombatVR
{
	SKSETrampolineInterface* g_trampolineInterface = nullptr;

	HiggsPluginAPI::IHiggsInterface001* higgsInterface;

	SkyrimVRESLPluginAPI::ISkyrimVRESLInterface001* skyrimVRESLInterface;

	void StartMod()
	{
		LOG("========================================");
		LOG("Mounted_NPC_Combat_VR: Initializing mod features...");
		LOG("========================================");
		
		// Setup the NPC dismount prevention hook
		LOG("Mounted_NPC_Combat_VR: Setting up NPC Dismount Prevention Hook...");
		SetupDismountHook();
		
		LOG("========================================");
		LOG("Mounted_NPC_Combat_VR: Mod initialization complete!");
		LOG(" - NPC Dismount Prevention: %s", PreventNPCDismountOnAttack ? "ENABLED" : "DISABLED");
		LOG("========================================");
	}
}
