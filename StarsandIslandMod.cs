using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;
using MelonLoader;

[assembly: MelonInfo(typeof(StarsandIslandMod.StarsandIslandMod), "Starsand Island Mod", "1.0.0", "ModAuthor")]
[assembly: MelonGame("SeedLab", "StarsandIsland")]

namespace StarsandIslandMod
{
    public class StarsandIslandMod : MelonMod
    {
        private static int _frameCount;

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern IntPtr LoadLibraryW(string lpLibFileName);

        public override void OnInitializeMelon()
        {
            LoggerInstance.Msg("Starsand Island Mod loaded! MelonLoader is working.");
            LoadMetadataDumpHook();
        }

        private void LoadMetadataDumpHook()
        {
            var modDir = Path.GetDirectoryName(Assembly.GetExecutingAssembly().Location)!;
            var dllPath = Path.Combine(modDir, "dump-metadata-hook.dll");
            if (string.IsNullOrEmpty(modDir) || !File.Exists(dllPath))
            {
                LoggerInstance.Warning($"Metadata dump hook not found: {dllPath}");
                return;
            }
            var h = LoadLibraryW(dllPath);
            if (h == IntPtr.Zero)
            {
                LoggerInstance.Warning($"Failed to load metadata dump hook: {Marshal.GetLastWin32Error()}");
                return;
            }
            LoggerInstance.Msg($"Metadata dump hook loaded. Dump will appear in game folder or %%TEMP%% when you load a save.");
        }

        public override void OnSceneWasLoaded(int buildIndex, string sceneName)
        {
            LoggerInstance.Msg($"Scene loaded: {sceneName} (index: {buildIndex})");
        }

        public override void OnUpdate()
        {
            // Log every 300 frames (roughly every 5 seconds) to confirm mod is active
            _frameCount++;
            if (_frameCount % 300 == 0)
            {
                LoggerInstance.Msg("Starsand Island Mod is running! (Add UnityEngine ref for Input/Time)");
            }
        }
    }
}
