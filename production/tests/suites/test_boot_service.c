/**
 * @file test_boot_service.c
 * @brief Exercises the boot service's heartbeat against gpio_fake.
 */
#include <stddef.h>

#include "boot_service.h"
#include "gpio_fake.h"
#include "test_framework.h"


TEST(boot_service, init_rejects_null_context)
{
    CHECK(boot_service_init(NULL) == IF_HW_FAULT);
}

TEST(boot_service, init_rejects_null_driver)
{
    BootService svc = {.gpio = NULL};

    CHECK(boot_service_init(&svc) == IF_HW_FAULT);
    CHECK(!svc.ready);
}

TEST(boot_service, init_drives_heartbeat_pin_low)
{
    BootService svc = {.gpio = &gpio_fake};

    gpio_fake_reset();

    CHECK(boot_service_init(&svc) == IF_OK);
    CHECK(svc.ready);
    CHECK(gpio_fake_is_configured(GPIO_PIN_TEST));
    CHECK(gpio_fake_dir(GPIO_PIN_TEST) == GPIO_DIR_OUTPUT);
    CHECK(gpio_fake_level(GPIO_PIN_TEST) == GPIO_LOW);
}


/*
 * Happy path
 */
TEST(boot_service, step_toggles_heartbeat_pin)
{
    BootService svc = {.gpio = &gpio_fake};

    gpio_fake_reset();
    CHECK(boot_service_init(&svc) == IF_OK);

    vBootServiceStep(&svc);
    CHECK(gpio_fake_level(GPIO_PIN_TEST) == GPIO_HIGH);
    vBootServiceStep(&svc);
    CHECK(gpio_fake_level(GPIO_PIN_TEST) == GPIO_LOW);
}

TEST(boot_service, step_with_null_context_is_a_no_op)
{
    vBootServiceStep(NULL);
}


/*
 * Driver init failure. gpio_fake only fails on a bad pin or config, which
 * the service never passes, so a stub driver stands in. Its init returns
 * IF_BUSY rather than IF_HW_FAULT, so the test can tell the driver's error
 * being passed back from the service's own NULL-driver fault.
 */
static unsigned int failing_gpio_toggles;

static IfStatus failing_gpio_init(GpioPin pin, const GpioConfig *config)
{
    (void)pin;
    (void)config;
    return IF_BUSY;
}

static IfStatus failing_gpio_toggle(GpioPin pin)
{
    (void)pin;
    failing_gpio_toggles++;
    return IF_OK;
}

static const GpioIf failing_gpio = {
    .init   = failing_gpio_init,
    .toggle = failing_gpio_toggle,
};

TEST(boot_service, init_returns_driver_error)
{
    BootService svc = {.gpio = &failing_gpio};

    CHECK(boot_service_init(&svc) == IF_BUSY);
    CHECK(!svc.ready);
}

TEST(boot_service, step_after_driver_init_error_leaves_pin_alone)
{
    BootService svc = {.gpio = &failing_gpio};

    failing_gpio_toggles = 0U;
    CHECK(boot_service_init(&svc) == IF_BUSY);

    vBootServiceStep(&svc);
    CHECK(failing_gpio_toggles == 0U);
}

TEST(boot_service, failed_reinit_stops_the_heartbeat)
{
    BootService svc = {.gpio = &gpio_fake};

    gpio_fake_reset();
    failing_gpio_toggles = 0U;
    CHECK(boot_service_init(&svc) == IF_OK);

    svc.gpio = &failing_gpio;
    CHECK(boot_service_init(&svc) == IF_BUSY);

    vBootServiceStep(&svc);
    CHECK(failing_gpio_toggles == 0U);                 /* the failed driver is not used */
    CHECK(gpio_fake_level(GPIO_PIN_TEST) == GPIO_LOW); /* nor the one before it */
}
