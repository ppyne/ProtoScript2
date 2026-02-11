#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <sys/stat.h>
#include <limits.h>
#include <pthread.h>

#include "ps/ps_api.h"

#define DST_EARLIER 0
#define DST_LATER 1
#define DST_ERROR 2

static pthread_mutex_t tz_lock = PTHREAD_MUTEX_INITIALIZER;

typedef struct {
  char *prev;
  int had_prev;
} TZState;

static int64_t days_from_civil(int64_t y, int64_t m, int64_t d) {
  y -= (m <= 2);
  int64_t era = (y >= 0 ? y : y - 399) / 400;
  int64_t yoe = y - era * 400;
  int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}

static int is_leap_year(int64_t y) {
  if (y % 400 == 0) return 1;
  if (y % 100 == 0) return 0;
  return (y % 4) == 0;
}

static int days_in_month(int64_t y, int64_t m) {
  if (m == 2) return is_leap_year(y) ? 29 : 28;
  if (m == 4 || m == 6 || m == 9 || m == 11) return 30;
  return 31;
}

static int tz_string_valid(const char *tz, size_t len) {
  if (!tz || len == 0) return 0;
  for (size_t i = 0; i < len; i++) {
    unsigned char c = (unsigned char)tz[i];
    if (isspace(c)) return 0;
    if (!(isalnum(c) || c == '_' || c == '+' || c == '-' || c == '/')) return 0;
  }
  return 1;
}

static int tz_path_exists(const char *tz) {
  const char *env = getenv("TZDIR");
  const char *dirs[] = { env, "/usr/share/zoneinfo", "/usr/share/lib/zoneinfo", "/usr/lib/zoneinfo", NULL };
  char path[PATH_MAX];
  for (size_t i = 0; dirs[i]; i++) {
    const char *dir = dirs[i];
    if (!dir || !*dir) continue;
    size_t need = strlen(dir) + 1 + strlen(tz) + 1;
    if (need >= sizeof(path)) continue;
    snprintf(path, sizeof(path), "%s/%s", dir, tz);
    struct stat st;
    if (stat(path, &st) == 0) return 1;
  }
  return 0;
}

static int tz_push(const char *tz, TZState *st) {
  st->prev = NULL;
  st->had_prev = 0;
  const char *prev = getenv("TZ");
  if (prev) {
    st->prev = strdup(prev);
    st->had_prev = 1;
  }
  if (setenv("TZ", tz, 1) != 0) return 0;
  tzset();
  return 1;
}

static void tz_pop(TZState *st) {
  if (st->had_prev && st->prev) setenv("TZ", st->prev, 1);
  else unsetenv("TZ");
  tzset();
  free(st->prev);
  st->prev = NULL;
  st->had_prev = 0;
}

static PS_Status throw_invalid_tz(PS_Context *ctx, const char *msg) {
  return ps_throw_exception(ctx, "InvalidTimeZoneException", msg ? msg : "invalid time zone");
}

static PS_Status throw_invalid_date(PS_Context *ctx, const char *msg) {
  return ps_throw_exception(ctx, "InvalidDateException", msg ? msg : "invalid date");
}

static PS_Status throw_invalid_iso(PS_Context *ctx, const char *msg) {
  return ps_throw_exception(ctx, "InvalidISOFormatException", msg ? msg : "invalid ISO 8601 format");
}

static PS_Status throw_dst_ambiguous(PS_Context *ctx, const char *msg) {
  return ps_throw_exception(ctx, "DSTAmbiguousTimeException", msg ? msg : "ambiguous DST time");
}

static PS_Status throw_dst_nonexistent(PS_Context *ctx, const char *msg) {
  return ps_throw_exception(ctx, "DSTNonExistentTimeException", msg ? msg : "non-existent DST time");
}

static int validate_tz(PS_Context *ctx, PS_Value *tzv, const char **out) {
  if (!tzv || ps_typeof(tzv) != PS_T_STRING) {
    ps_throw(ctx, PS_ERR_TYPE, "expected string time zone");
    return 0;
  }
  const char *tz = ps_string_ptr(tzv);
  size_t len = ps_string_len(tzv);
  if (!tz_string_valid(tz, len)) {
    throw_invalid_tz(ctx, "invalid time zone");
    return 0;
  }
  if (!tz_path_exists(tz)) {
    throw_invalid_tz(ctx, "unknown time zone");
    return 0;
  }
  *out = tz;
  return 1;
}

static int get_int_field(PS_Context *ctx, PS_Value *obj, const char *name, int64_t *out) {
  PS_Value *v = ps_object_get_str(ctx, obj, name, strlen(name));
  if (!v || ps_typeof(v) != PS_T_INT) {
    ps_throw(ctx, PS_ERR_TYPE, "invalid CivilDateTime");
    return 0;
  }
  *out = ps_as_int(v);
  return 1;
}

