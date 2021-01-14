/*
 * Copyright 2008 Search Solution Corporation
 * Copyright 2016 CUBRID Corporation
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 */

/*
 * timezone_lib_common.h : Timezone structures used at runtime and at
 *			   shared library generation
 */
#ifndef _TIMEZONE_LIB_COMMON_H_
#define _TIMEZONE_LIB_COMMON_H_

#define TZ_COUNTRY_CODE_LEN	  2
#define TZ_COUNTRY_CODE_SIZE	  3
#define TZ_COUNTRY_NAME_SIZE	  50
#define TZ_DS_RULESET_NAME_SIZE	  16
#define TZ_GENERIC_NAME_SIZE	  40
#define TZ_MAX_FORMAT_SIZE	  32
#define TZ_WINDOWS_ZONE_NAME_SIZE 70
#define TZ_CHECKSUM_SIZE          32

/*
 * The defines below are types of the data representation for the "ON" column
 * from the daylight saving rules. The "ON" column can be either a number
 * representing a day of the month, an identifier like "last<Weekday>" of the
 * specified month, such as "lastSun" or "lastFri", or a rule representing a
 * particular weekday after a specific day of the month, such as "Fri>=24".
 */
typedef enum
{
  TZ_DS_TYPE_FIXED = 0,
  TZ_DS_TYPE_VAR_SMALLER,
  TZ_DS_TYPE_VAR_GREATER
} TZ_DS_TYPE;

/*
 * The values below are needed for identifying and properly computing time
 * when a daylight saving event occurs, namely to track the suffixes
 * 's' (standard local time), 'g', 'u' and 'z' (GMT/UTC/Zulu), 'w' or
 * none (local wall clock time), from the AT column in daylight saving rules.
 */
typedef enum
{
  TZ_TIME_TYPE_LOCAL_STD = 0,
  TZ_TIME_TYPE_LOCAL_WALL,
  TZ_TIME_TYPE_UTC
} TZ_TIME_TYPE;

/*
 * The defines below are used for identifying the type of timezone library and
 * files which the gen_tz utility should use as input, and what to generate
 * as output. A C file to be compiled into a shared library is always
 * generated; the question is what to include in it, and weather or not it is
 * necessary to also generate either a new or an updated C file containing
 * timezone names and IDs (which should be included in the new CUBRID release)
 * TZ_GEN_TYPE_NEW - generate the TZ/ID arrays from scratch;
 *		   - include all the data from the TZ database into the
 *		     C file to be compiled into the TZ shared library
 * TZ_GEN_TYPE_UPDATE - don't generate any TZ/ID arrays
 *		      - use the existing TZ/ID arrays to identify which
 *		      timezones need to be updated;
 *		      - no new time zones are added;
 * TZ_GEN_TYPE_EXTEND - this flag is used when generating a new timezone library
 *		        using the old library and the new timezone data
 */
typedef enum
{
  TZ_GEN_TYPE_NEW = 0,
  TZ_GEN_TYPE_UPDATE,
  TZ_GEN_TYPE_EXTEND
} TZ_GEN_TYPE;

/* DATA STRUCTURES */
typedef struct tz_country TZ_COUNTRY;
struct tz_country
{
  char code[TZ_COUNTRY_CODE_SIZE];
  char full_name[TZ_COUNTRY_NAME_SIZE];
};

enum tz_until_flag
{
  UNTIL_INFINITE = 0,
  UNTIL_EXPLICIT = 1
};
typedef enum tz_until_flag TZ_UNTIL_FLAG;

enum ds_type
{
  DS_TYPE_FIXED = 0,
  DS_TYPE_RULESET_ID = 1
};
typedef enum ds_type DS_TYPE;

