# Description

<!-- What does this change and why? A couple of sentences is fine for small MRs. -->

Closes #

## Type of change

- [ ] Bug fix
- [ ] New feature
- [ ] Refactor (no behaviour change)
- [ ] CubeMX / `.ioc` regeneration
- [ ] Build system or tooling
- [ ] Documentation

## Subsystems touched

<!-- e.g. CAN, ADC, FreeRTOS tasks, shutdown circuit, bootloader -->

## How this was tested

- [ ] Builds clean with no new warnings
- [ ] Unit tests pass (or: none apply)
- [ ] Flashed and verified on hardware — board rev: <!-- e.g. Monoboard rev C -->
- [ ] Verified on the car / full harness
- [ ] Not tested on hardware yet (explain below)

<!-- Describe what you actually did: what you probed, what CAN traffic you sent,
     what you watched on the debugger. Attach traces or scope captures if useful. -->

## Safety and rules

- [ ] This does not touch the shutdown circuit, AMS, IMD, APPS/BSE plausibility, RTD sequencing, or tractive system indicators
- [ ] It does touch one of the above — rules checked and a second reviewer is required

<!-- If it does: which rules, and who reviewed? -->

## Checklist

- [ ] Branch is up to date with `main` and conflicts are resolved
- [ ] Follows the repo's naming and formatting conventions
- [ ] No commented-out code, stray `printf`, or debug breakpoints left in
- [ ] No magic numbers — new constants are named and documented
- [ ] Blocking calls and stack sizes considered for anything running in a FreeRTOS task
- [ ] `.ioc` and generated code are consistent (regenerated, not hand-patched around)
- [ ] CAN message or DBC changes are coordinated with the other ECUs
- [ ] Submodule pointers are intentional, not accidental
- [ ] Public functions have header comments

## Notes for reviewers

<!-- Anything you want a second pair of eyes on, known limitations, or follow-up work. -->
