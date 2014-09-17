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
 * tz_support.h : Timezone support
 */
#ifndef _TZ_SUPPORT_H_
#define _TZ_SUPPORT_H_

#include "timezone_lib_common.h"
#include "thread.h"

#define TZLIB_SYMBOL_NAME_SIZE 64
#define MAX_LEN_OFFSET 10

#if defined(_WIN32) || defined(WINDOWS) || defined(WIN64)
#define SHLIB_EXPORT_PREFIX "__declspec(dllexport)"
#define SHLIB_FILE_EXT "dll"
#else
#define SHLIB_EXPORT_PREFIX   ""
#define SHLIB_FILE_EXT "so"
#endif

#define IS_LEAP_YEAR(y)	((y & 3) == 0) && (((y % 25) != 0)  || ( (y & 15) == 0) )

/* WARNING: the define below yields 30 days in February; use with care! */
#define DAYS_IN_MONTH(m) (31 - ((m) > 6 ? (m) - 7 : (m)) % 2)

#define IS_EMPTY_STR(s) ((s) == NULL || *(s) == '\0')

#define TZ_MAX_YEAR 9999
/* julian date for 1 Jan 9999 */
#define TZ_MAX_JULIAN_DATE 5373119

#define TZ_ZONE_ID_MAX	0x3ff
#define TZ_OFFSET_ID_MAX  0xff
#define TZ_DS_ID_MAX  0xff

/* the difference in days-seconds between datetimes with timezone when is safe
 * to consider only date without time */
#define DATE_DIFF_MATCH_SAFE_THRESHOLD_DAYS 2
#define DATE_DIFF_MATCH_SAFE_THRESHOLD_SEC 172800ll

#define TZ_DS_STRING_SIZE 10
#define TZR_SIZE 100

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


typedef enum tz_region_type TZ_REGION_TYPE;
enum tz_region_type
{
  TZ_REGION_OFFSET = 0,
  TZ_REGION_ZONE = 1
};

typedef struct tz_region TZ_REGION;
struct tz_region
{
  TZ_REGION_TYPE type;		/* 0 : offset ; 1 : zone */
  union
  {
    int offset;			/* in seconds */
    unsigned int zone_id;	/* geographical zone id */
  };
};

