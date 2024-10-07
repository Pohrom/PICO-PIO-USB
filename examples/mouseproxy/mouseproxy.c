/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2019 Ha Thach (tinyusb.org)
 *                    sekigon-gonnoc
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

// This example runs both host and device concurrently. The USB host receive
// reports from HID device and print it out over USB Device CDC interface.
// For TinyUSB roothub port0 is native usb controller, roothub port1 is
// pico-pio-usb.

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "bsp/board_api.h"
#include "hardware/clocks.h"
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/bootrom.h"
#include "pico/sync.h"

#include "pio_usb.h"
#include "tusb.h"
#include "usb_descriptors.h"

//--------------------------------------------------------------------+
// MACRO CONSTANT TYPEDEF PROTYPES
//--------------------------------------------------------------------+
// packed
typedef struct __attribute__ ((packed)) {
  int32_t x;
  int32_t y;
  int8_t  splits;
} inject_report_t;
static_assert(sizeof(inject_report_t) == 9, "inject_report_t size is not correct");

typedef struct {
  int8_t buttons;
  int8_t x;
  int8_t y;
  int8_t wheel;
  int16_t lx;
  int16_t ly;
} device_report_t;

inject_report_t inject_report = {0};
int left_splits = 0;

auto_init_recursive_mutex(device_report_mu);
device_report_t device_report = {0};
bool to_consume = false;

void hid_task(void);

/*------------- MAIN -------------*/

// core1: handle host events
void core1_main() {
  sleep_ms(10);

  // Use tuh_configure() to pass pio configuration to the host stack
  // Note: tuh_configure() must be called before
  pio_usb_configuration_t pio_cfg = PIO_USB_DEFAULT_CONFIG;
  tuh_configure(BOARD_TUH_RHPORT, TUH_CFGID_RPI_PIO_USB_CONFIGURATION, &pio_cfg);

  // To run USB SOF interrupt in core1, init host stack for pio_usb (roothub
  // port1) on core1
  tuh_init(BOARD_TUH_RHPORT);

  while (true) {
    tuh_task(); // tinyusb host task
  }
}

// core0: handle device events
int main(void) {
  // default 125MHz is not appropreate. Sysclock should be multiple of 12MHz.
  set_sys_clock_khz(120000, true);

  sleep_ms(10);

  uart_inst_t *uart_inst = uart_get_instance(UART_DEV);
  stdio_uart_init_full(uart_inst, CFG_BOARD_UART_BAUDRATE, UART_TX_PIN, UART_RX_PIN);

  recursive_mutex_init(&device_report_mu);

  multicore_reset_core1();
  // all USB task run in core1
  multicore_launch_core1(core1_main);

  // init device stack on native usb (roothub port0)
  tud_init(BOARD_TUD_RHPORT);

  while (true) {
    tud_task(); // tinyusb device task
    hid_task();
    //tud_cdc_write_flush();
  }

  return 0;
}

//--------------------------------------------------------------------+
// Device HID
//--------------------------------------------------------------------+

int8_t clamp_to_int8(int32_t x) {
  if (x > INT8_MAX) {
    return INT8_MAX;
  } else if (x < INT8_MIN) {
    return INT8_MIN;
  } else {
    return x;
  }
}

