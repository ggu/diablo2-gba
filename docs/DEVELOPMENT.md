# Development

The default original-art Diablo II game builds and tests without game files, imported assets, an emulator, or a network connection. Required tools are GNU Make, `arm-none-eabi-gcc` with its `objcopy` and `size` tools, Clang, Python 3.10 or newer, and Git. The project uses Python's standard library; no `pip install` step is needed. mGBA is only needed to play.

## Install the tools

### macOS

Install Apple's Xcode Command Line Tools for Clang, Make, and Git, then install the ARM toolchain and Python with Homebrew:

```sh
xcode-select --install
brew install arm-none-eabi-gcc python
```

Homebrew's [ARM GCC formula](https://formulae.brew.sh/formula/arm-none-eabi-gcc) provides `arm-none-eabi-gcc`, `arm-none-eabi-objcopy`, and `arm-none-eabi-size`. To play, install the optional [mGBA app](https://formulae.brew.sh/cask/mgba-app):

```sh
brew install --cask mgba-app
```

The launcher checks `PATH` and `/Applications/mGBA.app/Contents/MacOS/mGBA`.

### Ubuntu 24.04

Install the build and test tools from Ubuntu's package archive:

```sh
sudo apt-get update
sudo apt-get install -y --no-install-recommends gcc-arm-none-eabi binutils-arm-none-eabi clang make python3 git
```

To play, install optional `mgba-sdl` from the [Ubuntu 24.04 package archive](https://packages.ubuntu.com/noble/mgba-sdl):

```sh
sudo apt-get install mgba-sdl
```

The emulator executable may be `/usr/games/mgba`; if it is not on `PATH`, set `MGBA=/usr/games/mgba` when running the play command. Ensure the Universe repository is enabled if apt cannot find the package.

Other systems need equivalent tools. `./dev doctor` reports missing required commands. Use `./dev doctor --emulator` to require the optional emulator too.

## Asset-free workflow

After cloning as described in the [README](../README.md), run:

```sh
./dev doctor
./dev build
./dev test
```

Doctor should report `OK` for ARM GCC, ARM objcopy, ARM size, Make, Clang, Python 3, and Git. Build creates and verifies `build/Diablo2-GBA.gba`. Test runs the default host-side checks. To play:

```sh
./dev doctor --emulator
./dev play diablo2
```

The launcher stores local emulator saves, states, screenshots, and logs in `.local/play/diablo2/`.

Default checks are local-first: run `python3 tools/check_source_tree.py`,
`./dev doctor`, `./dev build`, and `./dev test` on your machine. GitHub Actions
is disabled in the repository settings, and the source-check workflow is
manual-only; it uses a standard GitHub-hosted Ubuntu runner if explicitly
enabled and dispatched. It does not run automatically for pushes or pull
requests. Enabling it, dispatching it, or adding automatic triggers requires an
explicit request. Local verification of the proposed commit is sufficient for
review; do not wait for a redundant Actions run.

## Optional Den profile

The imported-art Den profile is not needed to build or contribute to the default game. Preparing it needs Python 3, Git and network access, CMake 3.10 or later, and a native C/C++ compiler with C++20 support. System zlib and bzip2 development libraries are optional; StormLib includes bundled fallbacks. mGBA is required only for playing.

On macOS, the Command Line Tools above provide the native compiler; add CMake:

```sh
brew install cmake
```

On Ubuntu 24.04, add the native compiler and CMake:

```sh
sudo apt-get install -y --no-install-recommends cmake g++
```

Then prepare and test the optional profile:

```sh
./dev den prepare
./dev den status
./dev den build
./dev den test
./dev den play
```

Preparation fetches pinned reader sources and a hash-verified public demo, then converts data into ignored local files. It does not run the demo installer. Read [the Den guide](D2-DEN.md) for the exact inputs, controls, source references, and content limits. Keep archives, extracted files, converted assets, generated headers, ROMs, saves, and captures in ignored local directories; do not commit or distribute them.

## Troubleshooting

- If `doctor` reports a missing command, install it and confirm it is on `PATH`. ARM GCC must be accompanied by `arm-none-eabi-objcopy` and `arm-none-eabi-size`. If using a custom prefix, set `PREFIX` to the prefix shared by all three programs.
- Cloning the public repository over HTTPS does not require a GitHub login. The optional Den preparation requires network access for its pinned sources and demo file.
- If `./dev play` cannot find mGBA, install it or set `MGBA` to the executable path. For example, on Ubuntu: `MGBA=/usr/games/mgba ./dev play diablo2`.
- If the Den build reports missing generated headers, run `./dev den prepare`. Those files are private local data and are not part of the repository.

See [validation](VALIDATION.md) for source-snapshot checks and known hardware-testing limits.
