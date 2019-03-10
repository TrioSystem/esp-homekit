/*  (c) 2018 HomeAccessoryKid
 *  This example makes an RGBW smart lightbulb as offered on e.g. alibaba
 *  with the brand of ZemiSmart. It uses an ESP8266 with a 1MB flash on a
 *  TYLE1R printed circuit board by TuyaSmart (also used in AiLight).
 *  There are terminals with markings for GND, 3V3, Tx, Rx and IO0
 *  There's a second GND terminal that can be used to set IO0 for flashing
 *  Popping of the plastic cap is sometimes hard, but never destructive
 *  Note that the LED color is COLD white.
 *  Changing them for WARM is possible but requires skill and nerves.
 */



#include <espressif/esp_common.h>
#include <rboot-api.h>
//#include <udplogger.h>





#include <stdio.h>
#include <espressif/esp_wifi.h>
#include <espressif/esp_sta.h>
#include <esp/uart.h>
#include <esp8266.h>
#include <FreeRTOS.h>
#include <task.h>

#include <sysparam.h>

#include <homekit/homekit.h>
#include <homekit/characteristics.h>
#include <wifi_config.h>

#include <math.h>  //requires LIBS ?= hal m to be added to Makefile
#include "mjpwm.h"
#include "ota-api.h"  // ota menue und trigger update


#define DEVICE_MANUFACTURER "TrioSystem"
#define DEVICE_NAME "TrioBulb"
#define DEVICE_MODEL "MY90xx-RGBW"
#define DEVICE_SERIAL "12345678"
#define FW_VERSION "0.0.104"

#define FADE_SPEED 15
#define HSI_RGB_SCALE 4095  // this is the scaling factor used for hsi-to-rgb color conversion

#define INITIAL_HUE 0
#define INITIAL_SATURATION 0
#define INITIAL_BRIGHTNESS 100
#define INITIAL_ON true

#define PIN_DI 				13
#define PIN_DCKI 			15

// Task handle used to suspend and resume the animate task (doesn't need to be always running!)
TaskHandle_t animateTH;
// Boolean flag that can be used to gracefully stop the animate task
bool shouldQuitAnimationTask = false;

void on_wifi_ready();
//float hue,sat,bri;
//bool on;

float led_hue = 0;
float led_sat = 0;
float led_bri = 0;
float hue = INITIAL_HUE;
float sat = INITIAL_SATURATION;
float bri = INITIAL_BRIGHTNESS;
bool on = INITIAL_ON;

const char* animateLightPCName = "animate_task";



homekit_characteristic_t ota_trigger  = API_OTA_TRIGGER;
homekit_characteristic_t name         = HOMEKIT_CHARACTERISTIC_(NAME, DEVICE_NAME);
homekit_characteristic_t manufacturer = HOMEKIT_CHARACTERISTIC_(MANUFACTURER,  DEVICE_MANUFACTURER);
homekit_characteristic_t serial       = HOMEKIT_CHARACTERISTIC_(SERIAL_NUMBER, DEVICE_SERIAL);
homekit_characteristic_t model        = HOMEKIT_CHARACTERISTIC_(MODEL,         DEVICE_MODEL);
homekit_characteristic_t revision     = HOMEKIT_CHARACTERISTIC_(FIRMWARE_REVISION,  FW_VERSION);



//http://blog.saikoled.com/post/44677718712/how-to-convert-from-hsi-to-rgb-white
void hsi2rgbw(float h, float s, float i, int* rgbw) {
    int r, g, b, w;
    float cos_h, cos_1047_h;
    //h = fmod(h,360); // cycle h around to 0-360 degrees
    h = 3.14159*h/(float)180; // Convert to radians.
    s /=(float)100; i/=(float)100; //from percentage to ratio
    s = s>0?(s<1?s:1):0; // clamp s and i to interval [0,1]
    i = i>0?(i<1?i:1):0;
    i = i*sqrt(i); //shape intensity to have finer granularity near 0

    if(h < 2.09439) {
        cos_h = cos(h);
        cos_1047_h = cos(1.047196667-h);
        r = s*HSI_RGB_SCALE*i/3*(1+cos_h/cos_1047_h);
        g = s*HSI_RGB_SCALE*i/3*(1+(1-cos_h/cos_1047_h));
        b = 0;
        w = HSI_RGB_SCALE*(1-s)*i;
    } else if(h < 4.188787) {
        h = h - 2.09439;
        cos_h = cos(h);
        cos_1047_h = cos(1.047196667-h);
        g = s*HSI_RGB_SCALE*i/3*(1+cos_h/cos_1047_h);
        b = s*HSI_RGB_SCALE*i/3*(1+(1-cos_h/cos_1047_h));
        r = 0;
        w = HSI_RGB_SCALE*(1-s)*i;
    } else {
        h = h - 4.188787;
        cos_h = cos(h);
        cos_1047_h = cos(1.047196667-h);
        b = s*HSI_RGB_SCALE*i/3*(1+cos_h/cos_1047_h);
        r = s*HSI_RGB_SCALE*i/3*(1+(1-cos_h/cos_1047_h));
        g = 0;
        w = HSI_RGB_SCALE*(1-s)*i;
    }

    rgbw[0]=r;
    rgbw[1]=g;
    rgbw[2]=b;
    rgbw[3]=w;
}





