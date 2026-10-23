# Vendored from Espressif's esp-iot-solution "iot_usbh_cdc" (Apache-2.0),
# via the p4-tab5 branch's lib/iot_usbh_cdc. No Kconfig shipped with it;
# pinned as plain -D flags below, matching upstream's platformio.ini.
idf_component_register(
    SRCS "iot_usbh_cdc.c" "iot_usbh_descriptor.c"
    INCLUDE_DIRS "."
    REQUIRES usb freertos esp_ringbuf
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE
    CONFIG_USBH_TASK_CORE_ID=0
    CONFIG_USBH_TASK_BASE_PRIORITY=5
    CONFIG_CONTROL_TRANSFER_BUFFER_SIZE=512
    CONFIG_IN_TRANSFER_BUFFER_SIZE=512
    CONFIG_OUT_TRANSFER_BUFFER_SIZE=512
    CONFIG_DEVICE_ADDRESS_LIST_NUM=10
    IOT_USBH_CDC_VER_MAJOR=2
    IOT_USBH_CDC_VER_MINOR=0
    IOT_USBH_CDC_VER_PATCH=1
)
