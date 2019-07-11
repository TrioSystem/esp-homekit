#include <stdio.h>
#include <espressif/esp_wifi.h>
#include <espressif/esp_sta.h>
#include <espressif/esp_common.h>
#include <esp/uart.h>
#include <esp8266.h>
#include <FreeRTOS.h>
#include <task.h>
#include <esp/hwrand.h>
#include <etstimer.h>

//#include <rboot-api.h>
#include <sysparam.h>

#include <math.h>
#include <esplibs/libmain.h>

#include <homekit/homekit.h>
#include <homekit/characteristics.h>
#include <wifi_config.h>

#include <dht/dht.h>
#include <ds18b20/ds18b20.h>

#include <qrcode.h>
#include <i2c/i2c.h>
#include <ssd1306/ssd1306.h>
#include <fonts/fonts.h>

#define SENSOR_PIN 14

#define QRCODE_VERSION 2

#define I2C_BUS 0
#define I2C_SDA_PIN 4
#define I2C_SCL_PIN 5

#define DISPLAY_WIDTH 128
#define DISPLAY_HEIGHT 64
#define DEFAULT_FONT FONT_FACE_TERMINUS_6X12_ISO8859_1
//#define DEFAULT_FONT0 FONT_FACE_TERMINUS_BOLD_8X14_ISO8859_1
//#define DEFAULT_FONT-x FONT_FACE_TERMINUS_BOLD_10X18_ISO8859_1
//#define DEFAULT_FONT-x FONT_FACE_TERMINUS_BOLD_11X22_ISO8859_1
#define DEFAULT_FONT1 FONT_FACE_TERMINUS_BOLD_12X24_ISO8859_1
#define DEFAULT_FONT2 FONT_FACE_TERMINUS_BOLD_14X28_ISO8859_1
#define DEFAULT_FONT3 FONT_FACE_TERMINUS_BOLD_16X32_ISO8859_1


#define DEVICE_MANUFACTURER "TrioSystem"
#define DEVICE_NAME "TrioSens"
#define DEVICE_MODEL "DHT-22-sensor"
#define DEVICE_SERIAL "12345678"
#define FW_VERSION "0.0.01"

#define DHT_SENSOR_TYPE_SYSPARAM                        "f"
#define HUM_OFFSET_SYSPARAM                             "2"
#define TEMP_OFFSET_SYSPARAM                            "3"
#define POLL_PERIOD_SYSPARAM                            "g"
#define TEMP_DEADBAND_SYSPARAM                          "B"

ETSTimer extra_func_timer;

void on_wifi_ready();

uint8_t dht_sensor_type = 2, hum_offset = 0, temp_offset = 0;
uint8_t poll_period = 30;
//uint8_t temp_deadband = 0;
volatile float old_humidity_value = 0.0, old_temperature_value = 0.0;



static const ssd1306_t display = {
    .protocol = SSD1306_PROTO_I2C,
    .screen = SSD1306_SCREEN,
    .i2c_dev.bus = I2C_BUS,
    .i2c_dev.addr = SSD1306_I2C_ADDR_0,
    .width = DISPLAY_WIDTH,
    .height = DISPLAY_HEIGHT,
};

static uint8_t display_buffer[DISPLAY_WIDTH * DISPLAY_HEIGHT / 8];

void display_init() {
    i2c_init(I2C_BUS, I2C_SCL_PIN, I2C_SDA_PIN, I2C_FREQ_400K);
    if (ssd1306_init(&display)) {
        printf("Failed to initialize OLED display\n");
        return;
    }
    ssd1306_set_whole_display_lighting(&display, false);
    ssd1306_set_scan_direction_fwd(&display, false);
    ssd1306_set_segment_remapping_enabled(&display, true);
}

bool password_displayed = false;
void display_password(const char *password) {
    ssd1306_display_on(&display, true);

    ssd1306_fill_rectangle(&display, display_buffer, 0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, OLED_COLOR_BLACK);
    ssd1306_draw_string(&display, display_buffer, font_builtin_fonts[DEFAULT_FONT1], 4, 20, (char*)password, OLED_COLOR_WHITE, OLED_COLOR_BLACK);

    ssd1306_load_frame_buffer(&display, display_buffer);

    password_displayed = true;
}

