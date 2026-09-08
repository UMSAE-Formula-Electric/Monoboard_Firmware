/**
 * @file main_stm32.c
 * @brief Composition root stub (see ARCHITECTURE.md).
 *
 * Real wiring -- drivers into services, task/queue creation, log sink
 * install, scheduler start -- lands here once those layers exist. No
 * stm32f4xx_it.c: FreeRTOSConfig.h renames the ARM_CM4F port's handlers
 * onto the vector table's names.
 */
int main(void)
{
    for (;;) {
    }

    return 0;
}