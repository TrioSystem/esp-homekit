/*
* This is an example of an rgb led strip using Magic Home wifi controller
*
* Debugging printf statements and UART are disabled below because it interfere with mutipwm
* you can uncomment them for debug purposes
*
* more info about the controller and flashing can be found here:
* https://github.com/arendst/Sonoff-Tasmota/wiki/MagicHome-LED-strip-controller
*
* Contributed April 2018 by https://github.com/PCSaito
*/
#include <espressif/esp_common.h>
#include <rboot-api.h>
#include <udplogger.h>


#include <stdio.h>
#include <espressif/esp_wifi.h>
#include <espressif/esp_sta.h>
#include <esp/uart.h>
#include <esp8266.h>
#include <FreeRTOS.h>
#include <task.h>
#include <math.h>

#include <sysparam.h>

#include <homekit/homekit.h>
#include <homekit/characteristics.h>
#include <wifi_config.h>

#include "multipwm.h"
#include "ota-api.h"  // ota menue und trigger update

#define LPF_SHIFT 4  // divide by 16
#define LPF_INTERVAL 10  // in milliseconds

#define RED_PWM_PIN 12
#define GREEN_PWM_PIN 5
#define BLUE_PWM_PIN 13
#define WHITE_PWM_PIN 15
//#define RED_PWM_PIN 5
//#define GREEN_PWM_PIN 12
//#define BLUE_PWM_PIN 13
#define LED_RGB_SCALE 255       // this is the scaling factor used for color conversion

#define DEVICE_MANUFACTURER "TrioSystem"
#define DEVICE_NAME "TrioStrip"
#define DEVICE_MODEL "LedStrip RGB"
#define DEVICE_SERIAL "12345678"
#define FW_VERSION "0.0.001"

void on_wifi_ready();

typedef union {
    struct {
        uint16_t blue;
        uint16_t green;
        uint16_t red;
        uint16_t white;
    };
    uint64_t color;
} rgb_color_t;

// Color smoothing variables
rgb_color_t current_color = { { 0, 0, 0, 0 } };
rgb_color_t target_color = { { 0, 0, 0, 0 } };

// Global variables
float led_hue = 0;              // hue is scaled 0 to 360
float led_saturation = 59;      // saturation is scaled 0 to 100
float led_brightness = 100;     // brightness is scaled 0 to 100
bool led_on = false;            // on is boolean on or off



homekit_characteristic_t ota_trigger  = API_OTA_TRIGGER;
homekit_characteristic_t name         = HOMEKIT_CHARACTERISTIC_(NAME, DEVICE_NAME);
homekit_characteristic_t manufacturer = HOMEKIT_CHARACTERISTIC_(MANUFACTURER,  DEVICE_MANUFACTURER);
homekit_characteristic_t serial       = HOMEKIT_CHARACTERISTIC_(SERIAL_NUMBER, DEVICE_SERIAL);
homekit_characteristic_t model        = HOMEKIT_CHARACTERISTIC_(MODEL,         DEVICE_MODEL);
homekit_characteristic_t revision     = HOMEKIT_CHARACTERISTIC_(FIRMWARE_REVISION,  FW_VERSION);


