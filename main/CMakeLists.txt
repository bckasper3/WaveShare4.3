# main: the ESP-IDF layer (docs/FIRMWARE_ARCH.md). Every .c in these folders is compiled.
# No REQUIRES list: main may use any ESP-IDF or managed component.
idf_component_register(
    SRC_DIRS "." "app" "board" "rtc" "usb" "comms" "sensor" "storage" "fwupdate" "ui" "console" "net"
    INCLUDE_DIRS "." "app" "board" "rtc" "usb" "comms" "sensor" "storage" "fwupdate" "ui" "console" "net")
