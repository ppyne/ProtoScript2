# ProtoScript2 — Normative Specification

# Module Time and TimeCivil

Status: Normative Target: Node runtime, C runtime, CLI, emit-C backend

---

# 1. Design Principles

1. The temporal base representation SHALL be UTC epoch milliseconds.
2. All civil computations SHALL derive from epoch milliseconds.
3. No implicit timezone SHALL exist.
4. All timezone usage SHALL be explicit.
5. No locale-dependent behavior SHALL be permitted.
6. All ISO rules SHALL follow ISO 8601-1:2019 definitions.
7. Ambiguous civil times SHALL require explicit resolution.
8. Non-existent civil times SHALL raise a dedicated exception.

---

# 2. Base Temporal Representation

## 2.1 EpochMillis

Type:

```
type EpochMillis = int
```

Definition:

- Signed 64-bit integer.
- Number of milliseconds since 1970-01-01T00:00:00Z.
- UTC only.
- Leap seconds are ignored (POSIX time semantics).

---

# 3. Module Time

## 3.1 API

```
Time.nowEpochMillis() : int
Time.nowMonotonicNanos() : int
Time.sleepMillis(int ms) : void
```

## 3.2 Semantics

### nowEpochMillis

- Returns UTC epoch milliseconds.
- Non-deterministic.

### nowMonotonicNanos

- Returns monotonic nanoseconds.
- MUST be non-decreasing.
- Not related to wall clock.

### sleepMillis

- Suspends execution for at least ms milliseconds.
- May overshoot.

---

# 4. Module TimeCivil

## 4.1 Types

```
prototype CivilDateTime {
    int year
    int month        // 1-12
    int day          // 1-31
    int hour         // 0-23
    int minute       // 0-59
    int second       // 0-59
    int millisecond  // 0-999
}

type TimeZone = string
```

// MUST be a valid IANA timezone identifier (e.g. "Europe/Paris")

Validation Rules:

1. The identifier SHALL match the canonical IANA timezone database name.
2. Identifiers are case-sensitive and MUST match exact casing as defined by IANA.
3. No locale-based normalization SHALL occur.
4. No automatic alias resolution SHALL occur unless provided by the system IANA database.
5. If the underlying system timezone database does not contain the identifier, the implementation SHALL raise InvalidTimeZoneException.
6. Implementations SHALL NOT silently fallback to UTC or system default timezone.
7. Leading or trailing whitespace SHALL cause InvalidTimeZoneException.
8. Identifiers containing invalid characters SHALL cause InvalidTimeZoneException.
9. Historical or common aliases (e.g. "UTC", "GMT", "Etc/UTC") SHALL be accepted only if they are present in the underlying system IANA database.
10. The implementation SHALL NOT introduce its own alias mapping table.
11. If multiple aliases resolve to the same canonical zone, behavior SHALL rely solely on the system IANA resolution.
12. The identifier "UTC" SHALL be supported if and only if the system database provides it; otherwise InvalidTimeZoneException SHALL be raised.

---

# 5. DST Strategy Constants

```
const int DST_EARLIER = 0
const int DST_LATER   = 1
const int DST_ERROR   = 2
```

Valid strategy values SHALL be {0,1,2}. Any other value SHALL raise a runtime error.

---

# 6. Conversion Functions

## 6.1 UTC Conversions

```
TimeCivil.fromEpochUTC(int epochMillis) : CivilDateTime
TimeCivil.toEpochUTC(CivilDateTime dt) : int
```

These functions SHALL NOT depend on timezone databases.

---

## 6.2 Zoned Conversions

```
TimeCivil.fromEpoch(int epochMillis, TimeZone tz) : CivilDateTime
TimeCivil.toEpoch(CivilDateTime dt, TimeZone tz, int strategy) : int
```

### Rules

1. strategy parameter is mandatory.
2. If civil time is ambiguous (DST fall back):
   - DST\_EARLIER selects earlier offset.
   - DST\_LATER selects later offset.
   - DST\_ERROR raises DSTAmbiguousTimeException.
3. If civil time does not exist (DST spring forward):
   - ALWAYS raise DSTNonExistentTimeException.
   - strategy SHALL NOT override this rule.

---

# 7. Timezone Functions