void hide_password() {
    if (!password_displayed)
        return;

    ssd1306_clear_screen(&display);
    ssd1306_display_on(&display, false);

    password_displayed = false;
}


void display_temperature(float temperature, float humidity) {
    //printf(" TEMP %g, HUM %g\n", temperature_value, humidity_value);

    char str[16];
    //float f = 123.456789;
    //snprintf(str, sizeof(str), "%.2f", temperature);
    snprintf(str, sizeof(str), "%.0f", temperature);
    /*
    This will give you a C string with two decimal places.
    And thatÍs especially convenient when you want to have multiple variables and possibly some text with it

        char str[64];
        float f1 = 123.456789;
        float f2 = -23.456789;
        snprintf(str, sizeof(str), "{\"SomeField\":\"%.2f\", \"SomeOther\":\"%.3f\"}", f1, f2);
    */

    ssd1306_display_on(&display, true);

    ssd1306_fill_rectangle(&display, display_buffer, 0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, OLED_COLOR_BLACK);
    ssd1306_draw_string(&display, display_buffer, font_builtin_fonts[DEFAULT_FONT3], 0, 0, str, OLED_COLOR_WHITE, OLED_COLOR_BLACK);

    //ssd1306_draw_string(&display, display_buffer, font_builtin_fonts[DEFAULT_FONT3], 0, 0, "2130", OLED_COLOR_WHITE, OLED_COLOR_BLACK);
    ssd1306_draw_string(&display, display_buffer, font_builtin_fonts[DEFAULT_FONT2], 110, 30, "C", OLED_COLOR_WHITE, OLED_COLOR_BLACK);
    ssd1306_draw_string(&display, display_buffer, font_builtin_fonts[DEFAULT_FONT1], 98, 18, "o", OLED_COLOR_WHITE, OLED_COLOR_BLACK);


    //ssd1306_draw_string(&display, display_buffer, font_builtin_fonts[DEFAULT_FONT], 0, 0, "Temperature", OLED_COLOR_WHITE, OLED_COLOR_BLACK);
    snprintf(str, sizeof(str), "%.0f", humidity);
    //ssd1306_draw_string(&display, display_buffer, font_builtin_fonts[DEFAULT_FONT3], 40, 30, str, OLED_COLOR_WHITE, OLED_COLOR_BLACK);
    //ssd1306_draw_string(&display, display_buffer, font_builtin_fonts[DEFAULT_FONT2], 80, 30, "%", OLED_COLOR_WHITE, OLED_COLOR_BLACK);
    //ssd1306_draw_string(&display, display_buffer, font_builtin_fonts[DEFAULT_FONT3], 2, 2, "0", OLED_COLOR_WHITE, OLED_COLOR_BLACK);


    ssd1306_load_frame_buffer(&display, display_buffer);


}

void temperature_sensor_identify(homekit_value_t _value) {
    printf("Temperature sensor identify\n");
}

//homekit_characteristic_t ota_trigger  = API_OTA_TRIGGER;
homekit_characteristic_t name         = HOMEKIT_CHARACTERISTIC_(NAME, DEVICE_NAME);
homekit_characteristic_t manufacturer = HOMEKIT_CHARACTERISTIC_(MANUFACTURER,  DEVICE_MANUFACTURER);
homekit_characteristic_t serial       = HOMEKIT_CHARACTERISTIC_(SERIAL_NUMBER, DEVICE_SERIAL);
homekit_characteristic_t model        = HOMEKIT_CHARACTERISTIC_(MODEL,         DEVICE_MODEL);
homekit_characteristic_t revision     = HOMEKIT_CHARACTERISTIC_(FIRMWARE_REVISION,  FW_VERSION);

