#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include <string.h>

struct DayStat {
  uint32_t yyyymmdd = 0;
  uint16_t count = 0;
  uint16_t minutes = 0;
};

class StatsManager {
public:
  static constexpr int MAX_DAYS = 7;

  uint32_t totalPomodoros = 0;
  uint32_t totalFocusMinutes = 0;
  DayStat days[MAX_DAYS]; // days[0] = oldest, days[MAX_DAYS - 1] = most recent

  void begin() {
    Preferences prefs;
    if (!prefs.begin("pomo_stats", true)) return;
    totalPomodoros = prefs.getUInt("tot_cnt", 0);
    totalFocusMinutes = prefs.getUInt("tot_min", 0);
    size_t readLen = prefs.getBytes("days", days, sizeof(days));
    if (readLen != sizeof(days)) {
      memset(days, 0, sizeof(days));
    }
    prefs.end();
  }

  void recordCompletion(uint32_t yyyymmdd, uint16_t focusMinutes) {
    if (focusMinutes == 0) focusMinutes = 1;
    totalPomodoros++;
    totalFocusMinutes += focusMinutes;

    // If RTC date is invalid (0), still track in the latest slot with date 0.
    int found = -1;
    for (int i = 0; i < MAX_DAYS; ++i) {
      if (days[i].yyyymmdd == yyyymmdd && (yyyymmdd != 0 || days[i].count > 0)) {
        found = i;
        break;
      }
    }

    if (found >= 0) {
      days[found].count++;
      days[found].minutes += focusMinutes;
    } else {
      // Shift older entries left and append today's slot at the end.
      for (int i = 0; i < MAX_DAYS - 1; ++i) {
        days[i] = days[i + 1];
      }
      days[MAX_DAYS - 1].yyyymmdd = yyyymmdd;
      days[MAX_DAYS - 1].count = 1;
      days[MAX_DAYS - 1].minutes = focusMinutes;
    }
    save();
  }

  uint16_t getTodayCount(uint32_t yyyymmdd) const {
    for (int i = MAX_DAYS - 1; i >= 0; --i) {
      if (days[i].yyyymmdd == yyyymmdd && (yyyymmdd != 0 || days[i].count > 0)) {
        return days[i].count;
      }
    }
    return 0;
  }

  uint16_t getTodayMinutes(uint32_t yyyymmdd) const {
    for (int i = MAX_DAYS - 1; i >= 0; --i) {
      if (days[i].yyyymmdd == yyyymmdd && (yyyymmdd != 0 || days[i].count > 0)) {
        return days[i].minutes;
      }
    }
    return 0;
  }

  void resetAll() {
    totalPomodoros = 0;
    totalFocusMinutes = 0;
    memset(days, 0, sizeof(days));
    save();
  }

private:
  void save() {
    Preferences prefs;
    if (!prefs.begin("pomo_stats", false)) return;
    prefs.putUInt("tot_cnt", totalPomodoros);
    prefs.putUInt("tot_min", totalFocusMinutes);
    prefs.putBytes("days", days, sizeof(days));
    prefs.end();
  }
};
