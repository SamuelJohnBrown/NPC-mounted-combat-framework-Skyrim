#include "skse64_common/skse_version.h"
#include <shlobj.h>
#include <intrin.h>
#include <string>
#include <xbyak/xbyak.h>

#include "skse64/PluginAPI.h"	
#include "Helper.h"
#include "SpecialDismount.h"
#include "HorseMountScanner.h"

#include "skse64_common/BranchTrampoline.h"

#include <skse64/PapyrusActor.cpp>

namespace MountedNPCCombatVR
{
	// ============================================
	// GLOBAL INTERFACES (moved from Engine.cpp)
	// ============================================
	SKSETrampolineInterface* g_trampolineInterface = nullptr;
	HiggsPluginAPI::IHiggsInterface001* higgsInterface;
	SkyrimVRESLPluginAPI::ISkyrimVRESLInterface001* skyrimVRESLInterface;

	// ============================================
	// SKSE INTERFACES
	// ============================================
	static SKSEMessagingInterface* g_messaging = NULL;
	static PluginHandle					g_pluginHandle = kPluginHandle_Invalid;
	static SKSEPapyrusInterface* g_papyrus = NULL;
	static SKSEObjectInterface* g_object = NULL;
	SKSETaskInterface* g_task = NULL;

	static SKSEVRInterface* g_vrInterface = nullptr;

	#pragma comment(lib, "Ws2_32.lib")

	// ============================================
	// START MOD (moved from Engine.cpp)
	// ============================================
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

	void SetupReceptors()
	{
		_MESSAGE("Building Event Sinks...");

		
	}