// Thermostat Setup
/*
homekit_characteristic_t dht_sensor_type = HOMEKIT_CHARACTERISTIC_(CUSTOM_DHT_SENSOR_TYPE, 2, .id=112, .callback=HOMEKIT_CHARACTERISTIC_CALLBACK(change_settings_callback));
homekit_characteristic_t hum_offset = HOMEKIT_CHARACTERISTIC_(CUSTOM_HUMIDITY_OFFSET, 0, .id=132, .callback=HOMEKIT_CHARACTERISTIC_CALLBACK(change_settings_callback));
homekit_characteristic_t temp_offset = HOMEKIT_CHARACTERISTIC_(CUSTOM_TEMPERATURE_OFFSET, 0.0, .id=133, .callback=HOMEKIT_CHARACTERISTIC_CALLBACK(change_settings_callback));
homekit_characteristic_t poll_period = HOMEKIT_CHARACTERISTIC_(CUSTOM_TH_PERIOD, 30, .id=125, .callback=HOMEKIT_CHARACTERISTIC_CALLBACK(change_settings_callback));
homekit_characteristic_t temp_deadband = HOMEKIT_CHARACTERISTIC_(CUSTOM_TEMPERATURE_DEADBAND, 0.0, .id=142, .callback=HOMEKIT_CHARACTERISTIC_CALLBACK(change_settings_callback));
*/

//homekit_characteristic_t temperature = HOMEKIT_CHARACTERISTIC_(CURRENT_TEMPERATURE, 0);
homekit_characteristic_t current_temperature = HOMEKIT_CHARACTERISTIC_(CURRENT_TEMPERATURE, 0, .min_value=(float[]) {-100}, .max_value=(float[]) {200});

//homekit_characteristic_t humidity    = HOMEKIT_CHARACTERISTIC_(CURRENT_RELATIVE_HUMIDITY, 0);
homekit_characteristic_t current_humidity = HOMEKIT_CHARACTERISTIC_(CURRENT_RELATIVE_HUMIDITY, 0);



void temperature_sensor_task(void *_args) {
    gpio_set_pullup(SENSOR_PIN, false, false);

    float humidity_value, temperature_value;
    while (1) {
        bool success = dht_read_float_data(
            DHT_TYPE_DHT22, SENSOR_PIN,

            &humidity_value, &temperature_value
        );
        if (success) {
            current_temperature.value.float_value = temperature_value;
            current_humidity.value.float_value = humidity_value;

            homekit_characteristic_notify(&current_temperature, HOMEKIT_FLOAT(temperature_value));
            homekit_characteristic_notify(&current_humidity, HOMEKIT_FLOAT(humidity_value));
        } else {
            printf("Couldnt read data from sensor\n");
        }

        vTaskDelay(3000 / portTICK_PERIOD_MS);
    }
}

// ***** Temperature and Humidity sensors

void temperature_sensor_worker() {
    float humidity_value, temperature_value;
    bool get_temp = false;

    if (dht_sensor_type < 3) {
        dht_sensor_type_t current_sensor_type = DHT_TYPE_DHT22;

        if (dht_sensor_type == 1) {
            current_sensor_type = DHT_TYPE_DHT11;
        }

        get_temp = dht_read_float_data(current_sensor_type, SENSOR_PIN, &humidity_value, &temperature_value);
    }
    /*
    else {    // dht_sensor_type == 3
        ds18b20_addr_t ds18b20_addr[1];

        if (ds18b20_scan_devices(SENSOR_PIN, ds18b20_addr, 1) == 1) {
            float temps[1];
            ds18b20_measure_and_read_multi(SENSOR_PIN, ds18b20_addr, 1, temps);
            temperature_value = temps[0];
            humidity_value = 0.0;
            get_temp = true;
        }
    }
    */
    if (get_temp) {
        temperature_value += temp_offset;
        if (temperature_value < -100) {
            temperature_value = -100;
        } else if (temperature_value > 200) {
            temperature_value = 200;
        }

        if (temperature_value != old_temperature_value) {
            old_temperature_value = temperature_value;
            current_temperature.value = HOMEKIT_FLOAT(temperature_value);
            homekit_characteristic_notify(&current_temperature, current_temperature.value);


        }

        humidity_value += hum_offset;
        if (humidity_value < 0) {
            humidity_value = 0;
        } else if (humidity_value > 100) {
            humidity_value = 100;
        }

        if (humidity_value != old_humidity_value) {
            old_humidity_value = humidity_value;
            current_humidity.value = HOMEKIT_FLOAT(humidity_value);
            homekit_characteristic_notify(&current_humidity, current_humidity.value);
        }

        printf(" TEMP %g, HUM %g\n", temperature_value, humidity_value);
        display_temperature(temperature_value, humidity_value);
    } else {

        printf(" ! ERROR Sensor\n");

    }
}






