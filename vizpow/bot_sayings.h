#ifndef BOT_SAYINGS_H
#define BOT_SAYINGS_H

#include <Arduino.h>

// ============================================================================
// Bot Sayings — Categorized Phrase Pools
// ============================================================================
// Short text phrases displayed in speech bubbles. Stored in PROGMEM.
// Categorized by context: greetings, idle chatter, reactions, time-based.
// Selection logic picks from appropriate category based on trigger type.
// ============================================================================

// Saying categories
enum SayingCategory : uint8_t {
  SAY_GREETING = 0,
  SAY_IDLE,
  SAY_REACT_SHAKE,
  SAY_REACT_TAP,
  SAY_TIME_MORNING,
  SAY_TIME_AFTERNOON,
  SAY_TIME_EVENING,
  SAY_TIME_NIGHT,
  SAY_STATUS,
  SAY_WAKE,
  SAY_SLEEP,
  SAY_CATEGORY_COUNT
};

// ============================================================================
// Phrase pools (PROGMEM)
// ============================================================================

// Greetings
const char say_greet_0[] PROGMEM = "Hey!";
const char say_greet_1[] PROGMEM = "Yo!";
const char say_greet_2[] PROGMEM = "Sup?";
const char say_greet_3[] PROGMEM = "Hiii~";
const char say_greet_4[] PROGMEM = "Hello!";
const char say_greet_5[] PROGMEM = "Hi there!";
const char say_greet_6[] PROGMEM = ":)";
const char say_greet_7[] PROGMEM = "Howdy!";

const char* const sayGreetings[] PROGMEM = {
  say_greet_0, say_greet_1, say_greet_2, say_greet_3,
  say_greet_4, say_greet_5, say_greet_6, say_greet_7
};
#define NUM_SAY_GREETINGS 8

// Idle chatter
const char say_idle_0[] PROGMEM = "I'm bored...";
const char say_idle_1[] PROGMEM = "Whatcha doing?";
const char say_idle_2[] PROGMEM = "*yawn*";
const char say_idle_3[] PROGMEM = "This is fine.";
const char say_idle_4[] PROGMEM = "...";
const char say_idle_5[] PROGMEM = "Hmm...";
const char say_idle_6[] PROGMEM = "La la la~";
const char say_idle_7[] PROGMEM = "Hello? Anyone?";
const char say_idle_8[] PROGMEM = "*whistles*";
const char say_idle_9[] PROGMEM = "Thinking...";
const char say_idle_10[] PROGMEM = "*taps foot*";
const char say_idle_11[] PROGMEM = "So quiet...";
const char say_idle_12[] PROGMEM = "Beep boop";

const char* const sayIdle[] PROGMEM = {
  say_idle_0, say_idle_1, say_idle_2, say_idle_3,
  say_idle_4, say_idle_5, say_idle_6, say_idle_7,
  say_idle_8, say_idle_9, say_idle_10, say_idle_11,
  say_idle_12
};
#define NUM_SAY_IDLE 13

// Shake reactions
const char say_shake_0[] PROGMEM = "Whoa!";
const char say_shake_1[] PROGMEM = "Easy!";
const char say_shake_2[] PROGMEM = "AHHH!";
const char say_shake_3[] PROGMEM = "I felt that.";
const char say_shake_4[] PROGMEM = "Dizzy...";
const char say_shake_5[] PROGMEM = "Stop it!";
const char say_shake_6[] PROGMEM = "Earthquake?!";
const char say_shake_7[] PROGMEM = "*wobble*";
const char say_shake_8[] PROGMEM = "Not again!";
const char say_shake_9[] PROGMEM = "My head...";

const char* const sayShake[] PROGMEM = {
  say_shake_0, say_shake_1, say_shake_2, say_shake_3,
  say_shake_4, say_shake_5, say_shake_6, say_shake_7,
  say_shake_8, say_shake_9
};
#define NUM_SAY_SHAKE 10

// Tap reactions
const char say_tap_0[] PROGMEM = "Ow!";
const char say_tap_1[] PROGMEM = "Hehe";
const char say_tap_2[] PROGMEM = "That tickles";
const char say_tap_3[] PROGMEM = "Poke.";
const char say_tap_4[] PROGMEM = "Hey!";
const char say_tap_5[] PROGMEM = "What?";
const char say_tap_6[] PROGMEM = "Boop!";
const char say_tap_7[] PROGMEM = "*squish*";
const char say_tap_8[] PROGMEM = "Again?";
const char say_tap_9[] PROGMEM = "I see you!";

const char* const sayTap[] PROGMEM = {
  say_tap_0, say_tap_1, say_tap_2, say_tap_3,
  say_tap_4, say_tap_5, say_tap_6, say_tap_7,
  say_tap_8, say_tap_9
};
#define NUM_SAY_TAP 10

// Time-based: Morning
const char say_morn_0[] PROGMEM = "Good morning!";
const char say_morn_1[] PROGMEM = "Rise & shine!";
const char say_morn_2[] PROGMEM = "Coffee time?";
const char say_morn_3[] PROGMEM = "New day!";

const char* const sayMorning[] PROGMEM = {
  say_morn_0, say_morn_1, say_morn_2, say_morn_3
};
#define NUM_SAY_MORNING 4