```
TimeCivil.isDST(int epochMillis, TimeZone tz) : bool
TimeCivil.offsetSeconds(int epochMillis, TimeZone tz) : int
TimeCivil.standardOffsetSeconds(TimeZone tz) : int
```

## Rules

- offsetSeconds SHALL return total UTC offset including DST.
- standardOffsetSeconds SHALL return non-DST base offset.

### Normative Algorithm for standardOffsetSeconds

The implementation SHALL determine the standard (non-DST) offset using the following algorithm:

1. Let Y be 2024 (normative reference year).
2. Compute oJan = offsetSeconds(epochMillis of Y-01-15T00:00:00.000Z, tz).
3. Compute oJul = offsetSeconds(epochMillis of Y-07-15T00:00:00.000Z, tz).
4. If oJan == oJul, return oJan.
5. Otherwise, return the numerically smaller of oJan and oJul.

This algorithm SHALL be implemented identically in Node and C runtimes to guarantee bit-for-bit consistency.

If the timezone database does not provide sufficient data to compute the above values, InvalidTimeZoneException SHALL be raised.
- isDST SHALL return true if offsetSeconds != standardOffsetSeconds.

Timezone resolution SHALL rely on system IANA database (Option A).

---

# 8. ISO Calendar Helpers

## 8.1 Day of Week

```
TimeCivil.dayOfWeek(int epochMillis, TimeZone tz) : int
```

Mapping:

```
1 = Monday
2 = Tuesday
3 = Wednesday
4 = Thursday
5 = Friday
6 = Saturday
7 = Sunday
```

---

## 8.2 Day of Year

```
TimeCivil.dayOfYear(int epochMillis, TimeZone tz) : int
```

Range: 1–365 or 1–366.

---

## 8.3 ISO Week

```
TimeCivil.weekOfYearISO(int epochMillis, TimeZone tz) : int
TimeCivil.weekYearISO(int epochMillis, TimeZone tz) : int
```

Rules (ISO 8601):

1. Week starts Monday.
2. Week 01 is the week containing January 4.
3. Equivalent rule: Week 01 is the first week containing a Thursday.
4. An ISO year may contain 52 or 53 weeks.
5. Dates at beginning/end of Gregorian year may belong to adjacent ISO year.

---

# 9. Pure Calendar Helpers

```
TimeCivil.isLeapYear(int year) : bool
TimeCivil.daysInMonth(int year, int month) : int
```

## Leap Year Rule

A year is leap if:

- divisible by 4 AND not divisible by 100
- OR divisible by 400

## Month Validation

- month must be 1–12.
- invalid month SHALL raise runtime error.

---

# 10. ISO 8601 Parsing and Formatting

```
TimeCivil.parseISO8601(string s) : int
TimeCivil.formatISO8601(int epochMillis) : string
```

## Supported Formats

- YYYY-MM-DD
- YYYY-MM-DDTHH\:MM\:SS
- YYYY-MM-DDTHH\:MM\:SS.sss
- With suffix Z
- With offset ±HH\:MM

Parsing SHALL:

- Reject invalid numeric ranges.
- Reject malformed strings.
- Normalize to UTC epoch milliseconds.
- If no timezone designator ("Z" or ±HH:MM) is present, the input SHALL be interpreted as UTC.
- A date-only form (YYYY-MM-DD) SHALL be interpreted as YYYY-MM-DDT00:00:00.000Z.

Formatting SHALL:

- Produce extended ISO format.
- Default output SHALL use Z (UTC).

No locale-based formatting SHALL exist.

---

# 11. Exceptions

The following exceptions SHALL be defined:

- DSTAmbiguousTimeException
- DSTNonExistentTimeException
- InvalidTimeZoneException
- InvalidDateException
- InvalidISOFormatException

Exceptions SHALL be deterministic and reproducible.

---

# 12. Determinism Guarantees

1. All pure calendar helpers SHALL be deterministic.
2. All epoch-based conversions SHALL be deterministic.
3. Time.now\* functions are explicitly non-deterministic.
4. No hidden timezone state SHALL exist.

---

# 13. Emit-C Requirements

1. Mapping SHALL use clock\_gettime or equivalent.
2. Timezone resolution SHALL use system IANA database.
3. ISO week computation SHALL NOT rely on strftime.
4. Behavior MUST match Node implementation bit-for-bit.

---

# End of Normative Specification

