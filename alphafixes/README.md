Includes editorfix and alphafixes. Press F11 to access patch menu or go to Settings > AlphaPatches.

Note:
Disabling Threaded optimization in NVIDIA Control Panel/NVIDIA App can in some cases improve performance.
Increasing Sound mix-ahead may improve perfomance on slower systems (by default 0.20, in default ini 0.18)
If you make new levels, please make sure to distribute .spawnflags file along with your level (if you changed difficulty spawn flags for entities)

Installation:
Copy the contents of this archive to the root folder of Serious Sam Alpha. Replace files if asked.

```
0.6 Public Test 2

Added:
- You can now change the title text (where it said Evaluation Version for Croteam_Test)
- Added new theme for the weapon selection UI - Clean
- Quick Save (F6) and Quick Load (F9)

Changed:
- Ported spawnflags from TFE

Fixed:
- Input delay due to sound engine buffer. This fix may introduce sound crackles on a slow PC, please use the F11 menu and increase Sound mix-ahead (by default 0.20)
- TouchField should work as it should
- Detail polygons from converted TFE levels no longer have Z-Buffer issues
- Ghostbuster ray on Low latency mouse fix
- Modeler app startup crash
- Modeler crash when opening a model
- Different gravity modes with Low latency mouse fix on
- Modeler save feature (.mdl/.ini/.h files are saved)

0.6 Public Test 1:

Added:
TFE-style third person camera
Ported RobotDriving entity from Alpha Remake
Ported Damager entity from TFE
Ported Copier entity from TFE
Trigger2 entity with 10 targets, score (should work like the Trigger from TFE)
New VirtualTree with the new entities and textures (needs to be loaded manually)

Changed:
SamAlphaEditorFixes.dll is now integrated in AlphaFixes.dll, no need to patch the editor anymore
Editor: entities property window now look more like Serious Editor 2017; this menu can be disabled in ini file

Fixed:
Ghostbuster ray acceleration
Pause menu now properly pauses the game
The pause key (not to be confused with the pause menu) no longer floods the console with messages
Trigger should now display messages (should work like in TFE)
BSP bug that sometimes made TouchField not work
TouchField no longer requires a portal for moving players
Low latency mouse fix should work properly in noclip/fly mode. It still bugs out a bit when in a different gravity sector, this will be fixed in the next version/public test
```

Please report any bugs/glitches/etc. to the issue tracker.

Credits:
Created by Sergi4UA
Entities & Models: Biktim
Testers: Biktim, Jarmen, Gyro Zeppeli
MinHook by Tsuda Kageyu
Special Thanks to: Zdzichu

Discord: Sergi4UA
Telegram: https://t.me/sergi4uaworkshop
YouTube: https://youtube.com/user/Sergi4UA
Part of the Serious Sam Hyper Server and Serious Sam Community on Telegram: https://t.me/Serious_Sam_Hyper_Server

Enjoy :)

This release was done with a lot of time spent on debugging and fixes. I always wanted to play the original Alpha version and Origins, but unfortuantely Origins mod was cancelled... but at least we have this alpha version.
I made this project in hopes that the Serious Sam community will find it useful... if you like the project, just a simple "Thank You" is enough :)