
# brew install gcc-arm-embedded
cmake_minimum_required(VERSION 3.13)
set(PICO_SDK_FETCH_FROM_GIT 1)
set(PICO_BOARD waveshare_rp2040_zero)
add_compile_definitions(PIO_USB_DP_PIN_DEFAULT=2)
add_compile_definitions(PICO_DEFAULT_LED_PIN=PICO_DEFAULT_WS2812_PIN)
add_compile_definitions(UART_DEV=1)
add_compile_definitions(UART_TX_PIN=8)
add_compile_definitions(UART_RX_PIN=9)
include (pico_sdk_import.cmake)
project(pio_usb)

pico_sdk_init()

# add pico-pio-usb library for examples
set(PICO_PIO_USB_DIR "${CMAKE_CURRENT_LIST_DIR}/../")
# the second argument to add_subdirectory is needed here because this isn't
# a subdirectory, it's out of tree.
add_subdirectory(${PICO_PIO_USB_DIR} pico_pio_usb)

add_subdirectory(usb_device)
add_subdirectory(host_hid_to_device_cdc)
add_subdirectory(test_ll)
add_subdirectory(mouseproxy)
