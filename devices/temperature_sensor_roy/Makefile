PROGRAM = temperature_sensor

EXTRA_COMPONENTS = \
	extras/dht \
	extras/http-parser \
	extras/dhcpserver \
	extras/ssd1306 \
	extras/i2c \
	extras/fonts \
	$(abspath ../../components/esp-8266/wifi_config) \
	$(abspath ../../components/common/wolfssl) \
	$(abspath ../../components/common/cJSON) \
	$(abspath ../../components/common/homekit) \
	$(abspath ../../components/esp-8266/qrcode)

# Enable fonts provided by extras/fonts package
FONTS_TERMINUS_6X12_ISO8859_1 = 1
FONTS_TERMINUS_BOLD_6X12_ISO8859_1 = 1
FONTS_TERMINUS_BOLD_12X24_ISO8859_1 = 1
FONTS_TERMINUS_BOLD_14X28_ISO8859_1 = 1
FONTS_TERMINUS_BOLD_16X32_ISO8859_1 = 1


FLASH_SIZE ?= 32

EXTRA_CFLAGS += -I../.. -DHOMEKIT_SHORT_APPLE_UUIDS \
	-DHOMEKIT_PASSWORD="$(HOMEKIT_PASSWORD)" \
	-DHOMEKIT_SETUP_ID="$(HOMEKIT_SETUP_ID)"

include $(SDK_PATH)/common.mk

monitor:
	$(FILTEROUTPUT) --port $(ESPPORT) --baud 115200 --elf $(PROGRAM_OUT)