static PS_Value *make_civil(PS_Context *ctx, int64_t year, int64_t month, int64_t day,
                            int64_t hour, int64_t minute, int64_t second, int64_t millisecond) {
  PS_Value *obj = ps_make_object(ctx);
  if (!obj) return NULL;
  PS_Value *v = NULL;
  v = ps_make_int(ctx, year); if (!v) return NULL; if (ps_object_set_str(ctx, obj, "year", 4, v) != PS_OK) { ps_value_release(v); return NULL; } ps_value_release(v);
  v = ps_make_int(ctx, month); if (!v) return NULL; if (ps_object_set_str(ctx, obj, "month", 5, v) != PS_OK) { ps_value_release(v); return NULL; } ps_value_release(v);
  v = ps_make_int(ctx, day); if (!v) return NULL; if (ps_object_set_str(ctx, obj, "day", 3, v) != PS_OK) { ps_value_release(v); return NULL; } ps_value_release(v);
  v = ps_make_int(ctx, hour); if (!v) return NULL; if (ps_object_set_str(ctx, obj, "hour", 4, v) != PS_OK) { ps_value_release(v); return NULL; } ps_value_release(v);
  v = ps_make_int(ctx, minute); if (!v) return NULL; if (ps_object_set_str(ctx, obj, "minute", 6, v) != PS_OK) { ps_value_release(v); return NULL; } ps_value_release(v);
  v = ps_make_int(ctx, second); if (!v) return NULL; if (ps_object_set_str(ctx, obj, "second", 6, v) != PS_OK) { ps_value_release(v); return NULL; } ps_value_release(v);
  v = ps_make_int(ctx, millisecond); if (!v) return NULL; if (ps_object_set_str(ctx, obj, "millisecond", 11, v) != PS_OK) { ps_value_release(v); return NULL; } ps_value_release(v);
  return obj;
}

static int split_epoch_ms(int64_t epoch_ms, time_t *out_sec, int64_t *out_ms) {
  int64_t sec = epoch_ms / 1000;
  int64_t ms = epoch_ms % 1000;
  if (ms < 0) { ms += 1000; sec -= 1; }
  *out_sec = (time_t)sec;
  *out_ms = ms;
  return 1;
}

static int64_t epoch_seconds_from_utc(int64_t year, int64_t month, int64_t day,
                                      int64_t hour, int64_t minute, int64_t second) {
  int64_t days = days_from_civil(year, month, day);
  return days * 86400 + hour * 3600 + minute * 60 + second;
}

static int64_t offset_seconds_for_epoch(PS_Context *ctx, int64_t epoch_ms, const char *tz) {
  time_t sec;
  int64_t ms;
  split_epoch_ms(epoch_ms, &sec, &ms);
  (void)ms;
  struct tm lt;
  TZState st;
  pthread_mutex_lock(&tz_lock);
  if (!tz_push(tz, &st)) { pthread_mutex_unlock(&tz_lock); throw_invalid_tz(ctx, "invalid time zone"); return 0; }
  if (!localtime_r(&sec, &lt)) {
    tz_pop(&st);
    pthread_mutex_unlock(&tz_lock);
    throw_invalid_date(ctx, "invalid epoch");
    return 0;
  }
  tz_pop(&st);
  pthread_mutex_unlock(&tz_lock);
  int64_t local_sec = epoch_seconds_from_utc((int64_t)lt.tm_year + 1900, (int64_t)lt.tm_mon + 1, (int64_t)lt.tm_mday,
                                             (int64_t)lt.tm_hour, (int64_t)lt.tm_min, (int64_t)lt.tm_sec);
  int64_t epoch_sec = (int64_t)sec;
  return local_sec - epoch_sec;
}

static int day_of_week_from_ymd(int64_t y, int64_t m, int64_t d) {
  int64_t days = days_from_civil(y, m, d);
  int64_t w = (days + 4) % 7; // 1970-01-01 is Thursday
  if (w < 0) w += 7;
  return w == 0 ? 7 : (int)w;
}

static int day_of_year_from_ymd(int64_t y, int64_t m, int64_t d) {
  int total = 0;
  for (int i = 1; i < (int)m; i++) total += days_in_month(y, i);
  return total + (int)d;
}

static int weeks_in_iso_year(int64_t y) {
  int jan1 = day_of_week_from_ymd(y, 1, 1);
  if (jan1 == 4) return 53;
  if (jan1 == 3 && is_leap_year(y)) return 53;
  return 52;
}

static void iso_week_info(int64_t y, int64_t m, int64_t d, int *out_week, int *out_year) {
  int dow = day_of_week_from_ymd(y, m, d);
  int doy = day_of_year_from_ymd(y, m, d);
  int week = (doy - dow + 10) / 7;
  int week_year = (int)y;
  if (week < 1) {
    week_year = (int)y - 1;
    week = weeks_in_iso_year(week_year);
  } else {
    int weeks = weeks_in_iso_year(y);
    if (week > weeks) { week_year = (int)y + 1; week = 1; }
  }
  *out_week = week;
  *out_year = week_year;
}

