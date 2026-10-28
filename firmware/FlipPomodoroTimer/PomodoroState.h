#pragma once
#include <Arduino.h>

enum TimerState {
  STATE_IDLE,           // 待机准备就绪
  STATE_FOCUS_RUNNING,   // 专注计时中
  STATE_FOCUS_PAUSED,    // 专注暂停中
  STATE_BREAK_RUNNING,   // 休息计时中
  STATE_BREAK_PAUSED,    // 休息暂停中
  STATE_COMPLETED       // 计时周期完成
};

enum PauseReason {
  PAUSE_NONE,
  PAUSE_BY_FLIP,        // 由翻转动作触发暂停
  PAUSE_BY_USER         // 由用户按键/触摸手动暂停
};

class PomodoroEngine {
public:
  static constexpr unsigned long DEFAULT_FOCUS_SECONDS = 25 * 60;  // 25 分钟
  static constexpr unsigned long DEFAULT_BREAK_SECONDS = 5 * 60;   // 5 分钟

  TimerState state = STATE_IDLE;
  PauseReason pauseReason = PAUSE_NONE;

  unsigned long focusDurationSec = DEFAULT_FOCUS_SECONDS;
  unsigned long breakDurationSec = DEFAULT_BREAK_SECONDS;

  unsigned long currentTotalDurationSec = DEFAULT_FOCUS_SECONDS;
  unsigned long elapsedMs = 0;
  unsigned long lastResumeTimestampMs = 0;

  uint16_t completedPomodoros = 0;
  uint8_t activeFace = 3; // 默认活跃面为面 3 (25 分钟标准番茄)
  bool stateChanged = true;
  bool isAlerting = false;

  void begin() {
    state = STATE_IDLE;
    pauseReason = PAUSE_NONE;
    activeFace = 3;
    currentTotalDurationSec = focusDurationSec;
    elapsedMs = 0;
    stateChanged = true;
  }

  void startFace(uint8_t faceIndex, unsigned long durationSec) {
    activeFace = faceIndex;
    focusDurationSec = durationSec;
    currentTotalDurationSec = durationSec;
    elapsedMs = 0;
    lastResumeTimestampMs = millis();
    state = STATE_FOCUS_RUNNING;
    pauseReason = PAUSE_NONE;
    isAlerting = false;
    stateChanged = true;
  }

  void startFocus() {
    state = STATE_FOCUS_RUNNING;
    pauseReason = PAUSE_NONE;
    currentTotalDurationSec = focusDurationSec;
    elapsedMs = 0;
    lastResumeTimestampMs = millis();
    isAlerting = false;
    stateChanged = true;
  }

  void startBreak() {
    state = STATE_BREAK_RUNNING;
    pauseReason = PAUSE_NONE;
    currentTotalDurationSec = breakDurationSec;
    elapsedMs = 0;
    lastResumeTimestampMs = millis();
    isAlerting = false;
    stateChanged = true;
  }

  void pause(PauseReason reason = PAUSE_BY_USER) {
    if (state == STATE_FOCUS_RUNNING) {
      updateElapsed();
      state = STATE_FOCUS_PAUSED;
      pauseReason = reason;
      stateChanged = true;
    } else if (state == STATE_BREAK_RUNNING) {
      updateElapsed();
      state = STATE_BREAK_PAUSED;
      pauseReason = reason;
      stateChanged = true;
    }
  }

  void resume() {
    if (state == STATE_FOCUS_PAUSED) {
      lastResumeTimestampMs = millis();
      state = STATE_FOCUS_RUNNING;
      pauseReason = PAUSE_NONE;
      stateChanged = true;
    } else if (state == STATE_BREAK_PAUSED) {
      lastResumeTimestampMs = millis();
      state = STATE_BREAK_RUNNING;
      pauseReason = PAUSE_NONE;
      stateChanged = true;
    } else if (state == STATE_IDLE) {
      startFocus();
    }
  }

  void toggle() {
    if (state == STATE_IDLE) {
      startFocus();
    } else if (state == STATE_FOCUS_RUNNING || state == STATE_BREAK_RUNNING) {
      pause(PAUSE_BY_USER);
    } else if (state == STATE_FOCUS_PAUSED || state == STATE_BREAK_PAUSED) {
      resume();
    } else if (state == STATE_COMPLETED) {
      confirmCompleted();
    }
  }

  void reset() {
    state = STATE_IDLE;
    pauseReason = PAUSE_NONE;
    currentTotalDurationSec = focusDurationSec;
    elapsedMs = 0;
    isAlerting = false;
    stateChanged = true;
  }

  void confirmCompleted() {
    isAlerting = false;
    if (state == STATE_COMPLETED) {
      // 专注完成后切换至准备休息
      if (currentTotalDurationSec == focusDurationSec) {
        completedPomodoros++;
        state = STATE_IDLE;
        currentTotalDurationSec = breakDurationSec;
        elapsedMs = 0;
      } else {
        // 休息完成后切回准备专注
        state = STATE_IDLE;
        currentTotalDurationSec = focusDurationSec;
        elapsedMs = 0;
      }
      stateChanged = true;
    }
  }

  void updateElapsed() {
    if (state == STATE_FOCUS_RUNNING || state == STATE_BREAK_RUNNING) {
      unsigned long now = millis();
      elapsedMs += (now - lastResumeTimestampMs);
      lastResumeTimestampMs = now;
    }
  }

  void tick(unsigned long nowMs) {
    if (state == STATE_FOCUS_RUNNING || state == STATE_BREAK_RUNNING) {
      unsigned long curElapsed = elapsedMs + (nowMs - lastResumeTimestampMs);
      unsigned long totalTargetMs = currentTotalDurationSec * 1000UL;

      if (curElapsed >= totalTargetMs) {
        elapsedMs = totalTargetMs;
        state = STATE_COMPLETED;
        isAlerting = true;
        stateChanged = true;
      }
    }
  }

  unsigned long getRemainingSeconds() const {
    unsigned long totalTargetMs = currentTotalDurationSec * 1000UL;
    unsigned long curElapsed = elapsedMs;
    if (state == STATE_FOCUS_RUNNING || state == STATE_BREAK_RUNNING) {
      curElapsed += (millis() - lastResumeTimestampMs);
    }
    if (curElapsed >= totalTargetMs) {
      return 0;
    }
    return (totalTargetMs - curElapsed + 999UL) / 1000UL;
  }

  float getProgress() const {
    if (currentTotalDurationSec == 0) return 1.0f;
    unsigned long totalTargetMs = currentTotalDurationSec * 1000UL;
    unsigned long curElapsed = elapsedMs;
    if (state == STATE_FOCUS_RUNNING || state == STATE_BREAK_RUNNING) {
      curElapsed += (millis() - lastResumeTimestampMs);
    }
    if (curElapsed >= totalTargetMs) return 1.0f;
    return (float)curElapsed / (float)totalTargetMs;
  }
};
