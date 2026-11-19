#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include "ConfigManager.h"
#include "StatsManager.h"
#include "RTCManager.h"
#include "ScheduleManager.h"

enum PortalMode {
  PORTAL_OFF = 0,
  PORTAL_CONNECTING_STA = 1,
  PORTAL_STA = 2,
  PORTAL_AP = 3
};

class WebPortalManager {
public:
  static constexpr const char *AP_SSID = "FlipPomodoro-Setup";
  static constexpr unsigned long STA_CONNECT_TIMEOUT_MS = 8000UL;
  static constexpr unsigned long IDLE_TIMEOUT_MS = 10UL * 60UL * 1000UL;

  PortalMode getMode() const { return mode; }
  bool isActive() const { return mode != PORTAL_OFF; }
  bool isRunningServer() const { return mode == PORTAL_STA || mode == PORTAL_AP; }

  bool consumeConfigChanged() {
    bool v = configChanged;
    configChanged = false;
    return v;
  }
  bool consumeSyncRequested() {
    bool v = syncRequested;
    syncRequested = false;
    return v;
  }
  bool consumeScheduleChanged() {
    bool v = scheduleChanged;
    scheduleChanged = false;
    return v;
  }
  const char *getIpString() const { return ipStr; }
  int getConnectedClients() const { return (mode == PORTAL_AP) ? WiFi.softAPgetStationNum() : 0; }

  void begin(ConfigManager &cfg, StatsManager &st, RTCManager &rtcMgr, ScheduleManager &schedMgr);
  void start();
  void stop();
  void handleClient();

private:
  ConfigManager *config = nullptr;
  StatsManager *stats = nullptr;
  RTCManager *rtc = nullptr;
  ScheduleManager *schedule = nullptr;
  WebServer *server = nullptr;
  DNSServer *dns = nullptr;

  PortalMode mode = PORTAL_OFF;
  char ipStr[24] = "0.0.0.0";
  bool configChanged = false;
  bool syncRequested = false;
  bool scheduleChanged = false;
  unsigned long connectStartMs = 0;
  unsigned long lastActivityMs = 0;

  void startApFallback();
  void startServerOnly();
  void setupRoutes();
  void handleRoot();
  void handleApiData();
  void handleSaveConfig();
  void handleSyncNow();
  void handleResetStats();
  void handleClosePortal();
  void handleGetSchedule();
  void handleSaveSchedule();
};