#ifdef __cplusplus
extern "C"
{
#endif
  extern int tz_load (bool is_optional);
  extern void tz_unload (void);
  extern DB_BIGINT tz_timestamp_encode_leap_sec_adj (const int year_century,
						     const int year,
						     const int mon,
						     const int day);
  extern void tz_timestamp_decode_sec (int timestamp, int *yearp,
				       int *monthsp, int *dayp, int *hoursp,
				       int *minutesp, int *secondsp);
  extern void tz_timestamp_decode_no_leap_sec (int timestamp, int *yearp,
					       int *monthsp, int *dayp,
					       int *hoursp, int *minutesp,
					       int *secondsp);
  extern int tz_get_timezone_offset (const char *tz_str, int tz_size,
				     char *result,
				     DB_DATETIME * utc_datetime);
  extern int tz_get_first_weekday_around_date (const int year,
					       const int month,
					       const int weekday,
					       const int after_day,
					       const bool before);
  extern const TZ_DATA *tz_get_data (void);
  extern const char *tz_get_system_timezone (void);
  extern const char *tz_get_session_local_timezone (void);
  extern void tz_get_session_tz_region (TZ_REGION * tz_region);
  extern void tz_id_to_region (const TZ_ID * tz_id, TZ_REGION * tz_region);
  extern const TZ_ID *tz_get_utc_tz_id (void);
  extern const TZ_REGION *tz_get_utc_tz_region (void);
  extern int tz_create_session_tzid_for_datetime (const DB_DATETIME * src_dt,
						  bool src_is_utc,
						  TZ_ID * tz_id);
  extern int tz_create_session_tzid_for_timestamp (const DB_UTIME * src_ts,
						   TZ_ID * tz_id);
  extern int tz_str_to_region (const char *tz_str, const int tz_str_size,
			       TZ_REGION * tz_region);
  extern int tz_create_session_tzid_for_time (const DB_TIME * src_time,
					      bool src_is_utc, TZ_ID * tz_id);
  extern int tz_str_read_number (const char *str, const bool strict,
				 const bool read_sign, int *val,
				 const char **str_next);
  extern int tz_str_read_time (const char *str, bool need_minutes,
			       bool allow_sec60, int *hour, int *min,
			       int *sec, const char **str_next);
  extern int tz_str_to_seconds (const char *str, int *sec,
				const char **str_next);
  extern int tz_get_ds_change_julian_date (const TZ_DS_RULE * ds_rule,
					   const int year,
					   int *ds_rule_julian_date);
  extern int tz_create_datetimetz (const DB_DATETIME * dt,
				   const char *tz_str,
				   const TZ_REGION * default_tz_region,
				   DB_DATETIMETZ * dt_tz,
				   const char **end_tz_str);
  extern int tz_create_timestamptz (const DB_DATE * date,
				    const DB_TIME * time, const char *tz_str,
				    const TZ_REGION * default_tz_region,
				    DB_TIMESTAMPTZ * ts_tz,
				    const char **end_tz_str);
  extern int tz_create_datetimetz_from_ses (const DB_DATETIME * dt,
					    DB_DATETIMETZ * dt_tz);
  extern int tz_create_timetz (const DB_TIME * time, const char *tz_str,
			       const TZ_REGION * default_tz_region,
			       DB_TIMETZ * time_tz, const char **end_tz_str);
  extern int tz_create_timetz_ext (const DB_TIME * time,
				   const char *timezone,
				   int len_timezone, DB_TIMETZ * time_tz);
  extern int tz_create_timetz_from_ses (const DB_TIME * time,
					DB_TIMETZ * time_tz);
  extern int tz_utc_datetimetz_to_local (const DB_DATETIME * dt_utc,
					 const TZ_ID * tz_id,
					 DB_DATETIME * dt_local);
  extern int tz_datetimeltz_to_local (const DB_DATETIME * dt_ltz,
				      DB_DATETIME * dt_local);
  extern int tz_utc_timetz_to_local (const DB_TIME * time_utc,
				     const TZ_ID * tz_id,
				     DB_TIME * time_local);
  extern int tz_timeltz_to_local (const DB_TIME * time_ltz,
				  DB_TIME * time_local);
  extern int tz_id_to_str (const TZ_ID * tz_id, char *tz_str,
			   const int tz_str_size);
  extern int tz_datetimetz_fix_zone (const DB_DATETIMETZ * src_dt_tz,
				     DB_DATETIMETZ * dest_dt_tz);
  extern int tz_timestamptz_fix_zone (const DB_TIMESTAMPTZ * src_ts_tz,
				      DB_TIMESTAMPTZ * dest_ts_tz);
  extern int tz_timetz_fix_zone (const DB_TIMETZ * src_time_tz,
				 DB_TIMETZ * dest_time_tz);
  extern int tz_conv_tz_datetime_w_region (const DB_DATETIME * src_dt,
					   const TZ_REGION * src_tz_region,
					   const TZ_REGION * dest_tz_region,
					   DB_DATETIME * dest_dt,
					   TZ_ID * src_tz_id_out,
					   TZ_ID * dest_tz_id_out);
  extern int tz_conv_tz_datetime_w_zone_name (const DB_DATETIME * src_dt,
					      const char *source_zone,
					      int len_source,
					      const char *dest_zone,
					      int len_dest,
					      DB_DATETIME * dest_dt);
  extern int tz_conv_tz_time_w_zone_name (const DB_TIME * time_source,
					  const char *source_zone,
					  int len_source,
					  const char *dest_zone,
					  int len_dest, DB_TIME * time_dest);
  extern int tz_explain_tz_id (const TZ_ID * tz_id, char *tzr,
			       const int tzr_size, char *tzdst,
			       const int tzdst_size, int *tzh, int *tzm);
  extern int tz_create_datetimetz_from_offset (const DB_DATETIME * dt,
					       const int tzh,
					       const int tzm,
					       DB_DATETIMETZ * dt_tz);
  extern int tz_create_timetz_from_offset (const DB_TIME * time,
					   const int tzh, const int tzm,
					   DB_TIMETZ * time_tz);
  extern int tz_create_timestamptz_from_offset (const DB_DATE * date,
						const DB_TIME * time,
						const int tzh, const int tzm,
						DB_TIMESTAMPTZ *
						timestamp_tz);
  extern int tz_get_best_match_zone (const char *name, int *size);
  extern int tz_create_datetimetz_from_zoneid_and_tzd (const DB_DATETIME * dt,
						       TZ_REGION *
						       default_tz_region,
						       const int zone_id,
						       const char *tzd,
						       const int tzd_len,
						       DB_DATETIMETZ * dt_tz);
  extern int tz_create_timetz_from_zoneid_and_tzd (const DB_TIME * time,
						   TZ_REGION *
						   default_tz_region,
						   const int zone_id,
						   const char *tzd,
						   const int tzd_len,
						   DB_TIMETZ * time_tz);
  extern int tz_create_timestamptz_from_zoneid_and_tzd (const DB_DATE * date,
							const DB_TIME * time,
							TZ_REGION *
							default_tz_region,
							const int zone_id,
							const char *tzd,
							const int tzd_len,
							DB_TIMESTAMPTZ *
							timestamp_tz);
  extern int tz_resolve_os_timezone (char *timezone, int buf_size);
#if !defined(SERVER_MODE)
  extern TZ_REGION *tz_get_client_tz_region_session (void);
#else
  extern TZ_REGION *tz_get_server_tz_region_session (void);
#endif
#if !defined (CS_MODE)
  extern int tz_timezones_start_scan (THREAD_ENTRY * thread_p, int show_type,
				      DB_VALUE ** arg_values, int arg_cnt,
				      void **ptr);
  extern int tz_full_timezones_start_scan (THREAD_ENTRY * thread_p,
					   int show_type,
					   DB_VALUE ** arg_values,
					   int arg_cnt, void **ptr);
#endif
#ifdef __cplusplus
}
#endif

#endif				/* _TZ_SUPPORT_H_ */
