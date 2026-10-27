#ifndef LOOP_STATS_H
#define LOOP_STATS_H

#include <Arduino.h>

// ============================================================================
// Loop timing for GET /state — the real frame rate, not the 33 ms target
// ============================================================================
// period: loop start to the next loop start (work + the pacing wait + any
// time the scheduler takes away). work: loop start to just before the delay.
// push: one canvas pushSprite to the panel. fps counts loops, pushFps counts
// frames that actually reached the screen. Reading starts a fresh window.
// ============================================================================

struct LoopStats {
  uint32_t windowStartUs, lastStartUs;
  uint32_t loops, periodSumUs, periodMaxUs;
  uint32_t workSumUs, workMaxUs;
  uint32_t pushes, pushSumUs, pushMaxUs;
};
LoopStats loopStats = {0};

inline void loopStatsBegin() {
  uint32_t now = micros();
  LoopStats &st = loopStats;
  if (st.lastStartUs) {
    uint32_t period = now - st.lastStartUs;
    st.periodSumUs += period;
    if (period > st.periodMaxUs) st.periodMaxUs = period;
    st.loops++;
  }
  if (!st.windowStartUs) st.windowStartUs = now;
  st.lastStartUs = now;
}

inline void loopStatsWorkDone() {
  uint32_t work = micros() - loopStats.lastStartUs;
  loopStats.workSumUs += work;
  if (work > loopStats.workMaxUs) loopStats.workMaxUs = work;
}

inline void loopStatsPush(uint32_t us) {
  loopStats.pushes++;
  loopStats.pushSumUs += us;
  if (us > loopStats.pushMaxUs) loopStats.pushMaxUs = us;
}

String loopStatsJson() {
  LoopStats &st = loopStats;
  uint32_t windowUs = micros() - st.windowStartUs;
  uint32_t loops = st.loops ? st.loops : 1;
  String json = "{\"fps\":" + String(st.periodSumUs ? st.loops * 1e6f / st.periodSumUs : 0.0f, 1) +
                ",\"pushFps\":" + String(windowUs ? st.pushes * 1e6f / windowUs : 0.0f, 1) +
                ",\"periodAvgUs\":" + String(st.periodSumUs / loops) +
                ",\"periodMaxUs\":" + String(st.periodMaxUs) +
                ",\"workAvgUs\":" + String(st.workSumUs / loops) +
                ",\"workMaxUs\":" + String(st.workMaxUs) +
                ",\"pushAvgUs\":" + String(st.pushes ? st.pushSumUs / st.pushes : 0) +
                ",\"pushMaxUs\":" + String(st.pushMaxUs) +
                ",\"loops\":" + String(st.loops) +
                ",\"windowMs\":" + String(windowUs / 1000) + "}";
  uint32_t lastStart = st.lastStartUs;
  st = LoopStats{0};
  st.windowStartUs = micros();
  st.lastStartUs = lastStart;
  return json;
}

#endif // LOOP_STATS_H
