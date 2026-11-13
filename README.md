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
  Kilrathi ignore it until it fires on them. With DOS data, SM2 keeps its
  saved games in `GAMEDAT/CRUSADE.WLD`, as the DOS release does, so your DOS
  SM2 saves carry over.
- **Languages:** choose **Español**, **Français**, or **Japanese (FM Towns)**
  in the launcher's **Language** menu.
- **German fan translation:** a DOS installation you have patched yourself
  with the German fan translation shows its German text, including the menus
  and cockpit messages it translates in `WC.EXE`. The launcher's **Language**
  menu then shows **German** only, and some artwork may stay in English.
- **Roland MT-32 music:** the DOS scores can play on an emulated MT-32 or
  CM-32L. Put your own control and PCM ROM files in a folder, choose
  **Roland MT-32** under **Music** in the launcher, and select the folder with
  **MT-32 ROMs…**. The launcher names the machine it found, preferring the
  CM-32L. The MT-32 plays the sound effects too, sharing its channels with the
  music as the original does. A *Kilrathi Saga* installation can instead keep
  its own digital music or play the AdLib scores, and both keep its digital
  sound effects. Without usable ROMs the game plays the AdLib music. The
  command line takes `--music=default|adlib|mt32`,
  `--mt32-roms=DIR`, `--mt32-model=ID` (such as `mt32_1_07`) and
  `--mt32-check`, which reports what a ROM folder holds.
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
  every gamepad button, trigger and stick axis.
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
- Optional extras: an enhanced renderer that draws ships at full resolution
  while keeping the original artwork, cockpit, and HUD; EGA colours; the
  restored startup intro; a cockpitless view; and a cheat mode.

| Enhanced renderer | Enhanced cockpit combat |
| --- | --- |
| [![Enhanced renderer showing two ships at output resolution](screenshots/enhanced-space-objects.webp)](screenshots/enhanced-space-objects.webp) | [![Enhanced renderer during cockpit combat](screenshots/enhanced-cockpit-combat.webp)](screenshots/enhanced-cockpit-combat.webp) |

## Fork fixes and features

On top of upstream's improvements, this fork adds the following.

