# ECU Firmware Layered Architecture

STM32 + FreeRTOS firmware organized into five layers plus a composition root.
The golden rule: **dependencies point downward only, and cross layer
boundaries exclusively through the Interface Layer.** All rules below are
enforced at compile time via CMake target visibility (`PUBLIC` / `PRIVATE` /
`INTERFACE` include propagation).

---

## Layer Overview

```
5. Application  ── pure decision logic
       │ calls (via structs in / commands out)
4. Services     ── tasks, domain modules (pedal, logging, NVM, CAN stack)
       │ uses function-pointer interfaces (injected at init)
3. Interfaces   ── headers only: contracts + shared plain types
       ▲ implements
2. Drivers      ── adc_stm32.c, adc_fake.c, can_stm32.c, log sinks
       │ includes
1. Vendor HAL   ── ST HAL / CMSIS (real target) or stub HAL (desktop)
```

Note the arrow at the Interface Layer: drivers point **up into** interfaces
(they implement them); services point **down into** interfaces (they consume
them). Drivers and Services never reference each other directly. They meet
only at runtime, when the composition root injects a concrete driver's
vtable into a service.

---

## 1. Vendor Layer (`hal`)

**Contains:** ST HAL, CMSIS, register definitions, startup code. On desktop
builds: a hand-written stub HAL with identical header names and no-op
bodies.

**May include:** nothing from this project.

**Included by:** Driver Layer only. No other layer may include a vendor
header — this is a compile error by construction, because only the
`drivers` target receives the HAL include path.

**Rules:**
- Third-party code; never edited.
- The stub HAL doubles as documentation of exactly which vendor functions
  the project depends on.
- Selection between real HAL and stub is a build-system decision (include
  path swap), never an `#ifdef` in project code.

---

## 2. Driver Layer (`drivers`)

**Contains:** Concrete implementations of the interface contracts:
`adc_stm32.c`, `adc_fake.c`, `can_stm32.c`, log sinks (RTT, USART-DMA,
CAN, stdout). Also hardware ISR handlers.

**May include:** Vendor Layer, Interface Layer.

**Included by:** Composition roots only. No service or application file
ever includes a driver header.

**Rules:**
- The only layer permitted to touch vendor headers or hardware registers.
- Hardware-flavored and dumb: raw ADC counts, CAN frames, bytes. No domain
  meaning (a driver knows channels, not pedals).
- Exposes functionality exclusively as `const` vtable structs
  (e.g. `const AdcIf adc_stm32`) so tables live in flash.
- Every real driver has a programmable fake sibling compiled into every
  build (real and desktop), so both stay warm and breakage is caught on
  every build.
- ISRs do minimal work: capture data, enqueue via `...FromISR` APIs, yield.
  Processing resumes in a service task.

---

## 3. Interface Layer (`interfaces`)

**Contains:** Headers only. Two kinds of content:
- **Contracts:** function-pointer structs (`AdcIf`, `CanIf`, `LogSinkIf`).
- **Shared plain types:** `PedalState`, `ThrottleCmd`, `LogMsg` — POD
  structs that form the common vocabulary of the system.

**May include:** Standard library headers only (`stdint.h`, `stdbool.h`,
`stddef.h`). Never vendor, driver, RTOS, or service headers.

**Included by:** Everything above the Vendor Layer (drivers, services,
application, composition roots).

**Rules:**
- Zero compiled code (CMake `INTERFACE` target).
- The most dependency-free code in the project — this is what makes it the
  legal meeting point between layers.
- Interfaces are **implemented by** drivers and **consumed by** services;
  the interface itself calls no one.
- Shared plain types may be used by any upper layer, including the
  Application Layer directly. The strict layering rule governs *behavior*
  (function calls), not POD data.

---

## 4. Service Layer (`services`)

**Contains:** Domain-aware middleware: pedal module (calibration,
filtering, plausibility checks), logging framework (front end, queue,
drain task), and future NVM manager, CAN stack / UDS, watchdog manager.
Owns RTOS **task bodies**, written as thin wrappers over testable step
functions.

**May include:** Interface Layer, FreeRTOS headers, other service headers.

**Included by:** Application Layer (for service APIs and task contexts),
composition roots.