// Hard write a color
void lightSETmjpwm(void) {
    int rgbw[4];
    //printf("initial h=%d,s=%d,b=%d\n",(int)hue,(int)sat,(int)bri);
    //printf("h=%d,s=%d,b=%d => ",(int)led_hue,(int)led_sat,(int)led_bri);

    hsi2rgbw(led_hue,led_sat,led_bri,rgbw);

    //printf("r=%d,g=%d,b=%d,w=%d\n",rgbw[0],rgbw[1],rgbw[2],rgbw[3]);

    mjpwm_send_duty(rgbw[0],rgbw[1],rgbw[2],rgbw[3]);
}

void lightSET(void) {
    int rgbw[4];
    if (on) {
        printf("h=%d,s=%d,b=%d => ",(int)led_hue,(int)led_sat,(int)led_bri);

        hsi2rgbw(hue,sat,bri,rgbw);

        printf("r=%d,g=%d,b=%d,w=%d\n",rgbw[0],rgbw[1],rgbw[2],rgbw[3]);

        mjpwm_send_duty(rgbw[0],rgbw[1],rgbw[2],rgbw[3]);
    } else {
        printf("off\n");
        mjpwm_send_duty(     0,      0,      0,      0 );
    }
}


float step_rot(float num, float target, float max, float stepWidth) {
    float distPlus = num < target ? target - num : (max - num + target);
    float distMin = num > target ? num - target : (num + max - target);
    if (distPlus < distMin) {
        if (distPlus > stepWidth) {
            float res = num + stepWidth;
            return res > max ? res - max : res;
        }
    } else {
        if (distMin > stepWidth) {
	    float res = num - stepWidth;
            return res < 0 ? res + max : res;
        }
    }
    return target;
}

float step(float num, float target, float stepWidth) {
    if (num < target) {
        if (num < target - stepWidth) return num + stepWidth;
	return target;
    } else if (num > target) {
        if (num > target + stepWidth) return num - stepWidth;
	return target;
    }

    return target;
}


void animate_light_transition_task(void* pvParameters) {
    //struct Color rgb;
    //int rgbw[4];
    float t_b;

    while (!shouldQuitAnimationTask) {
	// Compute brightness target value
	if (on) t_b = bri;
	else t_b = 0;

	// Do the transition
	while (!(hue == led_hue
	    	&& sat == led_sat
	   	&& t_b == led_bri)) {

	    // Update led values according to target
	    led_hue = step_rot(led_hue, hue, 360, 3.6);
	    led_sat = step(led_sat, sat, 1.0);
	    led_bri = step(led_bri, t_b, 1.0);

	    lightSETmjpwm();

	    // Only do this at most 60 times per second
	    vTaskDelay(FADE_SPEED / portTICK_PERIOD_MS);
	}

	vTaskSuspend(animateTH);
    }


    vTaskDelete(NULL);
}


void light_init() {
    mjpwm_cmd_t init_cmd = {
        .scatter = MJPWM_CMD_SCATTER_APDM,
        .frequency = MJPWM_CMD_FREQUENCY_DIVIDE_1,
        .bit_width = MJPWM_CMD_BIT_WIDTH_12,
        .reaction = MJPWM_CMD_REACTION_FAST,
        .one_shot = MJPWM_CMD_ONE_SHOT_DISABLE,
        .resv = 0,
    };
    mjpwm_init(PIN_DI, PIN_DCKI, 1, init_cmd);
    on=true; hue=0; sat=0; bri=100; //this should not be here, but part of the homekit init work
    //lightSET();
}


