using UnrealBuildTool;

namespace UnrealBuildTool.Rules
{
    public class ActorFlocking : ModuleRules
    {
        public ActorFlocking( ReadOnlyTargetRules Target )
            : base( Target )
        {
            PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
            bEnforceIWYU = true;
            PrivatePCHHeaderFile = "Private/ActorFlockingPCH.h";

            PrivateIncludePaths.Add("ActorFlocking/Private");
            
            PublicDependencyModuleNames.AddRange(
                new string[] { 
                    "Core",
                    "CoreUObject",
                    "Engine"
                }
            );
        }
    }
}