**Rules:**
- Talks downward only through interface pointers injected at init
  (`pedal_init(const AdcIf *adc)`). Never names a concrete driver.
- Task pattern: the `for(;;)` loop + `vTaskDelayUntil` wrapper is two
  lines; all real work lives in a `*_task_step(ctx)` function callable
  directly from desktop unit tests.
- Does **not** create its own tasks or queues — bodies are defined here,
  instantiation happens in the composition root.
- Inter-service communication via FreeRTOS queues (one queue item = one
  complete message). No additional mutexes around queues — they are
  already thread-safe. Mutexes only guard non-queue shared config, and
  ownership is structured so a resource has a single owning task wherever
  possible.
- Knows what a pedal is; does not know what an STM32 is.

---

## 5. Application Layer (`app`)

**Contains:** Pure decision logic: `pedal_arbitrate()`, mode management,
fault reaction policies.

**May include:** Service Layer headers and shared plain types from the
Interface Layer.

**Called by:** Service Layer tasks only. Application functions are invoked
from service step functions; the application never runs "on its own."

**Rules:**
- Pure functions: plain structs in, commands out.
- No hardware knowledge, no vendor or driver includes, and no FreeRTOS
  calls — application code does not know it runs inside a task at 1 kHz.
- Consequence: this layer is exhaustively unit-testable on the desktop
  with **zero mocks** (construct input structs, assert on outputs).
- Safety-relevant logic (e.g. brake-override / dual-pedal plausibility)
  lives here so it gets the strictest compiler flags and full test
  coverage.

---

## Composition Root (`main_stm32.c`, `main_desktop.c`, test mains)

Not a layer — one file per executable, and the **only** code allowed to see
everything.

**Responsibilities:**
- Select and wire concrete drivers into services
  (`pedal_init(&adc_stm32)` vs `pedal_init(&adc_fake)`).
- Create all queues and hand them into task contexts.
- Create all tasks (`xTaskCreateStatic`), owning every **priority and
  stack size** decision. All priorities are listed in a single
  `priorities.h` so system schedulability is reviewable at a glance.
- Install the active log sink.
- Start the scheduler.

Different executables = different compositions, same layers:

| Executable      | Drivers wired      | HAL        | Tasks            |
|-----------------|--------------------|------------|------------------|
| `firmware`      | `adc_stm32`, real sinks | ST HAL | full task set    |
| `tests`         | `adc_fake` / patched vtables | stub HAL | none (call step functions) |
| desktop sim (optional) | fakes       | stub HAL   | FreeRTOS POSIX port |

---

## Dependency Rules (enforced by CMake)

| Layer        | May include                          | May be included by            |
|--------------|--------------------------------------|-------------------------------|
| Vendor       | —                                    | Drivers                       |
| Drivers      | Vendor, Interfaces                   | Composition roots             |
| Interfaces   | Standard library only                | Drivers, Services, App, roots |
| Services     | Interfaces, FreeRTOS, other services | Application, roots            |
| Application  | Services, shared types               | Services (calls), roots       |
| Comp. root   | Everything                           | —                             |

Enforcement mechanism:
- Each layer is a CMake target; include paths propagate only along
  declared `target_link_libraries` edges.
- `drivers → hal` is **PRIVATE**: vendor headers do not leak upward even
  transitively.
- `interfaces` is an **INTERFACE** target: headers, no code.
- Services do not link drivers at all; concrete drivers meet abstract
  consumers only in composition roots.
- The desktop test build is itself a layering check: it contains no real
  HAL, so any code reaching around the stub fails to build.

---

## Cross-Cutting Concerns

**FreeRTOS** — usable in Services (and minimally in driver ISR handoff via
`FromISR` APIs). Forbidden in Application. Static allocation only; no
malloc after init.

**Logging** — a service with swappable sinks (sinks live in the Driver
Layer). Producers enqueue complete `LogMsg` items and never block or format
in hot paths; a single low-priority drain task owns the sink. Overflow
policy: drop and count.

**Control vs. dependency direction** — at runtime, control briefly flows
*upward* (driver ISR → queue → service task → application function), while
compile-time dependencies still point only downward. This is the intended
shape, made possible by the interface vtables and queue handoffs.
