#include "board_probe.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

void app_main(void) {
#if CONFIG_BI_DIAG_PROBE
  for (;;) {
    board_probe_run();
    vTaskDelay(pdMS_TO_TICKS(5000));
  }
#endif
}