static int read_civil(PS_Context *ctx, PS_Value *dt,
                      int64_t *year, int64_t *month, int64_t *day,
                      int64_t *hour, int64_t *minute, int64_t *second, int64_t *ms) {
  if (!dt || ps_typeof(dt) != PS_T_OBJECT) {
    ps_throw(ctx, PS_ERR_TYPE, "invalid CivilDateTime");
    return 0;
  }
  if (!get_int_field(ctx, dt, "year", year)) return 0;
  if (!get_int_field(ctx, dt, "month", month)) return 0;
  if (!get_int_field(ctx, dt, "day", day)) return 0;
  if (!get_int_field(ctx, dt, "hour", hour)) return 0;
  if (!get_int_field(ctx, dt, "minute", minute)) return 0;
  if (!get_int_field(ctx, dt, "second", second)) return 0;
  if (!get_int_field(ctx, dt, "millisecond", ms)) return 0;
  return 1;
}

static int validate_civil(PS_Context *ctx, int64_t year, int64_t month, int64_t day,
                          int64_t hour, int64_t minute, int64_t second, int64_t ms, int iso_error) {
  if (month < 1 || month > 12) return iso_error ? !throw_invalid_iso(ctx, "invalid month") : !throw_invalid_date(ctx, "invalid month");
  if (hour < 0 || hour > 23) return iso_error ? !throw_invalid_iso(ctx, "invalid hour") : !throw_invalid_date(ctx, "invalid hour");
  if (minute < 0 || minute > 59) return iso_error ? !throw_invalid_iso(ctx, "invalid minute") : !throw_invalid_date(ctx, "invalid minute");
  if (second < 0 || second > 59) return iso_error ? !throw_invalid_iso(ctx, "invalid second") : !throw_invalid_date(ctx, "invalid second");
  if (ms < 0 || ms > 999) return iso_error ? !throw_invalid_iso(ctx, "invalid millisecond") : !throw_invalid_date(ctx, "invalid millisecond");
  int dim = days_in_month(year, month);
  if (day < 1 || day > dim) return iso_error ? !throw_invalid_iso(ctx, "invalid day") : !throw_invalid_date(ctx, "invalid day");
  return 1;
}

static PS_Status mod_from_epoch_utc(PS_Context *ctx, int argc, PS_Value **argv, PS_Value **out) {
  (void)argc;
  if (!argv[0] || ps_typeof(argv[0]) != PS_T_INT) { ps_throw(ctx, PS_ERR_TYPE, "expected int epoch"); return PS_ERR; }
  int64_t epoch_ms = ps_as_int(argv[0]);
  time_t sec; int64_t ms;
  split_epoch_ms(epoch_ms, &sec, &ms);
  struct tm tm;
  if (!gmtime_r(&sec, &tm)) return throw_invalid_date(ctx, "invalid epoch");
  PS_Value *obj = make_civil(ctx,
                             (int64_t)tm.tm_year + 1900,
                             (int64_t)tm.tm_mon + 1,
                             (int64_t)tm.tm_mday,
                             (int64_t)tm.tm_hour,
                             (int64_t)tm.tm_min,
                             (int64_t)tm.tm_sec,
                             ms);
  if (!obj) return PS_ERR;
  *out = obj;
  return PS_OK;
}

static PS_Status mod_to_epoch_utc(PS_Context *ctx, int argc, PS_Value **argv, PS_Value **out) {
  (void)argc;
  int64_t year, month, day, hour, minute, second, ms;
  if (!read_civil(ctx, argv[0], &year, &month, &day, &hour, &minute, &second, &ms)) return PS_ERR;
  if (!validate_civil(ctx, year, month, day, hour, minute, second, ms, 0)) return PS_ERR;
  __int128 sec = (__int128)epoch_seconds_from_utc(year, month, day, hour, minute, second);
  __int128 total = sec * 1000 + ms;
  if (total < INT64_MIN || total > INT64_MAX) return throw_invalid_date(ctx, "date out of range");
  PS_Value *v = ps_make_int(ctx, (int64_t)total);
  if (!v) return PS_ERR;
  *out = v;
  return PS_OK;
}

