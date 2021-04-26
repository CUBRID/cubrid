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
 * tz_support.h : Timezone support
 */
#ifndef _TZ_SUPPORT_H_
#define _TZ_SUPPORT_H_

#include "dbtype_def.h"
#include "thread_compat.hpp"
#include "timezone_lib_common.h"

#define db_utime_to_string db_timestamp_to_string
#define db_string_to_utime db_string_to_timestamp
#define db_date_parse_utime db_date_parse_timestamp

enum
{
  TIME_SPECIFIER = 1,
  DATE_SPECIFIER = 2,
  DATETIME_SPECIFIER = 3,
  REMOVED_TIMETZ_SPECIFIER = 4,
  DATETIMETZ_SPECIFIER = 5
};

extern void db_date_locale_init (void);

#define TZLIB_SYMBOL_NAME_SIZE 64
#define MAX_LEN_OFFSET 10

#if defined(_WIN32) || defined(WINDOWS) || defined(WIN64)
#define SHLIB_EXPORT_PREFIX "__declspec(dllexport)"
#define LIB_TZ_NAME "libcubrid_timezones.dll"
#elif defined(_AIX)
#define makestring1(x) #x
#define makestring(x) makestring1(x)

#define SHLIB_EXPORT_PREFIX   ""
#define LIB_TZ_NAME \
  "libcubrid_timezones.a(libcubrid_timezones.so." makestring(MAJOR_VERSION) ")"
#else
#define SHLIB_EXPORT_PREFIX   ""
#define LIB_TZ_NAME "libcubrid_timezones.so"
#endif

#define IS_LEAP_YEAR(y)	((((y) & 3) == 0) && ((((y) % 25) != 0) || (((y) & 15) == 0)))

/* WARNING: the define below yields 30 days in February; use with care! */
#define DAYS_IN_MONTH(m) (31 - ((m) > 6 ? (m) - 7 : (m)) % 2)

#define IS_EMPTY_STR(s) ((s) == NULL || *(s) == '\0')

#define TZ_MAX_YEAR 9999
/* support dates up to 31/12/9999 23:59:59 */
/* we need 2 Jan 10000 because of timezones like America/New_York
 * which when transformed into UTC time reference exceed 1 Jan 10000 */
/* julian date for 2 Jan 10000 */
#define TZ_MAX_JULIAN_DATE 5373486

#define TZ_ZONE_ID_MAX	0x3ff
#define TZ_OFFSET_ID_MAX  0xff
#define TZ_DS_ID_MAX  0xff

/* the difference in days-seconds between datetimes with timezone when is safe
 * to consider only date without time */
#define DATE_DIFF_MATCH_SAFE_THRESHOLD_DAYS 2
#define DATE_DIFF_MATCH_SAFE_THRESHOLD_SEC 172800ll

#define TZ_DS_STRING_SIZE 10
#define TZR_SIZE 100
#define ZONE_MAX  10000

enum
{
  TZ_MON_JAN = 0,
  TZ_MON_FEB = 1,
  TZ_MON_MAR = 2,
  TZ_MON_APR = 3,
  TZ_MON_MAY = 4,
  TZ_MON_JUN = 5,
  TZ_MON_JUL = 6,
  TZ_MON_AUG = 7,
  TZ_MON_SEP = 8,
  TZ_MON_OCT = 9,
  TZ_MON_NOV = 10,
  TZ_MON_DEC = 11,
  TZ_MON_COUNT = 12
};

enum
{
  TZ_WEEK_DAY_SUN = 0,
  TZ_WEEK_DAY_MON = 1,
  TZ_WEEK_DAY_TUE = 2,
  TZ_WEEK_DAY_WED = 3,
  TZ_WEEK_DAY_THU = 4,
  TZ_WEEK_DAY_FRI = 5,
  TZ_WEEK_DAY_SAT = 6,
  TZ_WEEK_DAY_COUNT = 7
};

#define TZ_IS_ZONE_VALID_DECODE_INFO(tz_info) \
  ((tz_info)->type == TZ_REGION_OFFSET \
   || (tz_info)->zone.zone_id != TZ_ZONE_ID_MAX)

