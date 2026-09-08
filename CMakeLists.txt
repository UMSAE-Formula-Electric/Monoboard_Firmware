cmake_minimum_required(VERSION 3.21)

project(Monoboard C ASM)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# cppcheck/clang-tidy consume this; costs nothing.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Keep the source tree clean: an in-source build is what hid the
# relative-linker-script bug (it "worked" because build dir == source dir).
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR
            "In-source builds are disabled. Configure into build/<preset>, e.g.\n"
            "  cmake --preset mcu-debug\n"
            "Then remove the stale root artifacts: CMakeFiles/, CMakeCache.txt, Testing/")
endif()

# Platform is derived from HOW you configured (preset/toolchain),
# never from a variable someone can forget to set.
if(CMAKE_CROSSCOMPILING)
    set(PLATFORM_MCU ON)
else()
    set(PLATFORM_MCU OFF)
endif()

# Declared so it shows up in ccmake/cmake-gui instead of being a
# silently-off magic word.
option(SANITIZE "Build desktop/test targets with ASan+UBSan" OFF)

# ============================================================================
# MCU / PATH CONFIGURATION
# ============================================================================
# All paths absolute. Relative paths are resolved against
# CMAKE_CURRENT_SOURCE_DIR by CMake but against the BUILD dir by the
# linker/compiler driver -- mixing the two is how -T${LINKER_SCRIPT} broke.