static PS_Status mod_from_epoch(PS_Context *ctx, int argc, PS_Value **argv, PS_Value **out) {
  (void)argc;
  if (!argv[0] || ps_typeof(argv[0]) != PS_T_INT) { ps_throw(ctx, PS_ERR_TYPE, "expected int epoch"); return PS_ERR; }
  const char *tz = NULL;
  if (!validate_tz(ctx, argv[1], &tz)) return PS_ERR;
  int64_t epoch_ms = ps_as_int(argv[0]);
  time_t sec; int64_t ms;
  split_epoch_ms(epoch_ms, &sec, &ms);
  struct tm tm;
  TZState st;
  pthread_mutex_lock(&tz_lock);
  if (!tz_push(tz, &st)) { pthread_mutex_unlock(&tz_lock); return throw_invalid_tz(ctx, "invalid time zone"); }
  if (!localtime_r(&sec, &tm)) {
    tz_pop(&st);
    pthread_mutex_unlock(&tz_lock);
    return throw_invalid_date(ctx, "invalid epoch");
  }
  tz_pop(&st);
  pthread_mutex_unlock(&tz_lock);
  PS_Value *obj = make_civil(ctx,
                             (int64_t)tm.tm_year + 1900,
                             (int64_t)tm.tm_mon + 1,
                             (int64_t)tm.tm_mday,
                             (int64_t)tm.tm_hour,
                             (int64_t)tm.tm_min,
                             (int64_t)tm.tm_sec,
                             ms);
  if (!obj) return PS_ERR;
  *out = obj;
  return PS_OK;
}

static int match_local(struct tm *tm, int64_t year, int64_t month, int64_t day,
                       int64_t hour, int64_t minute, int64_t second) {
  if ((int64_t)tm->tm_year + 1900 != year) return 0;
  if ((int64_t)tm->tm_mon + 1 != month) return 0;
  if ((int64_t)tm->tm_mday != day) return 0;
  if ((int64_t)tm->tm_hour != hour) return 0;
  if ((int64_t)tm->tm_min != minute) return 0;
  if ((int64_t)tm->tm_sec != second) return 0;
  return 1;
}

static int candidate_epoch(time_t *out, int64_t year, int64_t month, int64_t day,
                           int64_t hour, int64_t minute, int64_t second, int isdst) {
  struct tm tm;
  memset(&tm, 0, sizeof(tm));
  tm.tm_year = (int)(year - 1900);
  tm.tm_mon = (int)(month - 1);
  tm.tm_mday = (int)day;
  tm.tm_hour = (int)hour;
  tm.tm_min = (int)minute;
  tm.tm_sec = (int)second;
  tm.tm_isdst = isdst;
  time_t t = mktime(&tm);
  if (t == (time_t)-1) {
    struct tm chk;
    if (!localtime_r(&t, &chk)) return 0;
  }
  struct tm chk;
  if (!localtime_r(&t, &chk)) return 0;
  if (!match_local(&chk, year, month, day, hour, minute, second)) return 0;
  *out = t;
  return 1;
}

static PS_Status mod_to_epoch(PS_Context *ctx, int argc, PS_Value **argv, PS_Value **out) {
  (void)argc;
  int64_t year, month, day, hour, minute, second, ms;
  if (!read_civil(ctx, argv[0], &year, &month, &day, &hour, &minute, &second, &ms)) return PS_ERR;
  if (!validate_civil(ctx, year, month, day, hour, minute, second, ms, 0)) return PS_ERR;
  if (!argv[2] || ps_typeof(argv[2]) != PS_T_INT) { ps_throw(ctx, PS_ERR_TYPE, "invalid DST strategy"); return PS_ERR; }
  int64_t strat = ps_as_int(argv[2]);
  if (!(strat == DST_EARLIER || strat == DST_LATER || strat == DST_ERROR)) {
    ps_throw(ctx, PS_ERR_TYPE, "invalid DST strategy");
    return PS_ERR;
  }
  const char *tz = NULL;
  if (!validate_tz(ctx, argv[1], &tz)) return PS_ERR;
  time_t t1 = 0, t2 = 0;
  int ok1 = 0, ok2 = 0;
  TZState st;
  pthread_mutex_lock(&tz_lock);
  if (!tz_push(tz, &st)) { pthread_mutex_unlock(&tz_lock); return throw_invalid_tz(ctx, "invalid time zone"); }
  ok1 = candidate_epoch(&t1, year, month, day, hour, minute, second, 0);
  ok2 = candidate_epoch(&t2, year, month, day, hour, minute, second, 1);
  tz_pop(&st);
  pthread_mutex_unlock(&tz_lock);
  if (!ok1 && !ok2) return throw_dst_nonexistent(ctx, "non-existent DST time");
  if (ok1 && ok2 && t1 == t2) ok2 = 0;
  if (ok1 && ok2) {
    if (strat == DST_ERROR) return throw_dst_ambiguous(ctx, "ambiguous DST time");
    time_t chosen = (strat == DST_EARLIER) ? (t1 < t2 ? t1 : t2) : (t1 > t2 ? t1 : t2);
    int64_t epoch_ms = (int64_t)chosen * 1000 + ms;
    PS_Value *v = ps_make_int(ctx, epoch_ms);
    if (!v) return PS_ERR;
    *out = v;
    return PS_OK;
  }
  time_t chosen = ok1 ? t1 : t2;
  int64_t epoch_ms = (int64_t)chosen * 1000 + ms;
  PS_Value *v = ps_make_int(ctx, epoch_ms);
  if (!v) return PS_ERR;
  *out = v;
  return PS_OK;
}

