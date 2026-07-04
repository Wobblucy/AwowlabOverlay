# AwowLab Overlay

A lightweight, always-on-top damage meter for World of Warcraft that reads your combat log as the game writes it. No addon, no in-game overhead — it runs as its own window on top of the game.

The overlay is the standalone companion to [AwowLab](https://github.com/Wobblucy/WoWLogreplayer-Public), a full combat log replayer and analysis tool. Everything you see live in the overlay can be explored in depth in AwowLab after the pull.

## Features

- **Live meters** — DPS, HPS, and damage taken, updating as the game writes the log
- **Pull history** — flip back through earlier pulls without leaving the fight
- **Views** — current pull, whole dungeon, or full session
- **Death recap** — what killed you, hit by hit
- **Player breakdowns** — per-spell damage and healing details, avoidable damage taken
- **Mob weighting** — down-weight unimportant adds so the meter reflects actual progress, not padding
- **Speaks your language** — automatically matches your WoW client language

## Getting Started

1. Enable advanced combat logging in WoW: `System → Network → Advanced Combat Logging`
2. Start logging with `/combatlog` (or use an addon that toggles it automatically)
3. Download the latest release and run `AwowLabOverlay.exe` — a single self-contained file, nothing to install; it finds your Logs folder automatically

The overlay stays on top of the game window. Drag it wherever you like; position and settings persist between sessions.

## Languages

The overlay reads your WoW client's language setting and comes up in the same language — no configuration needed. English, German, French, Spanish, Portuguese, and Russian are supported. Korean and Chinese clients currently see English; those scripts need fonts the overlay doesn't bundle yet.

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
