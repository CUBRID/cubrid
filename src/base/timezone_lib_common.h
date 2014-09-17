/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
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
#define TZ_MAX_FORMAT_SIZE	  10
#define TZ_WINDOWS_ZONE_NAME_SIZE 70


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
 * TZ_GEN_TYPE_EXTEND - this flag is intended to be used by CUBRID developers
 *		      - add new timezones (use the TZ/ID arrays generated in a
 *		      previous CUBRID release and generate incrementing IDs
 *		      for any new timezones found in IANA's updated TZ DB
 *		      - generate updated TZ/ID arrays
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

typedef enum tz_until_flag TZ_UNTIL_FLAG;
enum tz_until_flag
{
  UNTIL_INFINITE = 0,
  UNTIL_EXPLICIT = 1
};

typedef enum ds_type DS_TYPE;
enum ds_type
{
  DS_TYPE_FIXED = 0,
  DS_TYPE_RULESET_ID = 1
};

typedef struct tz_offset_rule TZ_OFFSET_RULE;
struct tz_offset_rule
{
  int gmt_off;			/* time offset from UTC, in seconds */
  int ds_ruleset;
  unsigned short until_year;
  unsigned char until_mon;	/* 0 - 11 */
  unsigned char until_day;	/* 0 - 30 */
  unsigned char until_hour;
  unsigned char until_min;
  unsigned char until_sec;
  TZ_TIME_TYPE until_time_type;	/* type for until: standard, wall, UTC */
  TZ_UNTIL_FLAG until_flag;	/* 0 if no ending time is specified; 1 otherwise */
  DS_TYPE ds_type;		/* 1 if ds_ruleset is a ruleset id,
				 * 0 if it is a fixed time offset (seconds) */
  int julian_date;		/* encoded julian date for until_year, until_mon,
				   and until_day */
  char *std_format;		/* format for standard time */
  char *save_format;		/* format for DST time (when saving time) */
  char *var_format;		/* format for variable time (mutually excluded
				 * with std and save formats */
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
  unsigned char day_of_month;	/* possible values: 0-27/28/29/30, 
				 * depending on the month; or 31
				 * if "ON" value is last<day> */
  unsigned char day_of_week;	/* possible values: 0-6,
				 * where 0=Sunday, 6=Saturday */
};

typedef struct tz_ds_rule TZ_DS_RULE;
struct tz_ds_rule
{
  short from_year;
  short to_year;		/* -1 if column value is "max" e.g. up to now */
  unsigned char in_month;	/* month when the daylight saving event occurs
				 * 0 - 11 */
  TZ_DS_CHANGE_ON change_on;	/* day of month, fixed or relative */
  int at_time;			/* time when DS event occurs */
  TZ_TIME_TYPE at_time_type;	/* type for at_time: standard, wall, UTC */
  int save_time;		/* amount of time saved, in seconds */
  char *letter_abbrev;		/* letter(s) to be used in the time zone string ID */
};

typedef struct tz_ds_ruleset TZ_DS_RULESET;
struct tz_ds_ruleset
{
  int index_start;
  int count;
  char *ruleset_name;
};

typedef struct tz_name TZ_NAME;
struct tz_name
{
  int zone_id;
  char *name;
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
  TZ_COUNTRY *countries;	/* export in tz list file, not in shared library */

  int timezone_count;
  TZ_TIMEZONE *timezones;
  char **timezone_names;	/* export in tz list file, not in shared library */

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
};

#endif /* _TIMEZONE_LIB_COMMON_H_ */
