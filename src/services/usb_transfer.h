#pragma once
#include <stdbool.h>
// Called by UI/console; changes are applied by the service worker.
void usb_transfer_request(bool enable);
void usb_transfer_poll(void);
bool usb_transfer_busy(void);
void usb_transfer_diagnostics(void);
// Serial-only diagnostic: exercise the real 60s timeout with USB detached.
void usb_transfer_test_nohost(void);