//http://blog.saikoled.com/post/44677718712/how-to-convert-from-hsi-to-rgb-white
static void hsi2rgb(float h, float s, float i, rgb_color_t* rgb) {
    int r, g, b;

    while (h < 0) { h += 360.0F; };     // cycle h around to 0-360 degrees
    while (h >= 360) { h -= 360.0F; };
    h = 3.14159F*h / 180.0F;            // convert to radians.
    s /= 100.0F;                        // from percentage to ratio
    i /= 100.0F;                        // from percentage to ratio
    s = s > 0 ? (s < 1 ? s : 1) : 0;    // clamp s and i to interval [0,1]
    i = i > 0 ? (i < 1 ? i : 1) : 0;    // clamp s and i to interval [0,1]
    //i = i * sqrt(i);                    // shape intensity to have finer granularity near 0

    if (h < 2.09439) {
        r = LED_RGB_SCALE * i / 3 * (1 + s * cos(h) / cos(1.047196667 - h));
        g = LED_RGB_SCALE * i / 3 * (1 + s * (1 - cos(h) / cos(1.047196667 - h)));
        b = LED_RGB_SCALE * i / 3 * (1 - s);
    }
    else if (h < 4.188787) {
        h = h - 2.09439;
        g = LED_RGB_SCALE * i / 3 * (1 + s * cos(h) / cos(1.047196667 - h));
        b = LED_RGB_SCALE * i / 3 * (1 + s * (1 - cos(h) / cos(1.047196667 - h)));
        r = LED_RGB_SCALE * i / 3 * (1 - s);
    }
    else {
        h = h - 4.188787;
        b = LED_RGB_SCALE * i / 3 * (1 + s * cos(h) / cos(1.047196667 - h));
        r = LED_RGB_SCALE * i / 3 * (1 + s * (1 - cos(h) / cos(1.047196667 - h)));
        g = LED_RGB_SCALE * i / 3 * (1 - s);
    }

    rgb->red = (uint8_t) r;
    rgb->green = (uint8_t) g;
    rgb->blue = (uint8_t) b;
}

void led_identify_task(void *_args) {
    printf("LED identify\n");

    rgb_color_t color = target_color;
    rgb_color_t black_color = { { 0, 0, 0, 0 } };
    rgb_color_t white_color = { { 128, 128, 128, 128 } };

    for (int i=0; i<3; i++) {
        for (int j=0; j<2; j++) {
            target_color = white_color;
            vTaskDelay(100 / portTICK_PERIOD_MS);

            target_color = black_color;
            vTaskDelay(100 / portTICK_PERIOD_MS);
        }

        vTaskDelay(250 / portTICK_PERIOD_MS);
    }

    target_color = color;

    vTaskDelete(NULL);
}

void led_identify(homekit_value_t _value) {
    xTaskCreate(led_identify_task, "LED identify", 128, NULL, 2, NULL);
}

homekit_value_t led_on_get() {
    return HOMEKIT_BOOL(led_on);
}

void led_on_set(homekit_value_t value) {
    if (value.format != homekit_format_bool) {
        // printf("Invalid on-value format: %d\n", value.format);
        return;
    }

    led_on = value.bool_value;
}

homekit_value_t led_brightness_get() {
    return HOMEKIT_INT(led_brightness);
}

void led_brightness_set(homekit_value_t value) {
    if (value.format != homekit_format_int) {
        // printf("Invalid brightness-value format: %d\n", value.format);
        return;
    }
    led_brightness = value.int_value;
}

homekit_value_t led_hue_get() {
    return HOMEKIT_FLOAT(led_hue);
}

void led_hue_set(homekit_value_t value) {
    if (value.format != homekit_format_float) {
        // printf("Invalid hue-value format: %d\n", value.format);
        return;
    }
    led_hue = value.float_value;
}

homekit_value_t led_saturation_get() {
    return HOMEKIT_FLOAT(led_saturation);
}

void led_saturation_set(homekit_value_t value) {
    if (value.format != homekit_format_float) {
        // printf("Invalid sat-value format: %d\n", value.format);
        return;
    }
    led_saturation = value.float_value;
}


homekit_accessory_t *accessories[] = {
    HOMEKIT_ACCESSORY(.id = 1, .category = homekit_accessory_category_lightbulb, .services = (homekit_service_t*[]) {
        HOMEKIT_SERVICE(ACCESSORY_INFORMATION,
            .characteristics = (homekit_characteristic_t*[]) {
                &name,
                &manufacturer,
                &serial,
                &model,
                &revision,
                HOMEKIT_CHARACTERISTIC(IDENTIFY, led_identify),
                NULL
            }),
        HOMEKIT_SERVICE(LIGHTBULB, .primary = true,
            .characteristics = (homekit_characteristic_t*[]) {
                HOMEKIT_CHARACTERISTIC(
                    ON, true,
                    .getter = led_on_get,
                    .setter = led_on_set
                ),
                HOMEKIT_CHARACTERISTIC(
                    BRIGHTNESS, 100,
                    .getter = led_brightness_get,
                    .setter = led_brightness_set
                ),
                HOMEKIT_CHARACTERISTIC(
                    HUE, 0,
                    .getter = led_hue_get,
                    .setter = led_hue_set
                ),
                HOMEKIT_CHARACTERISTIC(
                    SATURATION, 0,
                    .getter = led_saturation_get,
                    .setter = led_saturation_set
                ),
                &ota_trigger,
                NULL
            }),
        NULL
    }),
    NULL
};

