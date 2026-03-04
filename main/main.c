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

// Portrait (vertical) orientation: 240 wide x 320 tall
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

#define FONT_SCALE 3
#define CHAR_W (8 * FONT_SCALE)
#define CHAR_H (8 * FONT_SCALE)

static const char *TAG = "st7789";

// 8x8 bitmap font.  Each byte is one row; MSB is the leftmost pixel; row 0 is
// the top.  Only the characters that appear in "Hello World" are defined; all
// other entries default to zero (blank glyph).
static const uint8_t FONT8[128][8] = {
    // 0x42 = 0100 0010 → .#....#.   0x7E = 0111 1110 → .######.
    ['H'] = {0x42, 0x42, 0x42, 0x7E, 0x42, 0x42, 0x42, 0x00},

    // 0x82 = #.....#.  0x92 = #..#..#.  0xAA = #.#.#.#.  0x44 = .#...#..
    ['W'] = {0x82, 0x82, 0x92, 0x92, 0xAA, 0x44, 0x44, 0x00},

    // 0x06 = .....##.  0x3E = ..#####.  0x66 = .##..##.
    ['d'] = {0x06, 0x06, 0x3E, 0x66, 0x66, 0x66, 0x3E, 0x00},

    // 0x3C = ..####..  0x66 = .##..##.  0x7E = .######.  0x60 = .##.....
    ['e'] = {0x00, 0x00, 0x3C, 0x66, 0x7E, 0x60, 0x3C, 0x00},

    // 0x38 = ..###...  0x18 = ...##...  0x3C = ..####..
    ['l'] = {0x38, 0x18, 0x18, 0x18, 0x18, 0x18, 0x3C, 0x00},

    // 0x3C = ..####..  0x66 = .##..##.
    ['o'] = {0x00, 0x00, 0x3C, 0x66, 0x66, 0x66, 0x3C, 0x00},

    // 0x7C = .#####..  0x60 = .##.....
    ['r'] = {0x00, 0x00, 0x7C, 0x60, 0x60, 0x60, 0x60, 0x00},

    [' '] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
};

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

// Draw a string at pixel position (x, y) with foreground/background RGB565 colors.
// Characters are rendered from FONT8 at FONT_SCALE times their native 8x8 size.
static void lcd_draw_text(esp_lcd_panel_handle_t panel,
                          const char *text, int x, int y,
                          uint16_t fg, uint16_t bg)
{
    int len = (int)strlen(text);

    uint16_t *line = heap_caps_malloc(LCD_HRES * sizeof(uint16_t), MALLOC_CAP_DMA);
    if (!line) {
        ESP_LOGE(TAG, "Failed to allocate text line buffer");
        return;
    }

    for (int fy = 0; fy < 8; fy++) {           // font row (0-7)
        for (int sy = 0; sy < FONT_SCALE; sy++) {  // scale repetition
            int screen_y = y + fy * FONT_SCALE + sy;
            if (screen_y < 0 || screen_y >= LCD_VRES) continue;

            // Start with background across the full width
            for (int i = 0; i < LCD_HRES; i++) line[i] = bg;

            for (int ci = 0; ci < len; ci++) {
                uint8_t ch = (uint8_t)text[ci];
                if (ch >= 128) continue;
                uint8_t row_bits = FONT8[ch][fy];

                for (int fx = 0; fx < 8; fx++) {   // font column (0-7)
                    uint16_t color = ((row_bits >> (7 - fx)) & 1) ? fg : bg;
                    for (int sx = 0; sx < FONT_SCALE; sx++) {
                        int screen_x = x + ci * CHAR_W + fx * FONT_SCALE + sx;
                        if (screen_x >= 0 && screen_x < LCD_HRES) {
                            line[screen_x] = color;
                        }
                    }
                }
            }

            ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel, 0, screen_y, LCD_HRES, screen_y + 1, line));
        }
    }

    free(line);
}

void app_main(void)
{
    printf("Hello, ESP32-C5!\n");

    lcd_backlight_on();
    esp_lcd_panel_handle_t panel = st7789_init();

    // Clear screen to black
    lcd_fill_color(panel, 0x0000);

    // "Hello" and "World" on separate lines, each centered horizontally.
    // Font is 8x8 scaled 3x → each char is 24x24 px.
    // "Hello" / "World" = 5 chars × 24 px = 120 px wide.
    const char *text1 = "Hello";
    const char *text2 = "World";

    int line_gap   = FONT_SCALE * 2;            // 6 px between lines
    int text_w     = 5 * CHAR_W;               // 120 px (same for both)
    int total_h    = 2 * CHAR_H + line_gap;    // 54 px
    int x_center   = (LCD_HRES - text_w) / 2; // 60 px from left
    int y_start    = (LCD_VRES - total_h) / 2; // vertically centered

    lcd_draw_text(panel, text1, x_center, y_start,                   0xFFFF, 0x0000);
    lcd_draw_text(panel, text2, x_center, y_start + CHAR_H + line_gap, 0xFFFF, 0x0000);

    for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
}
