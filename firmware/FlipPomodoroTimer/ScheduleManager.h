#ifndef SCHEDULE_MANAGER_H
#define SCHEDULE_MANAGER_H

#include <Arduino.h>
#include <Preferences.h>
#include <algorithm>
#include <cstring>

enum ScheduleCategory : uint8_t {
  SCHED_FOCUS = 0,
  SCHED_BREAK = 1,
  SCHED_LIFE  = 2
};

struct ScheduleItem {
  uint16_t startMin; // 0..1439
  uint16_t endMin;   // 1..1440, must be > startMin (no 0-min items)
  uint8_t category;  // ScheduleCategory
  char name[48];     // UTF-8 name
};

class ScheduleManager {
public:
  static constexpr uint8_t MAX_ITEMS = 32;

  ScheduleItem items[MAX_ITEMS];
  uint8_t count = 0;

  void begin() {
    prefs.begin("pomo_sched", false);
    uint8_t ver = prefs.getUChar("ver", 0);
    uint8_t savedCount = prefs.getUChar("cnt", 0);
    if (ver != 1 || savedCount == 0 || savedCount > MAX_ITEMS) {
      resetDefaults();
      return;
    }

    size_t expectedBytes = sizeof(ScheduleItem) * savedCount;
    size_t readBytes = prefs.getBytes("items", items, expectedBytes);
    if (readBytes != expectedBytes) {
      resetDefaults();
      return;
    }

    count = savedCount;
    sanitizeAndSort();
    if (count == 0) {
      resetDefaults();
    }
  }

  void save() {
    sanitizeAndSort();
    prefs.putUChar("ver", 1);
    prefs.putUChar("cnt", count);
    if (count > 0) {
      prefs.putBytes("items", items, sizeof(ScheduleItem) * count);
    }
  }

  void resetDefaults() {
    struct DefItem {
      uint16_t startMin;
      uint16_t endMin;
      ScheduleCategory cat;
      const char* name;
    };

    static const DefItem kDefaults[] = {
      {  7 * 60 + 30,  9 * 60 +  0, SCHED_LIFE,  "起床与自由活动" },
      {  9 * 60 +  0,  9 * 60 + 45, SCHED_FOCUS, "专注周期1" },
      {  9 * 60 + 45,  9 * 60 + 55, SCHED_BREAK, "休息" },
      {  9 * 60 + 55, 10 * 60 + 40, SCHED_FOCUS, "专注周期2" },
      { 10 * 60 + 40, 10 * 60 + 50, SCHED_BREAK, "休息" },
      { 10 * 60 + 50, 11 * 60 + 35, SCHED_FOCUS, "专注周期3" },
      { 11 * 60 + 35, 11 * 60 + 45, SCHED_LIFE,  "简单整理" },
      { 11 * 60 + 45, 12 * 60 + 30, SCHED_LIFE,  "做饭、午餐" },
      { 12 * 60 + 30, 13 * 60 +  0, SCHED_LIFE,  "洗碗、散步" },
      { 13 * 60 +  0, 13 * 60 + 30, SCHED_BREAK, "午休" },
      { 13 * 60 + 30, 14 * 60 + 15, SCHED_FOCUS, "专注周期4" },
      { 14 * 60 + 15, 14 * 60 + 25, SCHED_BREAK, "休息" },
      { 14 * 60 + 25, 15 * 60 + 10, SCHED_FOCUS, "专注周期5" },
      { 15 * 60 + 10, 15 * 60 + 20, SCHED_BREAK, "休息" },
      { 15 * 60 + 20, 16 * 60 +  5, SCHED_FOCUS, "专注周期6" },
      { 16 * 60 +  5, 16 * 60 + 15, SCHED_FOCUS, "整理学习成果" },
      { 16 * 60 + 15, 16 * 60 + 45, SCHED_FOCUS, "口语训练" },
      { 16 * 60 + 45, 17 * 60 + 30, SCHED_FOCUS, "求职事务" },
      { 17 * 60 + 30, 18 * 60 + 30, SCHED_LIFE,  "晚餐" },
      { 18 * 60 + 30, 19 * 60 + 30, SCHED_BREAK, "饭后休息" },
      { 19 * 60 + 30, 20 * 60 + 30, SCHED_LIFE,  "运动" },
      { 20 * 60 + 30, 21 * 60 +  0, SCHED_LIFE,  "洗澡与整理" },
      { 21 * 60 +  0, 22 * 60 + 30, SCHED_LIFE,  "自由娱乐" },
      { 22 * 60 + 30, 22 * 60 + 45, SCHED_LIFE,  "回顾与规划" },
      { 22 * 60 + 45, 23 * 60 +  0, SCHED_LIFE,  "洗漱准备睡觉" },
      { 23 * 60 +  0, 23 * 60 + 30, SCHED_LIFE,  "睡前阅读" }
    };

    count = sizeof(kDefaults) / sizeof(kDefaults[0]);
    if (count > MAX_ITEMS) count = MAX_ITEMS;
    for (uint8_t i = 0; i < count; i++) {
      items[i].startMin = kDefaults[i].startMin;
      items[i].endMin   = kDefaults[i].endMin;
      items[i].category = static_cast<uint8_t>(kDefaults[i].cat);
      strncpy(items[i].name, kDefaults[i].name, sizeof(items[i].name) - 1);
      items[i].name[sizeof(items[i].name) - 1] = '\0';
    }
    save();
  }

  // Returns index of item where startMin <= nowMin < endMin, or -1 if none.
  int findActiveIndex(int nowMin) const {
    if (nowMin < 0 || nowMin >= 1440) return -1;
    for (uint8_t i = 0; i < count; i++) {
      if (nowMin >= items[i].startMin && nowMin < items[i].endMin) {
        return static_cast<int>(i);
      }
    }
    return -1;
  }

  // Returns index of next item starting after nowMin, or -1 if none left today.
  int findNextIndex(int nowMin) const {
    if (nowMin < 0) nowMin = 0;
    for (uint8_t i = 0; i < count; i++) {
      if (items[i].startMin > nowMin) {
        return static_cast<int>(i);
      }
    }
    return -1;
  }

  static void formatHHMM(uint16_t minOfDay, char* out, size_t len) {
    if (minOfDay > 1440) minOfDay = 1440;
    uint16_t h = minOfDay / 60;
    uint16_t m = minOfDay % 60;
    snprintf(out, len, "%02u:%02u", (unsigned)h, (unsigned)m);
  }

  static const char* categoryLabel(uint8_t cat) {
    switch (cat) {
      case SCHED_FOCUS: return "专注";
      case SCHED_BREAK: return "休息";
      default:          return "生活";
    }
  }

  void sanitizeAndSort() {
    uint8_t valid = 0;
    for (uint8_t i = 0; i < count && i < MAX_ITEMS; i++) {
      ScheduleItem it = items[i];
      if (it.startMin >= 1440) continue;
      if (it.endMin > 1440) it.endMin = 1440;
      // Reject 0-minute or inverted entries
      if (it.endMin <= it.startMin) continue;
      if (it.category > SCHED_LIFE) it.category = SCHED_LIFE;
      it.name[sizeof(it.name) - 1] = '\0';
      if (it.name[0] == '\0') {
        strncpy(it.name, "日程事项", sizeof(it.name) - 1);
      }
      items[valid++] = it;
    }
    count = valid;
    if (count > 1) {
      std::sort(items, items + count, [](const ScheduleItem& a, const ScheduleItem& b) {
        if (a.startMin != b.startMin) return a.startMin < b.startMin;
        return a.endMin < b.endMin;
      });
    }
  }

private:
  Preferences prefs;
};

#endif
