# Native keyboard and mouse controls

Run `Launch.cmd` for audio, or `Launch.cmd mute` for a muted game.
Keyboard/mouse play captures the cursor as soon as Continue or a confirmed New
Game starts loading. Camera movement begins when the gameplay client is ready;
mouse movement during loading is discarded. Escape and Alt-Tab release capture.
The first controller detected by the game uses the same local profile as keyboard and mouse,
regardless of its Windows controller slot. It stays selected until disconnected;
start the game with only your preferred controller connected to choose it.
Run `LaunchWithSettings.cmd` to choose video options and language before the game
opens, then play with audio. It reads and saves `DarkRecomp.settings.ini`, so
the pre-game launcher and in-game Video Settings share your choices.
Choose **Exit Game** in the pause or main menu and confirm to close the game,
or close its window. `Launch.cmd preview` and
`build_native/Release/DarkRecompPreview.exe` still default to muted;
the preview executable also accepts `--sound` or `--mute`.

Choose **Extra Content > Achievements** in the main menu to view achievement names,
descriptions and unlock status. Select a row to read its details; **Esc** returns
to the menu. The Xbox achievement screen opens a native viewer with earned points
and a **Close** button. Progress is saved locally in
`saves/achievements.dat`; keep that file when updating the port.

Normal launchers record lightweight slow-frame timings in their runtime log.
Automatic screenshots are disabled during normal play; use the preview executable's
`--capture-frames` option when diagnostic BMP captures are specifically needed.

Use `LaunchStallProfiler.cmd` to identify runtime hitches while playing with sound
and your saved settings. Close the game after reproducing the hitch, then open the
newest `build_native/run/desktop-*/runtime.log`. `[STALL]` lines report frames over
16.67 ms, runtime/HLE calls over 2 ms, and the largest contributors to a slow
frame. `[WAIT]` lines include duration, thread ID, guest PC/caller, function, and
the waited object or fence when available. Sections cover guest code, rendering,
audio, file I/O, waits, and Present. Profiling starts disabled; the launcher enables
`DARKRECOMP_STALL_PROFILE=1` only for its run. Extra game arguments are accepted.
Frame totals include normal pacing and VSync waits. Contributors use exclusive
wall time and show the frame thread separately from workers; concurrent worker
times can sum to more than the frame total. `pending=1` identifies a call still
in progress at the frame boundary. `dropped` or `incomplete_thread_intervals`
indicates that a bounded buffer or snapshot could not retain every observation.

For a requested rendering-stutter diagnostic, use `Launch.cmd render-profile`.
It records active render-thread instruction samples and their rendering phase in
`build_native/run/render-profile-*.log`. Close the game to finish recording. This
mode adds measurement overhead; use the normal launcher for regular play.

To record a gameplay stutter for later analysis, use `Launch.cmd stutter`.
It plays with sound under your saved graphics settings (no captures, no profiling),
auto-skips intro videos, and writes lightweight slow-frame timings to
`build_native/run/stutter-*.log` plus a slow-frame count at the end.

Both launchers default to borderless fullscreen using your monitor's aspect ratio,
including ultrawide and 32:9 displays. **Alt+Enter** switches between fullscreen
and a resizable window. Gameplay expands horizontally; movies keep their
original proportions. Resizing the window fits the current image without
stretching. Restart on a different monitor to select its aspect ratio.

The internal render height defaults to 720 pixels. Video Settings offers 360p,
480p, 720p, 1080p, 1440p and 2160p. Higher resolutions use native PC rendering
while keeping the original console allocations within their limits. Changes
apply after restarting the game. `--windowed --width 2560 --height 1080` selects
a window size and aspect ratio. Internal rendering supports up to 4096 x 2160,
preserving aspect at the width limit and rounding to supported game dimensions.

Open **F5** and check **Enable F6 resolution shortcut**, then press **F6** during
play to switch between **720p and 1440p** without reloading the level. The shortcut
starts disabled each launch; unchecking the option also cancels a pending switch.
A brief message shows the new resolution, which saves
automatically. The switch preserves the launch aspect ratio and existing game
buffers. It works with a 720-pixel-high game buffer: normal 16:9 launches at
720p, 1440p or 2160p, and typical 21:9 launches at 720p or 1440p. Other launch
resolutions or capped wide aspects show an unavailable message; choose 720p
before launching on a normal or 21:9 display to enable it.