set(SRC_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
set(PROD     "${SRC_ROOT}/production")
set(VENDOR   "${PROD}/cubemx/vendor")

set(MCU_FAMILY  "STM32F4xx")
set(MCU_MODEL   "STM32F446xx")
set(HAL_DRIVER_PATH "${VENDOR}/Drivers/STM32F4xx_HAL_Driver")
set(CMSIS_PATH      "${VENDOR}/Drivers/CMSIS")
set(FREERTOS_PATH   "${SRC_ROOT}/lib/FreeRTOS/FreeRTOS-Kernel")
set(LINKER_SCRIPT   "${VENDOR}/STM32F446XX_FLASH.ld")
set(STARTUP_ASM     "${VENDOR}/startup_stm32f446xx.s")

# Submodules are the #1 source of confusing first-build failures.
if(NOT EXISTS "${FREERTOS_PATH}/tasks.c")
    message(FATAL_ERROR
            "FreeRTOS-Kernel not found at ${FREERTOS_PATH}. "
            "Run: git submodule update --init --recursive")
endif()

# ============================================================================
# WARNING PROFILES (interface libraries: composable, per-layer)
# ============================================================================

# Baseline: every first-party layer gets this. Vendor code does NOT.
add_library(warnings INTERFACE)
target_compile_options(warnings INTERFACE
        -Wall -Wextra -Werror
        -Wconversion -Wshadow -Wundef
        -Werror=format -Werror=incompatible-pointer-types
)
# NOTE: -Werror=undefined-behavior is not a GCC flag and was breaking
# the old file. Runtime UB checking is -fsanitize=undefined -- desktop
# builds get it below via SANITIZE.

# Application layer: strictest gate (safety logic).
add_library(warnings_strict INTERFACE)
target_compile_options(warnings_strict INTERFACE
        -Wpedantic -Wdouble-promotion -Wcast-align
)

# Sanitizers as a target, not add_compile_options().
# Directory-scoped flags also instrumented the POSIX FreeRTOS port, whose
# signal-based scheduler trips ASan's interceptors and produces reports
# that have nothing to do with your code. Linked PUBLIC below, so the
# -fsanitize link flags still propagate up to the test executables.
add_library(sanitizers INTERFACE)
if(NOT PLATFORM_MCU AND SANITIZE)
    target_compile_options(sanitizers INTERFACE
            -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(sanitizers INTERFACE
            -fsanitize=address,undefined)
endif()

# ============================================================================
# FREERTOS KERNEL (port selected by platform; config in its own dir)
# ============================================================================

if(PLATFORM_MCU)
    set(FREERTOS_PORT_SRC ${FREERTOS_PATH}/portable/GCC/ARM_CM4F/port.c)
    set(FREERTOS_PORT_INC ${FREERTOS_PATH}/portable/GCC/ARM_CM4F)
    set(FREERTOS_CONFIG_DIR ${SRC_ROOT}/config/mcu)
else()
    set(FREERTOS_PORT_SRC
            ${FREERTOS_PATH}/portable/ThirdParty/GCC/Posix/port.c
            ${FREERTOS_PATH}/portable/ThirdParty/GCC/Posix/utils/wait_for_event.c)
    set(FREERTOS_PORT_INC ${FREERTOS_PATH}/portable/ThirdParty/GCC/Posix)
    set(FREERTOS_CONFIG_DIR ${SRC_ROOT}/config/desktop)
endif()

add_library(freertos_kernel STATIC
        ${FREERTOS_PATH}/event_groups.c
        ${FREERTOS_PATH}/list.c
        ${FREERTOS_PATH}/queue.c
        ${FREERTOS_PATH}/stream_buffer.c
        ${FREERTOS_PATH}/tasks.c
        ${FREERTOS_PATH}/timers.c
        ${FREERTOS_PORT_SRC}
        # NO heap_*.c: ARCHITECTURE.md mandates static allocation only.
        # configSUPPORT_STATIC_ALLOCATION=1, configSUPPORT_DYNAMIC_ALLOCATION=0
        # in FreeRTOSConfig.h -- then a stray xQueueCreate() is a LINK ERROR,
        # i.e. the build enforces the rule. Re-add heap_4.c only if a vendor
        # middleware truly requires it.
)
target_include_directories(freertos_kernel PUBLIC
        ${FREERTOS_PATH}/include
        ${FREERTOS_PORT_INC}
        ${FREERTOS_CONFIG_DIR}      # ONLY FreeRTOSConfig.h lives here --
)                               # never expose the whole src tree.
if(NOT PLATFORM_MCU)
    find_package(Threads REQUIRED)
    target_link_libraries(freertos_kernel PUBLIC Threads::Threads)
    # The POSIX port uses robust mutexes / CLOCK_REALTIME, which the
    # project-wide -std=c99 hides. Vendor code, so extensions here are fine.
    target_compile_definitions(freertos_kernel PRIVATE _GNU_SOURCE)
endif()

# ============================================================================
# VENDOR: STM32 HAL (MCU builds only; no first-party warning profile)
# ============================================================================

if(PLATFORM_MCU)
    add_library(stm32_hal STATIC
            ${HAL_DRIVER_PATH}/Src/stm32f4xx_hal.c
            ${HAL_DRIVER_PATH}/Src/stm32f4xx_hal_gpio.c
            ${HAL_DRIVER_PATH}/Src/stm32f4xx_hal_usart.c
            ${HAL_DRIVER_PATH}/Src/stm32f4xx_hal_can.c
            ${HAL_DRIVER_PATH}/Src/stm32f4xx_hal_dma.c
            ${HAL_DRIVER_PATH}/Src/stm32f4xx_hal_tim.c
            ${HAL_DRIVER_PATH}/Src/stm32f4xx_hal_rcc.c
            ${HAL_DRIVER_PATH}/Src/stm32f4xx_hal_cortex.c
            ${VENDOR}/Core/Src/system_stm32f4xx.c
    )
    target_include_directories(stm32_hal PUBLIC
            ${HAL_DRIVER_PATH}/Inc
            ${CMSIS_PATH}/Device/ST/${MCU_FAMILY}/Include
            ${CMSIS_PATH}/Include
            ${VENDOR}/Core/Inc                       # stm32f4xx_hal_conf.h
    )
    target_compile_definitions(stm32_hal PUBLIC ${MCU_MODEL} USE_HAL_DRIVER)
endif()

# ============================================================================
# LAYERS -- the dependency table from ARCHITECTURE.md, executable form.
# A layer can only #include what it links. Wrong include = compile error.
# ============================================================================

# -- Interfaces: headers only; the one thing everyone may see. ---------------
add_library(interfaces INTERFACE)
target_include_directories(interfaces INTERFACE ${PROD}/interfaces)

# -- Protocol modules: pure codecs. Interfaces only; no RTOS. ----------------
add_library(proto STATIC
        #proto/<filename>.c
        ${PROD}/proto/stub.c
)
target_include_directories(proto PUBLIC ${PROD}/proto)
target_link_libraries(proto
        PUBLIC  interfaces sanitizers
        PRIVATE warnings)

# -- Application: pure logic. Interfaces only; strictest warnings. -----------
add_library(app STATIC
        #app/<filename>.c
        ${PROD}/app/stub.c
)
target_include_directories(app PUBLIC ${PROD}/app)
target_link_libraries(app
        PUBLIC  interfaces sanitizers
        PRIVATE warnings warnings_strict)

# -- Services: RTOS + interfaces + proto; CALLS app. NO drivers, NO HAL. -----
add_library(services STATIC
        #services/<filename>.c
        ${PROD}/services/stub.c
)
target_include_directories(services PUBLIC ${PROD}/services)
target_link_libraries(services
        PUBLIC  interfaces freertos_kernel sanitizers
        PRIVATE proto app warnings)
# If someone adds `#include "can_stm32.h"` to a service: not on the
# include path -> compile error. That IS the architecture review.

# -- Drivers: fake sibling BUILDS everywhere (so it can't rot), but only
#    LINKS into desktop/test binaries. can_fake.c and can_stm32.c define
#    the same interface symbols by design; linking both into one image is
#    a duplicate-symbol error at best, and silent "whichever the linker
#    saw first" at worst -- i.e. shipping the fake CAN driver to a board.
add_library(drivers_fake STATIC
        ${PROD}/drivers/can_fake.c
        ${PROD}/drivers/gpio_fake.c
)
target_include_directories(drivers_fake PUBLIC ${PROD}/drivers)
target_link_libraries(drivers_fake
        PUBLIC  interfaces sanitizers
        PRIVATE warnings)

# -- Drivers: real. The ONLY first-party target that can see the HAL, --------
#    and the HAL is PRIVATE: its headers stop here, leaking to no one.
if(PLATFORM_MCU)
    add_library(drivers_stm32 STATIC
            ${PROD}/drivers/can_stm32.c
            ${PROD}/drivers/gpio_stm32.c
    )
    target_include_directories(drivers_stm32 PUBLIC ${PROD}/drivers)
    target_link_libraries(drivers_stm32
            PUBLIC  interfaces
            PRIVATE stm32_hal freertos_kernel warnings)
    # freertos PRIVATE: FromISR handoff only, per ARCHITECTURE.md.
endif()

# ============================================================================
# EXECUTABLES (composition roots -- the only targets that see everything)
# ============================================================================

if(PLATFORM_MCU)
    # CMakeFindBinUtils populates CMAKE_OBJCOPY/STRIP/NM/OBJDUMP/READELF --
    # but NOT CMAKE_SIZE. Unset, the POST_BUILD line silently ran nothing,
    # so you got no flash/RAM figure and no error either.
    if(NOT CMAKE_SIZE)
        find_program(CMAKE_SIZE
                NAMES ${CMAKE_C_COMPILER_TARGET}-size arm-none-eabi-size size
                DOC "binutils size")
    endif()
    if(NOT CMAKE_SIZE)
        message(WARNING "size(1) not found -- skipping post-build size report")
    endif()

    add_executable(${PROJECT_NAME}.elf
            ${PROD}/src/main_stm32.c
            ${STARTUP_ASM}              # was missing: no startup = no boot
    )
    target_link_libraries(${PROJECT_NAME}.elf PRIVATE
            services proto app
            drivers_stm32               # drivers_fake deliberately NOT here
            freertos_kernel stm32_hal
            warnings
            m c)
    target_link_options(${PROJECT_NAME}.elf PRIVATE
            -T${LINKER_SCRIPT}          # absolute: the linker resolves relative
            # paths against the BUILD dir, so this
            # only ever worked in-source
            -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.map
            -Wl,--gc-sections
            -Wl,--print-memory-usage)
    # Editing the .ld should force a relink. Without this it doesn't.
    set_target_properties(${PROJECT_NAME}.elf PROPERTIES
            LINK_DEPENDS ${LINKER_SCRIPT})

    add_custom_command(TARGET ${PROJECT_NAME}.elf POST_BUILD
            COMMAND ${CMAKE_OBJCOPY} -Oihex   $<TARGET_FILE:${PROJECT_NAME}.elf>
            ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.hex
            COMMAND ${CMAKE_OBJCOPY} -Obinary $<TARGET_FILE:${PROJECT_NAME}.elf>
            ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.bin
            COMMENT "Generating ${PROJECT_NAME}.hex / .bin")

    if(CMAKE_SIZE)
        add_custom_command(TARGET ${PROJECT_NAME}.elf POST_BUILD
                COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${PROJECT_NAME}.elf>)
    endif()
else()
    # Desktop: same layer targets, fake drivers, POSIX FreeRTOS.
    # Building this AT ALL proves no STM32 header leaked upward --
    # the desktop build is itself a layering check.
    enable_testing()
    add_subdirectory(production/tests)   # was `tests` -- no such dir at root
endif()

# ============================================================================
# STATIC ANALYSIS & FORMATTING (globs resolved by CMake, not the shell)
# ============================================================================

file(GLOB_RECURSE FIRST_PARTY_SOURCES CONFIGURE_DEPENDS
        ${PROD}/interfaces/*.h
        ${PROD}/proto/*.[ch]
        ${PROD}/app/*.[ch]
        ${PROD}/services/*.[ch]
        ${PROD}/drivers/*.[ch]
        ${PROD}/tests/*.[ch]
        ${PROD}/tests/suites/*.[ch]
        ${PROD}/src/main_stm32.c
)
# The rest of production/src/ is CubeMX-generated: excluded on purpose, it
# will never pass BARR-C formatting. Move any hand-written code out of there.

# cppcheck over compile_commands.json, but scoped to first-party TUs:
#   --file-filter *production*  keeps our code, drops FreeRTOS-Kernel;
#   --suppress *cubemx*         drops the HAL/CMSIS sources (which live under
#                               production/cubemx and are pulled in on MCU cfgs);
#   -D__GNUC__=13               lets the CMSIS compiler-detection headers parse;
#   unusedFunction suppressed   -- these are library-style modules whose callers
#                                  are other link units (tests, the .elf).
# Run it from the desktop build (cleanest coverage); the *_stm32.c drivers are
# additionally gated by -Wall -Wextra -Werror in the MCU build.
add_custom_target(cppcheck
        COMMAND cppcheck
        --project=${CMAKE_BINARY_DIR}/compile_commands.json
        --enable=all --inline-suppr --check-level=normal
        --file-filter=*production*
        --suppress=missingIncludeSystem
        --suppress=unusedFunction
        --suppress=checkersReport
        --suppress=unmatchedSuppression
        --suppress=*:*cubemx*
        -D__GNUC__=13
        --error-exitcode=1
        COMMENT "cppcheck (first-party TUs via compile_commands.json)")

add_custom_target(format-check
        COMMAND clang-format --dry-run -Werror ${FIRST_PARTY_SOURCES}
        COMMENT "Checking BARR-C formatting")

add_custom_target(format-fix
        COMMAND clang-format -i ${FIRST_PARTY_SOURCES}
        COMMENT "Applying BARR-C formatting")

add_custom_target(docs
        COMMAND doxygen Doxyfile
        WORKING_DIRECTORY ${SRC_ROOT}
        COMMENT "Generating Doxygen documentation")

# Guards the STM32CubeMX .ioc (and its generated main.h) against drift from the
# STM32 pin layer -- gpio_if.h's GpioPin catalogue, gpio_stm32.c's pin_map[],
# first-party GpioConfig call sites, and can_stm32.c's CAN pads. This is the
# STM32 driver's check (it validates *_stm32.c, which only build for MCU), so
# it is only offered for MCU configs; a non-STM32 driver would bring its own.
# Standalone like the analysis targets above -- run it in CI.
if(PLATFORM_MCU)
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        add_custom_target(ioc-check
                COMMAND ${Python3_EXECUTABLE} ${SRC_ROOT}/tools/ioc_check.py --root ${SRC_ROOT}
                WORKING_DIRECTORY ${SRC_ROOT}
                COMMENT "Checking CubeMX .ioc vs. the STM32 pin layer")
    else()
        message(STATUS "Python3 not found -- 'ioc-check' target unavailable")
    endif()
endif()