// Every 1ms, we will sent 1 report for each HID profile (keyboard, mouse etc ..)
// tud_hid_report_complete_cb() is used to send the next report after previous one is complete
void hid_task(void)
{
  // Poll every 1ms
  const uint32_t interval_ms = 1;
  static uint32_t start_ms = 0;

  if ( board_millis() - start_ms < interval_ms) return; // not enough time
  start_ms += interval_ms;

  // // fixme: board_button_read 会导致没响应。
  // uint32_t const btn = board_button_read();
  
  recursive_mutex_enter_blocking(&device_report_mu);
  if (to_consume || left_splits > 0)
  {
    device_report_t report = {0};

    // 这一时刻，用户按了就是按了，没按就是没按。
    if (to_consume) {
      report = device_report;
      to_consume = false;
    }
    recursive_mutex_exit(&device_report_mu);

    if (left_splits > 0 && inject_report.splits > 0) {
      int32_t x = report.x + inject_report.x / (int32_t)inject_report.splits;
      if (x > INT8_MAX) {
        report.x = INT8_MAX;
      } else if (x < INT8_MIN) {
        report.x = INT8_MIN;
      } else {
        report.x = x;
      }
      int32_t y = report.y + inject_report.y / (int32_t)inject_report.splits;
      if (y > INT8_MAX) {
        report.y = INT8_MAX;
      } else if (y < INT8_MIN) {
        report.y = INT8_MIN;
      } else {
        report.y = y;
      }
      int32_t lx = report.lx + inject_report.x / (int32_t)inject_report.splits;
      if (lx > INT16_MAX) {
        report.lx = INT16_MAX;
      } else if (lx < INT16_MIN) {
        report.lx = INT16_MIN;
      } else {
        report.lx = lx;
      }
      int32_t ly = report.ly + inject_report.y / (int32_t)inject_report.splits;
      if (ly > INT16_MAX) {
        report.ly = INT16_MAX;
      } else if (ly < INT16_MIN) {
        report.ly = INT16_MIN;
      } else {
        report.ly = ly;
      }
      left_splits--;
    }

    // if (report.lx != 0 || report.ly != 0) {
    //   report.x = clamp_to_int8(report.lx);
    //   report.y = clamp_to_int8(report.ly);
    // }

    // char l = report.buttons & MOUSE_BUTTON_LEFT   ? 'L' : '-';
    // char m = report.buttons & MOUSE_BUTTON_MIDDLE ? 'M' : '-';
    // char r = report.buttons & MOUSE_BUTTON_RIGHT  ? 'R' : '-';

    // char tempbuf[48];
    // int count = sprintf(tempbuf, "%c%c%c %d %d %d %d %d\r\n", l, m, r, report.x, report.y, report.lx, report.ly, report.wheel);

    // tud_cdc_write(tempbuf, count);
    // tud_cdc_write_flush();

    // tud_cdc_write_str("send mouse report\r\n");
    // tud_cdc_write_flush();
    tud_hid_report(10, &report, sizeof(report));
    return;
  }

  recursive_mutex_exit(&device_report_mu);
}

// Invoked when device is mounted
void tud_mount_cb(void)
{
}

// Invoked when device is unmounted
void tud_umount_cb(void)
{
}

// Invoked when usb bus is suspended
// remote_wakeup_en : if host allow us  to perform remote wakeup
// Within 7ms, device must draw an average of current less than 2.5 mA from bus
void tud_suspend_cb(bool remote_wakeup_en)
{
  (void) remote_wakeup_en;
}

// Invoked when usb bus is resumed
void tud_resume_cb(void)
{
}

// Invoked when received SET_PROTOCOL request
// protocol is either HID_PROTOCOL_BOOT (0) or HID_PROTOCOL_REPORT (1)
void tud_hid_set_protocol_cb(uint8_t instance, uint8_t protocol)
{
  (void) instance;
  (void) protocol;

  // nothing to do since we use the same compatible boot report for both Boot and Report mode.
  // TODO set a indicator for user
}

// Invoked when sent REPORT successfully to host
// Application can use this to send the next report
// Note: For composite reports, report[0] is report ID
void tud_hid_report_complete_cb(uint8_t instance, uint8_t const* report, uint16_t len)
{
  (void) instance;
  (void) report;
  (void) len;

  // nothing to do
}

// Invoked when received GET_REPORT control request
// Application must fill buffer report's content and return its length.
// Return zero will cause the stack to STALL request
uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t* buffer, uint16_t reqlen)
{
  (void) instance;
  (void) report_id;
  (void) report_type;
  (void) buffer;
  (void) reqlen;

  return 0;
  
  // uint8_t daddr = 0; // fixme
  // return (uint16_t)tuh_hid_get_report(daddr, instance, report_id, report_type, buffer, reqlen);
}

// Invoked when received SET_REPORT control request or
// received data on OUT endpoint ( Report ID = 0, Type = 0 )
void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t const* buffer, uint16_t bufsize)
{
  (void) instance;
  (void) report_id;
  (void) report_type;
  (void) buffer;
  (void) bufsize;

  // handle injected report from hidapi client
  // inject_report 同核心操作，不需要锁
  inject_report_t const *report = (inject_report_t const*) buffer;
  inject_report.x = report->x;
  inject_report.y = report->y;
  inject_report.splits = report->splits;
  left_splits = report->splits;
  
  // uint8_t daddr = 0; // fixme
  // tuh_hid_set_report(daddr, instance, report_id, report_type, (uint8_t*)buffer, bufsize);
  // return;
}

//--------------------------------------------------------------------+
// Device CDC
//--------------------------------------------------------------------+