homekit_server_config_t config = {
    .accessories = accessories,
    .password = "190-11-978",    //changed tobe valid
    .setupId="1QJ7",
};

IRAM void multipwm_task(void *pvParameters) {
    const TickType_t xPeriod = pdMS_TO_TICKS(LPF_INTERVAL);
    TickType_t xLastWakeTime = xTaskGetTickCount();

    uint8_t pins[] = {RED_PWM_PIN, GREEN_PWM_PIN, BLUE_PWM_PIN};

    pwm_info_t pwm_info;
    pwm_info.channels = 3;

    multipwm_init(&pwm_info);
    multipwm_set_freq(&pwm_info, 65535);
    for (uint8_t i=0; i<pwm_info.channels; i++) {
        multipwm_set_pin(&pwm_info, i, pins[i]);
    }

    while(1) {
        if (led_on) {
            // convert HSI to RGBW
            hsi2rgb(led_hue, led_saturation, led_brightness, &target_color);
        } else {
            target_color.red = 0;
            target_color.green = 0;
            target_color.blue = 0;
        }

        current_color.red += ((target_color.red * 256) - current_color.red) >> LPF_SHIFT ;
        current_color.green += ((target_color.green * 256) - current_color.green) >> LPF_SHIFT ;
        current_color.blue += ((target_color.blue * 256) - current_color.blue) >> LPF_SHIFT ;

        multipwm_stop(&pwm_info);
        multipwm_set_duty(&pwm_info, 0, current_color.red);
        multipwm_set_duty(&pwm_info, 1, current_color.green);
        multipwm_set_duty(&pwm_info, 2, current_color.blue);
        multipwm_start(&pwm_info);

        vTaskDelayUntil(&xLastWakeTime, xPeriod);
    }
}

void create_accessory_name() {
    uint8_t macaddr[6];
    sdk_wifi_get_macaddr(STATION_IF, macaddr);

    char *name_value = malloc(17);
    //snprintf(name_value, 17, "TrioSysems %02X%02X%02X", macaddr[3], macaddr[4], macaddr[5]);
	//snprintf(name_value, 17, "%s-%s-%02X%02X%02X", DEVICE_NAME, DEVICE_MODEL, macaddr[3], macaddr[4], macaddr[5]);
    snprintf(name_value, 17, "%s-%02X%02X%02X", DEVICE_NAME, macaddr[3], macaddr[4], macaddr[5]);
    name.value = HOMEKIT_STRING(name_value);

    char *serial_value = malloc(13);
    snprintf(serial_value, 13, "%02X%02X%02X%02X%02X%02X", macaddr[0], macaddr[1], macaddr[2], macaddr[3], macaddr[4], macaddr[5]);
    serial.value = HOMEKIT_STRING(serial_value);
}

void on_wifi_ready() {
    xTaskCreate(udplog_send, "logsend", 256, NULL, 2, NULL);
    homekit_server_init(&config);
}

void user_init(void) {
    UDPLGP("\n\n\n\n\n\n\nuser-init-start\n");
    //    uart_set_baud(0, 74880);
    uart_set_baud(0, 115200);

    create_accessory_name();
    wifi_config_init("TrioLed_Strip", NULL, on_wifi_ready);

    xTaskCreate(multipwm_task, "multipwm", 256, NULL, 2, NULL);

    UDPLGP("user-init-done\n");
}
