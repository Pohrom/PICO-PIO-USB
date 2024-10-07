#include <hidapi.h>


typedef struct __attribute__ ((packed)) {
  int8_t report_id;
  int32_t x;
  int32_t y;
  int8_t  splits;
} inject_report_t;

int main(void) {
  hid_device *handle;
  int res;
  inject_report_t report = {0};

  handle = hid_open(0xBADD, 0x4005, NULL);
  if (!handle) {
    fprintf(stderr, "unable to open device\n");
    return 1;
  }

  report.report_id = 0;
  report.x = 100;
  report.y = 100;
  report.splits = 1;
  
  // method 1
  res = hid_send_feature_report(handle, (unsigned char*)&report, sizeof(report));
  if (res < 0) {
      fprintf(stderr, "unable to write to device\n");
      return 1;
  }

  //// method 2
  // res = hid_write(handle, (unsigned char*)&report, sizeof(report));
  // if (res < 0) {
  //     fprintf(stderr, "unable to write to device\n");
  //     return 1;
  // }
  return 0;
}