#define TZ_IS_UTC_TZ_REGION(r) \
  ((r)->type == TZ_REGION_OFFSET && (r)->offset == 0)

typedef DB_BIGINT full_date_t;
#if defined (SA_MODE)
extern bool tz_Is_backward_compatible_timezone[];
extern bool tz_Compare_datetimetz_tz_id;
extern bool tz_Compare_timestamptz_tz_id;
#endif /* SA_MODE */

#ifdef __cplusplus
extern "C"
{
#endif
  extern int tz_load (void);
  extern void tz_unload (void);
  extern DB_BIGINT tz_timestamp_encode_leap_sec_adj (const int year_century, const int year, const int mon,
						     const int day);
  extern void tz_timestamp_decode_sec (int timestamp, int *yearp, int *monthsp, int *dayp, int *hoursp, int *minutesp,
				       int *secondsp);
  extern void tz_timestamp_decode_no_leap_sec (int timestamp, int *yearp, int *monthsp, int *dayp, int *hoursp,
					       int *minutesp, int *secondsp);
  extern int tz_get_timezone_offset (const char *tz_str, int tz_size, char *result, DB_DATETIME * utc_datetime);
  extern int tz_get_first_weekday_around_date (const int year, const int month, const int weekday, const int after_day,
					       const bool before);
  extern const TZ_DATA *tz_get_data (void);
  extern void tz_set_data (const TZ_DATA * data);
  extern const TZ_DATA *tz_get_new_timezone_data (void);
  extern void tz_set_new_timezone_data (const TZ_DATA * data);
  extern const char *tz_get_system_timezone (void);
  extern const char *tz_get_session_local_timezone (void);
  extern void tz_get_system_tz_region (TZ_REGION * tz_region);
  extern void tz_get_session_tz_region (TZ_REGION * tz_region);
  extern void tz_id_to_region (const TZ_ID * tz_id, TZ_REGION * tz_region);
  extern const TZ_ID *tz_get_utc_tz_id (void);
  extern const TZ_REGION *tz_get_utc_tz_region (void);
  extern int tz_create_session_tzid_for_datetime (const DB_DATETIME * src_dt, bool src_is_utc, TZ_ID * tz_id);
  extern int tz_create_session_tzid_for_timestamp (const DB_UTIME * src_ts, TZ_ID * tz_id);
  extern int tz_str_to_region (const char *tz_str, const int tz_str_size, TZ_REGION * tz_region);
  extern int tz_create_session_tzid_for_time (const DB_TIME * src_time, bool src_is_utc, TZ_ID * tz_id);
  extern int tz_str_read_number (const char *str, const char *str_end, const bool strict, const bool read_sign,
				 int *val, const char **str_next);
  extern int tz_str_read_time (const char *str, const char *str_end, bool need_minutes, bool allow_sec60, int *hour,
			       int *min, int *sec, const char **str_next);
  extern int tz_str_to_seconds (const char *str, const char *str_end, int *sec, const char **str_next,
				const bool is_offset);
  extern int tz_get_ds_change_julian_date_diff (const int src_julian_date, const TZ_DS_RULE * ds_rule, const int year,
						int *ds_rule_julian_date, full_date_t * date_diff);
  extern int tz_create_datetimetz (const DB_DATETIME * dt, const char *tz_str, const int tz_size,
				   const TZ_REGION * default_tz_region, DB_DATETIMETZ * dt_tz, const char **end_tz_str);
  extern int tz_create_timestamptz (const DB_DATE * date, const DB_TIME * time, const char *tz_str, const int tz_size,
				    const TZ_REGION * default_tz_region, DB_TIMESTAMPTZ * ts_tz,
				    const char **end_tz_str);
  extern int tz_create_datetimetz_from_ses (const DB_DATETIME * dt, DB_DATETIMETZ * dt_tz);
  extern int tz_utc_datetimetz_to_local (const DB_DATETIME * dt_utc, const TZ_ID * tz_id, DB_DATETIME * dt_local);
  extern int tz_datetimeltz_to_local (const DB_DATETIME * dt_ltz, DB_DATETIME * dt_local);
  extern int tz_id_to_str (const TZ_ID * tz_id, char *tz_str, const int tz_str_size);
  extern int tz_datetimetz_fix_zone (const DB_DATETIMETZ * src_dt_tz, DB_DATETIMETZ * dest_dt_tz);
  extern int tz_timestamptz_fix_zone (const DB_TIMESTAMPTZ * src_ts_tz, DB_TIMESTAMPTZ * dest_ts_tz);
  extern int tz_conv_tz_datetime_w_region (const DB_DATETIME * src_dt, const TZ_REGION * src_tz_region,
					   const TZ_REGION * dest_tz_region, DB_DATETIME * dest_dt,
					   TZ_ID * src_tz_id_out, TZ_ID * dest_tz_id_out);
  extern int tz_conv_tz_datetime_w_zone_name (const DB_DATETIME * src_dt, const char *source_zone, int len_source,
					      const char *dest_zone, int len_dest, DB_DATETIME * dest_dt);
  extern int tz_conv_tz_time_w_zone_name (const DB_TIME * time_source, const char *source_zone, int len_source,
					  const char *dest_zone, int len_dest, DB_TIME * time_dest);
  extern int tz_explain_tz_id (const TZ_ID * tz_id, char *tzr, const int tzr_size, char *tzdst, const int tzdst_size,
			       int *tzh, int *tzm);
  extern int tz_create_datetimetz_from_offset (const DB_DATETIME * dt, const int tzh, const int tzm,
					       DB_DATETIMETZ * dt_tz);
  extern int tz_create_timestamptz_from_offset (const DB_DATE * date, const DB_TIME * time, const int tzh,
						const int tzm, DB_TIMESTAMPTZ * timestamp_tz);
  extern int tz_get_best_match_zone (const char *name, int *size);
  extern int tz_create_datetimetz_from_zoneid_and_tzd (const DB_DATETIME * dt, TZ_REGION * default_tz_region,
						       const int zone_id, const char *tzd, const int tzd_len,
						       bool is_time_tz, DB_DATETIMETZ * dt_tz);
  extern int tz_create_timestamptz_from_zoneid_and_tzd (const DB_DATE * date, const DB_TIME * time,
							TZ_REGION * default_tz_region, const int zone_id,
							const char *tzd, const int tzd_len,
							DB_TIMESTAMPTZ * timestamp_tz);
  extern int tz_resolve_os_timezone (char *timezone, int buf_size);
  extern void tz_set_tz_region_system (const TZ_REGION * tz_region);
#if !defined(SERVER_MODE)
  extern TZ_REGION *tz_get_client_tz_region_session (void);
#else
  extern TZ_REGION *tz_get_server_tz_region_session (void);
#endif
#if !defined (CS_MODE)
  extern int tz_timezones_start_scan (THREAD_ENTRY * thread_p, int show_type, DB_VALUE ** arg_values, int arg_cnt,
				      void **ptr);
  extern int tz_full_timezones_start_scan (THREAD_ENTRY * thread_p, int show_type, DB_VALUE ** arg_values, int arg_cnt,
					   void **ptr);
#endif

  extern int tz_load_with_library_path (TZ_DATA * tzd, const char *timezone_library_path);
  extern int tz_check_geographic_tz (const TZ_ID * tz_id);
  extern int tz_check_session_has_geographic_tz (void);
#if !defined(SERVER_MODE)
  extern int put_timezone_checksum (char *checksum);
#endif				/* SERVER_MODE */

  extern int check_timezone_compat (const char *client_checksum, const char *server_checksum, const char *client_text,
				    const char *server_text);
  extern void tz_tzid_convert_region_to_offset (TZ_ID * tz_id);
  extern int tz_create_datetimetz_from_utc (const DB_DATETIME * src_dt, const TZ_REGION * dest_region,
					    DB_DATETIMETZ * dest_dt_tz);
  extern int tz_create_datetimetz_from_parts (const int m, const int d, const int y, const int h, const int mi,
					      const int s, const int ms, const TZ_ID * tz_id, DB_DATETIMETZ * dt_tz);
  extern int conv_tz (void *, const void *, DB_TYPE);
  int tz_get_offset_in_mins ();	//time zone offset in minutes from GMT
#ifdef __cplusplus
}
#endif

#endif				/* _TZ_SUPPORT_H_ */
