// Serial commands on the console port: USB Serial/JTAG on the 4" and 7", the UART port on the
// 7B. esp_console gives line editing and history; each command goes to app_cmd_run.
#include "console.h"
#include <stdio.h>
#include "app_cmd.h"
#include "esp_console.h"
#include "esp_log.h"
#include "sdkconfig.h"

static void line_out(const char *line) { printf("%s\n", line); }
static int forward(int argc, char **argv) { return app_cmd_run(argc, argv, line_out); }

void console_start(void) {
  esp_console_repl_t *repl = NULL;
  esp_console_repl_config_t rc = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
  rc.prompt = "bi> ";
  rc.task_stack_size = 6144;
  esp_err_t err;
#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
  esp_console_dev_usb_serial_jtag_config_t dev = ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
  err = esp_console_new_repl_usb_serial_jtag(&dev, &rc, &repl);
#else
  esp_console_dev_uart_config_t dev = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
  err = esp_console_new_repl_uart(&dev, &rc, &repl);
#endif
  if (err != ESP_OK) {
    ESP_LOGE("console", "cannot start the serial console: %s", esp_err_to_name(err));
    return;
  }
  static const char *const names[] = {"help", "status", "data", "config", "sk", "reboot"};
  static const char *const hints[] = {"list commands", "firmware, network, Signal K, memory", "<path prefix>: values, ages, sources", "show | set <path> <value> | reload",
                                      "request-access", "restart"};
  for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) {
    esp_console_cmd_t cmd = {.command = names[i], .help = hints[i], .func = forward};
    esp_console_cmd_register(&cmd);
  }
  esp_console_start_repl(repl);
}
