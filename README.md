# Corpus README
Corpus is the firmware written for the UMSAE Formula Electrics Vehicle Control Unit, it's main purpose is to take in
driver input, handle battery management system and motor controller communication, and monitor faults that happen within
the vehicle.

Corpus has a separation of application and baremetal code, this separation allows developers to more easily write tests
and simulations of state machines and protocols.

# Source Code
All source code for Corpus is stored using git. It is downloaded with the following command:
```
git clone git@github.com:UMSAE-Formula-Electric/Monoboard_Firmware.git
```
FreeRTOS and its related libraries are vendored directly into `lib/FreeRTOS/` (not a git submodule), so no extra
fetch step is needed — the clone above pulls everything required to build.

Work happens on branches named `<username>/<short-description>` (e.g. `mason/Initilize_Repo`), branched off `main`.
Code reviews are done using GitHub pull requests targeting `main`.

# Architecture
Corpus is organized into five layers (Vendor HAL, Drivers, Interfaces, Services, Application) plus a composition
root, with dependencies pointing downward and crossing layers only through the Interface Layer. See
[ARCHITECTURE.md](ARCHITECTURE.md) for the full breakdown.

# Repository Layout
```
production/
  app/         Composition roots (Highlevel code for data processing)
  drivers/     Concrete driver implementations (adc_stm32, can_stm32, ...)
  interfaces/  Header-only contracts and shared plain types
  services/    Tasks and domain modules (pedal, logging, NVM, CAN stack)
  src/         Entry points
  tests/       Unit tests
  proto/       Protocols
  cubemx/      Generated vendor HAL/CMSIS code
cmake/         Toolchain files
config/mcu/    MCU-specific configuration
lib/           Third-party libraries (FreeRTOS)
```

# Building
Builds are configured with CMake presets defined in `CMakePresets.json`:

- `mcu-debug` — STM32F446 debug build
- `mcu-release` — STM32F446 release build (LTO)
- `desktop` — host build for tests and simulation (host GCC, FreeRTOS POSIX port)

```
cmake --preset mcu-debug
cmake --build --preset mcu-debug
```

Swap `mcu-debug` for `mcu-release` or `desktop` as needed. In-source builds are disabled; always configure into
`build/<preset>`.

# Testing
Desktop unit tests run through CTest:
```
cmake --preset desktop
cmake --build --preset desktop
ctest --preset desktop --output-on-failure
```

# Hardware
Currently this project is targeted to build for the stm32f446VET microcontroller. Making use of the chips CAN,USART,DMA
IWDG, and HAL drivers.

## Peripheral Configuration
Clock tree, pin mux, and DMA channel assignments are maintained in CubeMX and live at
`production/cubemx/vendor/vendor.ioc`. Open it in STM32CubeMX to inspect or regenerate the vendor HAL init code —
regenerated output should only ever land under `production/cubemx/vendor/`, never in the Driver/Interface/Service
layers above it.

## Flashing & Debugging
`mcu-debug` and `mcu-release` builds produce `Monoboard.elf`, `.hex`, and `.bin` in the preset's build directory
(e.g. `build/mcu-debug/`). STM32CubeCLT bundles STM32CubeProgrammer, which can flash over SWD via an ST-Link:
```
STM32_Programmer_CLI -c port=SWD -w build/mcu-debug/Monoboard.elf -v -rst
```

# Releases
Releases are done such that after a successful integration tests a binary of both the debug and release versions will be
posted onto the GitHub's release section. Releases will be named Corpus-[version]-yy-mm-dd.elf

**Not yet automated** — there's no CI workflow in this repo yet (no `.github/workflows`), and the integration test
suite this process depends on isn't written (`production/tests` is still stubbed). Until both exist, this describes
the intended pipeline rather than something that happens automatically on push/tag.

Note: build output is currently `Monoboard.elf` (this repo will be renamed to Corpus once it's ready, at which point
the artifact name will line up).

# Dependencies
Currently, this Project makes use of the following technologies:
- CMake
- arm-none-eabi-gcc
- ninja
- HAL
- Clang-Tidy
- Clang-Format
- cppcheck
- Doxygen
- Ceedling
- Unity
- CMock

Unity and CMock are pulled in by Ceedling; there's no separate install step for them.

## Windows
Install **[STM32CubeCLT](https://www.st.com/en/development-tools/stm32cubeclt.html)** for CMake, arm-none-eabi-gcc,
and Ninja in one bundle.

The remaining tools can be installed with `winget`:
```
winget install Kitware.CMake
winget install LLVM.LLVM
winget install Cppcheck.Cppcheck
winget install DimitriVanHeesch.Doxygen
winget install RubyInstallerTeam.RubyWithDevKit
gem install ceedling
```

## Debian / Ubuntu
```
sudo apt update
sudo apt install cmake ninja-build gcc-arm-none-eabi clang-tidy clang-format cppcheck doxygen ruby-full build-essential
gem install ceedling
```
