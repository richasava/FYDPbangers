using UnrealBuildTool;

public class ALIVEGame : ModuleRules
{
	public ALIVEGame(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"Networking",
			"Sockets",
		});

		PrivateDependencyModuleNames.AddRange(new string[] { });
	}
}