static PS_Status mod_is_dst(PS_Context *ctx, int argc, PS_Value **argv, PS_Value **out) {
  (void)argc;
  if (!argv[0] || ps_typeof(argv[0]) != PS_T_INT) { ps_throw(ctx, PS_ERR_TYPE, "expected int epoch"); return PS_ERR; }
  const char *tz = NULL;
  if (!validate_tz(ctx, argv[1], &tz)) return PS_ERR;
  int64_t epoch_ms = ps_as_int(argv[0]);
  int64_t off = offset_seconds_for_epoch(ctx, epoch_ms, tz);
  int64_t jan = epoch_seconds_from_utc(2024, 1, 15, 0, 0, 0) * 1000;
  int64_t jul = epoch_seconds_from_utc(2024, 7, 15, 0, 0, 0) * 1000;
  int64_t off_jan = offset_seconds_for_epoch(ctx, jan, tz);
  int64_t off_jul = offset_seconds_for_epoch(ctx, jul, tz);
  int64_t std = off_jan < off_jul ? off_jan : off_jul;
  PS_Value *v = ps_make_bool(ctx, off != std);
  if (!v) return PS_ERR;
  *out = v;
  return PS_OK;
}

static PS_Status mod_offset_seconds(PS_Context *ctx, int argc, PS_Value **argv, PS_Value **out) {
  (void)argc;
  if (!argv[0] || ps_typeof(argv[0]) != PS_T_INT) { ps_throw(ctx, PS_ERR_TYPE, "expected int epoch"); return PS_ERR; }
  const char *tz = NULL;
  if (!validate_tz(ctx, argv[1], &tz)) return PS_ERR;
  int64_t epoch_ms = ps_as_int(argv[0]);
  int64_t off = offset_seconds_for_epoch(ctx, epoch_ms, tz);
  PS_Value *v = ps_make_int(ctx, off);
  if (!v) return PS_ERR;
  *out = v;
  return PS_OK;
}

static PS_Status mod_standard_offset_seconds(PS_Context *ctx, int argc, PS_Value **argv, PS_Value **out) {
  (void)argc;
  const char *tz = NULL;
  if (!validate_tz(ctx, argv[0], &tz)) return PS_ERR;
  int64_t jan = epoch_seconds_from_utc(2024, 1, 15, 0, 0, 0) * 1000;
  int64_t jul = epoch_seconds_from_utc(2024, 7, 15, 0, 0, 0) * 1000;
  int64_t off_jan = offset_seconds_for_epoch(ctx, jan, tz);
  int64_t off_jul = offset_seconds_for_epoch(ctx, jul, tz);
  int64_t std = off_jan < off_jul ? off_jan : off_jul;
  PS_Value *v = ps_make_int(ctx, std);
  if (!v) return PS_ERR;
  *out = v;
  return PS_OK;
}

static PS_Status mod_day_of_week(PS_Context *ctx, int argc, PS_Value **argv, PS_Value **out) {
  (void)argc;
  if (!argv[0] || ps_typeof(argv[0]) != PS_T_INT) { ps_throw(ctx, PS_ERR_TYPE, "expected int epoch"); return PS_ERR; }
  const char *tz = NULL;
  if (!validate_tz(ctx, argv[1], &tz)) return PS_ERR;
  int64_t epoch_ms = ps_as_int(argv[0]);
  time_t sec; int64_t ms;
  split_epoch_ms(epoch_ms, &sec, &ms);
  (void)ms;
  struct tm tm;
  TZState st;
  pthread_mutex_lock(&tz_lock);
  if (!tz_push(tz, &st)) { pthread_mutex_unlock(&tz_lock); return throw_invalid_tz(ctx, "invalid time zone"); }
  if (!localtime_r(&sec, &tm)) { tz_pop(&st); pthread_mutex_unlock(&tz_lock); return throw_invalid_date(ctx, "invalid epoch"); }
  tz_pop(&st);
  pthread_mutex_unlock(&tz_lock);
  int dow = day_of_week_from_ymd((int64_t)tm.tm_year + 1900, (int64_t)tm.tm_mon + 1, (int64_t)tm.tm_mday);
  PS_Value *v = ps_make_int(ctx, (int64_t)dow);
  if (!v) return PS_ERR;
  *out = v;
  return PS_OK;
}