// Invoked when CDC interface received data from host
void tud_cdc_rx_cb(uint8_t itf)
{
  (void) itf;

  char buf[64];
  uint32_t count = tud_cdc_read(buf, sizeof(buf));

  // TODO control LED on keyboard of host stack
  (void) count;
}

//--------------------------------------------------------------------+
// Host HID
//--------------------------------------------------------------------+

// Invoked when device with hid interface is mounted
// Report descriptor is also available for use. tuh_hid_parse_report_descriptor()
// can be used to parse common/simple enough descriptor.
// Note: if report descriptor length > CFG_TUH_ENUMERATION_BUFSIZE, it will be skipped
// therefore report_desc = NULL, desc_len = 0
void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance, uint8_t const* desc_report, uint16_t desc_len)
{
  (void)desc_report;
  (void)desc_len;

  // Interface protocol (hid_interface_protocol_enum_t)
  const char* protocol_str[] = { "None", "Keyboard", "Mouse" };
  uint8_t const itf_protocol = tuh_hid_interface_protocol(dev_addr, instance);

  uint16_t vid, pid;
  tuh_vid_pid_get(dev_addr, &vid, &pid);

  char tempbuf[256];
  int count = sprintf(tempbuf, "[%04x:%04x][%u] HID Interface%u, Protocol = %s\r\n", vid, pid, dev_addr, instance, protocol_str[itf_protocol]);

  tud_cdc_write(tempbuf, count);
  tud_cdc_write_flush();

  // Receive report from boot keyboard & mouse only
  // tuh_hid_report_received_cb() will be invoked when report is available
  if (itf_protocol == HID_ITF_PROTOCOL_KEYBOARD || itf_protocol == HID_ITF_PROTOCOL_MOUSE)
  {
    if ( !tuh_hid_receive_report(dev_addr, instance) )
    {
      tud_cdc_write_str("Error: cannot request report\r\n");
    }
  }

  TU_LOG1("[%04x:%04x][%u] HID Interface%u, Protocol = %s\r\n", vid, pid, dev_addr, instance, protocol_str[itf_protocol]);
}

// Invoked when device with hid interface is un-mounted
void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance)
{
  char tempbuf[256];
  int count = sprintf(tempbuf, "[%u] HID Interface%u is unmounted\r\n", dev_addr, instance);
  tud_cdc_write(tempbuf, count);
  tud_cdc_write_flush();
}

// convert hid keycode to ascii and print via usb device CDC (ignore non-printable)
static void process_kbd_report(uint8_t dev_addr, hid_keyboard_report_t const *report)
{
  (void) dev_addr;
  (void) report;

  // not supported
}

// send mouse report to usb device CDC
static void process_mouse_report(uint8_t dev_addr, device_report_t const * report, uint16_t len)
{
  (void) dev_addr;
  (void) len;

  // char l = report->buttons & MOUSE_BUTTON_LEFT   ? 'L' : '-';
  // char m = report->buttons & MOUSE_BUTTON_MIDDLE ? 'M' : '-';
  // char r = report->buttons & MOUSE_BUTTON_RIGHT  ? 'R' : '-';

  // char tempbuf[48];
  // int count = sprintf(tempbuf, "[%u] %c%c%c %d %d %d %d %d\r\n", dev_addr, l, m, r, report->x, report->y, report->lx, report->ly, report->wheel);

  // tud_cdc_write(tempbuf, count);
  // tud_cdc_write_flush();

  recursive_mutex_enter_blocking(&device_report_mu);
  device_report.buttons = report->buttons;
  device_report.x = report->x;
  device_report.y = report->y;
  device_report.wheel = report->wheel;
  device_report.lx = report->lx;
  device_report.ly = report->ly;
  to_consume = true;
  recursive_mutex_exit(&device_report_mu);
}

// Invoked when received report from device via interrupt endpoint
void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance, uint8_t const* report, uint16_t len)
{
  (void) len;
  uint8_t const itf_protocol = tuh_hid_interface_protocol(dev_addr, instance);

  switch(itf_protocol)
  {
    case HID_ITF_PROTOCOL_KEYBOARD:
      process_kbd_report(dev_addr, (hid_keyboard_report_t const*) report );
    break;

    case HID_ITF_PROTOCOL_MOUSE:
      process_mouse_report(dev_addr, (device_report_t const*) report , len);
    break;

    default: break;
  }

  // continue to request to receive report
  if ( !tuh_hid_receive_report(dev_addr, instance) )
  {
    tud_cdc_write_str("Error: cannot request report\r\n");
  }
}
