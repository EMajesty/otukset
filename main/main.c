#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "esp_system.h"

#define LCD_HOST SPI2_HOST

#define LCD_HRES 240
#define LCD_VRES 320
#define LCD_OFFSET_X 0
#define LCD_OFFSET_Y 0
#define LCD_MIRROR_X false
#define LCD_MIRROR_Y false
#define LCD_SWAP_XY false

#define PIN_NUM_MOSI 7
#define PIN_NUM_SCLK 6
#define PIN_NUM_CS 10
#define PIN_NUM_DC 5
#define PIN_NUM_RST 4
#define PIN_NUM_BKLT -1

#define LCD_PIXEL_CLOCK_HZ (1 * 1000 * 1000)
#define LCD_DMA_MAX_TRANSFER (LCD_HRES * LCD_VRES * 2 + 8)

static const char *TAG = "st7789";

static void lcd_backlight_on(void)
{
#if PIN_NUM_BKLT < 0
    return;
#else
    gpio_config_t bklt_cfg = {
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    bklt_cfg.pin_bit_mask = 1ULL << PIN_NUM_BKLT;
    ESP_ERROR_CHECK(gpio_config(&bklt_cfg));
    gpio_set_level(PIN_NUM_BKLT, 1);
#endif
}

static esp_lcd_panel_handle_t st7789_init(void)
{
    ESP_ERROR_CHECK(gpio_set_drive_capability(PIN_NUM_SCLK, GPIO_DRIVE_CAP_3));
    ESP_ERROR_CHECK(gpio_set_drive_capability(PIN_NUM_MOSI, GPIO_DRIVE_CAP_3));
    ESP_ERROR_CHECK(gpio_set_drive_capability(PIN_NUM_DC, GPIO_DRIVE_CAP_3));
    ESP_ERROR_CHECK(gpio_set_drive_capability(PIN_NUM_RST, GPIO_DRIVE_CAP_3));
    if (PIN_NUM_CS >= 0) {
        ESP_ERROR_CHECK(gpio_set_drive_capability(PIN_NUM_CS, GPIO_DRIVE_CAP_3));
    }

    spi_bus_config_t buscfg = {
        .sclk_io_num = PIN_NUM_SCLK,
        .mosi_io_num = PIN_NUM_MOSI,
        .miso_io_num = -1,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = LCD_DMA_MAX_TRANSFER,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = PIN_NUM_DC,
        .cs_gpio_num = PIN_NUM_CS,
        .pclk_hz = LCD_PIXEL_CLOCK_HZ,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 10,
        .cs_ena_pretrans = 2,
        .cs_ena_posttrans = 2,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config, &io_handle));

    esp_lcd_panel_handle_t panel_handle = NULL;
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = PIN_NUM_RST,
        .color_space = ESP_LCD_COLOR_SPACE_RGB,
        .data_endian = LCD_RGB_DATA_ENDIAN_LITTLE,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io_handle, &panel_config, &panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_set_gap(panel_handle, LCD_OFFSET_X, LCD_OFFSET_Y));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel_handle, LCD_MIRROR_X, LCD_MIRROR_Y));
    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel_handle, LCD_SWAP_XY));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_handle, true));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));

    return panel_handle;
}

static void lcd_fill_color(esp_lcd_panel_handle_t panel, uint16_t color)
{
    size_t line_bytes = LCD_HRES * sizeof(uint16_t);
    uint16_t *line = heap_caps_malloc(line_bytes, MALLOC_CAP_DMA);
    if (!line) {
        ESP_LOGE(TAG, "Failed to allocate DMA line buffer");
        return;
    }

    for (int x = 0; x < LCD_HRES; x++) {
        line[x] = color;
    }

    for (int y = 0; y < LCD_VRES; y++) {
        ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel, 0, y, LCD_HRES, y + 1, line));
    }

    free(line);
}

static uint16_t background_color_for_y(int y)
{
    static const uint16_t colors[] = {
        0xF800, // red
        0x07E0, // green
        0x001F, // blue
        0xFFE0, // yellow
        0xF81F, // magenta
        0x07FF, // cyan
        0xFFFF, // white
        0x0000, // black
    };
    const int color_count = (int)(sizeof(colors) / sizeof(colors[0]));
    const int band_height = LCD_VRES / color_count;
    int band = y / band_height;
    if (band < 0) {
        band = 0;
    } else if (band >= color_count) {
        band = color_count - 1;
    }
    return colors[band];
}

static uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

static void lcd_test_pattern(esp_lcd_panel_handle_t panel)
{
    size_t line_bytes = LCD_HRES * sizeof(uint16_t);
    uint16_t *line = heap_caps_malloc(line_bytes, MALLOC_CAP_DMA);
    if (!line) {
        ESP_LOGE(TAG, "Failed to allocate DMA line buffer");
        return;
    }

    while (1) {
        for (int y = 0; y < LCD_VRES; y++) {
            uint8_t g = (uint8_t)((y * 255) / (LCD_VRES - 1));
            for (int x = 0; x < LCD_HRES; x++) {
                uint8_t r = (uint8_t)((x * 255) / (LCD_HRES - 1));
                uint8_t b = (uint8_t)(((x ^ y) & 0xFF));
                line[x] = rgb565(r, g, b);
            }
            ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel, 0, y, LCD_HRES, y + 1, line));
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void app_main(void)
{
    printf("Hello, ESP32-C5!\n");

    lcd_backlight_on();
    esp_lcd_panel_handle_t panel = st7789_init();

    lcd_test_pattern(panel);
}
