# AwowLab Overlay

A lightweight, always-on-top damage meter for World of Warcraft that reads your combat log as the game writes it. No addon, no in-game overhead — it runs as its own window on top of the game.

The overlay is the standalone companion to [AwowLab](https://github.com/Wobblucy/WoWLogreplayer-Public), a full combat log replayer and analysis tool. Everything you see live in the overlay can be explored in depth in AwowLab after the pull.

## Features

- **Live meters** — DPS, HPS, and damage taken, updating as the game writes the log
- **Pull history** — flip back through earlier pulls without leaving the fight
- **Grouped runs** — each Mythic+ run collapses into its own group of trash and boss pulls; expand one to jump to any segment
- **Views** — current pull, whole dungeon, or full session
- **Death recap** — what killed you, hit by hit
- **Player breakdowns** — per-spell damage and healing details, avoidable damage taken; a player's pets are grouped by pet type
- **Enemy damage** — click a boss or add for its own ability breakdown; same-named enemies merge into one row
- **Mob weighting** — down-weight unimportant adds so the meter reflects actual progress, not padding
- **Copy report** — one click puts a text summary of the pull on your clipboard
- **Speaks your language** — pick your UI language in Settings (English, German, French, Spanish, Portuguese, Russian)

## Getting Started

1. Enable advanced combat logging in WoW: `System → Network → Advanced Combat Logging`
2. Start logging with `/combatlog` (or use an addon that toggles it automatically)
3. Download the latest release and run `AwowLabOverlay.exe` — a single self-contained file, nothing to install; it finds your Logs folder automatically

The overlay stays on top of the game window. Drag it wherever you like; position and settings persist between sessions. If it doesn't find your Logs folder — or you keep it somewhere unusual — open the settings button in the top row and point it at the right folder; the change takes effect without a restart.

## What's New in 0.2.0

- **Mythic+ runs are grouped.** The segment picker now lists every run you've done, each collapsing into its own group of trash and boss pulls — earlier runs no longer disappear when a new key starts. A segment is parsed only when you click into it.
- **Pick your language yourself.** Language moved into Settings and is chosen manually (the overlay no longer follows the WoW client). A settings button in the top row also lets you change your Logs folder without restarting.
- **Meters match Warcraft Logs.** Pet damage is now attributed to the pet's owner the way the logs do it, so per-player totals line up; a player's pet abilities are grouped by pet type in the breakdown.
- **Cleaner enemy and "taken by" lists.** Same-named enemies merge into one row, players and their pets are kept off the enemy lists, and environmental damage is labeled instead of showing a blank source.
- **Copy a report to your clipboard** with one click, and see the live meter slide across whatever pull is currently active.
- Smoother window dragging, an auto-sized boss-phase editor, and matching drill-downs between the overlay and the full AwowLab app.

## Languages

Choose the overlay's UI language from the settings button in the top row. English, German, French, Spanish, Portuguese, and Russian are supported, and the choice is remembered between sessions. New installs default to English. Korean and Chinese aren't offered yet; those scripts need fonts the overlay doesn't bundle.

## Requirements

- Windows 10/11 (64-bit)
- A Vulkan-capable GPU (anything from the last decade)

## Building from Source

Requires:

- Visual Studio 2022 with the "Desktop development with C++" workload
- CMake 3.25 or newer
- The [Vulkan SDK](https://vulkan.lunarg.com/)
- [vcpkg](https://github.com/microsoft/vcpkg), with the `VCPKG_ROOT` environment variable pointing at your vcpkg folder

Install the vcpkg-provided libraries once:

```
vcpkg install openssl zlib rapidjson --triplet x64-windows-static
```

Then configure and build — GLFW and ImGui are downloaded automatically during configure:

```
cmake --preset x64-release
cmake --build out/build/x64-release --config Release
```

The executable lands in `out/build/x64-release/AwowLab/Release` — a single self-contained file with the language data and UI font compiled in.

## Contributing

Issues and pull requests are welcome. Development happens in a private monorepo shared with the full AwowLab application; this repository is published from it, and accepted pull requests are imported there before showing up here in the next sync. That keeps the overlay and the full app on exactly the same engine.

## License

[AGPL-3.0](LICENSE)

The bundled Noto Sans font is licensed under the [SIL Open Font License 1.1](AwowLab/data/fonts/OFL.txt).