static PS_Status mod_day_of_year(PS_Context *ctx, int argc, PS_Value **argv, PS_Value **out) {
  (void)argc;
  if (!argv[0] || ps_typeof(argv[0]) != PS_T_INT) { ps_throw(ctx, PS_ERR_TYPE, "expected int epoch"); return PS_ERR; }
  const char *tz = NULL;
  if (!validate_tz(ctx, argv[1], &tz)) return PS_ERR;
  int64_t epoch_ms = ps_as_int(argv[0]);
  time_t sec; int64_t ms;
  split_epoch_ms(epoch_ms, &sec, &ms);
  (void)ms;
  struct tm tm;
  TZState st;
  pthread_mutex_lock(&tz_lock);
  if (!tz_push(tz, &st)) { pthread_mutex_unlock(&tz_lock); return throw_invalid_tz(ctx, "invalid time zone"); }
  if (!localtime_r(&sec, &tm)) { tz_pop(&st); pthread_mutex_unlock(&tz_lock); return throw_invalid_date(ctx, "invalid epoch"); }
  tz_pop(&st);
  pthread_mutex_unlock(&tz_lock);
  int doy = day_of_year_from_ymd((int64_t)tm.tm_year + 1900, (int64_t)tm.tm_mon + 1, (int64_t)tm.tm_mday);
  PS_Value *v = ps_make_int(ctx, (int64_t)doy);
  if (!v) return PS_ERR;
  *out = v;
  return PS_OK;
}

static PS_Status mod_week_of_year_iso(PS_Context *ctx, int argc, PS_Value **argv, PS_Value **out) {
  (void)argc;
  if (!argv[0] || ps_typeof(argv[0]) != PS_T_INT) { ps_throw(ctx, PS_ERR_TYPE, "expected int epoch"); return PS_ERR; }
  const char *tz = NULL;
  if (!validate_tz(ctx, argv[1], &tz)) return PS_ERR;
  int64_t epoch_ms = ps_as_int(argv[0]);
  time_t sec; int64_t ms;
  split_epoch_ms(epoch_ms, &sec, &ms);
  (void)ms;
  struct tm tm;
  TZState st;
  pthread_mutex_lock(&tz_lock);
  if (!tz_push(tz, &st)) { pthread_mutex_unlock(&tz_lock); return throw_invalid_tz(ctx, "invalid time zone"); }
  if (!localtime_r(&sec, &tm)) { tz_pop(&st); pthread_mutex_unlock(&tz_lock); return throw_invalid_date(ctx, "invalid epoch"); }
  tz_pop(&st);
  pthread_mutex_unlock(&tz_lock);
  int week = 0, wy = 0;
  iso_week_info((int64_t)tm.tm_year + 1900, (int64_t)tm.tm_mon + 1, (int64_t)tm.tm_mday, &week, &wy);
  PS_Value *v = ps_make_int(ctx, (int64_t)week);
  if (!v) return PS_ERR;
  *out = v;
  return PS_OK;
}

static PS_Status mod_week_year_iso(PS_Context *ctx, int argc, PS_Value **argv, PS_Value **out) {
  (void)argc;
  if (!argv[0] || ps_typeof(argv[0]) != PS_T_INT) { ps_throw(ctx, PS_ERR_TYPE, "expected int epoch"); return PS_ERR; }
  const char *tz = NULL;
  if (!validate_tz(ctx, argv[1], &tz)) return PS_ERR;
  int64_t epoch_ms = ps_as_int(argv[0]);
  time_t sec; int64_t ms;
  split_epoch_ms(epoch_ms, &sec, &ms);
  (void)ms;
  struct tm tm;
  TZState st;
  pthread_mutex_lock(&tz_lock);
  if (!tz_push(tz, &st)) { pthread_mutex_unlock(&tz_lock); return throw_invalid_tz(ctx, "invalid time zone"); }
  if (!localtime_r(&sec, &tm)) { tz_pop(&st); pthread_mutex_unlock(&tz_lock); return throw_invalid_date(ctx, "invalid epoch"); }
  tz_pop(&st);
  pthread_mutex_unlock(&tz_lock);
  int week = 0, wy = 0;
  iso_week_info((int64_t)tm.tm_year + 1900, (int64_t)tm.tm_mon + 1, (int64_t)tm.tm_mday, &week, &wy);
  PS_Value *v = ps_make_int(ctx, (int64_t)wy);
  if (!v) return PS_ERR;
  *out = v;
  return PS_OK;
}

static PS_Status mod_is_leap_year(PS_Context *ctx, int argc, PS_Value **argv, PS_Value **out) {
  (void)argc;
  if (!argv[0] || ps_typeof(argv[0]) != PS_T_INT) { ps_throw(ctx, PS_ERR_TYPE, "expected int year"); return PS_ERR; }
  int64_t y = ps_as_int(argv[0]);
  PS_Value *v = ps_make_bool(ctx, is_leap_year(y));
  if (!v) return PS_ERR;
  *out = v;
  return PS_OK;
}