| Fix or feature | Enable with |
| --- | --- |
| Every held key keeps repeating, so accelerating while using the radio works | always on |
| Protected transports are no longer blown up for flying through an enemy-held nav point while you're far away (mission 7-1 scored the second transport as lost although it survived) | always on |
| Autopilot waits ("Drayman Inbound") while a friendly ship you're meant to meet is still due to jump in at the current nav point; the original left it behind, so in mission 7-1 clearing the Jalthi early and autopiloting home scored the second transport as lost | always on |
| Ship objectives (such as the Fralthi in mission 8-2) are sighted when the ship itself is on screen within range; the original checked whether your wingman was on screen, so with no wingman, or one out of view, the sighting never counted | always on |
| Stationary asteroids appear where the field placed them; a wrong type check gave them the mines' random setback of up to 1000 units along their heading | always on |
| Camera-relative panning for in-flight sound effects: correct left/right (it was mirrored), a steeper pan curve, and on Kilrathi Saga data a slight far-ear delay plus a muffled, quieter sound for sources behind you | always on |
| Kilrathi Saga sound effects get quieter with distance (the Win32 build passed the distance as the volume, so far-away sounds were the loudest) | always on |
| Ships drawn from point-cloud models at any angle, instead of the 37 sprite views (`SHIP_Vnn.ply` / `SHIPTYPE_Vnn.ply` from `ship3d/`, relit from their `.npz` data after a one-time `python3 bin/ship3dLight.py`. With `--enhanced` they are drawn at output resolution with continuous true-colour shading between palette ramp entries, and still follow palette fades; without it they are splatted into the 320x200 frame. Crevices are darkened by ambient occlusion worked out from each hull's distance field, and edges turned towards a rim light behind the ship pick up the ship's own lit colours, as does a faint Fresnel outline where the hull is seen edge-on) | `--ship3d` or `--ship3d=<dir>` |
| 3D ships shaded in true colour instead of snapping each point to the nearest palette entry (with `--ship3d` and `--enhanced`; needs the version 3 `.light` sidecars from a fresh `python3 bin/ship3dLight.py`). These ships do not follow palette fades | `--ship3d-truecolour` |
| Debug overlay: each ship's collision volume as a semitransparent magenta shape, for judging hit detection. Ships with a `ship3d/` model show their hull inflated by a laser bolt's radius (from a signed distance field of the point cloud); others show the collision sphere. Drawn into the indexed frame, so under the `--enhanced` sprite layer; combine with `--ship3d` to see it over ships | `--hitspheres` |
| Fewer AI decisions in crowded fights, standing in for the frame-rate drop that eased big battles in 1996: once more than `<ships>` (default 6) fighters are in flight, each one's decision step runs every 2nd to 4th time instead of every time. Collision avoidance, flight physics and the player are unaffected | `--ai-throttle` or `--ai-throttle=<ships>` |
| No friendly fire: a gun shot or missile that touches a ship on its shooter's own side vanishes without damage or impact effects, and blasts (missile, flak, exploding ships) no longer damage ships on the same side as their source; same-side collisions still bounce but do no damage. Enemy hits and asteroids are unchanged | `--ff` |
| Gun-range crosshair: the central crosshair is dark grey until the target is within effective range of every enabled gun (muzzle speed plus your own speed, minus the target's, along your heading, times shot lifetime), then shows its normal bright colour | `--gun-range` |
| Shield flash: when a ship's shields take a hit, a translucent blue hemisphere flashes over it on the side the shot came from, follows the ship as it turns, and fades within about 0.3 s. Drawn into the indexed frame, or, when `--ship3d` ships are in the GL object layer, smoothly into that layer with them | `--shield-flash` |
| Mouse curve: pointer offset maps to yaw and pitch through a smooth curve, mostly linear near the centre and bending exponentially towards full deflection, instead of the original's uneven step table. Small adjustments stay small and a large flick reaches the maximum; the game still gets its -8..8 steps, so turn rates stay those of the craft. `=PCT` scales sensitivity (above 100 is more sensitive, default 100) | `--mouse-curve[=PCT]` |
| Joystick curve: the same linear-into-exponential shaping for gamepad and joystick axes, with an 8% dead zone (the extra axes otherwise use a fixed 25%). In the default original mode the game's own calibration still quantises the reshaped stick to its steps and keeps its one-step dead zone. `=PCT` scales sensitivity | `--joystick-curve[=PCT]` |
| Sturdier escorts: friendly transports and capital ships (Venture, Dilligent, Drayman, Exeter, Tiger's Claw) get double shields, buying time on escort and protection missions before you arrive | `--sturdy-friends` |
| Fewer missiles to dodge: every Kilrathi ship spawns with at most one missile of each type it carries (SWC flight: ammo capped at 1 too), so early engagements are less about baiting out missiles | `--low-missile` |
| Space flight drawn at three times the sim rate (60 fps from the 20 fps tick). The sim, AI and physics keep their tick; two extra frames per tick advance every object along its current velocity and turn rate, and the controls are re-read for each, so the view answers input without added delay. Cockpit and cockpit-less views only; chase and external views, frame skip and the training simulator draw one frame per tick as before. Pacing assumes a 60 Hz display | `--smooth` |
| Fine space dust: the eight dust sprites are replaced by a field of about a thousand small motes that stand still in space around the ship, so they drift past by parallax and stretch into faint streaks along the direction of travel as speed builds. With `--enhanced` they are drawn at output resolution, down to one display pixel wide; without it they are single dimmed pixels in the 320x200 frame. With `--smooth` they move at 60 fps. Purely visual: the original dust slots, hazard fields and explosion sparks are unchanged, and the sim's random sequence is not touched. Not used in Super Wing Commander flight | `--dust` |
| Fair asteroids: no rocks spawned on an intercept course. The original aims some asteroids that appear in view at the spot you will reach after their flight time, so holding course means a hit; with this switch they drift like the rest of the field | `--fair-asteroids` |
| Cockpit-less flight (`c`) fills a fullscreen display of any shape instead of sitting in a 4:3 box: on a wider display the view gains field of view at the sides, on a taller one above and below. The middle of the picture is exactly the 4:3 one, and the instruments stay in that middle block. Windowed play, the cockpit views and everything outside flight keep the original 4:3 frame; switching to or from fullscreen in flight takes effect on the next frame | `--aspectless` |
| Cockpit-less flight (`c`) can use a wider or narrower field of view than the original 90 degrees across the 4:3 picture. Ships, the nav cross and the overlays drawn over ships all follow it; a wider view makes everything smaller, but ships stay drawn and targetable out to the original range, and a missile lock holds over the original angle. The cockpit views keep the original perspective. Combines with `--aspectless`, which adds its extra width on top | `--fov=DEGREES` (60-140) |
| Debug mission picker: hold Alt while loading a saved game to jump to any series and mission (left/right = series, up/down = mission, Enter = go, Esc = keep the save as it was). Ship type follows the series that leads into the chosen one | `--mission-menu` |
| Unknown `--` options are rejected with a list of the accepted ones (`MODERN_ARGS=--help` shows it without starting the game) | `--help` |

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

In the Joystick tab's **Custom** control mode, click any input on the gamepad
drawing to rebind it for WC1's spaceflight, and point at a choice to see what
it does. A button or trigger can take any
flight key's action, cycle the views, or turn yaw into roll and pitch into
speed while held. A stick axis or trigger can pitch, yaw, roll or change speed,
and a stick axis can also set the throttle; any of these can be reversed.
Custom starts from the WCAT four-button, four-axis layout. Menus keep their
fixed buttons, and Super Wing Commander keeps the WCAT four-axis layout.

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
  (LGPL-2.1 or later) emulates the Roland MT-32 and CM-32L; and
- DaisenryakuFan for providing very advanced source reconstructions for the DOS
  releases of several *Wing Commander* titles.

## License

See [LICENSE](LICENSE). OpenAI Codex and Anthropic Claude were used during the
reconstruction.
