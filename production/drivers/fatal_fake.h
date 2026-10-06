/**
 * @file fatal_fake.h
 * @brief Host FatalIf: prints the FaultRecord to stderr and aborts the
 * process. Composition roots wire `fatal_fake` in exactly where
 * `fatal_stm32` would go, so on the desktop a failed configASSERT, stack
 * overflow or malloc failure kills the test run / sim loudly, with file
 * and line or task name -- it never prints and carries on.
 *
 * Tests that deliberately trip a fatal path run it in a forked child and
 * check the child's exit signal and stderr (see test_rtos_hooks.c).
 */
#ifndef FATAL_FAKE_H
#define FATAL_FAKE_H

#include "fatal_if.h"

extern const FatalIf fatal_fake;

#endif /* FATAL_FAKE_H */
