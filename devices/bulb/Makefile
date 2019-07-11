PROGRAM = light

EXTRA_COMPONENTS = \
	extras/http-parser \
	extras/dhcpserver \
	extras/rboot-ota \
	$(abspath ../../components/esp-8266/wifi_config) \
	$(abspath ../../components/common/wolfssl) \
	$(abspath ../../components/common/cJSON) \
	$(abspath ../../components/common/homekit)

FLASH_SIZE ?= 8
FLASH_MODE ?= dout
FLASH_SPEED ?= 40
HOMEKIT_SPI_FLASH_BASE_ADDR ?= 0x7A000

EXTRA_CFLAGS += -I../.. -DHOMEKIT_SHORT_APPLE_UUIDS

include $(SDK_PATH)/common.mk



LIBS += m



$(shell /usr/local/opt/openssl/bin/openssl   sha384 -binary -out firmware/light.bin.sig firmware/light.bin)
$(shell printf "%08x" `cat firmware/light.bin | wc -c`| xxd -r -p >>firmware/light.bin.sig)
 


monitor:
	$(FILTEROUTPUT) --port $(ESPPORT) --baud $(ESPBAUD) --elf $(PROGRAM_OUT)