void temperature_sensor_init() {
    xTaskCreate(temperature_sensor_task, "Temperatore Sensor", 256, NULL, 2, NULL);
}


void temperature_sensor_worker_init() {

    gpio_set_pullup(SENSOR_PIN, false, false);

    sdk_os_timer_setfn(&extra_func_timer, temperature_sensor_worker, NULL);
    sdk_os_timer_arm(&extra_func_timer, poll_period * 1000, 1);


    temperature_sensor_worker();
}




homekit_accessory_t *accessories[] = {
    HOMEKIT_ACCESSORY(.id=1, .category=homekit_accessory_category_thermostat, .services=(homekit_service_t*[]) {
        HOMEKIT_SERVICE(ACCESSORY_INFORMATION, .characteristics=(homekit_characteristic_t*[]) {
            &name,
            &manufacturer,
            &serial,
            &model,
            &revision,
            HOMEKIT_CHARACTERISTIC(IDENTIFY, temperature_sensor_identify),
            NULL
        }),
        HOMEKIT_SERVICE(TEMPERATURE_SENSOR, .primary=true, .characteristics=(homekit_characteristic_t*[]) {
            HOMEKIT_CHARACTERISTIC(NAME, "Temperature Sensor"),
            &current_temperature,
            NULL
        }),
        HOMEKIT_SERVICE(HUMIDITY_SENSOR, .characteristics=(homekit_characteristic_t*[]) {
            HOMEKIT_CHARACTERISTIC(NAME, "Humidity Sensor"),
            &current_humidity,
            NULL
        }),
        NULL
    }),
    NULL
};

void on_password(const char *password) {
    display_password(password);
}


void on_homekit_event(homekit_event_t event) {
    hide_password();
}

void on_homekit_event_extra(homekit_event_t event) {
    if (event == HOMEKIT_EVENT_PAIRING_ADDED) {
        //qrcode_hide();
    } else if (event == HOMEKIT_EVENT_PAIRING_REMOVED) {
        if (!homekit_is_paired())
            sdk_system_restart();
    }
}



homekit_server_config_t config = {
    .accessories = accessories,
    //.password = "111-11-111"
    .password_callback = on_password,
    .on_event = on_homekit_event,
};

void create_accessory_name() {
    uint8_t macaddr[6];
    sdk_wifi_get_macaddr(STATION_IF, macaddr);

    char *name_value = malloc(17);
    //snprintf(name_value, 17, "TrioSysems %02X%02X%02X", macaddr[3], macaddr[4], macaddr[5]);
	//snprintf(name_value, 17, "%s-%s-%02X%02X%02X", DEVICE_NAME, DEVICE_MODEL, macaddr[3], macaddr[4], macaddr[5]);
    snprintf(name_value, 17, "%s %02X%02X%02X", DEVICE_NAME, macaddr[3], macaddr[4], macaddr[5]);
    name.value = HOMEKIT_STRING(name_value);

    char *serial_value = malloc(13);
    snprintf(serial_value, 13, "%02X%02X%02X%02X%02X%02X", macaddr[0], macaddr[1], macaddr[2], macaddr[3], macaddr[4], macaddr[5]);
    serial.value = HOMEKIT_STRING(serial_value);
}

void on_wifi_ready() {
    printf("WiFi ready\n");
    homekit_server_init(&config);

    //temperature_sensor_init();
    temperature_sensor_worker_init();


}

void user_init(void) {
    uart_set_baud(0, 115200);

    create_accessory_name();
    display_init();

    wifi_config_init(DEVICE_NAME, NULL, on_wifi_ready);

    printf("user-init-done\n");

}