static PS_Status mod_days_in_month(PS_Context *ctx, int argc, PS_Value **argv, PS_Value **out) {
  (void)argc;
  if (!argv[0] || ps_typeof(argv[0]) != PS_T_INT || !argv[1] || ps_typeof(argv[1]) != PS_T_INT) {
    ps_throw(ctx, PS_ERR_TYPE, "expected int arguments");
    return PS_ERR;
  }
  int64_t y = ps_as_int(argv[0]);
  int64_t m = ps_as_int(argv[1]);
  if (m < 1 || m > 12) return throw_invalid_date(ctx, "invalid month");
  int64_t dim = days_in_month(y, m);
  PS_Value *v = ps_make_int(ctx, dim);
  if (!v) return PS_ERR;
  *out = v;
  return PS_OK;
}

static int read2(const char *s, size_t i, size_t len, int *out) {
  if (i + 2 > len) return 0;
  char a = s[i], b = s[i + 1];
  if (!isdigit((unsigned char)a) || !isdigit((unsigned char)b)) return 0;
  *out = (a - '0') * 10 + (b - '0');
  return 1;
}

static int read4(const char *s, size_t i, size_t len, int *out) {
  if (i + 4 > len) return 0;
  char a = s[i], b = s[i + 1], c = s[i + 2], d = s[i + 3];
  if (!isdigit((unsigned char)a) || !isdigit((unsigned char)b) || !isdigit((unsigned char)c) || !isdigit((unsigned char)d)) return 0;
  *out = (a - '0') * 1000 + (b - '0') * 100 + (c - '0') * 10 + (d - '0');
  return 1;
}

static PS_Status mod_parse_iso(PS_Context *ctx, int argc, PS_Value **argv, PS_Value **out) {
  (void)argc;
  if (!argv[0] || ps_typeof(argv[0]) != PS_T_STRING) return throw_invalid_iso(ctx, "invalid ISO 8601 format");
  const char *s = ps_string_ptr(argv[0]);
  size_t len = ps_string_len(argv[0]);
  if (len < 10) return throw_invalid_iso(ctx, "invalid ISO 8601 format");
  int year = 0, month = 0, day = 0;
  if (!read4(s, 0, len, &year) || s[4] != '-' || s[7] != '-') return throw_invalid_iso(ctx, "invalid ISO 8601 format");
  if (!read2(s, 5, len, &month) || !read2(s, 8, len, &day)) return throw_invalid_iso(ctx, "invalid ISO 8601 format");
  int hour = 0, minute = 0, second = 0, ms = 0;
  size_t i = 10;
  if (i == len) {
  } else {
    if (s[i] != 'T') return throw_invalid_iso(ctx, "invalid ISO 8601 format");
    if (i + 8 >= len) return throw_invalid_iso(ctx, "invalid ISO 8601 format");
    if (!read2(s, i + 1, len, &hour) || !read2(s, i + 4, len, &minute) || !read2(s, i + 7, len, &second)) return throw_invalid_iso(ctx, "invalid ISO 8601 format");
    if (s[i + 3] != ':' || s[i + 6] != ':') return throw_invalid_iso(ctx, "invalid ISO 8601 format");
    i += 9;
    if (i < len && s[i] == '.') {
      if (i + 3 >= len) return throw_invalid_iso(ctx, "invalid ISO 8601 format");
      char a = s[i + 1], b = s[i + 2], c = s[i + 3];
      if (!isdigit((unsigned char)a) || !isdigit((unsigned char)b) || !isdigit((unsigned char)c)) return throw_invalid_iso(ctx, "invalid ISO 8601 format");
      ms = (a - '0') * 100 + (b - '0') * 10 + (c - '0');
      i += 4;
    }
  }
  int offset_min = 0;
  if (i < len) {
    char sign = s[i];
    if (sign == 'Z') {
      if (i + 1 != len) return throw_invalid_iso(ctx, "invalid ISO 8601 format");
    } else if (sign == '+' || sign == '-') {
      if (i + 6 != len) return throw_invalid_iso(ctx, "invalid ISO 8601 format");
      int oh = 0, om = 0;
      if (!read2(s, i + 1, len, &oh) || !read2(s, i + 4, len, &om) || s[i + 3] != ':') return throw_invalid_iso(ctx, "invalid ISO 8601 format");
      if (oh > 23 || om > 59) return throw_invalid_iso(ctx, "invalid ISO 8601 format");
      offset_min = oh * 60 + om;
      if (sign == '-') offset_min = -offset_min;
    } else {
      return throw_invalid_iso(ctx, "invalid ISO 8601 format");
    }
  }
  if (!validate_civil(ctx, year, month, day, hour, minute, second, ms, 1)) return PS_ERR;
  __int128 sec = (__int128)epoch_seconds_from_utc(year, month, day, hour, minute, second);
  __int128 total = sec * 1000 + ms;
  total -= (__int128)offset_min * 60 * 1000;
  if (total < INT64_MIN || total > INT64_MAX) return throw_invalid_iso(ctx, "invalid ISO 8601 format");
  PS_Value *v = ps_make_int(ctx, (int64_t)total);
  if (!v) return PS_ERR;
  *out = v;
  return PS_OK;
}