Open **Options > Video Settings** for the game's native graphics rows:

- **Brightness:** 50% to 200%, with 100% neutral.
- **Gamma:** the original 12-step calibration control, with the middle position
  as default. Use Left/Right to adjust and choose Yes when leaving Options to save.
- **Color correction profile:** the original seven presets, from **Profile 0 (None)**
  through **Profile 6**. Use Left/Right to change and choose Yes when leaving
  Options to save.
- **Field of view:** Original or **60�120 horizontal degrees at 16:9**;
  ultrawide displays show more at the sides.
- **Bloom:** on by default; toggle the final-composite glow.
- **Vertical sync:** off by default; synchronize presentation to the display.
- **Frame limit:** 30, 60 (default), 90, 120, 144, 165, 240, 360, or Unlimited.
- **Resolution:** 360p, 480p, 720p, 1080p, 1440p or 2160p; requires a restart.
- **Display:** windowed or borderless fullscreen.
- **Motion blur:** Off (default) or On.
- **Antialiasing:** Off (default), FXAA, SMAA, MSAA 2x, MSAA 4x, or MSAA 8x.
  Applies immediately and saves automatically.
- **Language:** System, English, German, French, Spanish, or Italian; requires a restart.
- **Texture filtering:** Original, 2x, 4x, 8x, or **16x anisotropic (default)**.
  Applies immediately to world textures while preserving the original UI and effects sampling.
  Explicit levels override the original world-texture setting; Original preserves it.

Use the normal menu navigation: Up/Down selects a row,
Left/Right changes its value, and confirm cycles forward. The controller D-pad
and A work through the same original menu controls. Escape/Backspace or B goes
back. PC graphics changes save automatically to `DarkRecomp.settings.ini` beside
`Darkness` and apply immediately except resolution and language. Gamma uses the
original game profile and save confirmation. A failed PC settings save is shown in the row text.

All play modes use saved settings. Direct-launch options `--fov 100`, `--fps 120`,
`--render-height 720`, `--fullscreen` / `--windowed`, and `--vsync` / `--no-vsync`
override their saved values for that run. `--fov 0` selects Original. Editing a
menu option saves the current selection, including overrides. Valid custom
values remain unchanged until you adjust their row.

Choose **Language** in **Options > Video Settings**, or in the settings launcher
opened by `LaunchWithSettings.cmd`. In-game changes save immediately and take
effect after restarting; the launcher applies your choice when you click Play.
System follows the Windows UI language when it is English, German, French,
Spanish, or Italian; other UI languages use English. You can also edit
`DarkRecomp.settings.ini` beside `Darkness` and restart:

```ini
[Game]
Language=en
```

Use `en`, `de`, `fr`, `es`, or `it`; `auto` restores automatic selection.
The matching language content must be present in your own dump. Run
`Launch.cmd play --language en` to choose English for one run. When launching
`DarkRecomp.exe` directly, use the same `--language en` option; full English
language names are also accepted. This override applies only to that run and
does not replace your saved language choice when you adjust graphics settings.

The supported Russian localization uses the English content slot. With that
dump, choose **English**, or **System** on Russian Windows, to load its translated
text and audio. Keep its own executable and all matching content files together.
The added video, keyboard/mouse and exit menus follow the same selection.
Their Russian strings are in `assets/localization/native_menu_ru.json`; see
[the translation guide](assets/localization/README.md) for editing and rebuilding.

In menus, move the cursor over a choice and click to select it. The game releases
mouse capture while a menu is open and captures it automatically when keyboard/mouse
gameplay starts or resumes. Controller menu navigation leaves the mouse released.
After a manual release or Alt-Tab, click inside gameplay to recapture the mouse;
that first gameplay click only captures it.
Press **F1** for the controls guide, **F2** to toggle capture, or **Escape**
to pause and release the cursor. Alt-Tab, loss of focus, and closing the window
also release it. Click again after returning to the game.

