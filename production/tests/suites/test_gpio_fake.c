/**
 * @file test_gpio_fake.c
 * @brief Exercises the GpioIf contract (production/interfaces/gpio_if.h) through
 * gpio_fake, the dependency-free double for gpio_stm32.
 */
#include <stddef.h>

#include "gpio_fake.h"
#include "test_framework.h"

TEST(gpio_fake, write_before_init_fails)
{
    gpio_fake_reset();
    CHECK(gpio_fake.write(GPIO_PIN_TEST, GPIO_HIGH) == GPIO_ERR_HAL);
    CHECK(!gpio_fake_is_configured(GPIO_PIN_TEST));
}

TEST(gpio_fake, output_init_drives_initial_level)
{
    GpioConfig config = {.dir = GPIO_DIR_OUTPUT, .pull = GPIO_PULL_NONE, .initial = GPIO_HIGH};
    GpioLevel  level;

    gpio_fake_reset();
    CHECK(gpio_fake.init(GPIO_PIN_TEST, &config) == GPIO_OK);
    CHECK(gpio_fake_is_configured(GPIO_PIN_TEST));
    CHECK(gpio_fake_dir(GPIO_PIN_TEST) == GPIO_DIR_OUTPUT);

    CHECK(gpio_fake.read(GPIO_PIN_TEST, &level) == GPIO_OK);
    CHECK(level == GPIO_HIGH);
}

TEST(gpio_fake, write_then_read_roundtrips)
{
    GpioConfig config = {.dir = GPIO_DIR_OUTPUT, .pull = GPIO_PULL_NONE, .initial = GPIO_LOW};
    GpioLevel  level;

    gpio_fake_reset();
    CHECK(gpio_fake.init(GPIO_PIN_TEST, &config) == GPIO_OK);

    CHECK(gpio_fake.write(GPIO_PIN_TEST, GPIO_HIGH) == GPIO_OK);
    CHECK(gpio_fake_level(GPIO_PIN_TEST) == GPIO_HIGH);
    CHECK(gpio_fake.read(GPIO_PIN_TEST, &level) == GPIO_OK);
    CHECK(level == GPIO_HIGH);
}

TEST(gpio_fake, toggle_flips_output)
{
    GpioConfig config = {.dir = GPIO_DIR_OUTPUT, .pull = GPIO_PULL_NONE, .initial = GPIO_LOW};

    gpio_fake_reset();
    CHECK(gpio_fake.init(GPIO_PIN_TEST, &config) == GPIO_OK);

    CHECK(gpio_fake.toggle(GPIO_PIN_TEST) == GPIO_OK);
    CHECK(gpio_fake_level(GPIO_PIN_TEST) == GPIO_HIGH);
    CHECK(gpio_fake.toggle(GPIO_PIN_TEST) == GPIO_OK);
    CHECK(gpio_fake_level(GPIO_PIN_TEST) == GPIO_LOW);
}

TEST(gpio_fake, writing_an_input_is_rejected)
{
    GpioConfig config = {.dir = GPIO_DIR_INPUT, .pull = GPIO_PULL_DOWN, .initial = GPIO_LOW};

    gpio_fake_reset();
    CHECK(gpio_fake.init(GPIO_PIN_TEST, &config) == GPIO_OK);
    CHECK(gpio_fake.write(GPIO_PIN_TEST, GPIO_HIGH) == GPIO_ERR_DIR);
    CHECK(gpio_fake.toggle(GPIO_PIN_TEST) == GPIO_ERR_DIR);
}

TEST(gpio_fake, input_reads_driven_level)
{
    GpioConfig config = {.dir = GPIO_DIR_INPUT, .pull = GPIO_PULL_NONE, .initial = GPIO_LOW};
    GpioLevel  level;

    gpio_fake_reset();
    CHECK(gpio_fake.init(GPIO_PIN_TEST, &config) == GPIO_OK);

    gpio_fake_drive_input(GPIO_PIN_TEST, GPIO_HIGH);
    CHECK(gpio_fake.read(GPIO_PIN_TEST, &level) == GPIO_OK);
    CHECK(level == GPIO_HIGH);

    gpio_fake_drive_input(GPIO_PIN_TEST, GPIO_LOW);
    CHECK(gpio_fake.read(GPIO_PIN_TEST, &level) == GPIO_OK);
    CHECK(level == GPIO_LOW);
}

TEST(gpio_fake, input_pullup_defaults_high)
{
    GpioConfig config = {.dir = GPIO_DIR_INPUT, .pull = GPIO_PULL_UP, .initial = GPIO_LOW};
    GpioLevel  level;

    gpio_fake_reset();
    CHECK(gpio_fake.init(GPIO_PIN_TEST, &config) == GPIO_OK);
    CHECK(gpio_fake.read(GPIO_PIN_TEST, &level) == GPIO_OK);
    CHECK(level == GPIO_HIGH);
}

TEST(gpio_fake, unknown_pin_is_rejected)
{
    GpioConfig config = {.dir = GPIO_DIR_OUTPUT, .pull = GPIO_PULL_NONE, .initial = GPIO_LOW};
    GpioLevel  level;

    gpio_fake_reset();
    CHECK(gpio_fake.init(GPIO_PIN_COUNT, &config) == GPIO_ERR_ARG);
    CHECK(gpio_fake.write(GPIO_PIN_COUNT, GPIO_HIGH) == GPIO_ERR_ARG);
    CHECK(gpio_fake.read(GPIO_PIN_COUNT, &level) == GPIO_ERR_ARG);
}

TEST(gpio_fake, read_rejects_null_out)
{
    GpioConfig config = {.dir = GPIO_DIR_OUTPUT, .pull = GPIO_PULL_NONE, .initial = GPIO_LOW};

    gpio_fake_reset();
    CHECK(gpio_fake.init(GPIO_PIN_TEST, &config) == GPIO_OK);
    CHECK(gpio_fake.read(GPIO_PIN_TEST, NULL) == GPIO_ERR_ARG);
}

TEST(gpio_fake, reset_clears_configuration)
{
    GpioConfig config = {.dir = GPIO_DIR_OUTPUT, .pull = GPIO_PULL_NONE, .initial = GPIO_HIGH};

    gpio_fake_reset();
    CHECK(gpio_fake.init(GPIO_PIN_TEST, &config) == GPIO_OK);

    gpio_fake_reset();

    CHECK(!gpio_fake_is_configured(GPIO_PIN_TEST));
    CHECK(gpio_fake_level(GPIO_PIN_TEST) == GPIO_LOW);
    CHECK(gpio_fake.write(GPIO_PIN_TEST, GPIO_HIGH) == GPIO_ERR_HAL);
}