typedef struct tz_offset_rule TZ_OFFSET_RULE;
struct tz_offset_rule
{
  int gmt_off;			/* time offset from UTC, in seconds */
  int ds_ruleset;		/* it is either an index in DS ruleset or a fixed amount of daylight saving (in
				 * seconds) */
  unsigned short until_year;
  unsigned char until_mon;	/* 0 - 11 */
  unsigned char until_day;	/* 0 - 30 */
  unsigned char until_hour;
  unsigned char until_min;
  unsigned char until_sec;
  TZ_TIME_TYPE until_time_type;	/* type for until: standard, wall, UTC */
  TZ_UNTIL_FLAG until_flag;	/* 0 if no ending time is specified; 1 otherwise */
  DS_TYPE ds_type;		/* 1 if ds_ruleset is a ruleset id, 0 if it is a fixed time offset (seconds) */
  int julian_date;		/* encoded julian date for until_year, until_mon, and until_day */
  const char *std_format;	/* format for standard time */
  const char *save_format;	/* format for DST time (when saving time) */
  const char *var_format;	/* format for variable time (mutually excluded with std and save formats */
};

typedef struct tz_timezone TZ_TIMEZONE;
struct tz_timezone
{
  int zone_id;
  int country_id;
  int gmt_off_rule_start;
  int gmt_off_rule_count;
};

typedef struct tz_ds_change_on TZ_DS_CHANGE_ON;
struct tz_ds_change_on
{
  TZ_DS_TYPE type;
  unsigned char day_of_month;	/* possible values: 0-27/28/29/30, depending on the month; or 31 if "ON" value is
				 * last<day> */
  unsigned char day_of_week;	/* possible values: 0-6, where 0=Sunday, 6=Saturday */
};

typedef struct tz_ds_rule TZ_DS_RULE;
struct tz_ds_rule
{
  short from_year;
  short to_year;		/* -1 if column value is "max" e.g. up to now */
  unsigned char in_month;	/* month when the daylight saving event occurs 0 - 11 */
  TZ_DS_CHANGE_ON change_on;	/* day of month, fixed or relative */
  int at_time;			/* time when DS event occurs */
  TZ_TIME_TYPE at_time_type;	/* type for at_time: standard, wall, UTC */
  int save_time;		/* amount of time saved, in seconds */
  const char *letter_abbrev;	/* letter(s) to be used in the time zone string ID */
};

typedef struct tz_ds_ruleset TZ_DS_RULESET;
struct tz_ds_ruleset
{
  int index_start;
  int count;
  const char *ruleset_name;
  int to_year_max;		/* maximum of all to_year numbers in the ruleset */
  const char *default_abrev;	/* default abbreviation for all the ds_rules in this ruleset that have daylight
				 * saving time 0 */
};

typedef struct tz_name TZ_NAME;
struct tz_name
{
  int zone_id;
  const char *name;
  unsigned char is_alias;	/* 1 if it is an alias, 0 otherwise */
};

typedef struct tz_leap_sec TZ_LEAP_SEC;
struct tz_leap_sec
{
  unsigned short year;
  unsigned char month;
  unsigned char day;
  unsigned char corr_negative;
  unsigned char is_rolling;
};

typedef struct tz_windows_iana_map TZ_WINDOWS_IANA_MAP;
struct tz_windows_iana_map
{
  char windows_zone[TZ_WINDOWS_ZONE_NAME_SIZE + 1];
  char territory[TZ_COUNTRY_CODE_SIZE + 1];
  int iana_zone_id;
};

typedef struct tz_data TZ_DATA;
struct tz_data
{
  int country_count;
  TZ_COUNTRY *countries;

  int timezone_count;
  TZ_TIMEZONE *timezones;
  char **timezone_names;

  int offset_rule_count;
  TZ_OFFSET_RULE *offset_rules;

  int name_count;
  TZ_NAME *names;

  int ds_ruleset_count;
  TZ_DS_RULESET *ds_rulesets;

  int ds_rule_count;
  TZ_DS_RULE *ds_rules;

  int ds_leap_sec_count;
  TZ_LEAP_SEC *ds_leap_sec;
#if defined (WINDOWS)
  int windows_iana_map_count;
  TZ_WINDOWS_IANA_MAP *windows_iana_map;
#endif
  /*
   * 32 digits for the md5 checksum
   */
  char checksum[TZ_CHECKSUM_SIZE + 1];
};

#endif /* _TIMEZONE_LIB_COMMON_H_ */
