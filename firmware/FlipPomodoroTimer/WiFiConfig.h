#pragma once
#if __has_include("WiFiConfig.local.h")
#include "WiFiConfig.local.h"
#else
// Fresh clones can build and use the hardware RTC without a network.
const char *WIFI_SSID = "";
const char *WIFI_PASS = "";
#endif
