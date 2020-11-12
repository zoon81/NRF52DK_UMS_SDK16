#ifndef INC_LED_STRIP_H
#define INC_LED_STRIP_H

#include <inttypes.h>

#define NUMBER_OF_LEDS 128

struct led_color_data_s{
    uint8_t blue;
    uint8_t green;
    uint8_t red;
};

struct led_data_s{
    uint8_t global;
    struct led_color_data_s color;
};

void led_strip_init(void);
void led_strip_setAllLedColor(uint8_t r, uint8_t g, uint8_t b);
void led_strip_setColor(uint8_t r, uint8_t g, uint8_t b, uint8_t led_number);
void led_strip_writeFrameBuffer();
struct led_color_data_s led_strip_hsvToRgb(uint16_t h, uint8_t s, uint8_t v);

#endif