static PS_Status mod_format_iso(PS_Context *ctx, int argc, PS_Value **argv, PS_Value **out) {
  (void)argc;
  if (!argv[0] || ps_typeof(argv[0]) != PS_T_INT) { ps_throw(ctx, PS_ERR_TYPE, "expected int epoch"); return PS_ERR; }
  int64_t epoch_ms = ps_as_int(argv[0]);
  time_t sec; int64_t ms;
  split_epoch_ms(epoch_ms, &sec, &ms);
  struct tm tm;
  if (!gmtime_r(&sec, &tm)) return throw_invalid_date(ctx, "invalid epoch");
  int year = tm.tm_year + 1900;
  int month = tm.tm_mon + 1;
  int day = tm.tm_mday;
  int hour = tm.tm_hour;
  int minute = tm.tm_min;
  int second = tm.tm_sec;
  char ybuf[32];
  int yabs = year < 0 ? -year : year;
  if (yabs < 10) snprintf(ybuf, sizeof(ybuf), "%s000%d", year < 0 ? "-" : "", yabs);
  else if (yabs < 100) snprintf(ybuf, sizeof(ybuf), "%s00%d", year < 0 ? "-" : "", yabs);
  else if (yabs < 1000) snprintf(ybuf, sizeof(ybuf), "%s0%d", year < 0 ? "-" : "", yabs);
  else snprintf(ybuf, sizeof(ybuf), "%s%d", year < 0 ? "-" : "", yabs);
  char buf[64];
  snprintf(buf, sizeof(buf), "%s-%02d-%02dT%02d:%02d:%02d.%03lldZ",
           ybuf, month, day, hour, minute, second, (long long)ms);
  PS_Value *v = ps_make_string_utf8(ctx, buf, strlen(buf));
  if (!v) return PS_ERR;
  *out = v;
  return PS_OK;
}

PS_Status ps_module_init(PS_Context *ctx, PS_Module *out) {
  (void)ctx;
  static PS_NativeFnDesc fns[] = {
    {.name = "fromEpochUTC", .fn = mod_from_epoch_utc, .arity = 1, .ret_type = PS_T_OBJECT, .param_types = NULL, .flags = 0},
    {.name = "toEpochUTC", .fn = mod_to_epoch_utc, .arity = 1, .ret_type = PS_T_INT, .param_types = NULL, .flags = 0},
    {.name = "fromEpoch", .fn = mod_from_epoch, .arity = 2, .ret_type = PS_T_OBJECT, .param_types = NULL, .flags = 0},
    {.name = "toEpoch", .fn = mod_to_epoch, .arity = 3, .ret_type = PS_T_INT, .param_types = NULL, .flags = 0},
    {.name = "isDST", .fn = mod_is_dst, .arity = 2, .ret_type = PS_T_BOOL, .param_types = NULL, .flags = 0},
    {.name = "offsetSeconds", .fn = mod_offset_seconds, .arity = 2, .ret_type = PS_T_INT, .param_types = NULL, .flags = 0},
    {.name = "standardOffsetSeconds", .fn = mod_standard_offset_seconds, .arity = 1, .ret_type = PS_T_INT, .param_types = NULL, .flags = 0},
    {.name = "dayOfWeek", .fn = mod_day_of_week, .arity = 2, .ret_type = PS_T_INT, .param_types = NULL, .flags = 0},
    {.name = "dayOfYear", .fn = mod_day_of_year, .arity = 2, .ret_type = PS_T_INT, .param_types = NULL, .flags = 0},
    {.name = "weekOfYearISO", .fn = mod_week_of_year_iso, .arity = 2, .ret_type = PS_T_INT, .param_types = NULL, .flags = 0},
    {.name = "weekYearISO", .fn = mod_week_year_iso, .arity = 2, .ret_type = PS_T_INT, .param_types = NULL, .flags = 0},
    {.name = "isLeapYear", .fn = mod_is_leap_year, .arity = 1, .ret_type = PS_T_BOOL, .param_types = NULL, .flags = 0},
    {.name = "daysInMonth", .fn = mod_days_in_month, .arity = 2, .ret_type = PS_T_INT, .param_types = NULL, .flags = 0},
    {.name = "parseISO8601", .fn = mod_parse_iso, .arity = 1, .ret_type = PS_T_INT, .param_types = NULL, .flags = 0},
    {.name = "formatISO8601", .fn = mod_format_iso, .arity = 1, .ret_type = PS_T_STRING, .param_types = NULL, .flags = 0},
  };
  out->module_name = "TimeCivil";
  out->api_version = PS_API_VERSION;
  out->fn_count = sizeof(fns) / sizeof(fns[0]);
  out->fns = fns;
  return PS_OK;
}
