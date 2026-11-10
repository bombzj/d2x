# D2X — Diablo II: Lord of Destruction

D2X is an open-source C++20 reimplementation of the **Diablo II: Lord of Destruction 1.13c** client and game server. It aims to reproduce the original game's rules and behavior, reading maps, graphics, animations, audio, and data tables directly from the original MPQ archives.

One client provides Single Player, TCP/IP, and Battle.net modes. Single Player and LAN games use D2X's own host, while remote play uses the original D2GS protocol. The embedded host and dedicated server share the same gameplay core, with separate resource, networking, presentation, and save systems.

**D2X is under active development. Gameplay, visuals, and compatibility with the original game are incomplete.** See the [project baseline](BASELINE.md) for current capabilities and known limitations, and the [documentation index](docs/README.md) for implementation details. Most documentation is currently in Chinese.

Project Diablo 2 (PD2) support is also in development. Compatibility depends on the asset set, core version, and connection mode; see [PD2 status](docs/pd2/STATUS.md) and [supported versions](docs/pd2/BUILDS.md).

## Quick start

### Game assets

You must supply MPQ archives from a legally owned copy of Lord of Destruction. Use files from a consistent installation version. Blizzard game assets are not included in this repository or its distribution packages.

Place the required MPQs in `assets/mpq2/`, or specify their directory with `-Mpq` when launching. See the [MPQ guide](docs/resources/MPQ.md) for required files, including speech and music assets.

### Build and run on Windows

Requirements: CMake 3.25+, a C++20 compiler, Git, and an OpenGL 3.3-capable graphics driver. The first configuration downloads pinned dependencies. Run from the repository root:

```powershell
.\scripts\build.ps1 -Configuration Release
.\Play.cmd -Mpq assets/mpq2
```

Choose a game mode from the main menu. See [build and run](docs/development/BUILD_AND_RUN.md) for network configuration, launch options, and packaging. For PD2, prepare the matching assets and configuration using the [PD2 setup guide](docs/pd2/OPERATIONS.md), then launch `PlayPD2.cmd`.

With prepared local saves, `Play-AI.cmd` runs the Sorceress task AI by default. Use `Play-AI.cmd ama act1-2` for an Amazon run through Acts I–II. See the [task AI launcher guide](docs/development/BUILD_AND_RUN.md#诊断启动与任务-ai) for save paths and resume options.

Windows builds have been compiled and run. A Linux CMake build path is provided but has not yet been compiled or run on Linux; see the [Linux build notes](docs/development/BUILD_AND_RUN.md#linux-支持状态).

## Documentation

| Topic | Guide |
| --- | --- |
| Capabilities, limitations, and development status | [Project baseline](BASELINE.md) |
| Building, running, configuration, and packaging | [Build and run](docs/development/BUILD_AND_RUN.md) |
| Dedicated server and PvPGN deployment | [Server guide](docs/development/PVPGN_SERVER.md) |
| PD2 setup, differences, and version compatibility | [PD2 documentation](docs/pd2/README.md) |
| Code structure, module responsibilities, and data flow | [Architecture overview](docs/architecture/OVERVIEW.md) |
| Gameplay rules, modules, and original-game references | [Documentation index](docs/README.md) |

## Contributing

Before contributing, read the [contributor guidelines](AGENTS.md), [coding conventions](docs/development/CODING.md), and [validation guide](docs/development/TESTING.md). When reporting an issue, include your asset version, game mode, steps to reproduce, and the expected and actual behavior.

## License and acknowledgments

The source code is licensed under the [GNU GPL v3](LICENSE). Game assets belong to their respective rights holders and are not covered by this license.

D2X uses and draws on several open-source projects. See [third-party sources and licenses](docs/resources/THIRD_PARTY.md) for dependencies, references, and attribution.
