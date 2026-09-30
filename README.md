# Hotkey Atlas

All Skyrim and mod hotkeys in a single window.

Powered by the SKSE Menu Framework, this mod scans and collects all in-game keybinds (both vanilla and modded) right after launching the game, displaying them on a visual, interactive layout of a keyboard, mouse, or gamepad.

## Key Features

* **Full Control Over Binds:** Unbind keys completely, remap them to different buttons, or set up multi-key combinations.
* **Key Combinations:** Total freedom to create any combo you need. Keyboard + Keyboard, Keyboard + Mouse, and Gamepad + Gamepad combinations are all fully supported.
* **Double Tap & Hold:** While binding, switch on **Double tap** or **Hold** and the bind fires on a quick double press or when the button is held for a moment. A single short press of that button still does what it did before, just a moment later.
* **Rich Context & Info:** Detailed information about every single button: what action it performs, which mod it belongs to, and which file it was loaded from.
* **Notes & Tooltips:** The mod includes a `Notes.json` file with default system tooltips. You can also write your own custom notes directly in the SMF menu by clicking on a note. (Currently, tooltips are available only for vanilla game actions, but I plan to add descriptions for keys added by other mods in future updates.)
* **Blacklist:** You can blacklist any mod to hide its hotkeys from both the Edit Bind and All Binds menus.

## Technical Details & Safety

The mod does not edit any original files where the keybinds originate. It relies strictly on its own configuration. Feel free to experiment, remap, or unbind anything without fear of breaking your configs. You can easily revert changes back to default at any time using a single-key reset, a full device reset, or by switching presets.

## Presets

Works simply and intuitively: just type a preset name, press Enter, and your preset is ready. All changes are saved on the fly (no need to constantly click a "Save" button). Want to switch to another setup? Just click the preset you want, and you're good to go.

---

## Supported game versions

One DLL for every version: Skyrim SE 1.5.97, AE 1.6.317 – 1.6.1170 and 1.7.99 / 1.7.104 (Steam), and GOG (1.6.659, 1.6.1179). Addresses come from the Address Library, so take the Address Library file that matches your game (SE for 1.5.97, AE for 1.6.x and 1.7.x; 1.7.x needs All in One v13+ and SKSE 2.3.1).

## Requirements

* [SKSE64](https://skse.silverlock.org/)
* [Address Library for SKSE Plugins](https://www.nexusmods.com/skyrimspecialedition/mods/32444)
* [SKSE Menu Framework](https://www.nexusmods.com/skyrimspecialedition/mods/120352)
* [SkyUI](https://www.nexusmods.com/skyrimspecialedition/mods/12604) (optional: keeps the map's button hints on your binds)

## Files

| Path (under `Data/`) | What it is |
| --- | --- |
| `SKSE/Plugins/HotkeyAtlas.dll` | the plugin |
| `SKSE/Plugins/HotkeyAtlas/Notes.json` | built-in notes; add your own, including notes for mod hotkeys (see its `_readme`) |
| `SKSE/Plugins/HotkeyAtlas/Translations/*.txt` | interface translations |
| `SKSE/Plugins/HotkeyAtlas.ini` | your bind changes and settings (written by the plugin) |
| `SKSE/Plugins/HotkeyAtlas/Presets/*.ini` | your presets (written by the plugin) |

### Adding a note for a mod hotkey

Add an entry to the `mods` array in `Notes.json` and press **Rescan** in the menu:

```json
{ "mod": "TrueHUD", "file": "SKSE/Plugins/TrueHUD.ini", "section": "Keys", "setting": "uToggleKey", "text": "Show or hide the HUD" }
```

A text can carry its own translations: `"text": { "english": "Open the map", "russian": "Открыть карту" }`.

### Translating

Copy `Translations/english.txt` to `<language>.txt` (the game's `sLanguage`, e.g. `german.txt`) and change the right side of each line.

## Building

Requirements: [XMake](https://xmake.io) 3.0+, MSVC with C++23 (Visual Studio 2022 or newer).

```bat
git clone --recurse-submodules https://github.com/Neutral9/HotkeyAtlas.git
cd HotkeyAtlas
xmake build
```

To put the build straight into a mod manager, set `XSE_TES5_MODS_PATH` to its mods folder (e.g. `E:/MO2/mods`): every build then refreshes `<mods>/HotkeyAtlas` (dll plus `dist/`). `xmake install` does the same on demand, also with `XSE_TES5_GAME_PATH` (the game folder).

## Source layout

```
src/
  Plugin.cpp          SKSE entry point
  HotkeyAtlas.h       bind model and the API the pages use
  Core/               scanning, control map, input hook, settings, presets, translation, notes
  UI/                 SKSE Menu Framework pages: device pictures, bind table, capture
extern/               SKSE Menu Framework API header
dist/                 mod data shipped next to the dll
lib/commonlibsse-ng   CommonLibSSE-NG (submodule)
```

## License

[GPL-3.0](LICENSE) with the [modding exception](EXCEPTIONS).