- **WASD:** move; **mouse:** look.
- **Space:** jump while the mouse is captured; confirm or skip intros when released.
- **E:** use / confirm; **R:** reload; **Ctrl** or **C:** crouch.
- **Left click:** fire the right weapon; **right click:** fire the left weapon.
- **Middle click** or **Shift:** zoom.
- **Wheel:** previous/next choice in dialogue and menus, even with the mouse
  captured; during gameplay with mouse capture, cycle weapons. Releasing
  capture with **F2** disables gameplay wheel input. **1 / 2:** cycle weapons.
- **Q:** manifest Darkness; **G:** use Darkness power; **3 / 4:** cycle powers.
- **F:** redirect Darkling; **Tab:** journal; **Enter:** Start / pause.
- **Menus:** arrows to navigate, **E** or released **Space** to confirm,
  **Escape** or **Backspace** to go back.
- **Keyboard alternatives:** I / J / K / L to look, Z / X to fire left / right.

These are the default bindings. Open **Options > Controls > Keyboard bindings**
to change gameplay keys, mouse buttons, and wheel directions inside the original game menu. **Previous** and **Next**
switch between five pages of actions. Select a primary or secondary slot, then
press the new key or mouse button, or scroll up/down; **Escape** cancels capture and **Delete** clears the slot.
Left, right, middle, and the two side buttons (**M4 / M5**) can be assigned,
along with **Alt**. Release the click used to select a slot before pressing
the button or wheel direction to bind. **WHEEL UP / WHEEL DN** can be assigned
to either slot.
Each scroll step briefly presses its assigned action. Page **5 / 5** contains
**Prev Dialogue** and **Next Dialogue**, with separate primary and secondary slots.
Their defaults are **3 / Wheel Up** and **4 / Wheel Down**. Bind a different key,
mouse button or wheel direction there, or clear a slot with **Delete**.
Dialogue bindings only act while choices are visible. They can share inputs with
gameplay bindings without replacing them; weapon scrolling resumes after choices
close. Clearing the dialogue wheel slots disables wheel selection in dialogue.
Ordinary menu navigation remains available regardless of these bindings.
**Alt+Enter** and other host shortcuts stay available.
**Defaults** stages the original selection. **Save** applies the changes
and writes the `[Keyboard]` section of `DarkRecomp.settings.ini`.
**Cancel** or Back discards unsaved edits. Menu navigation stays available outside
key capture; reserved host shortcuts cannot be assigned.
In-game keyboard prompts follow the selected bindings.

The original game's camera sensitivity controls the controller. Mouse look
uses a separate, linear sensitivity and follows the game's inversion option.
Open **Options > Controls > Mouse sensitivity** to adjust it from **0.1x to 10x**
in 0.1x steps with Left/Right. Confirm or clicking the value increases it.
Changes apply immediately and save between launches; the default is **1x**.
The `--mouse-sensitivity` launch argument overrides the saved value for that run.
For example, `Launch.cmd play --mouse-sensitivity 2` uses a 2x multiplier.
Add `--mute --timeout-ms 0 --engine-preview` for a
muted interactive run. The play modes supply the interactive flags; the default
mode plays with sound instead of `--mute`.

## Developer tools

Press **F5** to open or close the developer panel. Select a campaign destination
and press **Load**, choose a player speed from **0.25× to 4×**, or toggle
**Invincible** and **Noclip**. Player controls become available once a player is active.
**Unlock all Darkness** grants all six original Darkness abilities.
**Max Darkness level** raises progression to the original maximum, level 5;
it preserves an existing higher heart count. These one-shot grants can be included
in later autosaves. Back up your checkpoint before changing progression.
**Enable F6 resolution shortcut** allows the live resolution hotkey for this session;
it starts off. **Restore defaults** returns to 1× speed and disables invincibility,
noclip and the F6 shortcut. It does not undo Darkness progression grants.
**Load** closes the panel before the mission's opening sequence; press F5 to reopen it.