// Time-based: Afternoon
const char say_aftn_0[] PROGMEM = "Lunch time?";
const char say_aftn_1[] PROGMEM = "Half way!";
const char say_aftn_2[] PROGMEM = "Afternoon~";
const char say_aftn_3[] PROGMEM = "Keep going!";

const char* const sayAfternoon[] PROGMEM = {
  say_aftn_0, say_aftn_1, say_aftn_2, say_aftn_3
};
#define NUM_SAY_AFTERNOON 4

// Time-based: Evening
const char say_eve_0[] PROGMEM = "Getting late...";
const char say_eve_1[] PROGMEM = "Dinner time!";
const char say_eve_2[] PROGMEM = "Evening~";
const char say_eve_3[] PROGMEM = "Winding down";

const char* const sayEvening[] PROGMEM = {
  say_eve_0, say_eve_1, say_eve_2, say_eve_3
};
#define NUM_SAY_EVENING 4

// Time-based: Night
const char say_nite_0[] PROGMEM = "Zzz...";
const char say_nite_1[] PROGMEM = "Sleepy time";
const char say_nite_2[] PROGMEM = "Night owl?";
const char say_nite_3[] PROGMEM = "So late...";

const char* const sayNight[] PROGMEM = {
  say_nite_0, say_nite_1, say_nite_2, say_nite_3
};
#define NUM_SAY_NIGHT 4

// Status messages
const char say_stat_0[] PROGMEM = "WiFi OK";
const char say_stat_1[] PROGMEM = "Ready!";
const char say_stat_2[] PROGMEM = "Mode changed";
const char say_stat_3[] PROGMEM = "All good!";

const char* const sayStatus[] PROGMEM = {
  say_stat_0, say_stat_1, say_stat_2, say_stat_3
};
#define NUM_SAY_STATUS 4

// Wake-up sayings
const char say_wake_0[] PROGMEM = "I'm up!";
const char say_wake_1[] PROGMEM = "Huh? What?";
const char say_wake_2[] PROGMEM = "Morning...?";
const char say_wake_3[] PROGMEM = "*stretches*";
const char say_wake_4[] PROGMEM = "Oh! Hi!";

const char* const sayWake[] PROGMEM = {
  say_wake_0, say_wake_1, say_wake_2, say_wake_3, say_wake_4
};
#define NUM_SAY_WAKE 5

// Sleep sayings
const char say_sleep_0[] PROGMEM = "Sleepy...";
const char say_sleep_1[] PROGMEM = "*yawns*";
const char say_sleep_2[] PROGMEM = "Goodnight~";
const char say_sleep_3[] PROGMEM = "So tired...";
const char say_sleep_4[] PROGMEM = "Zzz...";

const char* const saySleep[] PROGMEM = {
  say_sleep_0, say_sleep_1, say_sleep_2, say_sleep_3, say_sleep_4
};
#define NUM_SAY_SLEEP 5

// ============================================================================
// Selection helpers
// ============================================================================

// Get a random saying from a category
// Returns pointer to PROGMEM string — caller must use strcpy_P to read
const char* getRandomSaying(SayingCategory category) {
  const char* const* pool;
  uint8_t count;

  switch (category) {
    case SAY_GREETING:      pool = sayGreetings;  count = NUM_SAY_GREETINGS;  break;
    case SAY_IDLE:          pool = sayIdle;        count = NUM_SAY_IDLE;       break;
    case SAY_REACT_SHAKE:   pool = sayShake;       count = NUM_SAY_SHAKE;      break;
    case SAY_REACT_TAP:     pool = sayTap;         count = NUM_SAY_TAP;        break;
    case SAY_TIME_MORNING:  pool = sayMorning;     count = NUM_SAY_MORNING;    break;
    case SAY_TIME_AFTERNOON:pool = sayAfternoon;   count = NUM_SAY_AFTERNOON;  break;
    case SAY_TIME_EVENING:  pool = sayEvening;     count = NUM_SAY_EVENING;    break;
    case SAY_TIME_NIGHT:    pool = sayNight;       count = NUM_SAY_NIGHT;      break;
    case SAY_STATUS:        pool = sayStatus;      count = NUM_SAY_STATUS;     break;
    case SAY_WAKE:          pool = sayWake;        count = NUM_SAY_WAKE;       break;
    case SAY_SLEEP:         pool = saySleep;       count = NUM_SAY_SLEEP;      break;
    default:                pool = sayIdle;        count = NUM_SAY_IDLE;       break;
  }

  uint8_t idx = random(0, count);
  return (const char*)pgm_read_ptr(&pool[idx]);
}

// Read a PROGMEM saying into a RAM buffer
// Returns length of string. Buffer must be at least 32 bytes.
uint8_t readSaying(const char* progmemPtr, char* buffer, uint8_t bufSize) {
  strncpy_P(buffer, progmemPtr, bufSize - 1);
  buffer[bufSize - 1] = '\0';
  return strlen(buffer);
}

// Convenience: get a random saying as a RAM string
uint8_t getRandomSayingText(SayingCategory category, char* buffer, uint8_t bufSize) {
  const char* ptr = getRandomSaying(category);
  return readSaying(ptr, buffer, bufSize);
}

#endif // BOT_SAYINGS_H
