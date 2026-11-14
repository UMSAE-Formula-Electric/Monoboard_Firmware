/**
 * @file test_boot_service.c
 * @brief Exercises the boot service's init/step contract.
 */
#include <stddef.h>

#include "boot_service.h"
#include "test_framework.h"


TEST(boot_service, init_rejects_null_context)
{
    CHECK(boot_service_init(NULL) == IF_HW_FAULT);
}

TEST(boot_service, init_marks_ready)
{
    BootService svc = {.ready = false};

    CHECK(boot_service_init(&svc) == IF_OK);
    CHECK(svc.ready);
}

TEST(boot_service, step_is_safe_with_or_without_init)
{
    BootService svc = {.ready = false};

    vBootServiceStep(&svc);
    CHECK(boot_service_init(&svc) == IF_OK);
    vBootServiceStep(&svc);
}

TEST(boot_service, step_with_null_context_is_a_no_op)
{
    vBootServiceStep(NULL);
}