Loading a mission restarts the current session, then loads the destination
through the game's commands. It can write an autosave. Back up your checkpoint
before jumping between missions if you want to preserve your current progress.
The panel releases mouse capture and
blocks gameplay input while open; close it, then click the game or press F2
to resume mouse control.

## Cheats

Press **F7** to open or close the cheats overlay in the middle of the game
window. It blocks gameplay input while open; close it, then click the game or
press F2 to resume mouse control. Options last for this session only.

- **Player:** speed (0.25x to 4x), **Bhop** (hold jump to keep hopping; speed builds by 30% a second up to **Hop max**, 1.5x to 10x), **Air strafe** (while hopping you keep moving forward without W; speed builds only when you strafe with A/D toward the way you turn the mouse, and slowly drops otherwise), **Speedometer** (horizontal speed near the bottom of the screen) with an optional **Speed graph** of the last three seconds, **God mode** (invincible), **Noclip**, **Infinite ammo**, **God darklings** (your summoned darklings cannot die)
  (guns never use rounds; reload once if the magazine was already empty),
  **Unlock all Darkness**, **Max Darkness level** and **Camera FOV** (Original or
  60 to 120 degrees; this session only, Video Settings saves it permanently).
- **Aim:** **Aimbot** turns your view toward the head of the living NPC closest
  to your crosshair (within about 25 degrees) while you hold **V** or **Mouse 4**.
  It does not check line of sight. The **FOV** slider sets how far from the
  crosshair it will lock (5 to 90 degrees); **Show FOV circle** draws that area.
- **Aim:** the key button next to **Aimbot** sets its hold key: click it, then press any key or mouse button (Backspace restores V / Mouse 4, Escape cancels).
- **Teleport:** **Save position** / **Load position** (or **F9** / **F10** in game) store your spot and return to it.
- **Aimbot tuning:** choose the target nearest the crosshair or nearest by distance, and how quickly the aim moves (**Aim speed**; 100% is the fastest that stays steady).
- **Crosshair:** off, dot, cross or circle, with a color and size.
- **ESP filter:** **Enemies only** hides civilians and story characters from the ESP and the aimbot (judged from their name keys).
- **Menu:** cycle the F7 menu **Theme** and set its **Opacity**.
- **Spawn:** **Remove spawned** deletes every character you spawned. The speedometer also shows the current hop run's time and best speed.
- Cheat settings are saved to `DarkRecomp.cheats.ini` each time the overlay closes and restored on the next start.
- **Weapons:** select a campaign gun and press **Give** (or double-click it).
  A weapon can only be given in a level that loads it.
