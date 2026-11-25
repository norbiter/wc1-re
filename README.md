# Wing Commander source reconstruction and SDL2 port

This project recreates the source of **Wing Commander** as shipped in *Wing
Commander: The Kilrathi Saga* (1996). The rebuilt `WC1.EXE` matches the
original byte for byte.

It also provides a native port for Windows, Linux, and macOS that plays the
Kilrathi Saga and FM Towns versions, the original DOS release (in part), and the
Super Wing Commander Mac demo.

No original game data or Sega CD voice recordings are included.

## About this fork

This is a personal fork that tracks upstream
([neuromancer/wc1-re](https://github.com/neuromancer/wc1-re)) and keeps the
SDL2 port building and playable, with some fork-only features and fixes on top.
It does not pursue a byte-identical reconstruction itself: the upstream
reconstruction tooling (`make report`, `make compare`, `make verify`) is
inherited but not part of this fork's workflow. Nothing here is pushed back to
upstream; if it is published, it goes to a separate fork.

To keep rebasing onto upstream cheap, fork changes stay small and live in
`src/sdl/` or in the `SDL_PORT` arm of conditionals in shared source. See
[AGENTS.md](AGENTS.md) for the working rules.

If you find this project useful, you can [support its development through
GitHub Sponsors](https://github.com/sponsors/neuromancer).

## Screenshots

| Mission briefing | Tiger's Claw hangar |
| --- | --- |
| [![Mission briefing and flight path aboard the Tiger's Claw](screenshots/mission-briefing.png)](screenshots/mission-briefing.png) | [![Fighter parked in the Tiger's Claw hangar](screenshots/tigers-claw-hangar.png)](screenshots/tigers-claw-hangar.png) |

| Title sequence | Cockpit combat |
| --- | --- |
| [![Wing Commander title screen](screenshots/title-screen.png)](screenshots/title-screen.png) | [![Cockpit combat targeting a Dralthi](screenshots/cockpit-combat.png)](screenshots/cockpit-combat.png) |

## Download and play

Download the package for your platform from
[GitHub Releases](https://github.com/neuromancer/wc1-re/releases):

- **macOS 15 or later:** open the `.dmg` for Apple Silicon (`arm64`) or Intel
  (`x86_64`) and drag **Wing Commander** into **Applications**.
- **Windows:** run the `-setup.exe` installer, then open **Wing Commander**
  from the Start Menu.
- **Steam Deck / Linux:** in Desktop Mode, install the `.flatpak` with
  `flatpak install --user ./downloaded.flatpak` (replace the filename), then add
  **Wing Commander** to Steam as a non-Steam game. In Game Mode the launcher
  fills the screen, with its settings and controls in tabs and the joystick
  calibration in the same window.
- **Portable ZIPs** for Windows, macOS, and Linux: extract and run, keeping the
  bundled files together.

Game data is not included. Open the app, choose a detected installation or use
**Add path…** to select the game's folder: a Steam or GOG copy's main folder,
or any folder containing `GAMEDAT`. Then select **Start WC1**. The launcher
remembers your folder and options.

- **Secret Missions:** with Kilrathi Saga or DOS data that includes the
  expansions, the launcher also offers **Start SM1** and **Start SM2**, and can
  transfer a pilot from an existing save with their statistics and awards. Both
  releases play the expansions the same way, following the Kilrathi Saga, but
  SM2's captured Dralthi keeps its DOS disguise: in the stealth missions the
  Kilrathi ignore it until it fires on them. SM2's barracks poster can again
  be clicked for a closer look, as in the DOS release. With DOS data, SM2
  keeps its saved games in `GAMEDAT/CRUSADE.WLD`, as the DOS release does, so
  your DOS SM2 saves carry over.
- **Secret Missions 1.5:** the fans' port of *Super Wing Commander*'s Covert
  Missions plays on SM2. Put its four files (`CAMP.015`, `MODULE.015`,
  `BRIEFING.015` and `TITL15.VGA`, from either its DOS or its Kilrathi Saga
  package) in `GAMEDAT` beside SM2's, and the launcher offers **Start SM1.5**
  (`--campaign=sm15` on the command line). It shares SM2's saved games, as its
  patched executable does. Its first briefing plays without the edited
  `MODULE.015` the Kilrathi Saga needs.
- **Languages:** choose **Español**, **Français**, or **Japanese (FM Towns)**
  in the launcher's **Language** menu.
- **German fan translation:** a DOS installation you have patched yourself
  with the German fan translation shows its German text, including the menus
  and cockpit messages it translates in `WC.EXE`. The launcher's **Language**
  menu then shows **German** only, and some artwork may stay in English.
- **Music and sound effects:** the launcher's **Music** and **Sound** menus
  choose each on its own. A *Kilrathi Saga* installation offers its own
  **Digital** music and effects, or the DOS ones on an **AdLib** or a
  **Roland MT-32**; a DOS installation offers the AdLib and the MT-32.
- **Roland MT-32:** the DOS music and effects can play on an emulated MT-32 or
  CM-32L. Put your own control and PCM ROM files in a folder, choose
  **Roland MT-32** under **Music** or **Sound** in the launcher, and select the
  folder with **MT-32 ROMs…**, which appears with it. The launcher names the
  machine it found, preferring the CM-32L. Music and effects on one MT-32
  share its channels as the original does. Without usable ROMs the AdLib
  plays instead. The command line takes `--music=default|adlib|mt32`,
  `--sound=default|adlib|mt32`, `--mt32-roms=DIR`, `--mt32-model=ID` (such as
  `mt32_1_07`) and `--mt32-check`, which reports what a ROM folder holds.
  Without `--sound`, the effects follow MT-32 music onto the MT-32.
- **FM Towns:** copy `WC.EXP` and the complete `GAMEDAT` folder from your CD
  into a writable folder and select it. For CD music, keep the disc image and
  place its CUE sheet beside `WC.EXP` as `TOWNS.CUE`.
- **Sega CD voices:** if you own the Sega CD recordings, put them in
  `Data/Voice` beside `GAMEDAT` to hear voiced WC1 dialogue and radio messages.
- **Super Wing Commander demo:** download the
  [Mac demo](https://archive.org/details/super-wing-commander-demo-1.0.2-mac),
  unzip it, and select its `SuperWing DEMO` folder.
- **Autosave:** the game saves to slot 8 after starting or loading a campaign
  and after each mission you survive; manual saves use slots 1–7. You can turn
  it off under **Gameplay**.

## Improvements

- Resizable window, fullscreen, and aspect-correct 4:3 presentation.
- Gamepad support, rumble, and alternative joystick layouts, including
  WCAT-style four-button controls and, for WC1, a Custom mode that rebinds
  every gamepad button, trigger and stick axis, and a flight stick's buttons,
  hats and axes, and a Steam Deck preset.
- An adjustable joystick dead zone (22% by default, as in the original) and a
  launcher page that calibrates WC1's joystick. It writes `GAMEDAT/j.cal` as
  the game's own calibration does, and the game now reads all of that file,
  not only the centre.
- Launcher tabs that show the gamepad, keyboard and mouse bindings for WC1
  and Super Wing Commander and let you rebind keys, mouse buttons and the
  wheel.
- Fixes to the 1996 release, such as correctly scaled planets, static on
  knocked-out cockpit displays, landing on the *Tiger's Claw* from any
  direction, and `Esc` pausing spaceflight.
- Sound effects balanced against the music and panned with the camera, and
  mouse wheel throttle control.
- A game speed choice: spaceflight runs as fast as its frame rate, so the
  launcher's **Speed** offers **Normal** (the *Kilrathi Saga*'s 20 FPS) and
  **Slow** (16 FPS). In flight, `[` and `]` (the two keys right of `P`) lower
  and raise the rate in half steps from 8 to 32 FPS, as the *Kilrathi Saga*'s
  `Alt+N` and `Alt+M` did; the launcher keeps that rate and offers it as
  **Custom**. The command line takes `--flight-fps=N`.
- Optional extras: an enhanced renderer that draws ships at full resolution
  while keeping the original artwork, cockpit, and HUD, and can also show the
  flight HUD's text at double resolution (launcher checkbox or `--hires-hud`):
  cockpit text in Trov's double-resolution cockpit font, for English, and
  radio messages in the game's larger 11-pixel font; EGA colours; the restored
  startup intro; a cockpitless view; and a cheat mode.

| Enhanced renderer | Enhanced cockpit combat |
| --- | --- |
| [![Enhanced renderer showing two ships at output resolution](screenshots/enhanced-space-objects.webp)](screenshots/enhanced-space-objects.webp) | [![Enhanced renderer during cockpit combat](screenshots/enhanced-cockpit-combat.webp)](screenshots/enhanced-cockpit-combat.webp) |

## Fork fixes and features

On top of upstream's improvements, this fork adds the following. It also
leaves out one of upstream's changes: the hi-res HUD fonts (`--hires-hud` and
its launcher checkbox), which `--ttf-text` replaces.

Every feature with a switch also takes `--no-<name>`, and the last of the two
given wins. Three switches set several at once; a feature named on the command
line keeps its setting whatever the bundles say:

- `--enhanced` draws at display resolution with OpenGL and turns on the
  presentation features marked *enhanced* below. Those marked *enhanced,
  models* are turned on only when relit `ship3d/` models are found (in the
  working directory's `ship3d`, or `ship3d` beside or one or two levels above
  the program).
- `--easier` turns on the difficulty features marked *easier*.
- `--classic` turns off the features marked *default* and stops `--enhanced`
  turning anything on, leaving the original game apart from the fixes.

`--help` lists them all; `--help-debug` lists the debugging ones.

| Fix or feature | Default / switch |
| --- | --- |
| Protected transports are no longer blown up for flying through an enemy-held nav point while you're far away (mission 7-1 scored the second transport as lost although it survived) | always on |
| Autopilot waits ("Drayman Inbound") while a friendly ship you're meant to meet is still due to jump in at the current nav point; the original left it behind, so in mission 7-1 clearing the Jalthi early and autopiloting home scored the second transport as lost | always on |
| Ship objectives (such as the Fralthi in mission 8-2) are sighted when the ship itself is on screen within range; the original checked whether your wingman was on screen, so with no wingman, or one out of view, the sighting never counted | always on |
| Stationary asteroids appear where the field placed them; a wrong type check gave them the mines' random setback of up to 1000 units along their heading | always on |
| Camera-relative panning for in-flight sound effects: correct left/right (it was mirrored), a steeper pan curve, and on Kilrathi Saga data a slight far-ear delay plus a muffled, quieter sound for sources behind you | always on |
| Kilrathi Saga sound effects get quieter with distance (the Win32 build passed the distance as the volume, so far-away sounds were the loudest) | always on |
| Ships drawn from point-cloud models at any angle, instead of the 37 sprite views (`SHIP_Vnn.ply` / `SHIPTYPE_Vnn.ply` from `ship3d/`, relit from their `.npz` data after a one-time `python3 bin/ship3dLight.py`. With `--enhanced` they are drawn at output resolution with continuous true-colour shading between palette ramp entries, and still follow palette fades; without it they are splatted into the 320x200 frame. Crevices are darkened by ambient occlusion worked out from each hull's distance field, and edges turned towards a rim light behind the ship pick up the ship's own lit colours, as does a faint Fresnel outline where the hull is seen edge-on) | *enhanced, models*: `--ship3d` or `--ship3d=<dir>` |
| 3D ships shaded in true colour instead of snapping each point to the nearest palette entry (with `--ship3d` and `--enhanced`; needs the version 3 `.light` sidecars from a fresh `python3 bin/ship3dLight.py`). These ships do not follow palette fades | *enhanced, models*: `--ship3d-truecolour` |
| 3D asteroids: the asteroids and rock chunks are drawn from point-cloud models (`ROCK_16.ply` / `ROCK_17.ply` in `ship3d/`, relit after `python3 bin/ship3dLight.py`) carved from the original tumble frames, instead of the 13- and 14-frame flipbooks. Each rock tumbles about the axis its frames turn about, at the rate and in the direction its animation steps, smoothly between frames (and at 60 fps with `--smooth`), and each gets its own attitude and phase so the field no longer turns in step. Colours are the palette entries the rock's own sprite uses, so they follow palette fades, and the rocks get the same ambient occlusion, rim light, `--backdrop-light` and `--glare` as 3D ships. Purely visual: collisions, the sim and its random sequence are unchanged. Uses the `--ship3d` directory if one is given | *enhanced, models*: `--rock3d` |
| Debug overlay: each ship's collision volume as a semitransparent magenta shape, for judging hit detection. Ships with a `ship3d/` model show their hull inflated by a laser bolt's radius (from a signed distance field of the point cloud); others show the collision sphere. Drawn into the indexed frame, so under the `--enhanced` sprite layer; combine with `--ship3d` to see it over ships | `--hitspheres` (`--help-debug`) |
| Fewer AI decisions in crowded fights, standing in for the frame-rate drop that eased big battles in 1996: once more than `<ships>` (default 6) fighters are in flight, each one's decision step runs every 2nd to 4th time instead of every time. Collision avoidance, flight physics and the player are unaffected | *easier*: `--ai-throttle` or `--ai-throttle=<ships>` |
| No friendly fire: a gun shot or missile that touches a ship on its shooter's own side vanishes without damage or impact effects, and blasts (missile, flak, exploding ships) no longer damage ships on the same side as their source; same-side collisions still bounce but do no damage. Enemy hits and asteroids are unchanged | *easier*: `--no-friendly-fire` (`--friendly-fire` keeps the original) |
| Gun-range crosshair: the central crosshair is dark grey until the target is within effective range of every enabled gun (muzzle speed plus your own speed, minus the target's, along your heading, times shot lifetime), then shows its normal bright colour | *default*: `--gun-range` |
| Shield flash: when a ship's shields take a hit, a translucent blue hemisphere flashes over it on the side the shot came from, follows the ship as it turns, and fades within about 0.3 s. Drawn into the indexed frame, or, when `--ship3d` ships are in the GL object layer, smoothly into that layer with them | *default*: `--shield-flash` |
| Mouse curve: pointer offset maps to yaw and pitch through a smooth curve, mostly linear near the centre and bending exponentially towards full deflection, instead of the original's uneven step table. Small adjustments stay small and a large flick reaches the maximum; the game still gets its -8..8 steps, so turn rates stay those of the craft. `=PCT` scales sensitivity (above 100 is more sensitive, default 100) | `--mouse-curve[=PCT]` |
| Joystick curve: the same linear-into-exponential shaping for gamepad and joystick axes, with an 8% dead zone (the extra axes otherwise use a fixed 25%). In the default original mode the game's own calibration still quantises the reshaped stick to its steps and keeps its one-step dead zone. `=PCT` scales sensitivity | `--joystick-curve[=PCT]` |
| Sturdier escorts: friendly transports and capital ships (Venture, Dilligent, Drayman, Exeter, Tiger's Claw) get double shields, buying time on escort and protection missions before you arrive | *easier*: `--sturdy-friends` |
| Fewer missiles to dodge: every Kilrathi ship spawns with at most one missile of each type it carries (SWC flight: ammo capped at 1 too), so early engagements are less about baiting out missiles | *easier*: `--low-missile` |
| Smooth spaceflight: upstream's frame interpolation, which draws frames between the game's steps at the display's rate while the sim, AI and physics keep their tick. In the fork it is an *enhanced* feature, and its in-between frames also carry the fork's dust, glare, 3D ships and asteroids, and fine shots. `--interpolate` (upstream's name) does the same | *enhanced*: `--smooth` |
| Fine space dust: the eight dust sprites are replaced by a field of about a thousand small motes that stand still in space around the ship, so they drift past by parallax and stretch into faint streaks along the direction of travel as speed builds. Each mote shimmers and glints at its own pace, as the original's tumbling sprites did. With `--enhanced` they are drawn at output resolution, down to one display pixel wide; without it they are single dimmed pixels in the 320x200 frame. With `--smooth` they move at 60 fps. Purely visual: the original dust slots, hazard fields and explosion sparks are unchanged, and the sim's random sequence is not touched. Not used in Super Wing Commander flight | *enhanced*: `--dust` |
| Fine stars: with `--enhanced`, the backdrop stars are drawn at output resolution instead of as scaled-up 320x200 crosses. Each is a sharp core in the colour of the original sprite's centre, a soft halo in the colour of its arms, and, for the brighter stars, four thin glare spikes square to the screen that fade towards their tips. Sizes and colours come from each star's own sprite, and the colours follow palette fades. Without `--enhanced`, the original sprites are drawn | *enhanced*: `--fine-stars` |
| Fine backdrop: with `--enhanced`, the backdrop's planets, suns, nebulae, asteroids and comets are redrawn at output resolution instead of as scaled-up 320x200 sprites. Planets get an exactly drawn limb fitted to the sprite, its colours smoothed out of their dither, a dark night side where the painting stops, and a thin atmosphere on the lit side. Suns, nebulae and comets are treated as light, so their glow fades into the backdrop. Asteroids keep their shape with a clean, smooth outline. The ringed planet is redrawn as a banded sphere inside a tilted ring, with the planet's shadow falling across the ring's far side. Colours follow palette fades. Without `--enhanced`, the original sprites are drawn | *enhanced*: `--fine-backdrop` |
| Fine shots: with `--enhanced`, laser, mass driver, neutron and turret bolts are redrawn at output resolution instead of as scaled-up sprites, which a close pass blew up into blocky rings. Each is rebuilt from its own sprite: its stepped colour bands are smoothed into one even gradient from the hot core outward, the dither within a band evens out, and its outline is redrawn as a clean, antialiased curve, keeping each bolt's shape (the mass driver's lopsided tail too). Laser and neutron shots are drawn as bolts: the ball sits at the shot's true position, turned along its path, with a trail behind it that fades towards the tail. The trail is the ball swept back to where the shot was three sim ticks earlier, so its hot core runs back as a line. It is a fixed length in space, foreshortened with perspective, and the same at 20 or 60 fps (`--smooth` moves the head each frame and lets the trail grow out of the gun by fractions of a tick). A new shot's trail never reaches back past the gun. Colours follow palette fades. Without `--enhanced`, the original sprites are drawn | *enhanced*: `--fine-shots` |
| Fine HUD: with `--enhanced`, the flight HUD's marks are redrawn as antialiased lines at output resolution instead of scaled-up 320x200 pixels: the gun crosshair, the missile lock marker, the nav pointer and the target brackets. Their shapes were traced from the original sprites, so size, placement and the 320x200 frame's aspect are kept, with strokes a little thinner than one game pixel (never under one display pixel). Colours follow palette fades, and `--gun-range` still greys the crosshair. The nav marker on the radar scanner stays a sprite | *enhanced*: `--fine-hud` |
| Backdrop light: with `--ship3d`, the 3D ships' rim light comes from the light the backdrop implies instead of a fixed spot behind them on screen. A large sun lights from its own direction; otherwise the largest planet lights from the side its sprite is painted lit (series 1's blue planet from above), and failing that a small sun. The light stays fixed in space, so as a ship turns, the edges facing the backdrop's light catch it. Series with only nebulae or the galaxy keep the fixed rim light. Where a planet's painting disagrees with its series' light, the planet is turned to face it (series 11's blue planet toward the red giant; in series 7 and 13 the smaller planet toward the larger one's light; four planets in Secret Missions 2 toward its large sun) | *enhanced, models*: `--backdrop-light` |
| Glare: with `--enhanced`, a large backdrop sun (the red sun, the red giant, Secret Missions 2's large yellow sun) dazzles. A haze in the sun's colour lies over everything drawn in space, strong round the sun and faint well beyond it, so craft near the sun lose contrast; the cockpit, HUD and target brackets stay clear. With `--ship3d`, a ship lined up with the sun shows its dark side, rimmed by the sun's light, and a ship over the sun's disc cuts the glare by the share it covers. Follows palette fades | *enhanced*: `--glare` |
| HDR: with `--enhanced` on a Mac with an HDR (EDR) display, light sources glow brighter than standard white while everything else keeps its usual brightness. With `--fine-backdrop`, the hot core of each backdrop sun and star; with `--glare` as well, a bloom over a large sun, brightest on its disc and falling off past its edge, which a ship over the disc dims; with `--fine-shots`, the cores of the gun bolts and their trails. The cockpit, HUD, sprites and 3D ships are unchanged. How bright depends on the display's headroom, which macOS sets from the display and its brightness setting; `--hdr=PCT` scales the extra brightness (default 100). Does nothing on other systems or on a display without headroom | *enhanced*: `--hdr` |
| TrueType text: with `--enhanced`, the game's text is drawn at display resolution in the SPACE WING LEADER font by SFO (CC BY 3.0) instead of the 320x200 bitmap fonts. Its caps match the game font's height, condensed by up to a fifth and then made smaller so each word stays within the space the original gave it, so word wrap, alignment and boxes are unchanged. Text follows palette fades. A glyph is replaced only while the pixels the game drew for it are still on screen; anything else, including the gradient title font, accented or translated characters and FM Towns Japanese, keeps its bitmap | *enhanced*: `--ttf-text` |
| Fair asteroids: no rocks spawned on an intercept course. The original aims some asteroids that appear in view at the spot you will reach after their flight time, so holding course means a hit; with this switch they drift like the rest of the field | *easier*: `--fair-asteroids` |
| Fuel matters to every ship: wingmen and enemies spawn with their type's rated fuel and burn it as you do (5 a tick in flight, 200 a tick on afterburner), but the original only stopped *your* afterburner on an empty tank, so a Salthi could outrun a Scimitar by burning forever. Now any ship with an empty tank can't light its afterburner, and a burn ends the moment the tank runs dry. A full load (200,000 for Kilrathi fighters) is about 50 seconds of afterburner. Running dry does not otherwise slow a ship, as in the original | *default*: `--npc-fuel` |
| Low enemy fuel: Kilrathi ships spawn with a quarter of their rated fuel (about 12 seconds of afterburner for a fighter), since an enemy flies one engagement while you and your wingman fly several. A dry ship whose fuel tanks are hit always explodes, as in the original | *easier*: `--low-enemy-fuel` |
| Cockpit-less flight (`c`) fills a fullscreen display of any shape instead of sitting in a 4:3 box: on a wider display the view gains field of view at the sides, on a taller one above and below. The middle of the picture is exactly the 4:3 one, and the instruments stay in that middle block. Windowed play, the cockpit views and everything outside flight keep the original 4:3 frame; switching to or from fullscreen in flight takes effect on the next frame | *enhanced*: `--aspectless` |
| Cockpit-less flight (`c`) can use a wider or narrower field of view than the original 90 degrees across the 4:3 picture. Ships, the nav cross and the overlays drawn over ships all follow it; a wider view makes everything smaller, but ships stay drawn and targetable out to the original range, and a missile lock holds over the original angle. The cockpit views keep the original perspective. Combines with `--aspectless`, which adds its extra width on top | `--fov=DEGREES` (60-140) |
| Debug mission picker: hold Alt while loading a saved game to jump to any series and mission (left/right = series, up/down = mission, Enter = go, Esc = keep the save as it was). Ship type follows the series that leads into the chosen one | `--mission-menu` (`--help-debug`) |
| Headless capture for automated testing: runs the game with no visible window and no sound, saves frames as PNG files and the mixed audio as a WAV, and plays scripted key and mouse input (see "Headless capture" below) | `WC1_CAPTURE_DIR`, `WC1_INPUT_SCRIPT` and related environment variables |
| Unknown `--` options are rejected with a list of the accepted ones (`MODERN_ARGS=--help` shows it without starting the game) | `--help` |

### Headless capture

For checking a change without touching the desktop, the game can run
privately, driven by environment variables (full reference at the top of
`src/sdl/capture.c`):

| Variable | Effect |
| --- | --- |
| `WC1_CAPTURE_DIR=dir` | Write PNG frames to `dir` (the 320x200 frame, or the GL output size with `--enhanced`) |
| `WC1_CAPTURE_INTERVAL=ms` | Also save a frame every `ms` milliseconds |
| `WC1_CAPTURE_AUDIO=file.wav` | Record the mixed game audio (22050 Hz stereo; not FM Towns CD audio) |
| `WC1_INPUT_SCRIPT=file` | Play timed input: `wait MS`, `key NAME [HOLD]`, `down`/`up NAME`, `text STRING`, `mouse X Y`, `click X Y [right]`, `shot [NAME]`, `quit` |
| `WC1_HIDDEN=1` | Create the game window hidden (needed for `--enhanced`, which requires a real OpenGL context) |

```sh
# Classic renderer: no window, no audio device.
WC1_CAPTURE_DIR=/tmp/cap WC1_INPUT_SCRIPT=test.script make run-modern-headless
# Enhanced renderer: hidden window, silent audio device.
WC1_HIDDEN=1 SDL_AUDIODRIVER=dummy WC1_CAPTURE_DIR=/tmp/cap \
    WC1_INPUT_SCRIPT=test.script make run-modern MODERN_ARGS=--enhanced
```

Script times are wall-clock milliseconds from the first event pump, so
leave generous waits around loading and intros.

### Known mission quirks and ideas

Some escort missions are notoriously fragile in the original game too (mission
7-1 is one of them), so these are likely original behaviour rather than
reconstruction errors.

- Entering the radius of *any* type-1 nav point, in any order, makes
  `ReleaseStaleNavTarget` switch the active sphere. `set_up_action_sphere` then
  explodes every on-screen ship of the old sphere and marks it dead (state 3),
  friendly escort targets included. This probably explains fighters and
  transports vanishing mid-fight when the player turns back toward a nav
  point such as the Tiger's Claw. Unconfirmed for the Krant that vanished
  later in 7-1.
- Possible fixes: remove protect/escort objective ships quietly on a switch,
  without the explosion or the dead mark; or defer the switch while hostile
  ships are near the player (would need a flag, as it changes pacing).
- `abandoned()` is now bypassed for ships a protect/escort objective refers to
  (always on). Other ships still get written off when they enter a hostile
  sphere more than 10000 from the player.

## Controls

| Shortcut | Action |
| --- | --- |
| `Cmd+Enter` (macOS), `Alt+Enter` (Windows and Linux) | Toggle fullscreen |
| `Cmd+Q` (macOS) | Quit the game |
| `Ctrl+S` / `Ctrl+M` (spaceflight) | Toggle sound effects / music |
| `Esc` (spaceflight) | Pause; press any key to resume |
| `[` / `]` (spaceflight) | Slow the game down / speed it up (the frame rate, 8 to 32 FPS) |
| Mouse wheel (spaceflight) | Increase or decrease speed |
| Gamepad RT / LT (spaceflight) | Same as A / B: fire guns / roll and speed (missiles in the WCAT layouts) |
| Gamepad Start (spaceflight) | Pause or resume |
| Gamepad Back | Cycle views in spaceflight; Escape/back elsewhere |
| Gamepad Y (`Y/N` prompts) | Confirm Yes |
| `J` (main menu, barracks), `Ctrl+J` (spaceflight) | Calibrate the joystick, as in the original |

The launcher's **Joystick** and **Keyboard and mouse** tabs show every
binding. Click a key or mouse input to change it, or **+** to add a second one;
changes apply when the game starts. `Esc`, and Super Wing Commander's menu
shortcuts and in-flight computer keys, cannot be changed.

In the Joystick tab's **Custom** control mode, press an input on the
controller, or click it on the gamepad drawing, to rebind it for WC1's
spaceflight, and point at a choice to see what it does. A button or trigger can
take any flight key's action, cycle the views, or turn yaw into roll and pitch
into speed while held. A stick axis or trigger can pitch, yaw, roll or change
speed, and a stick axis can also set the throttle; any of these can be reversed.
Custom starts from the WCAT four-button, four-axis layout. Menus keep their
fixed buttons, and Super Wing Commander keeps the WCAT four-axis layout.

The **Steam Deck** control mode (`--joystick-mode=steam-deck`) follows the
layout shared in [#32](https://github.com/neuromancer/wc1-re/issues/32): roll
and speed on the left stick, yaw and pitch on the right, and the D-pad on the
navigation map, damage display, communications and target lock. The Deck's
rear buttons mirror the D-pad: L4 up, L5 down, R4 left and R5 right; with
Steam Input, give those directions to the rear buttons in Steam's controller
layout. A is left free, as WC1 has no jump drive. Super Wing Commander keeps
the WCAT four-axis layout in this mode too. In Custom, **Source** picks
**Automatic**, **Gamepad**, **Steam Deck** or **Joystick**: Automatic follows
the connected device, Gamepad and Steam Deck show editable drawings (the Deck's
rear buttons share the D-pad's bindings), and Joystick lists numbered inputs
even for a device SDL knows as a gamepad. Choosing the preset keeps the Custom
bindings.

A joystick that SDL does not know as a gamepad, such as a flight stick, is
shown as its axes, hats and buttons by number, each lit while held, with the
axes named where the system describes them (X axis, Z rotation, slider, rudder
and so on). Custom rebinds its first 32 buttons, two hats and eight axes, and
starts with the stick steering, a twist or rudder taking the other turn and its
throttle lever setting the speed. Elsewhere its buttons stand in for the
gamepad's: 1 for A, 2 for B, 3 for X, 4 for Y and 9 for Back, with its first
hat as the D-pad.

As in the original, a mission ends only by landing on the *Tiger's Claw*,
ejecting, or dying; quit from the barracks menu.

## Build from source

```sh
git submodule update --init --recursive
make -j modern-gui
```

This needs Python 3.10+, a C/C++ compiler, the SDL2 and LZO2 development
packages, CMake, Rust, and a C++20 compiler; the game is written to
`out-modern/`. `make -j` rebuilds the original `WC1.EXE`.

## Acknowledgements

Special thanks to:

- [Origin Systems](https://en.wikipedia.org/wiki/Origin_Systems)
  who created *Wing Commander* and developed its *Kilrathi Saga* port;
- [Electronic Arts](https://www.ea.com/) for publishing *Wing Commander: The
  Kilrathi Saga*;
- [GOG](https://www.gog.com/en/game/wing_commander_1_2) for keeping *Wing
  Commander 1 + 2* readily available;
- AllTinker for the [W.C.A.T. overhaul](https://alltinker.itch.io/wcat), whose
  fixes and analysis of the DOS release have been valuable references;
- the [WCDX project](https://github.com/Bekenn/wcdx) for its pioneering
  compatibility work on the *Kilrathi Saga* release;
- L@Zar0 and darkpadawan for the Spanish translation v1.1; its
  [original terms](translations/upstream/Leeme.txt) specify GPLv3 with an additional
  no-charge condition. [Provenance](translations/provenance.json) records the import;
- WingLoader v0.91 for the initial associations between dialogue and recording filenames;
- [Greg14](https://www.wcnews.com/chatzone/members/greg14.11738/) for the French
  translation, adapted with permission; [provenance](translations/fr/provenance.json);
- Aaron Giles for the [DREAMM emulator](https://dreamm.aarongiles.com/), used
  to run and study the original release, and
  [ymfm](https://github.com/aaronsgiles/ymfm), which powers the SDL2 port's
  AdLib emulation;
- the [Munt project](https://github.com/munt/munt), whose libmt32emu
  (LGPL-2.1 or later) emulates the Roland MT-32 and CM-32L;
- Trov for the [double-resolution cockpit font](third_party/double-resolution-cockpit-font/README.md)
  (CC BY 4.0) that the hi-res HUD shows cockpit text in; and
- DaisenryakuFan for providing very advanced source reconstructions for the DOS
  releases of several *Wing Commander* titles.

## License

See [LICENSE](LICENSE). OpenAI Codex and Anthropic Claude were used during the
reconstruction.
