#include "config.h"

namespace MountedNPCCombatVR {
		
	int logging = 1;  // Default to WARN level so we see important messages
    int leftHandedMode = 0;
	
	// NPC Dismount Prevention - enabled by default
	bool PreventNPCDismountOnAttack = true;

    void loadConfig() 
    {
        std::string runtimeDirectory = GetRuntimeDirectory();

        if (!runtimeDirectory.empty()) 
  {
     std::string filepath = runtimeDirectory + "Data\\SKSE\\Plugins\\Mounted_NPC_Combat_VR.ini";
  
        _MESSAGE("loadConfig: Looking for config file at: %s", filepath.c_str());
     
        std::ifstream file(filepath);

            if (!file.is_open()) 
            {
  transform(filepath.begin(), filepath.end(), filepath.begin(), ::tolower);
     _MESSAGE("loadConfig: Trying lowercase path: %s", filepath.c_str());
                file.open(filepath);
          }

if (file.is_open()) 
            {
      _MESSAGE("loadConfig: Config file opened successfully");
  
          std::string line;
      std::string currentSection;

      while (std::getline(file, line)) 
   {
  trim(line);
            skipComments(line);

          if (line.empty()) continue;

       if (line[0] == '[') 
       {
  // New section
            size_t endBracket = line.find(']');
    if (endBracket != std::string::npos) 
    {
           currentSection = line.substr(1, endBracket - 1);
       trim(currentSection);
        _MESSAGE("loadConfig: Entering section [%s]", currentSection.c_str());
         }
    }
            else if (currentSection == "Settings") 
        {
      std::string variableName;
      std::string variableValueStr = GetConfigSettingsStringValue(line, variableName);

   if (variableName == "Logging") 
       {
               logging = std::stoi(variableValueStr);
     _MESSAGE("loadConfig: Logging level set to %d", logging);
     }
           else if (variableName == "PreventNPCDismountOnAttack")
                   {
        PreventNPCDismountOnAttack = (std::stoi(variableValueStr) != 0);
             _MESSAGE("loadConfig: PreventNPCDismountOnAttack set to %s", 
           PreventNPCDismountOnAttack ? "ENABLED" : "DISABLED");
         }
       }  
        } 
     
      file.close();
            }
        else
            {
         _MESSAGE("loadConfig: Config file not found, using defaults:");
   _MESSAGE("  - Logging: %d", logging);
       _MESSAGE("  - PreventNPCDismountOnAttack: %s", PreventNPCDismountOnAttack ? "ENABLED" : "DISABLED");
     }
       
        _MESSAGE("loadConfig: Configuration loaded:");
          _MESSAGE("  - Logging level: %d (0=ERR, 1=WARN, 2=INFO)", logging);
            _MESSAGE("  - PreventNPCDismountOnAttack: %s", PreventNPCDismountOnAttack ? "ENABLED" : "DISABLED");
       
return;
        }
        
        _MESSAGE("loadConfig: Runtime directory is empty, using defaults");
     return;
    }

	void Log(const int msgLogLevel, const char* fmt, ...)
	{
		if (msgLogLevel > logging)
		{
			return;
		}

		va_list args;
		char logBuffer[4096];

		va_start(args, fmt);
		vsprintf_s(logBuffer, sizeof(logBuffer), fmt, args);
		va_end(args);

		_MESSAGE(logBuffer);
	}

}