- **Visuals:** **Box ESP**, **Skeleton ESP** and **Tracers** (lines from the
  bottom of the screen) draw over every living NPC the game is animating, even
  through walls, with optional **Health**, **Distance** and **Names** (the level designer's name, e.g. Mobster) labels; **Range** hides targets farther than the chosen meters. Each color button cycles through eight
  colors, and **Opacity** sets how see-through the drawing is. They include
  friendly and neutral characters.
- **Spawn:** pick a character (gangsters, SWAT, civilians, WW1 soldiers or
  darklings) or type any template name and press **Spawn** to
  create it 96 units in front of you. Only templates the current level has
  loaded can spawn. `python tools\list_weapon_templates.py --npcs` lists the
  NPC templates in each level.
- **Mission:** choose a destination and press **Load**.
- **Settings:** the F6 resolution shortcut, **Restore defaults** (turns every
  option off and resets speed), and **Dump** (diagnostic data for the runtime log).

Mission loads, Darkness grants and given weapons can be autosaved. Back up your
checkpoint first. The F5 developer panel remains available with the same tools.

## Implementation

Keyboard input uses Win32 window messages. Mouse look uses foreground Raw Input
(`WM_INPUT` / `GetRawInputData`) with relative mouse counts. These native C++
paths feed the AOT game's existing input ABI directly. No virtual controller,
external remapper, or runtime interpreter is installed or required. Physical
XInput controllers remain supported.

Relative mouse counts are kept separately from the XInput state. Reading
controller input does not consume mouse movement or turn it into a held stick
velocity. Mouse counts feed the game's relative-angle commands, bypassing
stick dead zones, acceleration and speed limits while retaining the original
player look restrictions, pitch bounds and zoom behavior. The default is
8/65536 turn per count (about 0.044 degrees). Fractions smaller than one game
angle unit carry into the next movement; fast swipes split into complete angle
commands instead of wrapping their signed-short fields. Releasing capture
clears held mouse buttons, queued wheel events and pending motion.
Absolute-position raw devices are not used for look.

Action bindings follow the original [2K game manual](https://device.report/m/ba98250caa0f4727559b5c36a08a35cb76cb0d40ca14be8787638ba49d708053).
Raw Input uses the [Microsoft Win32 interface](https://learn.microsoft.com/en-us/windows/win32/inputdev/using-raw-input).

## Prompt artwork source

In-game button prompts use the light keyboard and mouse artwork from
[Xelu's free CC0 prompt pack](https://thoseawesomeguys.com/prompts/). When a physical
controller becomes the primary active input, prompts switch to the original
controller artwork; keyboard/mouse activity, focus loss, or controller
disconnect restores the keyboard/mouse set. Shared action icons combine their
bindings, such as R/Esc in menus or Shift/middle mouse during gameplay.
Gameplay prompts use their action bindings, so portal choices show R without
Esc. Keyboard and mouse keycaps use 64px artwork and appear 25% larger when
the screen layout has room. The artwork is embedded in the
game; no separate icon download is needed.

## Console

The F7 overlay's CONSOLE box runs any original engine command through the
game's own `CConsole::ExecuteString`. Type a command and press Enter or RUN;
a bare name gets `()` appended, and arguments use script syntax, for example
`setdifficulty(2)` or `changemap('ny1_chinatown', 0)`. Recent commands are kept
in the drop-down. Commands are developer leftovers: some do nothing in retail,
and some can crash the game or break saves.

<details><summary>All registered commands</summary>

`abs` `acos` `addlocalclient` `addoption` `addviewscale` `asin` `atan` `award_delayed_achievement` `benchmark` `bind` `bind2` `bind3` `bindlist` `bindlistscan` `bindrep` `bindrep2` `bindscan` `bindscan2` `cachecommand` `camera_tweak` `campaignmap` `canquit` `capture` `capturedemo` `capturescreen` `ceil` `cg_backout` `cg_blackloading` `cg_clearmenus` `cg_confirmrumble` `cg_cuberesetwindow` `cg_cubeseq` `cg_cubeseqforce` `cg_cubeside` `cg_dowindowswitch` `cg_dumpwnd` `cg_forceblackgame` `cg_grabscreen` `cg_loadlastvalidprofile` `cg_menu` `cg_menuimpulse` `cg_newgame` `cg_playrandommusic` `cg_playsound` `cg_playsound_sfxvol` `cg_prevmenu` `cg_prevmenu2` `cg_reloaduiregistry` `cg_rootmenu` `cg_rootmenu_ingame` `cg_scrflt` `cg_showdevtext` `cg_submenu` `cg_switchmenu` `changemap` `cheat` `chechassavegames` `checkbrokendc` `cl_clearstats` `cl_dumpstats` `cl_synconclientrender` `cls` `commands` `commitdebugdifficulty` `connect` `ConOut` `continuewithoutsave` `cos` `cosh` `cpu` `cpu_disable` `cpu_enable` `cpu_updatefreq` `crash` `crashCCException` `dbgcam_lookvelocity_x` `dbgcam_lookvelocity_y` `dbgcam_movebackward` `dbgcam_moveforward` `dbgcam_moveleft` `dbgcam_moveright` `dc_getdesc` `dc_getn` `dc_getname` `dc_set` `deferredscript` `deferredscriptgrabscreen` `deleteallsavegames` `deleteprofile` `deletesavegame` `deletesavegames` `deloption` `demo` `dir` `disconnect` `docachedcommand` `doprecache` `echo` `equip_weapon` `exec` `exp` `fakedeleteuidone` `floor` `forceoption` `freeresources` `GetExePath` `GetPlatformName` `handlecurrbrokendc` `help` `hintload` `history` `in_debugkeys` `in_enablevibration` `in_logkeys` `in_setgamepadmapping` `inv_forceupdate` `inv_mod` `inv_switch` `inv_switch2` `inv_unmod` `isallowedtosave` `issignedin` `launch` `list` `listoptions` `loadgame` `log` `log10` `loot_exit` `loot_money` `loot_object` `map` `mcc_gaf` `mod` `mp_abortnetworkinfo` `mp_askkickplayer` `mp_askshutdown` `mp_askshutdownsession` `mp_autoswitchweapons` `mp_broadcast` `mp_cancelconnect` `mp_canopenchangegamemode` `mp_canopenchangemap` `mp_clearachievements` `mp_clearleaderboard` `mp_connectionspeed` `mp_createsession` `mp_customgame` `mp_error` `mp_errorunknown` `mp_getgamemode` `mp_init` `mp_initonline` `mp_joininvitedsession` `mp_kickplayer` `mp_lan` `mp_leaveserverlist` `mp_minplayers` `mp_openmultiplayeroptions` `mp_quickgame` `mp_ranked` `mp_ready` `mp_searchforsessions` `mp_selectweapon` `mp_setdarklingmodelname` `mp_setgamemode` `mp_setmodelname` `mp_setmultiplayerstatus` `mp_setteam` `mp_showfriends` `mp_showleaderboard` `mp_shutdown` `mp_shutdownlan` `mp_shutdownsession` `mp_startgame` `mp_switchtolobby` `mp_togglemultiplayerstatus` `mp_updateadvancedsettings` `mp_updatesession` `multithread` `nextcontent` `noop` `option_checkneedsave` `option_clear` `option_clearbinds` `option_getfromenv` `option_getfromopt` `option_preupdate` `option_reset` `option_save` `option_setdefault` `option_setdefaultbinds` `option_setdefaultglobal` `option_update` `padlock` `pause` `pausetimer` `pausetimer5` `play` `prevcontent` `quit` `quittomain` `r_anisotropy` `r_antialias` `r_backbufferformat` `r_directvbm` `r_disable` `r_displayformats` `r_dumptextures` `r_dynamicloadmips` `r_enable` `r_filter` `r_forcemaxmap` `r_forcemipbias` `r_forcemipbiasadd` `r_frontbuffer10bit` `r_picmip` `r_showgamma` `r_textureformats` `r_updateprograms` `r_vmodes` `r_vsync` `r_zformats` `rand` `rcd` `rdir` `rdump` `rdump2` `record` `RecreateBinds` `regset` `removepadlock` `resettimer` `richpresence` `rs_asynccacheblock` `rs_classes` `rs_resources` `rs_resourcesbyclass` `rs_resourcessorted` `rs_updateanims` `rs_updatemodels` `rs_updateregistry` `rs_updatesurfaces` `rset` `savegame` `saveprofile` `scriptlayer` `setASR` `setdifficulty` `setgame` `setgameclass` `setgamekey` `setloadingtext` `setnextgametype` `setoption` `setpreloadflag` `setwidescreen` `showAnimToggle` `showbind` `showbindscan` `showDebugVarToggle` `showFPSLiteToggle` `showFPSToggle` `showinfoscreen` `showreconnectpadtext` `showSoundToggle` `showStatsToggle` `showvbetoggle` `silentlogon` `simulatefractional` `sin` `singlestep` `sinh` `smap` `snd_dump` `snd_mix_autoreload` `snd_mix_reload` `soakmode` `splitaxis` `splitscreen` `srand` `stop` `storageselect` `stream_play` `stream_relation` `stream_stop` `stream_volume` `sv_characterpush` `sv_clearstats` `sv_clientvar` `sv_clvars` `sv_dialogueinfo` `sv_dumpstats` `sv_executetestrun` `sv_forceberserker` `sv_game` `sv_info` `sv_objdestroy` `sv_perfgraph` `sv_physrender` `sv_restart` `sv_set` `sv_vars` `sv_wlod_maxlights_bsp0` `sv_wlod_maxlights_bsp1` `sv_wlod_maxlights_trimesh0` `sv_wlod_maxlights_trimesh1` `sv_wlod_maxshadows_bsp0` `sv_wlod_maxshadows_bsp1` `sv_wlod_maxshadows_trimesh0` `sv_wlod_maxshadows_trimesh1` `sv_wlod_range_dynlightfadestart` `sv_wlod_range_dynlightfadestop` `sv_wlod_range_lightfadestart` `sv_wlod_range_lightfadestop` `sv_wlod_range_model_bsp` `sv_wlod_range_model_bspdecal` `sv_wlod_range_model_bspprez` `sv_wlod_range_model_trimesh` `sv_wlod_range_model_trimeshdecal` `sv_wlod_range_model_trimeshprez` `sv_wlod_range_onclientrender` `sv_wlod_range_onclientrender_char` `sv_wlod_range_onclientrender_enginepath` `sv_wlod_range_onclientrender_object` `sv_wlod_range_onclientrender_pickup` `switchcontrollermethod` `sys_exit` `sys_memorycheckenable` `sys_memorydebug` `sys_memoryhideallocs` `sys_memoryreport` `sys_memoryshowallocs` `sys_memorytracking` `sys_perfcountrender` `sys_perfcountsystem` `sys_showmemory` `sys_testmemory` `sys_tracerecordgamerefresh` `sys_tracerecordrender` `sys_tracerecordsystem` `tan` `tanh` `threadpool` `timedemo` `timedemograph` `timeleap` `toggleASR` `toggleShowMemInfo` `toggleShowNetworkStats` `tryresave` `trysignin` `unbind` `unequip_weapon` `unlockall` `vid_blacklevel` `vid_brightness` `vid_cableprofile` `vid_gamma` `vid_pixelaspect` `viewscale` `vp_aspectratio` `vp_backplane` `vp_fov` `vp_frontplane` `vp_zoom` `wdump` `wenable` `wswitch` `xr_bspdebugflags` `xr_bsptoggledebugflags` `xr_cc_blacklevel` `xr_cc_cableprofile` `xr_cc_gamma` `xr_cctest` `xr_cctesttexture` `xr_clearviewport` `xr_clipvc0` `xr_debugflags` `xr_debugfontsize` `xr_dlight` `xr_drawmode` `xr_fastlight` `xr_flares` `xr_fogculloffset` `xr_freeze` `xr_lightdebugflags` `xr_lod` `xr_lodoffset` `xr_lodscale` `xr_maxportals` `xr_maxrecursion` `xr_modeoverride` `xr_nhfog` `xr_objectsonly` `xr_optimizedsurfaces` `xr_particles` `xr_portalsonly` `xr_portaltexturesize` `xr_ppdebugflags` `xr_ppexposureblacklevel` `xr_ppexposurecontrast` `xr_ppexposuredebug` `xr_ppexposureexp` `xr_ppexposurelevelsmoderate` `xr_ppexposuresaturation` `xr_ppexposurescale` `xr_ppglowbias` `xr_ppglowcenter` `xr_ppglowexp` `xr_ppglowgamma` `xr_ppglowscale` `xr_ppglowzone` `xr_ppmbdebugflags` `xr_ppmbmaxradius` `xr_pptoggledynamicexposure` `xr_pptoggleexposuredebug` `xr_shaderdebugrendermode` `xr_shaderfalloffexp_gamma` `xr_shaderfalloffexp_linear` `xr_shaderlfmscale` `xr_shaderlightscale` `xr_shadermode` `xr_shadervirtualattr` `xr_shadowdecals` `xr_shadowdecaltexturesize` `xr_shadowmapmode` `xr_showbounding` `xr_showfoginfo` `xr_showportalfence` `xr_showrecurse` `xr_showtiming` `xr_showvbtime` `xr_showvbtypes` `xr_showvelocity` `xr_sky` `xr_smm_debugflags` `xr_smm_polygonoffset` `xr_specularforcepower` `xr_specularmode` `xr_stencilshadows` `xr_surfoptions` `xr_synconrender` `xr_tlenable` `xr_unifiedambience` `xr_unifiedclear` `xr_vfog` `xr_wallmarks` `xr_worldonly` `xr_zfog`

</details>
