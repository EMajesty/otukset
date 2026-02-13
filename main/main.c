#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"

void app_main(void)
{
    printf("Hello, ESP32-C5!\n");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