homekit_value_t light_on_get() {
    return HOMEKIT_BOOL(on);
}
void light_on_set(homekit_value_t value) {
    if (value.format != homekit_format_bool) {
        printf("Invalid on-value format: %d\n", value.format);
        return;
    }
    on = value.bool_value;
    //lightSET();
    vTaskResume(animateTH);
}

homekit_value_t light_bri_get() {
    return HOMEKIT_INT(bri);
}
void light_bri_set(homekit_value_t value) {
    if (value.format != homekit_format_int) {
        printf("Invalid bri-value format: %d\n", value.format);
        return;
    }
    bri = value.int_value;
    //lightSET();
    vTaskResume(animateTH);
}

homekit_value_t light_hue_get() {
    return HOMEKIT_FLOAT(hue);
}
void light_hue_set(homekit_value_t value) {
    if (value.format != homekit_format_float) {
        printf("Invalid hue-value format: %d\n", value.format);
        return;
    }
    hue = value.float_value;
    //lightSET();
    vTaskResume(animateTH);
}

homekit_value_t light_sat_get() {
    return HOMEKIT_FLOAT(sat);
}
void light_sat_set(homekit_value_t value) {
    if (value.format != homekit_format_float) {
        printf("Invalid sat-value format: %d\n", value.format);
        return;
    }
    sat = value.float_value;
    //lightSET();
    vTaskResume(animateTH);
}


void light_identify_task(void *_args) {
    for (int i=0;i<5;i++) {
        mjpwm_send_duty(4095,    0,    0,    0);
        vTaskDelay(300 / portTICK_PERIOD_MS); //0.3 sec
        mjpwm_send_duty(   0, 4095,    0,    0);
        vTaskDelay(300 / portTICK_PERIOD_MS); //0.3 sec
        mjpwm_send_duty(   0,    0, 4095,    0);
        vTaskDelay(300 / portTICK_PERIOD_MS); //0.3 sec
    }
    lightSET();
    //vTaskResume(animateTH);

    vTaskDelete(NULL);
}

void light_identify(homekit_value_t _value) {
    printf("Light Identify\n");
    xTaskCreate(light_identify_task, "Light identify", 256, NULL, 2, NULL);
}

homekit_accessory_t *accessories[] = {
    HOMEKIT_ACCESSORY(
        .id=1,
        .category=homekit_accessory_category_lightbulb,
        .services=(homekit_service_t*[]){
            HOMEKIT_SERVICE(ACCESSORY_INFORMATION,
                .characteristics=(homekit_characteristic_t*[]){
                    &name,
                    &manufacturer,
                    &serial,
                    &model,
                    &revision,
                    HOMEKIT_CHARACTERISTIC(IDENTIFY, light_identify),
                    NULL
                }),
            HOMEKIT_SERVICE(LIGHTBULB, .primary=true,
                .characteristics=(homekit_characteristic_t*[]){
                    //HOMEKIT_CHARACTERISTIC(NAME, DEVICE_NAME),
                    HOMEKIT_CHARACTERISTIC(
                        ON, true,
                        .getter=light_on_get,
                        .setter=light_on_set
                    ),
                    HOMEKIT_CHARACTERISTIC(
                        BRIGHTNESS, 100,
                        .getter=light_bri_get,
                        .setter=light_bri_set
                    ),
                    HOMEKIT_CHARACTERISTIC(
                        HUE, 0,
                        .getter=light_hue_get,
                        .setter=light_hue_set
                    ),
                    HOMEKIT_CHARACTERISTIC(
                        SATURATION, 0,
                        .getter=light_sat_get,
                        .setter=light_sat_set
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
    .password = "123-45-678",
    //.setupId="1QJ8",
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
}

void user_init(void) {
    //    uart_set_baud(0, 74880);
    uart_set_baud(0, 115200);
    printf("\n\n\n\n\n\n\nuser-init-start\n");
    light_init();

    create_accessory_name();
    wifi_config_init(DEVICE_NAME, NULL, on_wifi_ready);

    // Task for animating the led transition
    xTaskCreate(animate_light_transition_task, animateLightPCName, 1024, NULL, 1, &animateTH);

    printf("user-init-done\n");
}