	extern "C" {

		bool SKSEPlugin_Query(const SKSEInterface* skse, PluginInfo* info) {	// Called by SKSE to learn about this plugin and check that it's safe to load it
			gLog.OpenRelative(CSIDL_MYDOCUMENTS, "\\My Games\\Skyrim VR\\SKSE\\Mounted_NPC_Combat_VR.log");
			gLog.SetPrintLevel(IDebugLog::kLevel_Error);
			gLog.SetLogLevel(IDebugLog::kLevel_DebugMessage);
			//gLog.SetLogLevel(IDebugLog::kLevel_FatalError);

			std::string logMsg("Mounted_NPC_Combat_VR: ");
			logMsg.append(MountedNPCCombatVR::MOD_VERSION_STR);
			_MESSAGE(logMsg.c_str());

			// populate info structure
			info->infoVersion = PluginInfo::kInfoVersion;
			info->name = "Mounted_NPC_Combat_VR";
			info->version = MountedNPCCombatVR::MOD_VERSION;

			// store plugin handle so we can identify ourselves later
			g_pluginHandle = skse->GetPluginHandle();

			std::string skseVers = "SKSE Version: ";
			skseVers += std::to_string(skse->runtimeVersion);
			_MESSAGE(skseVers.c_str());

			if (skse->isEditor)
			{
				_MESSAGE("loaded in editor, marking as incompatible");

				return false;
			}
			else if (skse->runtimeVersion < CURRENT_RELEASE_RUNTIME)
			{
				_MESSAGE("unsupported runtime version %08X", skse->runtimeVersion);

				return false;
			}

			// ### do not do anything else in this callback
			// ### only fill out PluginInfo and return true/false

			// supported runtime version
			return true;
		}

		inline bool file_exists(const std::string& name) {
			struct stat buffer;
			return (stat(name.c_str(), &buffer) == 0);
		}

		static const size_t TRAMPOLINE_SIZE = 256;

		//Listener for SKSE Messages
		void OnSKSEMessage(SKSEMessagingInterface::Message* msg)
		{
			if (msg)
			{
				_MESSAGE("SKSE Message received: type=%d", msg->type);
				
				if (msg->type == SKSEMessagingInterface::kMessage_PostLoad)
				{
					_MESSAGE("=== POST LOAD (DLL loaded) ===");
					// DLL loaded - do NOT touch game state here
				}
				else if (msg->type == SKSEMessagingInterface::kMessage_InputLoaded)
				{
					_MESSAGE("=== INPUT LOADED ===");
					SetupReceptors();
				}
				else if (msg->type == SKSEMessagingInterface::kMessage_DataLoaded)
				{
					_MESSAGE("=== DATA LOADED ===");
					MountedNPCCombatVR::loadConfig();

					// NEW SKSEVR feature: trampoline interface object from QueryInterface()
					if (MountedNPCCombatVR::g_trampolineInterface)
					{
						void* branch = MountedNPCCombatVR::g_trampolineInterface->AllocateFromBranchPool(g_pluginHandle, TRAMPOLINE_SIZE);
						if (!branch) {
							_ERROR("couldn't acquire branch trampoline from SKSE. this is fatal. skipping remainder of init process.");
							return;
						}

						g_branchTrampoline.SetBase(TRAMPOLINE_SIZE, branch);

						void* local = MountedNPCCombatVR::g_trampolineInterface->AllocateFromLocalPool(g_pluginHandle, TRAMPOLINE_SIZE);
						if (!local) {
							_ERROR("couldn't acquire codegen buffer from SKSE. this is fatal. skipping remainder of init process.");
							return;
						}

						g_localTrampoline.SetBase(TRAMPOLINE_SIZE, local);

						_MESSAGE("Using new SKSEVR trampoline interface memory pool alloc for codegen buffers.");
					}
					else
					{
						if (!g_branchTrampoline.Create(TRAMPOLINE_SIZE))
						{
							_FATALERROR("[ERROR] couldn't create branch trampoline. this is fatal. skipping remainder of init process.");
							return;
						}

						if (!g_localTrampoline.Create(TRAMPOLINE_SIZE, nullptr))
						{
							_FATALERROR("[ERROR] couldn't create codegen buffer. this is fatal. skipping remainder of init process.");
							return;
						}

						_MESSAGE("Using legacy SKSE trampoline creation.");
					}

					// Setup hooks - only once per process lifetime (this is correct)
					MountedNPCCombatVR::GameLoad();
					MountedNPCCombatVR::StartMod();
					
					// Initialize spells for SpecialDismount (ESP data is now available)
					InitSpecialDismountSpells();
					
					// Initialize the horse mount scanner
					InitHorseMountScanner();
					ResetHorseMountScanner();
					
					// IMPORTANT: For Skyrim VR, kMessage_NewGame may not fire reliably
					// Activate the mod here with a delay - it will be reset on actual game loads
					_MESSAGE("=== DATA LOADED: Activating mod with delay (will be reset on game load events) ===");
					MountedNPCCombatVR::ActivateModWithDelay();
				}
				else if (msg->type == SKSEMessagingInterface::kMessage_PostPostLoad)
				{
					_MESSAGE("=== POST POST LOAD ===");
					higgsInterface = HiggsPluginAPI::GetHiggsInterface001(g_pluginHandle, g_messaging);
					if (higgsInterface)
					{
						_MESSAGE("Got HIGGS interface. Buildnumber: %d", higgsInterface->GetBuildNumber());
					}
					else
					{
						_MESSAGE("Did not get HIGGS interface");
					}

					skyrimVRESLInterface = SkyrimVRESLPluginAPI::GetSkyrimVRESLInterface001(g_pluginHandle, g_messaging);
					if (skyrimVRESLInterface)
					{
						_MESSAGE("Got SkyrimVRESL interface");
					}
					else
					{
						_MESSAGE("Did not get SkyrimVRESL interface");
					}
					
					// Initialize Special Dismount module (registers HIGGS callbacks)
					InitSpecialDismount();
				}
				else if (msg->type == SKSEMessagingInterface::kMessage_NewGame)
				{
					_MESSAGE("=== NEW GAME STARTED ===");
					_MESSAGE("Resetting all runtime state for new game...");
					MountedNPCCombatVR::OnPreLoadGame();  // Reset state (same as before load)
					InitHorseMountScanner();
					ResetHorseMountScanner();
					MountedNPCCombatVR::OnNewGame(); // Then activate with delay
				}
				else if (msg->type == SKSEMessagingInterface::kMessage_PreLoadGame)
				{
					_MESSAGE("=== PRE LOAD GAME ===");
					MountedNPCCombatVR::OnPreLoadGame();
					StopHorseMountScanner();
				}
				else if (msg->type == SKSEMessagingInterface::kMessage_PostLoadGame)
				{
					if ((bool)(msg->data) == true)
					{
						_MESSAGE("=== POST LOAD GAME (Success) ===");
						
						// Log player pointer for diagnostics
						if (g_thePlayer && (*g_thePlayer))
						{
							_MESSAGE("Player pointer: 0x%p, FormID: %08X", *g_thePlayer, (*g_thePlayer)->formID);
						}
						else
						{
							_MESSAGE("WARNING: Player pointer is NULL!");
						}
						
						MountedNPCCombatVR::PostLoadGame();
						InitHorseMountScanner();
						ResetHorseMountScanner();
					}
					else
					{
						_MESSAGE("=== POST LOAD GAME (Failed) ===");
						MountedNPCCombatVR::DeactivateMod();
					}
				}
			}
		}

		bool SKSEPlugin_Load(const SKSEInterface* skse) {	// Called by SKSE to load this plugin

			g_task = (SKSETaskInterface*)skse->QueryInterface(kInterface_Task);

			g_papyrus = (SKSEPapyrusInterface*)skse->QueryInterface(kInterface_Papyrus);

			g_messaging = (SKSEMessagingInterface*)skse->QueryInterface(kInterface_Messaging);
			g_messaging->RegisterListener(g_pluginHandle, "SKSE", OnSKSEMessage);

			g_vrInterface = (SKSEVRInterface*)skse->QueryInterface(kInterface_VR);
			if (!g_vrInterface) {
				_MESSAGE("[CRITICAL] Couldn't get SKSE VR interface. You probably have an outdated SKSE version.");
				return false;
			}

			return true;
		}
	};
}