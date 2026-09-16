# Mega Man X5 Recomp UWP

This project ports [MegaManX5Recomp](https://github.com/mstan/MegaManX5Recomp) to x64 UWP. The retail game is not included.

At first launch, select the game from internal or external storage. Keep CUE and BIN files together. The launcher remembers the selected game.

Later launches start the game automatically. The storage menu returns if the file is unavailable.

Press Xbox **Menu + View** while playing to open Display Settings. Image smoothing and widescreen apply immediately. Internal resolution applies after restarting the app. Settings are saved automatically.

## Build

Use a Redump-compatible Mega Man X5 USA dump (SLUS-01334). Generated code and disc data are ignored by Git.

```powershell
./tools/generate.ps1 -DiscCue "C:/path/to/Mega Man X5 (USA).cue"
./tools/with-msvc.ps1 cmake -S . -B build/uwp -G Ninja -DCMAKE_BUILD_TYPE=Release -DMMX5_UWP_DEPS="C:/path/to/uwp-deps"
./tools/with-msvc.ps1 cmake --build build/uwp --target mmx5 -j 6
./tools/package_uwp.ps1
./tools/sign_uwp_package.ps1 build/uwp/MegaManX5RecompUWP.appx
./tools/make_release.ps1
```

Install `MegaManX5RecompUWP.appx` through Xbox Device Portal. Add `Dependencies/x64/Microsoft.VCLibs.x64.14.00.appx` if the portal requests it.

## Xbox storage

- **Internal:** place the game in `LocalState/Game`.
- **External:** select **External Storage** to open `E:\`. A folder picker appears if direct access fails.
- Internal saves use `LocalState`. External saves use the `MegaManX5Recomp` folder beside the game.
