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
 * tz_support.c : Timezone runtime support
 */

#include "config.h"

#include <assert.h>

#include "porting.h"
#include "utility.h"
#include "db_date.h"
#include "environment_variable.h"
#include "chartype.h"
#include "error_manager.h"
#include "thread.h"
#if !defined(WINDOWS)
#include <dlfcn.h>
#include <unistd.h>
#endif

#include "tz_list.h"
#include "tz_support.h"
#include "system_parameter.h"
#include "memory_alloc.h"
#include "show_scan.h"
#if !defined (CS_MODE)
#include "session.h"
#endif

typedef struct tz_decode_info TZ_DECODE_INFO;
struct tz_decode_info
{
  TZ_REGION_TYPE type;		/* 0 : offset ; 1 : zone + DST */
  union
  {
    int offset;			/* in seconds */
    struct
    {
      unsigned int zone_id;	/* geographical zone id */
      unsigned int offset_id;	/* GMT offset rule id;
				 * MAX_INT if not yet determined */
      unsigned int dst_id;	/* daylight rule id ;
				 * MAX_INT if not yet determined */
      char dst_str[TZ_DS_STRING_SIZE];

      TZ_TIMEZONE *p_timezone;
      TZ_OFFSET_RULE *p_zone_off_rule;
      TZ_DS_RULE *p_ds_rule;
    } zone;
  };
};

typedef DB_BIGINT full_date_t;

#define FULL_DATE(jul_date, time_sec) ((full_date_t) jul_date * 86400ll \
				       + (full_date_t) time_sec)
#define TIME_OFFSET(is_utc, offset) \
  ((is_utc) ? (-offset) : (offset))
#define ABS(i) ((i) >= 0 ? (i) : -(i))

#define TZ_INVALID_VALUE 0xffffffff

static int tz_initialized = 0;

static int tz_load_library (const char *lib_file, void **handle,
			    bool is_optional);
static int tz_load_data_from_lib (TZ_DATA * tzd, void *lib_handle);
static bool tz_get_leapsec_support (void);
static void tz_get_session_tz_info (TZ_DECODE_INFO * tz_info);
static int tz_create_tzid_for_utc_datetime (DB_DATETIME * dt,
					    TZ_DECODE_INFO * tz_info,
					    TZ_ID * tz_id);
static DB_DATE tz_get_current_date (void);
static int tz_str_timezone_decode (const char *tz_str, const int tz_str_size,
				   TZ_DECODE_INFO * tz_info,
				   const char **tz_end);
static int tz_zone_info_to_str (const TZ_DECODE_INFO * tz_info, char *tz_str,
				const int tz_str_size);
static void tz_encode_tz_id (const TZ_DECODE_INFO * tz_info, TZ_ID * tz_id);
static void tz_encode_tz_region (const TZ_DECODE_INFO * tz_info,
				 TZ_REGION * tz_region);
static void tz_decode_tz_id (const TZ_ID * tz_id, const bool is_full_decode,
			     TZ_DECODE_INFO * tz_info);
static void tz_decode_tz_region (const TZ_REGION * tz_region,
				 TZ_DECODE_INFO * tz_info);
static int tz_fast_find_ds_rule (const TZ_DATA * tzd,
				 const TZ_DS_RULESET * ds_ruleset,
				 const int src_julian_date, int src_year,
				 int *ds_rule_id);
static bool tz_check_ds_match_string (const TZ_OFFSET_RULE * off_rule,
				      const TZ_DS_RULE * ds_rule,
				      const char *ds_string);
static int tz_datetime_utc_conv (const DB_DATETIME * src_dt,
				 TZ_DECODE_INFO * tz_info,
				 bool src_is_utc, bool only_tz_adjust,
				 DB_DATETIME * dest_dt);
static int tz_conv_tz_datetime_w_zone_info (const DB_DATETIME * src_dt,
					    const TZ_DECODE_INFO *
					    src_zone_info_in,
					    const TZ_DECODE_INFO *
					    dest_zone_info_in,
					    DB_DATETIME * dest_dt,
					    TZ_DECODE_INFO *
					    src_zone_info_out,
					    TZ_DECODE_INFO *
					    dest_zone_info_out);
static int tz_print_tz_offset (char *result, int tz_offset);
static int starts_with (const char *prefix, const char *str);
static int tz_get_zone_id_by_name (const char *name, const int name_size);
static void
tz_timestamp_decode_leap_sec_adj (int timestamp, int *yearp, int *monthsp,
				  int *dayp, int *hoursp, int *minutesp,
				  int *secondsp);
#if defined (LINUX)
static int find_timezone_from_clock (char *timezone_name, int buf_len);
static int find_timezone_from_localtime (char *timezone_name, int buf_len);
#endif
#if defined(WINDOWS)
static int tz_get_iana_zone_id_by_windows_zone (const char
						*windows_zone_name);
#endif

#if !defined(SERVER_MODE)
static TZ_REGION tz_region_session;
#endif

/*
 * structures and functions for loading timezone data
 */
static void *tz_lib_handle = NULL;
#if defined(WINDOWS)
static TZ_DATA timezone_data =
  { 0, NULL, 0, NULL, NULL, 0, NULL, 0, NULL, 0, NULL, 0, NULL, 0, NULL, 0,
NULL };
#else
static TZ_DATA timezone_data =
  { 0, NULL, 0, NULL, NULL, 0, NULL, 0, NULL, 0, NULL, 0, NULL, 0, NULL };
#endif

static const int days_of_month[] =
  { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
static const int days_up_to_month[] =
  { 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 365 };

#if defined(WINDOWS)
#define TZ_GET_SYM_ADDR(lib, sym) GetProcAddress(lib, sym)
#else
#define TZ_GET_SYM_ADDR(lib, sym) dlsym(lib, sym)
#endif

#define TZLIB_GET_ADDR(v, SYM_NAME, SYM_TYPE, lh)			    \
  do {									    \
    v = (SYM_TYPE) TZ_GET_SYM_ADDR (lh, SYM_NAME);			    \
    if (v == NULL)							    \
      {									    \
	goto error_loading_symbol;					    \
      }									    \
  } while (0)

#define TZLIB_GET_VAL(v, SYM_NAME, SYM_TYPE, lh)		\
  do {								\
    SYM_TYPE* aux;						\
    TZLIB_GET_ADDR(aux, SYM_NAME, SYM_TYPE*, lh);		\
    v = *aux;							\
  } while (0)

/*
 * tz_load_library - loads the timezone specific DLL/so
 * Returns : error code - ER_LOC_INIT if library load fails
 *			- NO_ERROR if success
 * lib_file(in)  : path to library
 * handle(out)   : handle to the loaded library
 */
static int
tz_load_library (const char *lib_file, void **handle, bool is_optional)
{
  int err_status = NO_ERROR;
  char err_msg[512];
#if defined(WINDOWS)
  DWORD loading_err;
  char *lpMsgBuf;
  UINT error_mode = 0;
#else
  char *error;
#endif

  assert (lib_file != NULL);

#if defined(WINDOWS)
  error_mode = SetErrorMode (SEM_NOOPENFILEERRORBOX | SEM_FAILCRITICALERRORS);
  *handle = LoadLibrary (lib_file);
  SetErrorMode (error_mode);
  loading_err = GetLastError ();
#else
  dlerror ();			/* Clear any existing error */
  *handle = dlopen (lib_file, RTLD_NOW);
#endif

  if (*handle == NULL)
    {
      err_status = ER_TZ_LOAD_ERROR;
    }

  if (err_status == ER_TZ_LOAD_ERROR && is_optional == false)
    {
#if defined(WINDOWS)
      FormatMessage (FORMAT_MESSAGE_ALLOCATE_BUFFER
		     | FORMAT_MESSAGE_FROM_SYSTEM
		     | FORMAT_MESSAGE_ARGUMENT_ARRAY,
		     NULL,
		     loading_err,
		     MAKELANGID (LANG_NEUTRAL, SUBLANG_DEFAULT),
		     (char *) &lpMsgBuf, 1, &lib_file);
      snprintf (err_msg, sizeof (err_msg) - 1,
		"Library file is invalid or not accessible.\n"
		" Unable to load %s !\n %s", lib_file, lpMsgBuf);
      LocalFree (lpMsgBuf);
#else
      error = dlerror ();
      snprintf (err_msg, sizeof (err_msg) - 1,
		"Library file is invalid or not accessible.\n"
		" Unable to load %s !\n %s", lib_file, error);
#endif
      printf ("%s\n", err_msg);
    }

  return err_status;
}

/*
 * tz_load_from_lib() - uses the handle to load the data from the timezone
 *			shared library into tzd
 * Returns: 0 (NO_ERROR) if success, -1 otherwise
 * tzd(out): TZ_DATA parameter where to load the data into
 * lib_handle(in): shared library/object handle
 */
static int
tz_load_data_from_lib (TZ_DATA * tzd, void *lib_handle)
{
  char sym_name[TZLIB_SYMBOL_NAME_SIZE + 1];
  char err_msg[512 + PATH_MAX];

  assert (lib_handle != NULL);
  assert (tzd != NULL);

  /* use countries and timezone names exported into timezone_list.c */
  tzd->countries = (TZ_COUNTRY *) tz_countries;
  tzd->country_count = DIM (tz_countries);
  tzd->timezone_names = (char **) tz_timezone_names;

  /* load all other data from the shared library */
  TZLIB_GET_VAL (tzd->timezone_count, "timezone_count", int, lib_handle);

  TZLIB_GET_ADDR (tzd->timezones, "timezones", TZ_TIMEZONE *, lib_handle);

  TZLIB_GET_VAL (tzd->offset_rule_count, "offset_rule_count", int,
		 lib_handle);
  TZLIB_GET_ADDR (tzd->offset_rules, "offset_rules", TZ_OFFSET_RULE *,
		  lib_handle);

  TZLIB_GET_VAL (tzd->name_count, "tz_name_count", int, lib_handle);
  TZLIB_GET_ADDR (tzd->names, "tz_names", TZ_NAME *, lib_handle);

  TZLIB_GET_VAL (tzd->ds_ruleset_count, "ds_ruleset_count", int, lib_handle);
  TZLIB_GET_ADDR (tzd->ds_rulesets, "ds_rulesets", TZ_DS_RULESET *,
		  lib_handle);

  TZLIB_GET_VAL (tzd->ds_rule_count, "ds_rule_count", int, lib_handle);
  TZLIB_GET_ADDR (tzd->ds_rules, "ds_rules", TZ_DS_RULE *, lib_handle);

  TZLIB_GET_VAL (tzd->ds_leap_sec_count, "ds_leap_sec_count", int,
		 lib_handle);
  TZLIB_GET_ADDR (tzd->ds_leap_sec, "ds_leap_sec", TZ_LEAP_SEC *, lib_handle);

#if defined(WINDOWS)
  TZLIB_GET_VAL (tzd->windows_iana_map_count, "windows_iana_map_count", int,
		 lib_handle);
  TZLIB_GET_ADDR (tzd->windows_iana_map, "windows_iana_map",
		  TZ_WINDOWS_IANA_MAP *, lib_handle);
#endif

  return NO_ERROR;

error_loading_symbol:
  snprintf (err_msg, sizeof (err_msg) - 1,
	    "Cannot load symbol %s from the timezone library file!",
	    sym_name);
  printf ("%s", err_msg);

  return -1;
}

/*
 * tz_load() - opens the timezone library and loads the data into the tzd
 *	       parameter
 * is_optional(in): true if module loading is optional
 * Returns: 0 (NO_ERROR) if success, -1 otherwise
 */
int
tz_load (bool is_optional)
{
  int err_status = NO_ERROR;
  char lib_file[PATH_MAX] = { 0 };
  char lib_short_file[PATH_MAX] = { 0 };

  if (tz_initialized)
    {
      return err_status;
    }

  snprintf (lib_short_file, sizeof (lib_short_file) - 1,
	    "libcubrid_timezones.%s", SHLIB_FILE_EXT);

  envvar_libdir_file (lib_file, PATH_MAX, lib_short_file);

  err_status = tz_load_library (lib_file, &tz_lib_handle, is_optional);
  if (err_status != NO_ERROR)
    {
      if (is_optional)
	{
	  tz_initialized = 1;
	  err_status = NO_ERROR;
	}
      goto exit;
    }

  err_status = tz_load_data_from_lib (&timezone_data, tz_lib_handle);
  if (err_status != NO_ERROR)
    {
      goto exit;
    }

  tz_initialized = 1;

exit:
  if (err_status != NO_ERROR)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TZ_LOAD_ERROR,
	      1, lib_file);
    }
  return err_status;
}

/*
 * tz_unload () - destroy all timezone related data
 * Returns:
 */
void
tz_unload (void)
{
  memset (&timezone_data, 0, sizeof (timezone_data));

  if (tz_lib_handle != NULL)
    {
#if defined(WINDOWS)
      FreeLibrary (tz_lib_handle);
#else
      dlclose (tz_lib_handle);
#endif
      tz_lib_handle = NULL;
    }
  tz_initialized = 0;
}

/*
 * tz_get_leapsec_support() - returns true if leap-seconds
 * are activated and false if not
 */

static bool
tz_get_leapsec_support (void)
{
  static bool leapsec_support_init = false;
  static bool leapsec_support;

  if (leapsec_support_init == false)
    {
      leapsec_support = prm_get_bool_value (PRM_ID_TZ_LEAP_SECOND_SUPPORT);
      leapsec_support_init = true;
    }

  return leapsec_support;
}

/*
 * tz_timestamp_encode_leap_sec_adj() - returns offset in seconds for leap
 *					second adjustment
 */
DB_BIGINT
tz_timestamp_encode_leap_sec_adj (const int year_century, const int year,
				  const int mon, const int day)
{
  DB_BIGINT t = 0;
  int len, index;
  const TZ_DATA *tzd;
  TZ_LEAP_SEC leap_second;

  tzd = tz_get_data ();

  if (tzd == NULL || tz_get_leapsec_support () == false)
    {
      return 0;
    }

  len = tzd->ds_leap_sec_count;
  index = 0;
  while (index < len)
    {
      leap_second = tzd->ds_leap_sec[index];

      if (leap_second.year > year_century + year)
	{
	  break;
	}
      if (leap_second.year == year_century + year)
	{
	  if ((mon < leap_second.month) || (mon == leap_second.month &&
					    day <= leap_second.day))
	    {
	      break;
	    }
	}
      index++;
    }
  t = index;

  return t;
}

/*
* tz_timestamp_decode_sec() - extracts from a UNIX timestamp
*			      the year, month, day, hour, minute
*			      and second
*		
*/
void
tz_timestamp_decode_sec (int timestamp, int *yearp, int *monthsp,
			 int *dayp, int *hoursp, int *minutesp, int *secondsp)
{
  if (tz_get_leapsec_support () == false)
    {
      tz_timestamp_decode_no_leap_sec (timestamp, yearp, monthsp, dayp,
				       hoursp, minutesp, secondsp);
    }
  else
    {
      tz_timestamp_decode_leap_sec_adj (timestamp, yearp, monthsp, dayp,
					hoursp, minutesp, secondsp);
    }
}

/*
 * tz_timestamp_decode_leap_sec_adj() - returns offset in seconds for leap
 *					second adjustment
 */
static void
tz_timestamp_decode_leap_sec_adj (int timestamp, int *yearp, int *monthsp,
				  int *dayp, int *hoursp, int *minutesp,
				  int *secondsp)
{
  const int year_base = 1970;
  const int secs_per_day = 24 * 3600;
  int index, leap_cnt;
  int leap, diff, i;
  int year, months, day;
  int hours, minutes, seconds;
  const TZ_DATA *tzd;

  year = 0;
  months = 0;

  tzd = tz_get_data ();
  index = 0;

  if (tzd != NULL)
    {
      leap_cnt = tzd->ds_leap_sec_count;
    }
  else
    {
      leap_cnt = 0;
    }

  /* get number of years */
  for (;;)
    {
      int days_in_year;
      leap = 0;

      if (IS_LEAP_YEAR (year_base + year))
	{
	  days_in_year = 366;
	}
      else
	{
	  days_in_year = 365;
	}

      while (index < leap_cnt
	     && tzd->ds_leap_sec[index].year == year_base + year)
	{
	  index++;
	  leap++;
	}

      if (timestamp - days_in_year * secs_per_day - leap < 0)
	{
	  index -= leap;
	  break;
	}

      timestamp -= days_in_year * secs_per_day + leap;
      year++;
    }

  /* Get the number of months */
  for (i = TZ_MON_JAN; i <= TZ_MON_DEC; i++)
    {
      int subtract = days_of_month[i] * secs_per_day;
      leap = 0;

      /* leap year and february */
      if (IS_LEAP_YEAR (year_base + year) && i == TZ_MON_FEB)
	{
	  subtract += secs_per_day;
	}

      while (index < leap_cnt
	     && tzd->ds_leap_sec[index].year == year_base + year
	     && tzd->ds_leap_sec[index].month == months)
	{
	  index++;
	  leap++;
	}

      if (timestamp - subtract - leap < 0)
	{
	  index -= leap;
	  break;
	}
      timestamp -= subtract + leap;
      months++;
    }

  /* Get the number of days */
  day = timestamp / secs_per_day;
  timestamp = timestamp % secs_per_day;
  leap = 0;

  while (index < leap_cnt && tzd->ds_leap_sec[index].year == year_base + year
	 && tzd->ds_leap_sec[index].month == months
	 && tzd->ds_leap_sec[index].day <= day)
    {
      index++;
      leap++;
    }

  diff = timestamp - leap;
  if (diff < 0)
    {
      day--;
      timestamp = secs_per_day + diff;
    }
  else
    {
      timestamp = diff;
    }
  day++;

  /* Get the hours, minutes and seconds */
  hours = timestamp / 3600;
  minutes = (timestamp % 3600) / 60;
  seconds = (timestamp % 3600) % 60;

  *yearp = year + year_base;
  *monthsp = months;
  *dayp = day;
  *hoursp = hours;
  *minutesp = minutes;
  *secondsp = seconds;
}

/*
 * tz_timestamp_decode_no_leap_sec() - extracts from a UNIX timestamp
 *				       the year, month, day, hour, minute
 *				       and second without taking into account
 *				       leap seconds
 */
void
tz_timestamp_decode_no_leap_sec (int timestamp, int *yearp, int *monthsp,
				 int *dayp, int *hoursp, int *minutesp,
				 int *secondsp)
{
  const int year_base = 1970;
  const int secs_per_day = 24 * 3600;
  const int days_in_year = 365;
  const int secs_in_a_year = secs_per_day * days_in_year;
  int year, month, day;
  int hours, minutes, seconds;
  int secs_until_last_year;
  int secs_until_last_month = 0;
  int days_last_month;

  /* Get approximate number of years */
  year = timestamp / (days_in_year * secs_per_day);
  secs_until_last_year = year * secs_in_a_year
    + ((year + 1) / 4) * secs_per_day
    - ((year + 69) / 100) * secs_per_day
    + ((year + 369) / 400) * secs_per_day;

  /* If we overestimated the number of years take back one year */
  if (timestamp - secs_until_last_year < 0)
    {
      year--;
      secs_until_last_year -= secs_in_a_year;
      if (IS_LEAP_YEAR (year_base + year))
	{
	  secs_until_last_year -= secs_per_day;
	}
    }
  timestamp -= secs_until_last_year;

  /* Get approximate number of months */
  month = timestamp / (31 * secs_per_day);

  /* If we underestimated the number of months add another one */
  if (month > TZ_MON_JAN)
    {
      secs_until_last_month = days_up_to_month[month - 1] * secs_per_day;
      if (IS_LEAP_YEAR (year_base + year) && month > TZ_MON_FEB)
	{
	  secs_until_last_month += secs_per_day;
	}
    }

  days_last_month = days_of_month[month] * secs_per_day;
  if (IS_LEAP_YEAR (year_base + year) && month == TZ_MON_FEB)
    {
      days_last_month += secs_per_day;
    }

  if (timestamp - secs_until_last_month - days_last_month >= 0)
    {
      secs_until_last_month += days_last_month;
      month++;
    }

  timestamp -= secs_until_last_month;

  /* Get the number of days */
  day = timestamp / secs_per_day;
  day++;
  timestamp = timestamp % secs_per_day;

  /* Get the hours, minutes and seconds */
  hours = timestamp / 3600;
  minutes = (timestamp % 3600) / 60;
  seconds = (timestamp % 3600) % 60;

  *yearp = year + year_base;
  *monthsp = month;
  *dayp = day;
  *hoursp = hours;
  *minutesp = minutes;
  *secondsp = seconds;
}

/*
 * Timezone CUBRID access functions
 */

/*
 * tz_get_data() - get a reference to the global timezone data
 * Returns: NULL if timezone data is not loaded, reference to it otherwise
 */
const TZ_DATA *
tz_get_data (void)
{
  if (timezone_data.timezone_count == 0)
    {
      return NULL;
    }

  return &timezone_data;
}

/* 
 *  tz_get_session_local_timezone - returns session timezone
 * 
 */
const char *
tz_get_session_local_timezone (void)
{
  return prm_get_string_value (PRM_ID_TIMEZONE);
}

/*
 * tz_get_system_timezone - returns server timezone
 * 
 */
const char *
tz_get_system_timezone (void)
{
  return prm_get_string_value (PRM_ID_SERVER_TIMEZONE);
}

/*
 * tz_get_session_tz_region() - get the session timezone region
 */
void
tz_get_session_tz_region (TZ_REGION * tz_region)
{
  TZ_REGION *session_tz_region;

#if !defined(SERVER_MODE)
  session_tz_region = tz_get_client_tz_region_session ();
#else
  session_tz_region = tz_get_server_tz_region_session ();
#endif
  *tz_region = *session_tz_region;
}

/*
 * tz_id_to_region() - converts a TZ_ID into a TZ_REGION
 */
void
tz_id_to_region (const TZ_ID * tz_id, TZ_REGION * tz_region)
{
  int er_status = NO_ERROR;
  TZ_DECODE_INFO tz_info;

  tz_decode_tz_id (tz_id, false, &tz_info);
  tz_encode_tz_region (&tz_info, tz_region);
}

/* 
 * tz_get_session_tz_info() - 
 * tz_info (out) - decoded timezone info
 */
static void
tz_get_session_tz_info (TZ_DECODE_INFO * tz_info)
{
  int er_status = NO_ERROR;
  const char *str_tz;

  str_tz = tz_get_session_local_timezone ();

  er_status = tz_str_timezone_decode (str_tz, strlen (str_tz), tz_info, NULL);
  if (er_status != NO_ERROR)
    {
      tz_info->type = TZ_REGION_OFFSET;
      tz_info->offset = 0;
    }
}

/*
 * tz_get_utc_tz_id() - returns the compressed timezone identifer for UTC
 * 
 */
const TZ_ID *
tz_get_utc_tz_id (void)
{
  static const TZ_ID utc_tz_id = 0x1 << 30;
  return &utc_tz_id;
}

/*
 * tz_get_utc_tz_region() - return the timezone region for UTC
 * 
 */
const TZ_REGION *
tz_get_utc_tz_region (void)
{
  static const TZ_REGION utc_region = { 0, {0} };
  return &utc_region;
}

/*
 * tz_get_current_date() - 
 * 
 */
static DB_DATE
tz_get_current_date (void)
{
  DB_DATETIME datetime;
  time_t time_val;
  struct tm c_time_struct, *c_time_struct_p;

  time_val = time (NULL);
  c_time_struct_p = localtime_r (&time_val, &c_time_struct);
  if (c_time_struct_p == NULL)
    {
      db_datetime_encode (&datetime, 1, 1, 2012, 10, 30, 0, 0);
    }
  else
    {
      db_datetime_encode (&datetime, c_time_struct_p->tm_mon + 1,
			  c_time_struct_p->tm_mday,
			  c_time_struct_p->tm_year + 1900,
			  c_time_struct_p->tm_hour, c_time_struct_p->tm_min,
			  c_time_struct_p->tm_sec, 0);
    }

  return datetime.date;
}

/*
 * tz_print_tz_offset () - stores in result in the format hh:mm or
 *			hh:mm:ss the number of seconds in tz_offset,
 *			where hh is the number of hours, mm the number of
 *			minutes and ss is the number of seconds
 *		        hh:mm:ss format is used only if the number of
 *			seconds is positive
 *                      
 * tz_offset (in) : timezone offset represented in seconds
 * result (out) : output timezone offset
 */

static int
tz_print_tz_offset (char *result, int tz_offset)
{
  const int sign_hour_minutes = 6;
  const int seconds = 3;
  int off_hour, off_min, off_sec, out_len = 0;
  char sign = '+';

  if (tz_offset < 0)
    {
      sign = '-';
      tz_offset = -tz_offset;
    }

  off_hour = tz_offset / 3600;
  off_min = (tz_offset % 3600) / 60;
  off_sec = (tz_offset % 3600) % 60;

  out_len += sign_hour_minutes;
  if (off_sec != 0)
    {
      out_len += seconds;
    }

  if (!off_sec)
    {
      snprintf (result, out_len + 1, "%c%02d:%02d", sign, off_hour, off_min);
    }
  else
    {
      snprintf (result, out_len + 1, "%c%02d:%02d:%02d", sign,
		off_hour, off_min, off_sec);
    }
  (result)[out_len] = '\0';

  return NO_ERROR;
}

/*
 * tz_get_timezone_offset () - puts in result the timezone offset of 
 *                             tz_str timezone
 * tz_str (in) : name or offset of timezone
 * tz_size (in) : length of timezone string 
 * datetime (in) : the current UTC datetime
 * result (out) : the timezone offset
 */
int
tz_get_timezone_offset (const char *tz_str, int tz_size,
			char *result, DB_DATETIME * utc_datetime)
{
  const char *p = tz_str;
  int error = NO_ERROR;

  while (p < tz_str + tz_size && char_isspace (*p))
    {
      p++;
    }

  if (p >= tz_str + tz_size)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TZ_INVALID_TIMEZONE, 0);
      return ER_TZ_INVALID_TIMEZONE;
    }

  if (*p == '+' || *p == '-')
    {
      int seconds = 0;
      const char *zone_end;
      if (tz_str_to_seconds (p, &seconds, &zone_end) != NO_ERROR)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TZ_INVALID_TIMEZONE,
		  0);
	  return ER_TZ_INVALID_TIMEZONE;
	}

      while (zone_end < tz_str + tz_size && char_isspace (*zone_end))
	{
	  zone_end++;
	}

      if (zone_end != tz_str + tz_size)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TZ_INVALID_TIMEZONE,
		  0);
	  return ER_TZ_INVALID_TIMEZONE;
	}

      error = tz_print_tz_offset (result, seconds);
    }
  /* Handle the main case when the timezone
   * is a name
   */
  else
    {
      int zone_id;
      TZ_DECODE_INFO tzinfo;
      DB_DATETIME dest_datetime;
      int tdif;
      const char *save_poz;
      const char *end;

      save_poz = p;
      while (p < tz_str + tz_size && !char_isspace (*p))
	{
	  p++;
	}
      end = p;

      while (end < tz_str + tz_size && char_isspace (*end))
	{
	  end++;
	}
      if (end != tz_str + tz_size)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TZ_INVALID_TIMEZONE,
		  0);
	  return ER_TZ_INVALID_TIMEZONE;
	}

      zone_id = tz_get_zone_id_by_name (save_poz, p - save_poz);
      if (zone_id == -1)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TZ_INVALID_TIMEZONE,
		  0);
	  return ER_TZ_INVALID_TIMEZONE;
	}

      tzinfo.type = TZ_REGION_ZONE;
      tzinfo.zone.zone_id = zone_id;
      tzinfo.zone.dst_str[0] = '\0';

      error = tz_datetime_utc_conv (utc_datetime, &tzinfo, true, false,
				    &dest_datetime);
      if (error != NO_ERROR)
	{
	  return error;
	}

      tdif =
	(int) (dest_datetime.date - utc_datetime->date) * 3600 * 24 +
	(int) (dest_datetime.time - utc_datetime->time) / 1000;

      error = tz_print_tz_offset (result, tdif);
    }

  return error;
}

/*
 * tz_create_session_tzid_for_datetime() - Creates a TZ_ID object for a
 *	      DATETIME
 *
 * Returns: error code
 * src_dt(in): DATETIME value
 * src_is_utc(in): if true, than source DATETIME is considered in UTC,
 *		   otherwise in in session timezone
 * tz_id(out): result TZ_ID
 */
int
tz_create_session_tzid_for_datetime (const DB_DATETIME * src_dt,
				     bool src_is_utc, TZ_ID * tz_id)
{
  TZ_DECODE_INFO tz_info;
  int er_status = NO_ERROR;
  DB_DATETIME dummy_dt;
  TZ_REGION session_tz_region;

  tz_get_session_tz_region (&session_tz_region);
  tz_decode_tz_region (&session_tz_region, &tz_info);

  /* we use tz_info which only has zone_id valid to establish correct
   * offset and dst_id according to dt value */
  er_status = tz_datetime_utc_conv (src_dt, &tz_info, src_is_utc, true,
				    &dummy_dt);
  if (er_status != NO_ERROR)
    {
      return er_status;
    }

  tz_encode_tz_id (&tz_info, tz_id);

  return er_status;
}

/*
 * tz_create_session_tzid_for_timestamp() - Creates a TZ_ID object for a
 *					    TIMESTAMP
 *
 * Returns: error code
 * src_ts(in): TIMESTAMP value
 * tz_id(out): result TZ_ID
 * 
 */
int
tz_create_session_tzid_for_timestamp (const DB_UTIME * src_ts, TZ_ID * tz_id)
{
  DB_DATE date;
  DB_TIME time;
  DB_DATETIME dt;

  db_timestamp_decode_utc (src_ts, &date, &time);
  dt.date = date;
  dt.time = time * 1000;

  return tz_create_session_tzid_for_datetime (&dt, true, tz_id);
}

/*
 * tz_create_session_tzid_for_time() - Creates a TZ_ID object for a TIME
 *
 * Returns: error code
 * src_dt(in): TIME value
 * src_is_utc(in): if true, than source TIME is considered in UTC,
 *		   otherwise in in session timezone
 * tz_id(out): result TZ_ID
 * 
 */
int
tz_create_session_tzid_for_time (const DB_TIME * src_time, bool src_is_utc,
				 TZ_ID * tz_id)
{
  int er_status = NO_ERROR;
  DB_DATETIME src_dt;

  src_dt.date = tz_get_current_date ();
  src_dt.time = *src_time;

  return tz_create_session_tzid_for_datetime (&src_dt, src_is_utc, tz_id);
}

/*
 * tz_get_zone_id_by_name() - returns the ID of a timezone having the
 *			      specified name or alias.
 *
 * Returns: ID of zone, or -1 if not found
 * name(in): timezone name or alias to search for (not null-terminated)
 * name_size(in): size in bytes of name
 */
static int
tz_get_zone_id_by_name (const char *name, const int name_size)
{
  const TZ_DATA *tzd;
  int name_index = -1;
  int index_bot, index_top;
  int cmp_res;

  assert (name != NULL);

  tzd = tz_get_data ();
  if (tzd == NULL)
    {
      return -1;
    }

  index_bot = 0;
  index_top = tzd->name_count - 1;

  while (index_bot <= index_top)
    {
      name_index = (index_bot + index_top) >> 1;
      cmp_res = strncmp (name, tzd->names[name_index].name, name_size);

      if (cmp_res < 0)
	{
	  index_top = name_index - 1;
	}
      else if (cmp_res > 0)
	{
	  index_bot = name_index + 1;
	}
      else
	{
	  assert (cmp_res == 0);
	  if (strlen (tzd->names[name_index].name) != name_size)
	    {
	      index_top = name_index - 1;
	      continue;
	    }
	  return tzd->names[name_index].zone_id;
	}
    }

  return -1;
}

/* 
 * tz_str_timezone_decode () - 
 *
 * Return: error code
 * tz_str(in): string containing timezone information (zone, daylight saving);
 *	       not null-terminated
 * tz_str(in): string containing timezone information (zone, daylight saving);
 *	       null-terminated
 * tz_info(out): object containing decoded timezone info
 * tz_end(out): pointer to end of zone information
 *
 *  Valid formats for timezone string (leading/trailing whitespaces and
 *  leading zeros are optional) :
 *    - " +08:00 "
 *    - " Europe/Berlin "
 *    - " Europe/Berlin +08:00 "
 */
static int
tz_str_timezone_decode (const char *tz_str, const int tz_str_size,
			TZ_DECODE_INFO * tz_info, const char **tz_end)
{
  const char *zone_str, *tz_str_end;
  const char *zone_str_end;
  int zone_id;

  tz_str_end = tz_str + tz_str_size;

  zone_str = tz_str;
  while (zone_str < tz_str_end && char_isspace (*zone_str))
    {
      zone_str++;
    }

  if (zone_str >= tz_str_end)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TZ_INVALID_TIMEZONE, 0);
      return ER_TZ_INVALID_TIMEZONE;
    }

  if (*zone_str == '+' || *zone_str == '-')
    {
      if (tz_str_to_seconds (zone_str, &(tz_info->offset), &zone_str_end)
	  != NO_ERROR)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TZ_INVALID_TIMEZONE,
		  0);
	  return ER_TZ_INVALID_TIMEZONE;
	}
      tz_info->type = TZ_REGION_OFFSET;
    }
  else
    {
      const char *dst_str, *dst_str_end, *reg_str_end;
      /* zone plus optional DST */
      reg_str_end = zone_str;
      while (reg_str_end < tz_str_end && !char_isspace (*reg_str_end))
	{
	  reg_str_end++;
	}

      dst_str = reg_str_end;
      while (dst_str < tz_str_end && char_isspace (*dst_str))
	{
	  dst_str++;
	}

      if (dst_str < tz_str_end)
	{
	  dst_str_end = dst_str;
	  while (dst_str_end < tz_str_end && !char_isspace (*dst_str_end))
	    {
	      dst_str_end++;
	    }
	  zone_str_end = dst_str_end;
	}
      else
	{
	  zone_str_end = dst_str;
	  dst_str = NULL;
	}

      zone_id = tz_get_zone_id_by_name (zone_str, reg_str_end - zone_str);
      if (zone_id == -1)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TZ_INVALID_TIMEZONE,
		  0);
	  return ER_TZ_INVALID_TIMEZONE;
	}

      tz_info->type = TZ_REGION_ZONE;
      tz_info->zone.zone_id = zone_id;
      tz_info->zone.dst_id = TZ_DS_ID_MAX;
      tz_info->zone.offset_id = TZ_OFFSET_ID_MAX;

      if (dst_str != NULL)
	{
	  if (dst_str_end - dst_str > (int) sizeof (tz_info->zone.dst_str))
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TZ_INVALID_DST, 0);
	      return ER_TZ_INVALID_TIMEZONE;
	    }

	  strncpy (tz_info->zone.dst_str, dst_str, dst_str_end - dst_str);
	  tz_info->zone.dst_str[dst_str_end - dst_str] = '\0';
	  zone_str = dst_str_end;
	}
      else
	{
	  tz_info->zone.dst_str[0] = '\0';
	}
    }

  if (tz_end != NULL)
    {
      while (zone_str_end < tz_str_end && char_isspace (*zone_str_end))
	{
	  zone_str_end++;
	}
      *tz_end = zone_str_end;
    }

  return NO_ERROR;
}

/* 
 * tz_str_to_region ()
 *
 * Return: error code
 * tz_str(in): string containing timezone information (zone, daylight saving);
 *	       not null-terminated
 * tz_str_size(in): size in characters of zone information of tz_str
 * tz_region(out): object containing timezone info
 *
 *  Valid formats for timezone string (leading/trailing white-spaces and
 *  leading zeros are optional) :
 *    - " +08:00 "
 *    - " Europe/Berlin "
 *    - " Europe/Berlin +08:00 "
 */
int
tz_str_to_region (const char *tz_str, const int tz_str_size,
		  TZ_REGION * tz_region)
{
  const char *zone_str, *tz_str_end;
  const char *zone_str_end;
  int reg_zone_id, reg_offset;
  TZ_REGION_TYPE reg_type;

  tz_str_end = tz_str + tz_str_size;
  zone_str = tz_str;

  while (zone_str < tz_str_end && char_isspace (*zone_str))
    {
      zone_str++;
    }

  if (zone_str >= tz_str_end)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TZ_INVALID_TIMEZONE, 0);
      return ER_TZ_INVALID_TIMEZONE;
    }

  if (*zone_str == '+' || *zone_str == '-')
    {
      if (tz_str_to_seconds (zone_str, &reg_offset, &zone_str_end)
	  != NO_ERROR)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TZ_INVALID_TIMEZONE,
		  0);
	  return ER_TZ_INVALID_TIMEZONE;
	}

      while (zone_str_end < tz_str_end && char_isspace (*zone_str_end))
	{
	  zone_str_end++;
	}

      if (zone_str_end != tz_str_end)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TZ_INVALID_TIMEZONE,
		  0);
	  return ER_TZ_INVALID_TIMEZONE;
	}

      reg_type = TZ_REGION_OFFSET;
    }
  else
    {
      const char *reg_str_end;

      reg_str_end = zone_str;
      while (reg_str_end < tz_str_end && !char_isspace (*reg_str_end))
	{
	  reg_str_end++;
	}

      reg_zone_id = tz_get_zone_id_by_name (zone_str, reg_str_end - zone_str);
      if (reg_zone_id == -1)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TZ_INVALID_TIMEZONE,
		  0);
	  return ER_TZ_INVALID_TIMEZONE;
	}

      reg_type = TZ_REGION_ZONE;
    }

  if (tz_region != NULL)
    {
      tz_region->type = reg_type;
      if (reg_type == TZ_REGION_OFFSET)
	{
	  tz_region->offset = reg_offset;
	}
      else
	{
	  tz_region->zone_id = reg_zone_id;
	}
    }

  return NO_ERROR;
}

/* 
 * tz_create_datetimetz () - transforms a DATETIME and timezone string (or
 *			     default timezone identifier) into a DATETIME
 *			     (in UTC) with timezone info, considering the
 *			     source DATETIME in specified timezone
 *
 * Return: error code
 * datetime(in): decoded local datetime value (as appears in the user string)
 * tz_str(in): string containing timezone information (zone, daylight saving);
 *	       null-terminated, can be NULL, in which case default_tz_region
 *	       is used
 * default_tz_region(in): default timezone region to apply if input string
 *			  does not contain a valid zone information
 * datetime_tz(out): object containing datetime value (adjusted to UTC) and
 *		     timezone info
 * end_tz_str(out): pointer to end of parsed string
 *
 *  Valid formats for timezone string (leading/trailing white-spaces and
 *  leading zero are optional) :
 *    - " +08:00 "
 *    - " Europe/Berlin "
 *    - " Europe/Berlin +08:00 "
 */
int
tz_create_datetimetz (const DB_DATETIME * dt, const char *tz_str,
		      const TZ_REGION * default_tz_region,
		      DB_DATETIMETZ * dt_tz, const char **end_tz_str)
{
  int err_status = NO_ERROR;
  DB_DATETIME utc_dt;
  TZ_DECODE_INFO tz_info;

  if (end_tz_str != NULL)
    {
      *end_tz_str = NULL;
    }

  if (tz_str != NULL)
    {
      err_status = tz_str_timezone_decode (tz_str, strlen (tz_str), &tz_info,
					   end_tz_str);
      if (err_status != NO_ERROR)
	{
	  goto exit;
	}
    }
  else
    {
      tz_decode_tz_region (default_tz_region, &tz_info);
    }

  err_status = tz_datetime_utc_conv (dt, &tz_info, false, false, &utc_dt);
  if (err_status != NO_ERROR)
    {
      goto exit;
    }
  tz_encode_tz_id (&tz_info, &(dt_tz->tz_id));
  dt_tz->datetime = utc_dt;

exit:
  return err_status;
}

/*
 * tz_create_timestamptz () - creates a timestamp with timezone from date
 *			      and time values considering the user timezone
 *			      from tz_str; if this is NULL, then
 *			      default_tz_region is used 
 *
 * Return: error code
 * date(in): local date
 * time(in): local time
 * tz_str(in): string containing timezone information (zone, daylight saving);
 *	       null-terminated, can be NULL, in which case default_tz_region
 *	       is used
 * default_tz_region(in): default timezone region to apply if input string
 *			  does not contain a valid zone information
 * ts_tz(out): object containing timestamp value and timezone info
 *
 */
int
tz_create_timestamptz (const DB_DATE * date, const DB_TIME * time,
		       const char *tz_str,
		       const TZ_REGION * default_tz_region,
		       DB_TIMESTAMPTZ * ts_tz, const char **end_tz_str)
{
  int err_status = NO_ERROR;
  DB_DATETIME utc_dt, dt;
  TZ_DECODE_INFO tz_info;
  DB_DATE date_utc;
  DB_TIME time_utc;

  if (end_tz_str != NULL)
    {
      *end_tz_str = NULL;
    }

  if (tz_str != NULL)
    {
      err_status = tz_str_timezone_decode (tz_str, strlen (tz_str), &tz_info,
					   end_tz_str);
      if (err_status != NO_ERROR)
	{
	  goto exit;
	}
    }
  else
    {
      tz_decode_tz_region (default_tz_region, &tz_info);
    }

  dt.date = *date;
  dt.time = (*time) * 1000;

  err_status = tz_datetime_utc_conv (&dt, &tz_info, false, false, &utc_dt);
  if (err_status != NO_ERROR)
    {
      goto exit;
    }
  date_utc = utc_dt.date;
  time_utc = utc_dt.time / 1000;

  err_status = db_timestamp_encode_utc (&date_utc, &time_utc,
					&ts_tz->timestamp);
  if (err_status != NO_ERROR)
    {
      goto exit;
    }
  tz_encode_tz_id (&tz_info, &(ts_tz->tz_id));

exit:
  return err_status;
}

/* 
 * tz_create_datetimetz_from_ses () - creates a datetime with timezone from
 *				      a datetime using session timezone
 *			     
 * Return: error code
 * dt(in): decoded local datetime value (as appears in the user string)
 * dt_tz(out): object containing datetime value (adjusted to UTC) and
 *	       timezone info

 */
int
tz_create_datetimetz_from_ses (const DB_DATETIME * dt, DB_DATETIMETZ * dt_tz)
{
  int err_status = NO_ERROR;
  DB_DATETIME utc_dt;
  TZ_DECODE_INFO tz_info;
  TZ_REGION session_tz_region;

  tz_get_session_tz_region (&session_tz_region);
  tz_decode_tz_region (&session_tz_region, &tz_info);

  err_status = tz_datetime_utc_conv (dt, &tz_info, false, false, &utc_dt);
  if (err_status != NO_ERROR)
    {
      goto exit;
    }
  tz_encode_tz_id (&tz_info, &(dt_tz->tz_id));
  dt_tz->datetime = utc_dt;

exit:
  return err_status;
}

/* 
 * tz_create_timetz () - transforms a TIME and timezone string (or
 *			 default timezone identifier) into a TIME
 *			 (in UTC) with timezone info, considering the
 *			 source TIME in specified timezone
 *
 * Return: error code
 * time(in): decoded local time value (as appears in the user string)
 * tz_str(in): string containing timezone information (zone, daylight saving);
 *	       null-terminated, can be NULL
 * default_tz_region(in): default timezone region to apply if input string
 *		          does not contain a valid zone information
 * time_tz(out): object containing time value (adjusted to UTC) and
 *		 timezone info
 *
 */
int
tz_create_timetz (const DB_TIME * time, const char *tz_str,
		  const TZ_REGION * default_tz_region, DB_TIMETZ * time_tz,
		  const char **end_tz_str)
{
  int err_status = NO_ERROR;
  DB_DATETIME dt;
  DB_DATETIMETZ dt_tz;

  dt.date = tz_get_current_date ();
  dt.time = (*time) * 1000;

  err_status = tz_create_datetimetz (&dt, tz_str, default_tz_region, &dt_tz,
				     end_tz_str);
  if (err_status == NO_ERROR)
    {
      time_tz->time = dt_tz.datetime.time / 1000;
      time_tz->tz_id = dt_tz.tz_id;
    }

  return err_status;
}

/*
 * tz_create_timetz_ext  () - Takes a time in UTC and a timezone
 *			      and creates a time with timezone
 *			      information
 *
 * Return: error code
 * time (in): object containing source time value
 * timezone (in): timezone string
 * len_timezone (in): length of timezone string
 * time_tz (out): object containing time with timezone
 */
int
tz_create_timetz_ext (const DB_TIME * time,
		      const char *timezone,
		      int len_timezone, DB_TIMETZ * time_tz)
{
  int err_status = NO_ERROR;
  DB_DATETIME dt;
  DB_DATETIMETZ dt_tz;
  TZ_REGION region;

  dt.date = tz_get_current_date ();
  dt.time = (*time) * 1000;

  err_status = tz_str_to_region (timezone, len_timezone, &region);
  if (err_status != NO_ERROR)
    {
      return err_status;
    }
  err_status = tz_create_datetimetz (&dt, NULL, &region, &dt_tz, NULL);

  if (err_status == NO_ERROR)
    {
      time_tz->time = dt_tz.datetime.time / 1000;
      time_tz->tz_id = dt_tz.tz_id;
    }

  return err_status;
}

/*
 * tz_conv_tz_time_w_zone_name() - Converts the time_source from timezone
 *				   source zone into timezone dest_zone
 *				     
 *
 * Return: error code
 * time_source(in): object containing source time value
 * source_zone(in): source timezone string
 * len_source(in): length of source timezone string
 * dest_zone(in): destination timezone string
 * len_dest(in): length of destination timezone string
 * time_dest(out): object containing output time value
 */
int
tz_conv_tz_time_w_zone_name (const DB_TIME * time_source,
			     const char *source_zone, int len_source,
			     const char *dest_zone, int len_dest,
			     DB_TIME * time_dest)
{
  int err_status = NO_ERROR, tz_id1, tz_id2;
  DB_DATETIME dt, utc_dt, dest_dt;
  TZ_DECODE_INFO tz_info;

  tz_id1 = tz_get_zone_id_by_name (source_zone, len_source);
  tz_id2 = tz_get_zone_id_by_name (dest_zone, len_dest);

  if (tz_id1 == -1 || tz_id2 == -1)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TZ_INVALID_TIMEZONE, 0);
      return ER_TZ_INVALID_TIMEZONE;
    }

  dt.date = tz_get_current_date ();
  dt.time = (*time_source) * 1000;

  tz_info.type = TZ_REGION_ZONE;
  tz_info.zone.zone_id = tz_id1;
  tz_info.zone.dst_str[0] = '\0';

  err_status = tz_datetime_utc_conv (&dt, &tz_info, false, false, &utc_dt);
  if (err_status != NO_ERROR)
    {
      return err_status;
    }

  tz_info.zone.zone_id = tz_id2;
  err_status =
    tz_datetime_utc_conv (&utc_dt, &tz_info, true, false, &dest_dt);

  if (err_status != NO_ERROR)
    {
      return err_status;
    }
  *time_dest = dest_dt.time / 1000;

  return err_status;
}

/*
 * tz_create_timetz_from_ses () - creates a time with timezone from
 *		    a simple time using session timezone
 *
 * Return: error code
 * time(in): time value (as appears in the user string)
 * time_tz(out): object containing time value (adjusted to UTC) and
 *		 timezone info
 */
int
tz_create_timetz_from_ses (const DB_TIME * time, DB_TIMETZ * time_tz)
{
  int err_status = NO_ERROR;
  DB_DATETIME dt;
  DB_DATETIMETZ dt_tz;

  dt.date = tz_get_current_date ();
  dt.time = (*time) * 1000;

  err_status = tz_create_datetimetz_from_ses (&dt, &dt_tz);
  if (err_status != NO_ERROR)
    {
      goto exit;
    }
  time_tz->tz_id = dt_tz.tz_id;
  time_tz->time = dt_tz.datetime.time / 1000;

exit:
  return err_status;
}

/*
 * tz_utc_datetimetz_to_local () - 
 *
 * Return: error code
 * dt_utc(in): object containing datetime value (in UTC reference)
 * tz_id(in): TZ_ID encoded value
 * dt_local(out): object containing datetime value (adjusted to timezone
 *		  contained in tz_id)
 *
 */
int
tz_utc_datetimetz_to_local (const DB_DATETIME * dt_utc,
			    const TZ_ID * tz_id, DB_DATETIME * dt_local)
{
  int total_offset;
  int err_status = NO_ERROR;
  TZ_DECODE_INFO tz_info;

  tz_decode_tz_id (tz_id, true, &tz_info);

  if (tz_info.type == TZ_REGION_OFFSET)
    {
      total_offset = tz_info.offset;
    }
  else
    {
      assert (tz_info.zone.p_zone_off_rule != NULL);
      total_offset = tz_info.zone.p_zone_off_rule->gmt_off;

      if (tz_info.zone.p_zone_off_rule->ds_type == DS_TYPE_RULESET_ID
	  && tz_info.zone.p_ds_rule != NULL)
	{
	  total_offset += tz_info.zone.p_ds_rule->save_time;
	}
    }

  err_status = db_add_int_to_datetime ((DB_DATETIME *) dt_utc,
				       total_offset * 1000, dt_local);
  if (err_status != NO_ERROR)
    {
      goto exit;
    }
exit:
  return err_status;
}

/*
 * tz_datetimeltz_to_local () - 
 *
 * Return: error code
 * dt_ltz(in): object containing datetime value in UTC representing a datetime
 *	       in local timezone
 * dt_local(out): object containing datetime value (adjusted to session
 *		   timezone)
 *
 */
int
tz_datetimeltz_to_local (const DB_DATETIME * dt_ltz, DB_DATETIME * dt_local)
{
  int error = NO_ERROR;
  TZ_ID ses_tz_id;

  error = tz_create_session_tzid_for_datetime (dt_ltz, true, &ses_tz_id);
  if (error != NO_ERROR)
    {
      return error;
    }

  return tz_utc_datetimetz_to_local (dt_ltz, &ses_tz_id, dt_local);
}

/*
 * tz_utc_timetz_to_local () - 
 *
 * Return: error code
 * time_utc(in): object containing time value (in UTC reference)
 * tz_id(in): TZ_ID encoded value
 * time_local(out): object containing time value (adjusted to timezone
 *		    contained in tz_id)
 *
 */
int
tz_utc_timetz_to_local (const DB_TIME * time_utc, const TZ_ID * tz_id,
			DB_TIME * time_local)
{
  int err = NO_ERROR;
  DB_DATETIME dt_utc, dt_local;

  dt_utc.date = tz_get_current_date ();
  dt_utc.time = (*time_utc) * 1000;

  err = tz_utc_datetimetz_to_local (&dt_utc, tz_id, &dt_local);
  if (err != NO_ERROR)
    {
      return err;
    }
  *time_local = dt_local.time / 1000;

  return err;
}

/*
 * tz_timeltz_to_local () - 
 *
 * Return: error code
 * time_ltz(in): object containing time value (in UTC reference) representing
 *		 a time in session time zone
 * time_local(out): object containing time value adjusted to session timezone
 *
 */
int
tz_timeltz_to_local (const DB_TIME * time_ltz, DB_TIME * time_local)
{
  TZ_ID ses_tz_id;
  int error = NO_ERROR;

  error = tz_create_session_tzid_for_time (time_ltz, true, &ses_tz_id);

  if (error != NO_ERROR)
    {
      return NO_ERROR;
    }

  return tz_utc_timetz_to_local (time_ltz, &ses_tz_id, time_local);
}

/*
 * tz_zone_info_to_str () - Print a timezone decoded information into a string
 *
 * Return: print characters
 * tz_info(in): object containing timezone info
 * tz_str(in/out): buffer string to print in
 * tz_str_size(in): size of buffer
 *
 */
int
tz_zone_info_to_str (const TZ_DECODE_INFO * tz_info, char *tz_str,
		     const int tz_str_size)
{
  const TZ_DATA *tzd = tz_get_data ();
  int zone_name_size;
  int zone_id;
  int zone_offset_id;
  int dst_format_size;
  int total_len;
  TZ_TIMEZONE *timezone;
  TZ_OFFSET_RULE *zone_off_rule;
  char dst_format[TZ_MAX_FORMAT_SIZE];
  char *p_dst_format = NULL;

  if (tz_info->type == TZ_REGION_OFFSET)
    {
      int offset = tz_info->offset;
      int hour, min, sec, n;
      char sign;

      sign = (tz_info->offset < 0) ? '-' : '+';

      offset = (offset < 0) ? (-offset) : offset;
      assert (offset < 3600 * 24);

      db_time_decode ((DB_TIME *) & offset, &hour, &min, &sec);

      if (sec > 0)
	{
	  n = sprintf (tz_str, "%c%02d:%02d:%02d", sign, hour, min, sec);
	}
      else
	{
	  n = sprintf (tz_str, "%c%02d:%02d", sign, hour, min);
	}

      return n;
    }

  if (tzd == NULL)
    {
      return -1;
    }

  zone_id = tz_info->zone.zone_id;
  zone_offset_id = tz_info->zone.offset_id;
  zone_name_size = strlen (tzd->timezone_names[zone_id]);

  timezone = &(tzd->timezones[zone_id]);
  zone_off_rule = &(tzd->offset_rules[timezone->gmt_off_rule_start
				      + zone_offset_id]);

  p_dst_format = zone_off_rule->std_format;
  if (zone_off_rule->ds_type == DS_TYPE_RULESET_ID)
    {
      TZ_DS_RULESET *ds_ruleset;
      TZ_DS_RULE *ds_rule;
      int dst_id = tz_info->zone.dst_id;
      char *ds_abbr = NULL;

      ds_ruleset = &(tzd->ds_rulesets[zone_off_rule->ds_ruleset]);
      if (dst_id < ds_ruleset->count)
	{
	  ds_rule = &(tzd->ds_rules[dst_id + ds_ruleset->index_start]);
	  ds_abbr = ds_rule->letter_abbrev;
	}

      if (zone_off_rule->var_format != NULL)
	{
	  if (ds_abbr == NULL)
	    {
	      p_dst_format = NULL;
	    }
	  else
	    {
	      snprintf (dst_format, sizeof (dst_format) - 1,
			zone_off_rule->var_format,
			(ds_abbr != NULL && *ds_abbr != '-') ? ds_abbr : "");
	      p_dst_format = dst_format;
	    }
	}
      else
	{
	  if (ds_rule->save_time != 0)
	    {
	      p_dst_format = zone_off_rule->save_format;
	    }
	}
    }

  if (p_dst_format != NULL)
    {
      dst_format_size = strlen (p_dst_format);
    }
  else
    {
      dst_format_size = 0;
    }

  if (dst_format_size > 0)
    {
      total_len = dst_format_size + zone_name_size + 2;
    }
  else
    {
      total_len = zone_name_size + 1;
    }

  if (total_len > tz_str_size)
    {
      /* silent return */
      return -1;
    }

  if (p_dst_format != NULL)
    {
      snprintf (tz_str, tz_str_size, "%s %s", tzd->timezone_names[zone_id],
		p_dst_format);
    }
  else
    {
      snprintf (tz_str, tz_str_size, "%s", tzd->timezone_names[zone_id]);
    }

  return total_len - 1;
}

/*
 * tz_id_to_str () - Print a timezone compressed identifier into a string
 *
 * Return: printed characters
 * tz_id(in): complete valid zone identifier
 * tz_str(in): buffer string to print in
 * tz_str_size(in): size of buffer
 *
 */
int
tz_id_to_str (const TZ_ID * tz_id, char *tz_str, const int tz_str_size)
{
  TZ_DECODE_INFO tz_info;

  tz_decode_tz_id (tz_id, true, &tz_info);
  return tz_zone_info_to_str (&tz_info, tz_str, tz_str_size);
}

/*
 * tz_datetimetz_fix_zone () - Adjusts timezone identifier part of a
 *			       DATETIMETZ object so that offset and DST parts
 *			       are adjusted to new DATETIME
 *
 * Return: error code
 * src_dt_tz(in): datetime value (adjusted to UTC) and timezone identifier
 * dest_dt_tz(out): fixed DATETIMETZ value
 *
 *  Note : After an arithmetic operation (DATETIMETZ + number), the DATETIME
 *	   part is changed, but the TZ_ID part remains unchanged and is not
 *	   compatible. This functions adjusts the TZ_ID. If the TZ_ID is of
 *	   offset type, no adjustements are necessary. If TZ_ID is of
 *	   geographic type, only the zone idenfier is kept, while the offset
 *	   and daylight saving rule identifiers are changed according to new
 *	   date. This ensures that the output value is ready to be decoded.
 *
 */
int
tz_datetimetz_fix_zone (const DB_DATETIMETZ * src_dt_tz,
			DB_DATETIMETZ * dest_dt_tz)
{
  int er_status = NO_ERROR;
  TZ_DECODE_INFO tz_info;

  tz_decode_tz_id (&(src_dt_tz->tz_id), false, &tz_info);

  er_status = tz_datetime_utc_conv (&(src_dt_tz->datetime), &tz_info,
				    true, true, &(dest_dt_tz->datetime));
  if (er_status != NO_ERROR)
    {
      return er_status;
    }

  tz_encode_tz_id (&tz_info, &(dest_dt_tz->tz_id));

  return er_status;
}

/*
 * tz_timestamptz_fix_zone () - Adjusts timezone identifier part of a
 *			       DATETIMETZ object so that offset and DST parts
 *			       are adjusted to new DATETIME
 *
 * Return: error code
 * src_ts_tz(in): timestamp value and timezone identifier
 * dest_ts_tz(out): fixed TIMESTAMPTZ value
 *
 */
int
tz_timestamptz_fix_zone (const DB_TIMESTAMPTZ * src_ts_tz,
			 DB_TIMESTAMPTZ * dest_ts_tz)
{
  int er_status = NO_ERROR;
  DB_DATETIMETZ src_dt_tz, dest_dt_tz;
  DB_DATE date;
  DB_TIME time;

  db_timestamp_decode_utc (&src_ts_tz->timestamp, &date, &time);
  src_dt_tz.datetime.date = date;
  src_dt_tz.datetime.time = time * 1000;
  src_dt_tz.tz_id = src_ts_tz->tz_id;

  er_status = tz_datetimetz_fix_zone (&src_dt_tz, &dest_dt_tz);
  if (er_status != NO_ERROR)
    {
      return er_status;
    }

  date = dest_dt_tz.datetime.date;
  time = dest_dt_tz.datetime.time / 1000;

  dest_ts_tz->tz_id = dest_dt_tz.tz_id;
  er_status = db_timestamp_encode_utc (&date, &time, &dest_ts_tz->timestamp);

  return er_status;
}

/*
 * tz_timetz_fix_zone () - Adjusts timezone identifier part of a TIMETZ object
 *			 so that offset and DST parts are adjusted to new TIME
 *
 * Return: error code
 * src_time_tz(in): time value and timezone identifier
 * dest_time_tz(out): fixed TIMETZ value
 *
 */
int
tz_timetz_fix_zone (const DB_TIMETZ * src_time_tz, DB_TIMETZ * dest_time_tz)
{
  int er_status = NO_ERROR;
  DB_DATETIMETZ src_dt_tz, dest_dt_tz;
  DB_TIME time;

  src_dt_tz.datetime.date = tz_get_current_date ();
  src_dt_tz.datetime.time = src_time_tz->time * 1000;
  src_dt_tz.tz_id = src_time_tz->tz_id;

  er_status = tz_datetimetz_fix_zone (&src_dt_tz, &dest_dt_tz);
  if (er_status != NO_ERROR)
    {
      return er_status;
    }

  time = dest_dt_tz.datetime.time / 1000;

  dest_time_tz->tz_id = dest_dt_tz.tz_id;
  dest_time_tz->time = time;

  return er_status;
}

/*
 * Utility functions
 */

/*
 * tz_encode_tz_id () - Encodes a timezone decoded information into a
 *			compressed timezone identifier (ready for storage)
 *
 * Return: none
 * tz_info(in): object containing decoded timezone info
 * tz_id(out): compressed timezone identifier
 *
 *  FFZZZZZZ ZZZZZZZZ OOOOOOOO DDDDDDDD
 *  F = 01 : TZ_ID is number representing positive offset in seconds
 *  F = 10 : TZ_ID is number representing negative offset in seconds
 *  F = 00 : TZ_ID is encoded as ZOD
 *  F = 11 : not used
 *  Z = geographical zone id
 *  O = GMT offset sub-rule id
 *  D = DST sub-rule id
 */
static void
tz_encode_tz_id (const TZ_DECODE_INFO * tz_info, TZ_ID * tz_id)
{
  if (tz_info->type == TZ_REGION_OFFSET)
    {
      int offset = (tz_info->offset < 0)
	? (-tz_info->offset) : tz_info->offset;

      offset = offset & 0x3fffffff;

      if (tz_info->offset < 0)
	{
	  *tz_id = offset | (0x2 << 30);
	}
      else
	{
	  *tz_id = offset | (0x1 << 30);
	}
    }
  else
    {
      int zone_id = tz_info->zone.zone_id;
      int offset_id = tz_info->zone.offset_id;
      int dst_id = tz_info->zone.dst_id;

      zone_id = zone_id & TZ_ZONE_ID_MAX;
      offset_id = offset_id & TZ_OFFSET_ID_MAX;
      dst_id = dst_id & TZ_DS_ID_MAX;

      *tz_id = dst_id | (offset_id << 8) | (zone_id) << 16;
    }
}

/*
 * tz_encode_tz_region () - encodes a partial timezone decoding struct into
 *			    a timezone region
 *
 * Return: none
 * tz_info(in): object containing decoded timezone info
 * tz_region(out): time zone region
 */
static void
tz_encode_tz_region (const TZ_DECODE_INFO * tz_info, TZ_REGION * tz_region)
{
  tz_region->type = tz_info->type;
  if (tz_info->type == TZ_REGION_OFFSET)
    {
      tz_region->offset = tz_info->offset;
    }
  else
    {
      assert (tz_info->zone.zone_id < TZ_ZONE_ID_MAX);
      tz_region->zone_id = tz_info->zone.zone_id;
    }
}

/*
 * tz_decode_tz_id () - Decodes a timezone compressed identifier into a
 *			structure
 *
 * Return: none
 * tz_id(in): full
 * is_full_decode(in): true if full decoding should be performed; otherwise
 *		       only zone identifier is decoded for a geographical
 *		       TZ_ID
 * tz_info(out): object containing decoded timezone info
 *
 */
static void
tz_decode_tz_id (const TZ_ID * tz_id, const bool is_full_decode,
		 TZ_DECODE_INFO * tz_info)
{
  unsigned int val = (unsigned int) *tz_id;
  unsigned int flag = (val & 0xc0000000) >> 30;

  memset (tz_info, 0, sizeof (tz_info[0]));

  if (flag == 0)
    {
      tz_info->zone.zone_id = (val & (TZ_ZONE_ID_MAX << 16)) >> 16;
      tz_info->zone.offset_id = (val & (TZ_OFFSET_ID_MAX << 8)) >> 8;
      tz_info->zone.dst_id = val & TZ_DS_ID_MAX;
      tz_info->type = TZ_REGION_ZONE;

      assert (tz_info->zone.zone_id < TZ_ZONE_ID_MAX);

      if (is_full_decode)
	{
	  int zone_id;
	  int zone_offset_id;
	  const TZ_DATA *tzd = tz_get_data ();
	  TZ_TIMEZONE *timezone;
	  TZ_OFFSET_RULE *zone_off_rule;

	  if (tzd == NULL)
	    {
	      tz_info->type = TZ_REGION_OFFSET;
	      tz_info->offset = 0;
	      return;
	    }

	  zone_id = tz_info->zone.zone_id;
	  zone_offset_id = tz_info->zone.offset_id;

	  assert (zone_offset_id >= 0 && zone_offset_id < TZ_OFFSET_ID_MAX);

	  timezone = &(tzd->timezones[tz_info->zone.zone_id]);
	  tz_info->zone.p_timezone = timezone;

	  zone_off_rule = &(tzd->offset_rules[timezone->gmt_off_rule_start
					      + zone_offset_id]);
	  tz_info->zone.p_zone_off_rule = zone_off_rule;

	  if (zone_off_rule->ds_type == DS_TYPE_RULESET_ID)
	    {
	      TZ_DS_RULESET *ds_ruleset;
	      TZ_DS_RULE *ds_rule;
	      int dst_id;
	      char *ds_abbr = NULL;

	      ds_ruleset = &(tzd->ds_rulesets[zone_off_rule->ds_ruleset]);
	      dst_id = tz_info->zone.dst_id;

	      /* we may not have a DST rule, if Daylight Saving does not apply
	       */
	      if (dst_id != TZ_DS_ID_MAX)
		{
		  assert (dst_id >= 0 && dst_id < ds_ruleset->count);
		  ds_rule =
		    &(tzd->ds_rules[dst_id + ds_ruleset->index_start]);
		  ds_abbr = ds_rule->letter_abbrev;

		  tz_info->zone.p_ds_rule = ds_rule;

		  if (zone_off_rule->var_format != NULL)
		    {
		      snprintf (tz_info->zone.dst_str,
				sizeof (tz_info->zone.dst_str) - 1,
				zone_off_rule->var_format,
				(ds_abbr != NULL && *ds_abbr != '-')
				? ds_abbr : "");
		    }
		}
	    }
	}
    }
  else
    {
      tz_info->type = TZ_REGION_OFFSET;
      if (flag == 0x2)
	{
	  /* negative offset */
	  tz_info->offset = -(int) (val & 0x3fffffff);
	}
      else
	{
	  /* positive offset  */
	  assert (flag == 0x1);
	  tz_info->offset = val & 0x3fffffff;
	}
    }
}

/*
 * tz_decode_tz_region () - Decodes a timezone region structure into a helper
 *			    timezone decoder structure
 *
 * Return: none
 * tz_region(in): time zone region
 * tz_info(out): object containing partial decoded timezone info
 *
 */
static void
tz_decode_tz_region (const TZ_REGION * tz_region, TZ_DECODE_INFO * tz_info)
{
  tz_info->type = tz_region->type;

  if (tz_region->type == TZ_REGION_OFFSET)
    {
      tz_info->offset = tz_region->offset;
    }
  else
    {
      tz_info->zone.zone_id = tz_region->zone_id;
    }

  tz_info->zone.offset_id = TZ_OFFSET_ID_MAX;
  tz_info->zone.dst_id = TZ_DS_ID_MAX;
  tz_info->zone.p_timezone = NULL;
  tz_info->zone.p_zone_off_rule = NULL;
  tz_info->zone.p_ds_rule = NULL;
  tz_info->zone.dst_str[0] = '\0';
}

/*
 * tz_get_first_weekday_around_date () - find the day of month when a specific
 *					 weekday occurs
 * Returns: -1 if error; otherwise, day of month (0 to 27/28/29/30, depending
 *	    on the input month.
 *
 * year(in):
 * month(in): month (0-11)
 * weekday(in): weekday ( 0 = Sunday through 6 = Saturday)
 * ref_day(in): reference day of month (0 based)
 *
 */
int
tz_get_first_weekday_around_date (const int year, const int month,
				  const int weekday, const int ref_day,
				  const bool before)
{
  int first_weekday = -1;
  int wday = -1;

  assert (year >= 1900);

  wday = db_get_day_of_week (year, month + 1, ref_day + 1);
  first_weekday = ref_day;

  while (wday != weekday)
    {
      if (before == true)
	{
	  wday = (wday == TZ_WEEK_DAY_SUN) ? TZ_WEEK_DAY_SAT : (wday - 1);
	  first_weekday--;
	  assert (first_weekday >= 0);
	}
      else
	{
	  wday = (wday + 1) % TZ_WEEK_DAY_COUNT;
	  first_weekday++;

	  assert (first_weekday < ((month == TZ_MON_FEB)
				   ? (((IS_LEAP_YEAR (year)) ? 29 : 28)) :
				   DAYS_IN_MONTH (month)));
	}
    }

  return first_weekday;
}

/*
 * tz_str_read_number() - attempts to read an integer value from a string
 *
 * Returns: error code
 * str(in): string to parse
 * strict(in): true, if no trailing characters allowed
 * read_sign(in): true, if shoud read leading sign
 * val(out): the integer found in the input string
 * str_next(out): reference to the first character after the read integer.
 *		  If no value was read, str_next will reference str.
 */
int
tz_str_read_number (const char *str, const bool strict, const bool read_sign,
		    int *val, const char **str_next)
{
  int cur_val = 0;
  const char *str_cursor;
  bool is_negative = false;

  assert (!IS_EMPTY_STR (str));

  str_cursor = str;

  if (read_sign == true)
    {
      if (*str_cursor == '-')
	{
	  is_negative = true;
	  str_cursor++;
	}
      else if (*str_cursor == '+')
	{
	  str_cursor++;
	}
    }

  while (char_isdigit (*str_cursor))
    {
      cur_val = cur_val * 10 + (*str_cursor - '0');
      str_cursor++;
    }

  *val = is_negative ? -cur_val : cur_val;

  *str_next = (char *) str_cursor;

  if (strict && str_cursor == str)
    {
      return ER_FAILED;
    }

  return NO_ERROR;
}

/*
 * tz_str_read_time() - read a time value from an input string. Input time can
 *			be hh, hh:mm or hh:mm:ss (s or u)
 *
 * Returns: ER_FAILED (message error is not set) or NO_ERROR
 * str(in): string to parse
 * need_minutes(in): true if it is mandatory to read minutes part, otherwise
 *		     hour specifier is enough
 * allow_sec60(in): true if 60 is allowed for the value of seconds,
 *		    false otherwise
 * hour(out): parsed value for hour
 * min(out) : parsed value for minutes
 * sec(out) : parsed value for seconds
 * str_next(out): pointer to the char after the parsed time value
 */
int
tz_str_read_time (const char *str, bool need_minutes, bool allow_sec60,
		  int *hour, int *min, int *sec, const char **str_next)
{
  const char *str_cursor;
  int val_read = 0;

  assert (!IS_EMPTY_STR (str));

  *hour = *min = *sec = 0;
  str_cursor = str;

  if (tz_str_read_number (str_cursor, true, false, &val_read, str_next) !=
      NO_ERROR)
    {
      return ER_FAILED;
    }
  if (val_read < 0 || val_read > 24)
    {
      return ER_FAILED;
    }
  *hour = val_read;

  str_cursor = *str_next;
  if (*str_cursor != ':')
    {
      if (!need_minutes && *str_cursor == '\0')
	{
	  return NO_ERROR;
	}
      /* invalid text representation for time */
      return ER_FAILED;
    }
  str_cursor++;
  if (IS_EMPTY_STR (str_cursor))
    {
      /* missing minute token */
      return ER_FAILED;
    }
  if (tz_str_read_number (str_cursor, true, false, &val_read, str_next) !=
      NO_ERROR)
    {
      return ER_FAILED;
    }
  if (val_read < 0 || val_read > 60 || (val_read > 0 && *hour == 24))
    {
      return ER_FAILED;
    }
  *min = val_read;

  str_cursor = *str_next;
  if (*str_cursor == ':')
    {
      /* if there is a token for seconds, read it */
      str_cursor++;
      if (tz_str_read_number (str_cursor, true, false, &val_read, str_next) !=
	  NO_ERROR)
	{
	  return ER_FAILED;
	}
      if (val_read < 0 || val_read > (allow_sec60 ? 61 : 60))
	{
	  return ER_FAILED;
	}
      *sec = val_read;
    }

  return NO_ERROR;
}

/*
 * tz_str_to_seconds() - parses a string representing a signed time offset
 *
 * Returns: error code
 * str(in): string to parse
 * seconds(out): the signed number of seconds that the string represents
 * str_next(out): pointer to the first char after the parsed time
 */
int
tz_str_to_seconds (const char *str, int *seconds, const char **str_next)
{
  int err_status = NO_ERROR;
  int pos = -1;
  int result = 0;
  int cur_num = 0;
  int token_count = 1;		/* will be 2 if hh:mm, 3 if hh:mm:ss */
  const char *str_cursor = NULL;
  int hour = 0, min = 0, sec = 0;
  bool is_negative = false;

  assert (str != NULL);

  str_cursor = str;
  if (*str_cursor == '-')
    {
      is_negative = true;
      str_cursor++;
    }
  if (*str_cursor == '+')
    {
      str_cursor++;
    }

  err_status =
    tz_str_read_time (str_cursor, false, false, &hour, &min, &sec, str_next);
  if (err_status != NO_ERROR)
    {
      return err_status;
    }

  result = sec + min * 60 + hour * 3600;

  *seconds = is_negative ? -result : result;
  return err_status;
}

/*
 * tz_get_ds_change_julian_date () - Computes the exact date when a daylight
 *				     saving rule applies in year
 *
 * Returns: error code
 * ds_rule(in): daylight saving rule
 * year(in): current year to appply rule
 * ds_rule_julian_date(out): julian date
 *
 */
int
tz_get_ds_change_julian_date (const TZ_DS_RULE * ds_rule, const int year,
			      int *ds_rule_julian_date)
{
  int ds_rule_day;
  int ds_rule_month = ds_rule->in_month;

  /* get exact julian date/time for this rule in year 'local_year' */
  if (ds_rule->change_on.type == TZ_DS_TYPE_FIXED)
    {
      ds_rule_day = ds_rule->change_on.day_of_month;
    }
  else
    {
      int ds_rule_weekday, day_month_bound;
      bool before = (ds_rule->change_on.type
		     == TZ_DS_TYPE_VAR_SMALLER) ? true : false;

      ds_rule_weekday = ds_rule->change_on.day_of_week;
      day_month_bound = ds_rule->change_on.day_of_month;

      ds_rule_day = tz_get_first_weekday_around_date (year,
						      ds_rule_month,
						      ds_rule_weekday,
						      day_month_bound,
						      before);

      if (ds_rule_day == -1)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TZ_INTERNAL_ERROR, 0);
	  return ER_TZ_INTERNAL_ERROR;
	}
    }

  *ds_rule_julian_date = julian_encode (1 + ds_rule_month,
					1 + ds_rule_day, year);

  return NO_ERROR;
}

/*
 * tz_fast_find_ds_rule () - Performs a search to find the daylight saving
 *			     rule for which a certain date applies to
 *
 * Returns: error code
 * tzd(in): Daylight saving data context
 * ds_ruleset(in): set of daylight saving rules
 * src_julian_date(in): julian date for which we search a rule
 * src_year(in): year of date
 * ds_rule_id(out): fount rule
 */
static int
tz_fast_find_ds_rule (const TZ_DATA * tzd, const TZ_DS_RULESET * ds_ruleset,
		      const int src_julian_date, int src_year,
		      int *ds_rule_id)
{
  int curr_ds_id;
  int er_status = NO_ERROR;
  TZ_DS_RULE *curr_ds_rule;
  full_date_t smallest_date_diff = -1;

  *ds_rule_id = -1;

  for (curr_ds_id = 0; curr_ds_id < ds_ruleset->count; curr_ds_id++)
    {
      int ds_rule_julian_date;
      int curr_time_offset = 0;
      full_date_t date_diff;
      bool rule_matched = false;

      assert (curr_ds_id + ds_ruleset->index_start < tzd->ds_rule_count);
      curr_ds_rule = &(tzd->ds_rules[curr_ds_id + ds_ruleset->index_start]);

      if (src_year + 1 < curr_ds_rule->from_year)
	{
	  /* no more rules will match */
	  break;
	}

      if (src_year - 1 > curr_ds_rule->to_year
	  || (src_year < curr_ds_rule->from_year
	      && curr_ds_rule->in_month > TZ_MON_JAN)
	  || (src_year > curr_ds_rule->to_year
	      && curr_ds_rule->in_month < TZ_MON_DEC))
	{
	  /* this rule cannot apply */
	  continue;
	}

      er_status = tz_get_ds_change_julian_date (curr_ds_rule, src_year,
						&ds_rule_julian_date);
      if (er_status != NO_ERROR)
	{
	  goto exit;
	}

      if (ds_rule_julian_date == -1)
	{
	  /* not found a rule for this year, search for precedeeing year */
	  er_status = tz_get_ds_change_julian_date (curr_ds_rule,
						    src_year - 1,
						    &ds_rule_julian_date);
	  if (er_status != NO_ERROR)
	    {
	      goto exit;
	    }
	  if (ds_rule_julian_date == -1)
	    {
	      er_status = ER_TZ_INTERNAL_ERROR;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, er_status, 0);
	      goto exit;
	    }
	}

      date_diff = FULL_DATE (src_julian_date, 0)
	- FULL_DATE (ds_rule_julian_date, 0);
      if (date_diff >= DATE_DIFF_MATCH_SAFE_THRESHOLD_SEC
	  && (smallest_date_diff == -1 || date_diff < smallest_date_diff))
	{
	  /* a date difference of at least two days */
	  *ds_rule_id = curr_ds_id;
	  smallest_date_diff = date_diff;
	}
    }

exit:
  return er_status;
}

/*
 * tz_check_ds_match_string () - Checks if user supplied daylight saving
 *				 string specifier matches the DS rule
 *
 * Returns: true if the DS string matches with the selected offset and DS rule
 * off_rule(in): Offset rule
 * ds_rule(in): daylight saving rule
 * ds_string(in): daylight saving specifier (user source)
 */
static bool
tz_check_ds_match_string (const TZ_OFFSET_RULE * off_rule,
			  const TZ_DS_RULE * ds_rule, const char *ds_string)
{
  bool rule_matched = true;

  if (off_rule->var_format != NULL)
    {
      char rule_dst_format[TZ_MAX_FORMAT_SIZE];

      if (ds_rule != NULL && ds_rule->letter_abbrev != NULL
	  && *ds_rule->letter_abbrev != '-')
	{
	  snprintf (rule_dst_format, sizeof (rule_dst_format) - 1,
		    off_rule->var_format, ds_rule->letter_abbrev);
	}
      else
	{
	  snprintf (rule_dst_format, sizeof (rule_dst_format) - 1,
		    off_rule->var_format, "");
	}

      if (strcasecmp (rule_dst_format, ds_string) != 0)
	{
	  /* not maching with variable format */
	  rule_matched = false;
	}
    }
  else if (off_rule->save_format != NULL
	   && (ds_rule->save_time == 0
	       || strcasecmp (off_rule->save_format, ds_string) != 0))
    {
      /* not mathcing with DST format */
      rule_matched = false;
    }
  else if (off_rule->std_format != NULL
	   && ((ds_rule->save_time != 0
		&& off_rule->save_format == NULL)
	       || strcasecmp (off_rule->std_format, ds_string) != 0))
    {
      /* not mathcing with standard format */
      rule_matched = false;
    }

  return rule_matched;
}

/*
 * tz_datetime_utc_conv () - 
 *
 * Return: error code
 * src_dt(in): object containing source datetime value;
 *	       if 'src_is_utc' is true, than is UTC datetime, otherwise
 *	       is local datetime
 * tz_info(in/out): (partial) decoded timezone info associated with source
 *		    datetime, additional information is change/added after
 *		    conversion
 * src_is_utc(in): true if 'src_dt' is in UTC time reference, false if
 *		   src_dt is in local time reference
 * only_tz_adjust(in): true if only timezone adjustment is desired,
 *		       datetime itself is not changed (used in context of
 *		       datetime arithmetic)
 * dt_dest(out): object containing destination datetime
 *
 */
static int
tz_datetime_utc_conv (const DB_DATETIME * src_dt, TZ_DECODE_INFO * tz_info,
		      bool src_is_utc, bool only_tz_adjust,
		      DB_DATETIME * dest_dt)
{
  int src_julian_date, rule_julian_date;
  int src_time_sec, rule_time_sec;
  int src_year;
  int gmt_std_offset_sec;
  int total_offset_sec;
  int err_status = NO_ERROR;
  int curr_offset_id = -1;
  int curr_ds_id = -1, applying_ds_id = -1;
  full_date_t applying_date_diff = -1;
  TZ_TIMEZONE *timezone;
  const TZ_DATA *tzd;
  TZ_OFFSET_RULE *curr_off_rule = NULL, *next_off_rule = NULL;
  TZ_DS_RULESET *ds_ruleset;
  TZ_DS_RULE *curr_ds_rule, *applying_ds_rule;
  bool check_user_dst = false;
  bool applying_with_prev_year = false;
  bool applying_is_in_leap_interval = false;

  if (tz_info->type == TZ_REGION_OFFSET)
    {
      /* keep offset and zone decode info */
      total_offset_sec = tz_info->offset;

      goto exit;
    }

  assert (tz_info->type == TZ_REGION_ZONE);

  tzd = tz_get_data ();

  if (tzd == NULL)
    {
      err_status = ER_TZ_INTERNAL_ERROR;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, err_status, 0);
      goto exit;
    }

  /* start decoding zone , GMT offset id, DST id */
  assert ((int) tz_info->zone.zone_id < tzd->timezone_count);
  timezone = &(tzd->timezones[tz_info->zone.zone_id]);

  assert (timezone->gmt_off_rule_count > 0);

  src_julian_date = src_dt->date;
  src_time_sec = src_dt->time / 1000;

  for (curr_offset_id = 0; curr_offset_id < timezone->gmt_off_rule_count;
       curr_offset_id++)
    {
      assert (timezone->gmt_off_rule_start + curr_offset_id
	      < tzd->offset_rule_count);
      curr_off_rule =
	&(tzd->offset_rules[timezone->gmt_off_rule_start + curr_offset_id]);

      if (curr_off_rule->until_flag == UNTIL_EXPLICIT)
	{
	  rule_julian_date = curr_off_rule->julian_date;
	  rule_time_sec = (curr_off_rule->until_hour * 60
			   + curr_off_rule->until_min)
	    * 60 + curr_off_rule->until_sec;
	}
      else
	{
	  rule_julian_date = TZ_MAX_JULIAN_DATE;
	  rule_time_sec = 0;
	}

      /* we don't take into account locale time here, we do that
       * later, when we consider DST,
       * we add a safety buffer (1 julian day) to account of threshold local
       * dates */
      if (src_julian_date < rule_julian_date - 1)
	{
	  /* this is a candidate, we still have to check the exact time when
	   * rule ends */
	  if (curr_off_rule->until_flag == UNTIL_EXPLICIT)
	    {
	      /* set also next */
	      assert (curr_offset_id < timezone->gmt_off_rule_count - 1);
	      assert (timezone->gmt_off_rule_start + curr_offset_id + 1
		      < tzd->offset_rule_count);
	      next_off_rule =
		&(tzd->offset_rules[timezone->gmt_off_rule_start
				    + curr_offset_id + 1]);
	    }
	  /* rule found */
	  break;
	}
    }

  assert (curr_off_rule != NULL);
  assert (next_off_rule != NULL
	  || curr_off_rule->until_flag == UNTIL_INFINITE);

  julian_decode (src_julian_date, NULL, NULL, &src_year, NULL);

detect_dst:
  if (curr_off_rule->until_flag == UNTIL_EXPLICIT)
    {
      rule_julian_date = curr_off_rule->julian_date;
      rule_time_sec = (curr_off_rule->until_hour * 60
		       + curr_off_rule->until_min)
	* 60 + curr_off_rule->until_sec;
    }
  else
    {
      rule_julian_date = TZ_MAX_JULIAN_DATE;
      rule_time_sec = 0;
    }

  gmt_std_offset_sec = curr_off_rule->gmt_off;
  total_offset_sec = gmt_std_offset_sec;

  if (curr_off_rule->ds_type == DS_TYPE_FIXED)
    {
      int curr_time_offset = 0;
      /* TODO : (do we need code for types : wall, standard ?) */
      if (curr_off_rule->until_time_type == TZ_TIME_TYPE_UTC)
	{
	  curr_time_offset += gmt_std_offset_sec;
	}

      if (FULL_DATE (src_julian_date, src_time_sec)
	  <= FULL_DATE (rule_julian_date, rule_time_sec
			+ TIME_OFFSET (src_is_utc, curr_time_offset)))
	{
	  if (src_is_utc == false
	      && tz_info->zone.dst_str[0] != '\0'
	      && curr_off_rule->var_format == NULL
	      && (curr_off_rule->std_format == NULL
		  || strcasecmp (curr_off_rule->std_format,
				 tz_info->zone.dst_str) != 0)
	      && (curr_off_rule->save_format == NULL
		  || strcasecmp (curr_off_rule->save_format,
				 tz_info->zone.dst_str) != 0))
	    {
	      err_status = ER_TZ_DST_NOT_SUPPORTED;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, err_status, 0);
	      goto exit;
	    }

	  applying_ds_id = 0;
	  /* finished */
	  goto exit;
	}

      /* try next GMT offset zone */
      if (next_off_rule == NULL)
	{
	  err_status = ER_TZ_INVALID_COMBINATION;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, err_status, 0);
	  goto exit;
	}
      curr_off_rule = next_off_rule;
      next_off_rule = NULL;

      goto detect_dst;
    }

  /* the zone uses DST rules */
  assert (curr_off_rule->ds_type == DS_TYPE_RULESET_ID);
  assert (curr_off_rule->ds_ruleset < tzd->ds_ruleset_count);
  ds_ruleset = &(tzd->ds_rulesets[curr_off_rule->ds_ruleset]);

  applying_ds_rule = NULL;
  if (src_is_utc == false && tz_info->zone.dst_str[0] != '\0')
    {
      check_user_dst = true;
    }

  for (curr_ds_id = 0; curr_ds_id < ds_ruleset->count; curr_ds_id++)
    {
      int ds_rule_julian_date;
      full_date_t date_diff;
      bool rule_matched = false;
      bool is_in_leap_interval = false;
      bool check_prev_year = true;

      assert (curr_ds_id + ds_ruleset->index_start < tzd->ds_rule_count);
      curr_ds_rule = &(tzd->ds_rules[curr_ds_id + ds_ruleset->index_start]);

      if (src_year + 1 < curr_ds_rule->from_year)
	{
	  /* no more rules will match */
	  break;
	}

      if (src_year - 1 > curr_ds_rule->to_year
	  || (src_year < curr_ds_rule->from_year
	      && curr_ds_rule->in_month > TZ_MON_JAN)
	  || (src_year > curr_ds_rule->to_year
	      && curr_ds_rule->in_month < TZ_MON_DEC))
	{
	  /* this rule cannot apply */
	  continue;
	}

      err_status = tz_get_ds_change_julian_date (curr_ds_rule, src_year,
						 &ds_rule_julian_date);
      if (err_status != NO_ERROR)
	{
	  goto exit;
	}

      date_diff = FULL_DATE (src_julian_date, 0)
	- FULL_DATE (ds_rule_julian_date, 0);
      if (date_diff >= DATE_DIFF_MATCH_SAFE_THRESHOLD_SEC)
	{
	  /* a date difference of at two days */
	  rule_matched = true;
	}
      else if (ABS (date_diff) < DATE_DIFF_MATCH_SAFE_THRESHOLD_SEC)
	{
	  int wall_ds_rule_id;
	  int wall_safe_julian_date;
	  TZ_DS_RULE *wall_ds_rule;
	  full_date_t leap_interval;
	  int ds_time_offset = 0;

	  /* there may be an ambiguity :
	   * check the time deviation of a date before current candidate
	   * DS rule applies (to be safe, 4 days before) */
	  wall_safe_julian_date = src_julian_date
	    - 2 * DATE_DIFF_MATCH_SAFE_THRESHOLD_DAYS;

	  err_status = tz_fast_find_ds_rule (tzd, ds_ruleset,
					     wall_safe_julian_date, src_year,
					     &wall_ds_rule_id);
	  if (err_status != NO_ERROR)
	    {
	      goto exit;
	    }

	  assert (wall_ds_rule_id + ds_ruleset->index_start
		  < tzd->ds_rule_count);
	  wall_ds_rule = &(tzd->ds_rules[wall_ds_rule_id
					 + ds_ruleset->index_start]);

	  /* take time into account */
	  if (curr_ds_rule->at_time_type == TZ_TIME_TYPE_UTC)
	    {
	      ds_time_offset = gmt_std_offset_sec;
	    }
	  else if (curr_ds_rule->at_time_type == TZ_TIME_TYPE_LOCAL_STD)
	    {
	      /* TODO : implement code for local standard time with zero offset 
	       */
	      ds_time_offset = 0;
	    }
	  else if (curr_ds_rule->at_time_type == TZ_TIME_TYPE_LOCAL_WALL)
	    {
	      /* wall clock: may indicate either the daylight time or 
	       * standard time */
	      ds_time_offset = -wall_ds_rule->save_time;
	    }

	  leap_interval = wall_ds_rule->save_time - curr_ds_rule->save_time;

	  date_diff = FULL_DATE (src_julian_date, src_time_sec)
	    - FULL_DATE (ds_rule_julian_date,
			 +TIME_OFFSET (src_is_utc,
				       ds_time_offset +
				       curr_ds_rule->at_time));
	  if (date_diff >= 0)
	    {
	      if (date_diff <= ABS (leap_interval))
		{
		  is_in_leap_interval = true;
		}

	      if (is_in_leap_interval && leap_interval < 0)
		{
		  /* invalid time, abort */
		  err_status = ER_TZ_DURING_DS_LEAP;
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, err_status, 0);
		  goto exit;
		}
	      rule_matched = true;
	    }
	}

    match_ds_rule:
      if (rule_matched)
	{
	  assert (date_diff >= 0);

	  if (applying_date_diff > 0 && date_diff > applying_date_diff
	      && !applying_is_in_leap_interval)
	    {
	      /* a better rule was previously found */
	      continue;
	    }

	  if (is_in_leap_interval || applying_is_in_leap_interval)
	    {
	      if (check_user_dst)
		{
		  rule_matched =
		    tz_check_ds_match_string (curr_off_rule, curr_ds_rule,
					      tz_info->zone.dst_str);
		}
	      else if (applying_ds_id != -1)
		{
		  /* ambiguity : user did not specified a daylight saving;
		   * a previous rule was matched, but also the current one is;
		   * We are during the DS interval change, we choose the one
		   * before (timewise) DS change is performed */
		  if (applying_ds_rule->in_month < curr_ds_rule->in_month
		      || applying_with_prev_year)
		    {
		      rule_matched = false;
		    }
		}
	    }

	  if (rule_matched
	      && (applying_date_diff < 0 || date_diff < applying_date_diff
		  || applying_is_in_leap_interval))
	    {
	      /* this is the best rule so far (ignoring user DST) */
	      applying_date_diff = date_diff;
	      applying_ds_rule = curr_ds_rule;
	      applying_ds_id = curr_ds_id;
	      applying_is_in_leap_interval = is_in_leap_interval;
	      if (check_prev_year == false)
		{
		  applying_with_prev_year = true;
		}
	    }
	}
      else if (curr_ds_rule->from_year < src_year
	       && check_prev_year == true && date_diff < 0)
	{
	  err_status = tz_get_ds_change_julian_date (curr_ds_rule,
						     src_year - 1,
						     &ds_rule_julian_date);
	  if (err_status != NO_ERROR)
	    {
	      goto exit;
	    }

	  date_diff = FULL_DATE (src_julian_date, 0)
	    - FULL_DATE (ds_rule_julian_date, 0);

	  assert (date_diff >= 0);
	  rule_matched = true;
	  check_prev_year = false;

	  goto match_ds_rule;
	}
    }

  if (applying_ds_id != -1)
    {
      if (check_user_dst)
	{
	  if (tz_check_ds_match_string (curr_off_rule, applying_ds_rule,
					tz_info->zone.dst_str) == false)
	    {
	      err_status = ER_TZ_INVALID_COMBINATION;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, err_status, 0);
	      goto exit;
	    }
	}
    }
  else
    {
      assert (applying_ds_id == -1);
      /* try next GMT offset zone */
      if (next_off_rule == NULL)
	{
	  /* check if provided DS specifier matches the offset rule format */
	  if (tz_info->zone.dst_str[0] != '\0'
	      && curr_off_rule->var_format != NULL
	      && tz_check_ds_match_string (curr_off_rule, NULL,
					   tz_info->zone.dst_str) == false)
	    {
	      err_status = ER_TZ_INVALID_DST;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, err_status, 0);
	      goto exit;
	    }
	  applying_ds_id = TZ_DS_ID_MAX;
	  goto exit;
	}
      curr_off_rule = next_off_rule;
      next_off_rule = NULL;
      goto detect_dst;
    }

  assert (applying_ds_rule != NULL);

  total_offset_sec += applying_ds_rule->save_time;

  assert (curr_offset_id != -1);
  assert (applying_ds_id != -1);

exit:
  if (err_status == NO_ERROR)
    {
      if (curr_offset_id >= 0)
	{
	  tz_info->zone.offset_id = curr_offset_id;

	  if (applying_ds_id >= 0)
	    {
	      tz_info->zone.dst_id = applying_ds_id;
	    }
	}

      if (only_tz_adjust == true)
	{
	  *dest_dt = *src_dt;
	}
      else
	{
	  err_status =
	    db_add_int_to_datetime ((DB_DATETIME *) src_dt,
				    -1000 * TIME_OFFSET (src_is_utc,
							 total_offset_sec),
				    dest_dt);
	  if (err_status != NO_ERROR)
	    {
	      goto exit;
	    }
	}
    }

  return err_status;
}

/*
 * tz_conv_tz_datetime_w_zone_info () - Converts a source DATETIME from one
 *				        timezone to another
 *
 * Return: error code
 * src_dt(in): object containing source datetime value;
 * src_zone_info_in(in): (partial) decoded timezone info associated with
 *			 source datetime
 * dest_zone_info_in(in): (partial) decoded timezone info for the desired
 *			  timezone
 * dest_dt(out): destination datetime value
 * src_zone_info_out(out): complete timezone information for source
 * dest_zone_info_out(out): complete timezone information for destination
 */
static int
tz_conv_tz_datetime_w_zone_info (const DB_DATETIME * src_dt,
				 const TZ_DECODE_INFO * src_zone_info_in,
				 const TZ_DECODE_INFO * dest_zone_info_in,
				 DB_DATETIME * dest_dt,
				 TZ_DECODE_INFO * src_zone_info_out,
				 TZ_DECODE_INFO * dest_zone_info_out)
{
  int err_status = NO_ERROR;
  DB_DATETIME dt_utc;
  TZ_DECODE_INFO tmp_zone_info;

  assert (src_dt != NULL);
  assert (src_zone_info_in != NULL);
  assert (dest_zone_info_in != NULL);
  assert (dest_dt != NULL);

  tmp_zone_info = *src_zone_info_in;
  if (src_zone_info_in->type == TZ_REGION_OFFSET
      && src_zone_info_in->offset == 0)
    {
      /* the source is UTC */
      dt_utc = *src_dt;
    }
  else
    {
      /* convert to UTC */
      err_status = tz_datetime_utc_conv (src_dt, &tmp_zone_info, false,
					 false, &dt_utc);
      if (err_status != NO_ERROR)
	{
	  goto exit;
	}
    }

  if (src_zone_info_out != NULL)
    {
      *src_zone_info_out = tmp_zone_info;
    }

  assert (TZ_IS_ZONE_VALID_DECODE_INFO (src_zone_info_in));

  if (src_zone_info_in->type == dest_zone_info_in->type
      && ((src_zone_info_in->type == TZ_REGION_ZONE
	   && src_zone_info_in->zone.zone_id
	   == dest_zone_info_in->zone.zone_id)
	  || (src_zone_info_in->type == TZ_REGION_OFFSET
	      && src_zone_info_in->offset == dest_zone_info_in->offset)))
    {
      /* same zone, copy value and zone information */
      *dest_dt = *src_dt;
      if (dest_zone_info_out != NULL)
	{
	  *dest_zone_info_out = tmp_zone_info;
	}
      return err_status;
    }

  tmp_zone_info = *dest_zone_info_in;
  if (dest_zone_info_in->type == TZ_REGION_OFFSET
      && dest_zone_info_in->offset == 0)
    {
      /* the destination is UTC */
      *dest_dt = dt_utc;
    }
  else
    {
      err_status = tz_datetime_utc_conv (&dt_utc, &tmp_zone_info, true,
					 false, dest_dt);
    }

  if (dest_zone_info_out != NULL)
    {
      *dest_zone_info_out = tmp_zone_info;
    }

exit:
  return err_status;
}

/*
 * tz_conv_tz_datetime_w_region () - Converts a source DATETIME from one
 *				      timezone to another (uses region
 *				      arguments)
 *
 * Return: error code
 * src_dt(in): object containing source datetime value;
 * src_tz_region(in): timezone region associated with source datetime
 * dest_tz_region(in): desired timezone region
 * dest_dt(out): destination datetime value
 * src_tz_id_out(out): compressed timezone identifier of the source
 * dest_tz_id_out(out): compressed timezone identifier of the destination
 */
int
tz_conv_tz_datetime_w_region (const DB_DATETIME * src_dt,
			      const TZ_REGION * src_tz_region,
			      const TZ_REGION * dest_tz_region,
			      DB_DATETIME * dest_dt, TZ_ID * src_tz_id_out,
			      TZ_ID * dest_tz_id_out)
{
  int err_status = NO_ERROR;
  TZ_DECODE_INFO src_zone_info;
  TZ_DECODE_INFO dest_zone_info_in;
  TZ_DECODE_INFO src_zone_info_out;
  TZ_DECODE_INFO dest_zone_info_out;

  assert (src_dt != NULL);
  assert (src_tz_region != NULL);
  assert (dest_tz_region != NULL);
  assert (dest_dt != NULL);

  tz_decode_tz_region (src_tz_region, &src_zone_info);
  tz_decode_tz_region (dest_tz_region, &dest_zone_info_in);

  err_status = tz_conv_tz_datetime_w_zone_info (src_dt, &src_zone_info,
						&dest_zone_info_in, dest_dt,
						&src_zone_info_out,
						&dest_zone_info_out);
  if (src_tz_id_out != NULL)
    {
      tz_encode_tz_id (&src_zone_info_out, src_tz_id_out);
    }

  if (dest_tz_id_out != NULL)
    {
      tz_encode_tz_id (&dest_zone_info_out, dest_tz_id_out);
    }

  return err_status;
}

/*
 * tz_conv_tz_datetime_w_zone_name () - Converts a source DATETIME from one
 *				        timezone to another
 *				     
 *
 * Return: error code
 * src_dt(in): object containing source datetime value
 * source_zone(in): source timezone string
 * len_source(in): length of source timezone string
 * dest_zone(in): destination timezone string
 * len_dest(in): length of destination timezone string
 * dest_dt(out): object containing output datetime value
 */

int
tz_conv_tz_datetime_w_zone_name (const DB_DATETIME * src_dt,
				 const char *source_zone,
				 int len_source,
				 const char *dest_zone,
				 int len_dest, DB_DATETIME * dest_dt)
{
  int source_zone_id, dest_zone_id;
  TZ_REGION source, dest;

  source_zone_id = tz_get_zone_id_by_name (source_zone, len_source);
  dest_zone_id = tz_get_zone_id_by_name (dest_zone, len_dest);

  if (source_zone_id == -1 || dest_zone_id == -1)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TZ_INVALID_TIMEZONE, 0);
      return ER_TZ_INVALID_TIMEZONE;
    }

  source.type = TZ_REGION_ZONE;
  source.zone_id = source_zone_id;
  dest.type = TZ_REGION_ZONE;
  dest.zone_id = dest_zone_id;

  return tz_conv_tz_datetime_w_region (src_dt, &source, &dest, dest_dt,
				       NULL, NULL);
}

/*
 * tz_explain_tz_id () - get timezone information
 *
 * Return: print characters
 * tz_id(in): complete valid zone identifier
 * tzr(out): buffer string for timezone region info
 * tzr_size(in): size of tz_str
 * tzdst(out): buffer string for daylight saving time info
 * tzdst_size (in) : size of tzdst 
 * tzh(out): time zone hour offset
 * tzm(out): time zone minute offset
 * Return: error or no error
 */
int
tz_explain_tz_id (const TZ_ID * tz_id, char *tzr,
		  const int tzr_size, char *tzdst,
		  const int tzdst_size, int *tzh, int *tzm)
{
#define LEN_MIN_HOUR 6
#define LEN_MIN_HOUR_SEC 9
  const TZ_DATA *tzd = tz_get_data ();
  int zone_name_size;
  int zone_id;
  int zone_offset_id;
  int dst_format_size;
  TZ_TIMEZONE *timezone;
  TZ_OFFSET_RULE *zone_off_rule;
  TZ_DECODE_INFO tz_info;
  char dst_format[TZ_MAX_FORMAT_SIZE];
  char *p_dst_format = NULL;
  int total_offset = 0;
  int er_status = NO_ERROR;

  tz_decode_tz_id (tz_id, true, &tz_info);
  if (tz_info.type == TZ_REGION_OFFSET)
    {
      int offset = tz_info.offset;
      int hour, min, sec;
      char sign;

      sign = (tz_info.offset < 0) ? '-' : '+';

      offset = (offset < 0) ? (-offset) : offset;
      if (offset >= 24 * 3600)
	{
	  er_status = ER_DATE_CONVERSION;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, er_status, 0);
	  return er_status;
	}

      db_time_decode ((DB_TIME *) & offset, &hour, &min, &sec);

      *tzh = hour;
      *tzm = min;
      if (sign == '-')
	{
	  *tzh = -(*tzh);
	  *tzm = -(*tzm);
	}

      if (sec > 0)
	{
	  sprintf (tzr, "%c%02d:%02d:%02d", sign, hour, min, sec);
	  tzr[LEN_MIN_HOUR_SEC] = '\0';
	}
      else
	{
	  sprintf (tzr, "%c%02d:%02d", sign, hour, min);
	  tzr[LEN_MIN_HOUR] = '\0';
	}
      return er_status;
    }

  if (tzd == NULL)
    {
      er_status = ER_TZ_LOAD_ERROR;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, er_status, 0);
      return er_status;
    }

  assert (tz_info.zone.p_zone_off_rule != NULL);
  total_offset = tz_info.zone.p_zone_off_rule->gmt_off;
  if (tz_info.zone.p_zone_off_rule->ds_type == DS_TYPE_RULESET_ID
      && tz_info.zone.p_ds_rule != NULL)
    {
      total_offset += tz_info.zone.p_ds_rule->save_time;
    }
  *tzh = total_offset / 3600;
  *tzm = (total_offset % 3600) / 60;
  zone_id = tz_info.zone.zone_id;
  zone_offset_id = tz_info.zone.offset_id;
  zone_name_size = strlen (tzd->timezone_names[zone_id]);

  timezone = &(tzd->timezones[zone_id]);
  zone_off_rule = &(tzd->offset_rules[timezone->gmt_off_rule_start
				      + zone_offset_id]);

  p_dst_format = zone_off_rule->std_format;
  if (zone_off_rule->ds_type == DS_TYPE_RULESET_ID)
    {
      TZ_DS_RULESET *ds_ruleset;
      TZ_DS_RULE *ds_rule;
      int dst_id = tz_info.zone.dst_id;
      char *ds_abbr = NULL;

      ds_ruleset = &(tzd->ds_rulesets[zone_off_rule->ds_ruleset]);
      if (dst_id < ds_ruleset->count)
	{
	  ds_rule = &(tzd->ds_rules[dst_id + ds_ruleset->index_start]);
	  ds_abbr = ds_rule->letter_abbrev;
	}

      if (zone_off_rule->var_format != NULL)
	{
	  if (ds_abbr == NULL)
	    {
	      p_dst_format = NULL;
	    }
	  else
	    {
	      snprintf (dst_format, sizeof (dst_format) - 1,
			zone_off_rule->var_format,
			(ds_abbr != NULL && *ds_abbr != '-') ? ds_abbr : "");
	      p_dst_format = dst_format;
	    }
	}
      else
	{
	  if (ds_rule->save_time != 0)
	    {
	      p_dst_format = zone_off_rule->save_format;
	    }
	}
    }

  if (p_dst_format != NULL)
    {
      dst_format_size = strlen (p_dst_format);
    }
  else
    {
      dst_format_size = 0;
    }

  if (zone_name_size + 1 > tzr_size || dst_format_size + 1 > tzdst_size)
    {
      er_status = ER_FAILED;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, er_status, 0);
      return er_status;
    }

  if (p_dst_format != NULL)
    {
      snprintf (tzr, tzr_size, "%s", tzd->timezone_names[zone_id]);
      snprintf (tzdst, tzdst_size, "%s", p_dst_format);
      tzdst[dst_format_size] = '\0';
    }
  else
    {
      snprintf (tzr, tzr_size, "%s", tzd->timezone_names[zone_id]);
    }

  tzr[zone_name_size] = '\0';

  return er_status;
#undef LEN_MIN_HOUR
#undef LEN_MIN_HOUR_SEC
}

/* 
 * tz_create_datetimetz_from_offset () - creates a datetime with timezone info
 *					 from a timezone hour offset and a 
 *					 a timezone minute offset
 *			   
 *
 * Return: error or no error
 * dt (in): local datetime value
 * tzh (in): timezone hour offset
 * tzm (in): timezone minute offset
 * dt_tz (out): object containing datetime value (adjusted to UTC) and
 *	        timezone info
 *
 */
int
tz_create_datetimetz_from_offset (const DB_DATETIME * dt, const int tzh,
				  const int tzm, DB_DATETIMETZ * dt_tz)
{
  int err_status = NO_ERROR;
  DB_DATETIME utc_dt;
  TZ_DECODE_INFO tz_info;

  tz_info.type = TZ_REGION_OFFSET;
  tz_info.offset = tzh * 3600 + tzm * 60;

  if (tz_info.offset >= 24 * 3600 || -tz_info.offset >= 24 * 3600)
    {
      err_status = ER_DATE_CONVERSION;
      return err_status;
    }

  err_status = tz_datetime_utc_conv (dt, &tz_info, false, false, &utc_dt);
  if (err_status != NO_ERROR)
    {
      return err_status;
    }
  tz_encode_tz_id (&tz_info, &(dt_tz->tz_id));
  dt_tz->datetime = utc_dt;
  return err_status;
}

/* 
 * tz_create_timetz_from_offset () - creates a time with timezone info 
 *				     from a timezone hour and a timezone minute
 *				     offset 
 *			 
 *
 * Return: error code
 * time(in): local time value
 * tzh(in): timezone hour offset
 * tzm(in): timezone minute offset
 * time_tz(out): object containing time value (adjusted to UTC) and
 *		 timezone info
 *
 */
int
tz_create_timetz_from_offset (const DB_TIME * time, const int tzh,
			      const int tzm, DB_TIMETZ * time_tz)
{
  int err_status = NO_ERROR;
  DB_DATETIME dt, utc_dt;
  TZ_DECODE_INFO tz_info;

  tz_info.type = TZ_REGION_OFFSET;
  tz_info.offset = tzh * 3600 + tzm * 60;

  if (tz_info.offset >= 24 * 3600 || -tz_info.offset >= 24 * 3600)
    {
      err_status = ER_DATE_CONVERSION;
      return err_status;
    }

  dt.date = tz_get_current_date ();
  dt.time = (*time) * 1000;

  err_status = tz_datetime_utc_conv (&dt, &tz_info, false, false, &utc_dt);
  if (err_status != NO_ERROR)
    {
      return err_status;
    }

  tz_encode_tz_id (&tz_info, &(time_tz->tz_id));
  time_tz->time = utc_dt.time / 1000;
  return err_status;
}

/* 
 * tz_create_timestamptz_from_offset () - creates a timestamp with timezone info 
 *					  from a timezone hour and a timezone 
 *					  minute offset 
 *			 
 *
 * Return: error code
 * date(in): local date value
 * time(in): local time value
 * tzh(in): timezone hour offset
 * tzm(in): timezone minute offset
 * timestamp_tz(out): object containing timestamp value (adjusted to UTC) and
 *		      timezone info
 *
 */
int
tz_create_timestamptz_from_offset (const DB_DATE * date,
				   const DB_TIME * time,
				   const int tzh, const int tzm,
				   DB_TIMESTAMPTZ * timestamp_tz)
{
  int err_status = NO_ERROR;
  DB_DATETIME dt, utc_dt;
  TZ_DECODE_INFO tz_info;
  DB_DATE date_utc;
  DB_TIME time_utc;

  tz_info.type = TZ_REGION_OFFSET;
  tz_info.offset = tzh * 3600 + tzm * 60;

  if (tz_info.offset >= 24 * 3600 || -tz_info.offset >= 24 * 3600)
    {
      err_status = ER_DATE_CONVERSION;
      return err_status;
    }

  dt.date = *date;
  dt.time = (*time) * 1000;

  err_status = tz_datetime_utc_conv (&dt, &tz_info, false, false, &utc_dt);
  if (err_status != NO_ERROR)
    {
      goto exit;
    }
  date_utc = utc_dt.date;
  time_utc = utc_dt.time / 1000;

  err_status = db_timestamp_encode_utc (&date_utc, &time_utc,
					&timestamp_tz->timestamp);
  if (err_status != NO_ERROR)
    {
      goto exit;
    }
  tz_encode_tz_id (&tz_info, &(timestamp_tz->tz_id));

exit:
  return err_status;
}

/*
 * tz_get_best_match_zone() - searches for the longest timezone matching the
 *			      start of the string
 *
 * Returns: matched zone id (negative if not matched)
 * name(in): string (null terminated)
 * size(out): matched size
 * 
 */
int
tz_get_best_match_zone (const char *name, int *size)
{
  const TZ_DATA *tzd;
  int index_bot, index_top;
  int cmp_res;

  assert (name != NULL);

  tzd = tz_get_data ();
  if (tzd == NULL)
    {
      return -1;
    }

  index_bot = 0;
  index_top = tzd->name_count - 1;

  while (index_bot <= index_top)
    {
      int mid = index_bot + ((index_top - index_bot) >> 1);
      cmp_res = starts_with (tzd->names[mid].name, name);

      if (cmp_res <= 0)
	{
	  index_bot = mid + 1;
	}
      else
	{
	  index_top = mid - 1;
	}
    }

  if (index_bot == 0
      || starts_with (tzd->names[index_bot - 1].name, name) != 0)
    {
      return -1;
    }

  *size = strlen (tzd->names[index_bot - 1].name);
  return tzd->names[index_bot - 1].zone_id;
}

/* 
 * tz_create_datetimetz_from_zoneid_and_tzd () - creates a datetime with timezone 
 *						 info from a datetime, a zone id
 *					         and daylight saving time info
 *			   
 *
 * Return: error or no error
 * dt(in): local datetime value
 * default_tz_region(in): default timezone region (used when zone_id is invalid)
 * zone_id(in): the zone id
 * tzd(in): daylight saving time info string
 * tzd_len(in): length of the tzd string
 * dt_tz(out): object containing datetime value(adjusted to UTC) and
 *	       timezone info
 */
int
tz_create_datetimetz_from_zoneid_and_tzd (const DB_DATETIME * dt,
					  TZ_REGION * default_tz_region,
					  const int zone_id, const char *tzd,
					  const int tzd_len,
					  DB_DATETIMETZ * dt_tz)
{
  int err_status = NO_ERROR;
  DB_DATETIME utc_dt;
  TZ_DECODE_INFO tz_info;

  if (zone_id != -1)
    {
      tz_info.type = TZ_REGION_ZONE;
      tz_info.zone.zone_id = zone_id;
      tz_info.zone.dst_id = TZ_DS_ID_MAX;
      tz_info.zone.offset_id = TZ_OFFSET_ID_MAX;
    }
  else
    {
      tz_decode_tz_region (default_tz_region, &tz_info);
    }

  if (tzd != NULL)
    {
      if (tzd_len + 1 > (int) sizeof (tz_info.zone.dst_str))
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TZ_INVALID_DST, 0);
	  return ER_TZ_INVALID_DST;
	}
      strncpy (tz_info.zone.dst_str, tzd, tzd_len);
      tz_info.zone.dst_str[tzd_len] = '\0';
    }
  else
    {
      tz_info.zone.dst_str[0] = '\0';
    }

  err_status = tz_datetime_utc_conv (dt, &tz_info, false, false, &utc_dt);
  if (err_status != NO_ERROR)
    {
      return err_status;
    }
  tz_encode_tz_id (&tz_info, &(dt_tz->tz_id));
  dt_tz->datetime = utc_dt;

  return err_status;
}

/* 
 * tz_create_timetz_from_zoneid_and_tzd () - creates a time with timezone 
 *					     info from a time, a zone id
 *					     and daylight saving time info
 *			   
 *
 * Return: error or no error
 * time(in): local time value
 * default_tz_region(in): default timezone region (used when zone_id is invalid)
 * zone_id(in): the zone id
 * tzd(in): daylight saving time info string
 * tzd_len(in): length of the tzd string
 * time_tz(out): object containing datetime value(adjusted to UTC) and
 *	       timezone info
 */
int
tz_create_timetz_from_zoneid_and_tzd (const DB_TIME * time,
				      TZ_REGION * default_tz_region,
				      const int zone_id, const char *tzd,
				      const int tzd_len, DB_TIMETZ * time_tz)
{
  int err_status = NO_ERROR;
  DB_DATETIME dt;
  DB_DATETIMETZ dt_tz;

  dt.date = tz_get_current_date ();
  dt.time = (*time) * 1000;

  err_status =
    tz_create_datetimetz_from_zoneid_and_tzd (&dt, default_tz_region, zone_id,
					      tzd, tzd_len, &dt_tz);
  if (err_status == NO_ERROR)
    {
      time_tz->time = dt_tz.datetime.time / 1000;
      time_tz->tz_id = dt_tz.tz_id;
    }

  return err_status;
}

/* 
 * tz_create_timestamptz_from_zoneid_and_tzd () - creates a timestamp with timezone 
 *						 info from a datetime, a zone id
 *					         and daylight saving time info
 *			   
 * Return: error or no error
 * date(in): local date value
 * time(in): local time value
 * default_tz_region(in): default timezone region (used when zone_id is invalid)
 * zone_id(in): the zone id
 * tzd(in): daylight saving time info string
 * tzd_len(in): length of the tzd string
 * timestamp_tz(out): object containing timestamp value(adjusted to UTC) and
 *		      timezone info
 */
int
tz_create_timestamptz_from_zoneid_and_tzd (const DB_DATE * date,
					   const DB_TIME * time,
					   TZ_REGION * default_tz_region,
					   const int zone_id, const char *tzd,
					   const int tzd_len,
					   DB_TIMESTAMPTZ * timestamp_tz)
{
  int err_status = NO_ERROR;
  DB_DATETIME utc_dt, dt;
  TZ_DECODE_INFO tz_info;
  DB_DATE date_utc;
  DB_TIME time_utc;

  if (zone_id != -1)
    {
      tz_info.type = TZ_REGION_ZONE;
      tz_info.zone.zone_id = zone_id;
      tz_info.zone.dst_id = TZ_DS_ID_MAX;
      tz_info.zone.offset_id = TZ_OFFSET_ID_MAX;
    }
  else
    {
      tz_decode_tz_region (default_tz_region, &tz_info);
    }

  if (tzd != NULL)
    {
      if (tzd_len + 1 > (int) sizeof (tz_info.zone.dst_str))
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TZ_INVALID_DST, 0);
	  return ER_TZ_INVALID_DST;
	}
      strncpy (tz_info.zone.dst_str, tzd, tzd_len);
      tz_info.zone.dst_str[tzd_len] = '\0';
    }
  else
    {
      tz_info.zone.dst_str[0] = '\0';
    }

  dt.date = *date;
  dt.time = *(time) * 1000;
  err_status = tz_datetime_utc_conv (&dt, &tz_info, false, false, &utc_dt);

  if (err_status != NO_ERROR)
    {
      return err_status;
    }

  date_utc = utc_dt.date;
  time_utc = utc_dt.time / 1000;
  err_status = db_timestamp_encode_utc (&date_utc, &time_utc,
					&(timestamp_tz->timestamp));
  if (err_status != NO_ERROR)
    {
      return err_status;
    }

  tz_encode_tz_id (&tz_info, &(timestamp_tz->tz_id));
  return err_status;
}

/*
 * starts_with() - verifies if prefix is a prefix of str
 *                 otherwise, verifies if prefix is lexicographically greater
 *                 or smaller than str
 * Return 0 if prefix is a prefix of str
 * Return > 0 if prefix is lexicographically greater than str
 * Return < 0 if prefix is lexicographically smaller than str
 * prefix(in): the prefix string
 * str(in): the string to search for the prefix
 * 
 */
static int
starts_with (const char *prefix, const char *str)
{
  while (*prefix != '\0' && (*prefix) == (*str))
    {
      prefix++, str++;
    }
  if (*prefix == '\0')
    {
      return 0;
    }

  return *(const unsigned char *) prefix - *(const unsigned char *) str;
}

#if defined(LINUX)
/*
 * find_timezone_from_clock() - Tries to put in timezone_name
 *                              the local timezone taken from
 *                              /etc/sysconfig/clock
 * Return >= 0 if no error
 * Return < 0 if error
 * timezone_name(out): the local timezone
 * buf_len(in): number of elements in the buffer without the null ending
 *              character
 */
static int
find_timezone_from_clock (char *timezone_name, int buf_len)
{
#define MAX_LINE_SIZE 256
  int cnt = 0;
  FILE *fp;
  char str[MAX_LINE_SIZE + 1];
  char *zone = NULL;

  fp = fopen ("/etc/sysconfig/clock", "r");
  if (fp == NULL)
    {
      return -1;
    }

  while (fgets (str, MAX_LINE_SIZE + 1, fp) != NULL)
    {
      if (*str == '#')
	{
	  continue;
	}
      if (cnt == 0)
	{
	  zone = strstr (str, "ZONE");
	  if (zone != NULL)
	    {
	      zone += 4;
	      while (*zone == ' ')
		{
		  zone++;
		}

	      if (*zone != '=')
		{
		  goto error;
		}

	      zone++;
	      while (*zone == ' ')
		{
		  zone++;
		}

	      if (*zone != '"')
		{
		  goto error;
		}

	      zone++;
	      while (*zone != '"' && *zone != '\0')
		{
		  timezone_name[cnt++] = *zone;
		  zone++;
		  if (cnt > buf_len)
		    {
		      goto error;
		    }
		}
	      if (*zone == '"')
		{
		  break;
		}
	    }
	}
      else
	{
	  zone = str;
	  while (*zone != '"' && *zone != '\0')
	    {
	      timezone_name[cnt++] = *zone;
	      zone++;
	      if (cnt > buf_len)
		{
		  goto error;
		}
	    }
	  break;
	}
    }

  timezone_name[cnt] = '\0';
  fclose (fp);
  return 0;

error:
  fclose (fp);
  return -1;
#undef MAX_LINE_SIZE
}

/*
 * find_timezone_from_localtime() - Tries to put in timezona_name
 *                                  the local timezone taken from
 *                                  /etc/localtime
 * Return >= 0 if no error
 * Return < 0 if error
 * timezone_name(out): the local timezone
 * buf_len(in): number of elements in the buffer without the null ending
 *              character
 */
static int
find_timezone_from_localtime (char *timezone_name, int buf_len)
{
  char linkname[PATH_MAX + 1];
  char *p;
  const char *zone_info = "zoneinfo";
  bool start = false;
  ssize_t r;
  int cnt = 0;

  memset (linkname, 0, sizeof (linkname));
  r = readlink ("/etc/localtime", linkname, PATH_MAX + 1);
  if (r < 0)
    {
      return r;
    }

  if (r > PATH_MAX)
    {
      return -1;
    }

  p = strtok (linkname, "/");
  while (p != NULL)
    {
      if (!start)
	{
	  if (strcmp (p, zone_info) == 0)
	    {
	      start = true;
	    }
	}
      else
	{
	  while (*p != '\0')
	    {
	      if (cnt > buf_len)
		{
		  goto error;
		}
	      timezone_name[cnt++] = *p;
	      p++;
	    }
	  if (cnt > buf_len)
	    {
	      goto error;
	    }
	  timezone_name[cnt++] = '/';
	}
      p = strtok (NULL, "/");
    }

  timezone_name[cnt - 1] = '\0';
  return 0;

error:
  return -1;
}
#endif

#if defined(WINDOWS)
/*
 * tz_get_iana_zone_id_by_windows_zone() - returns the ID of the leftmost timezone
 *					   corresponding to the specified windows 
 *					   zone name
 *
 * Returns: ID of zone, or -1 if not found
 * windows_zone_name(in): windows zone name to search for (null-terminated)
 */
static int
tz_get_iana_zone_id_by_windows_zone (const char *windows_zone_name)
{
  const TZ_DATA *tzd;
  int mid = 0;
  int index_bot, index_top;
  int cmp_res;

  assert (windows_zone_name != NULL);

  tzd = tz_get_data ();
  if (tzd == NULL)
    {
      return -1;
    }

  index_bot = 0;
  index_top = tzd->windows_iana_map_count - 1;

  while (index_bot <= index_top)
    {
      mid = index_bot + ((index_top - index_bot) >> 1);
      cmp_res = strcmp (windows_zone_name,
			tzd->windows_iana_map[mid].windows_zone);

      if (cmp_res <= 0)
	{
	  index_top = mid - 1;
	}
      else
	{
	  index_bot = mid + 1;
	}
    }

  if (strcmp
      (tzd->windows_iana_map[index_bot].windows_zone, windows_zone_name) != 0)
    {
      return -1;
    }
  return tzd->windows_iana_map[index_bot].iana_zone_id;
}
#endif

/*
 * resolve_timezone() - Puts in timezone the local timezone
 * Return >= 0 if no error
 * Return < 0 if error
 * timezone(out): the local timezone
 * buf_len(in): number of elements in the buffer without the null ending
 *              character 
 */
int
tz_resolve_os_timezone (char *timezone, int buf_len)
{
  int ret = 0;
  const TZ_DATA *tz_data;
  char *env = NULL;
  int len_iana_zone, iana_zone_id;

#if defined (WINDOWS)
  tz_data = tz_get_data ();
  tzset ();
  iana_zone_id = tz_get_iana_zone_id_by_windows_zone (tzname[0]);
  if (iana_zone_id < 0)
    {
      return iana_zone_id;
    }
  len_iana_zone = strlen (tz_data->timezone_names[iana_zone_id]);
  if (buf_len < len_iana_zone)
    {
      return -1;
    }
  memmove (timezone, tz_data->timezone_names[iana_zone_id], len_iana_zone);
  timezone[len_iana_zone] = '\0';
  return 0;
#elif defined(AIX)
  env = getenv ("TZ");
  if (env == NULL)
    {
      return -1;
    }
  else
    {
      strncpy (timezone, env, buf_len);
      timezone[buf_len] = '\0';
    }
  ret = tz_get_zone_id_by_name (timezone, strlen (timezone));
  return ret;
#else
  ret = find_timezone_from_clock (timezone, buf_len);
  if (ret < 0)
    {
      ret = find_timezone_from_localtime (timezone, buf_len);
    }
  if (ret >= 0)
    {
      ret = tz_get_zone_id_by_name (timezone, strlen (timezone));
    }
  return ret;
#endif
}

/*
 * tz_get_client_tz_region_session() - get a reference to the global 
 *                                     tz_region_session variable
 * Returns: reference to the tz_region_session variable
 */
#if !defined(SERVER_MODE)
TZ_REGION *
tz_get_client_tz_region_session (void)
{
  return &tz_region_session;
}
#endif

/*
 * tz_get_server_tz_region_session() - get a reference to the session TZ_REGION
 * Returns: reference to the session TZ_REGION variable
 */
#if defined(SERVER_MODE)
TZ_REGION *
tz_get_server_tz_region_session (void)
{
  THREAD_ENTRY *thread_p;
  TZ_REGION *session_tz_region;

  thread_p = thread_get_thread_entry_info ();
  session_tz_region = session_get_session_tz_region (thread_p);

  return session_tz_region;
}
#endif

#if !defined (CS_MODE)
/*
 * tz_timezones_start_scan() - start scan function for 
 *			      'SHOW TIMEZONES'
 *   return: NO_ERROR, or ER_code
 *   thread_p(in): thread entry
 *   show_type(in):
 *   arg_values(in):
 *   arg_cnt(in):
 *   ptr(in/out): 'show timezones' context
 */
int
tz_timezones_start_scan (THREAD_ENTRY * thread_p, int show_type,
			 DB_VALUE ** arg_values, int arg_cnt, void **ptr)
{
  int error = NO_ERROR;
  SHOWSTMT_ARRAY_CONTEXT *ctx = NULL;
  DB_VALUE *vals = NULL;
  const int col_num = 1;
  const TZ_DATA *tzd;
  int i;

  tzd = tz_get_data ();
  if (tzd == NULL)
    {
      /* no timezones, just return */
      return error;
    }

  *ptr = NULL;

  ctx = showstmt_alloc_array_context (thread_p, tzd->name_count, col_num);
  if (ctx == NULL)
    {
      error = er_errid ();
      return error;
    }

  for (i = 0; i < tzd->name_count; i++)
    {
      vals = showstmt_alloc_tuple_in_context (thread_p, ctx);
      if (vals == NULL)
	{
	  error = er_errid ();
	  goto exit_on_error;
	}
      /* Geographic timezone name */
      db_make_string (&vals[0], tzd->names[i].name);
    }

  *ptr = ctx;
  return NO_ERROR;

exit_on_error:
  if (ctx != NULL)
    {
      showstmt_free_array_context (thread_p, ctx);
    }

  return error;
}

/*
 * tz_full_timezones_start_scan() - start scan function for 
 *				   'SHOW FULL TIMEZONES'
 *   return: NO_ERROR, or ER_code
 *   thread_p(in): thread entry
 *   show_type(in):
 *   arg_values(in):
 *   arg_cnt(in):
 *   ptr(in/out): 'show timezones' context
 */
int
tz_full_timezones_start_scan (THREAD_ENTRY * thread_p, int show_type,
			      DB_VALUE ** arg_values, int arg_cnt, void **ptr)
{
  int error = NO_ERROR;
  SHOWSTMT_ARRAY_CONTEXT *ctx = NULL;
  DB_VALUE *vals = NULL;
  const int col_num = 4;
  const TZ_DATA *tzd;
  int i;
  time_t cur_time;
  DB_DATETIME utc_datetime, dummy_datetime;
  int year, month, day, hour, minute, second;
  TZ_DECODE_INFO tzinfo;
  TZ_TIMEZONE timezone;
  TZ_OFFSET_RULE zone_off_rule;
  char dst_format[TZ_MAX_FORMAT_SIZE];
  char *dst_name = NULL;
  char gmt_offset[MAX_LEN_OFFSET];
  char dst_offset[MAX_LEN_OFFSET];
  char empty_string[1];

  empty_string[0] = '\0';
  tzd = tz_get_data ();
  if (tzd == NULL)
    {
      /* no timezones, just return */
      return error;
    }

  *ptr = NULL;

  ctx = showstmt_alloc_array_context (thread_p, tzd->name_count, col_num);
  if (ctx == NULL)
    {
      error = er_errid ();
      return error;
    }

  /* Get the current time in seconds */
  time (&cur_time);
  tz_timestamp_decode_no_leap_sec (cur_time, &year, &month,
				   &day, &hour, &minute, &second);

  error = db_datetime_encode (&utc_datetime, month + 1, day, year,
			      hour, minute, second, 0);
  if (error != NO_ERROR)
    {
      goto exit_on_error;
    }

  for (i = 0; i < tzd->name_count; i++)
    {
      int idx = 0;
      int dst_save_time = 0;
      int zone_id;

      vals = showstmt_alloc_tuple_in_context (thread_p, ctx);
      if (vals == NULL)
	{
	  error = er_errid ();
	  goto exit_on_error;
	}
      /* Geographic timezone name */
      db_make_string (&vals[idx++], tzd->names[i].name);

      /* First get the zone id */
      zone_id = tzd->names[i].zone_id;

      tzinfo.type = TZ_REGION_ZONE;
      tzinfo.zone.zone_id = zone_id;
      tzinfo.zone.dst_str[0] = '\0';

      error = tz_datetime_utc_conv (&utc_datetime, &tzinfo, true, true,
				    &dummy_datetime);
      if (error != NO_ERROR)
	{
	  goto exit_on_error;
	}

      timezone = tzd->timezones[zone_id];
      zone_off_rule = tzd->offset_rules[timezone.gmt_off_rule_start +
					tzinfo.zone.offset_id];

      /* Timezone offset */
      tz_print_tz_offset (gmt_offset, zone_off_rule.gmt_off);
      db_make_string_copy (&vals[idx++], gmt_offset);

      dst_name = zone_off_rule.std_format;
      if (zone_off_rule.ds_type == DS_TYPE_RULESET_ID)
	{
	  TZ_DS_RULESET ds_ruleset;
	  TZ_DS_RULE ds_rule;
	  int dst_id;
	  char *ds_abbr = NULL;

	  ds_ruleset = tzd->ds_rulesets[zone_off_rule.ds_ruleset];
	  dst_id = tzinfo.zone.dst_id;

	  if (dst_id != TZ_DS_ID_MAX)
	    {
	      ds_rule = tzd->ds_rules[dst_id + ds_ruleset.index_start];
	      dst_save_time = ds_rule.save_time;
	      ds_abbr = ds_rule.letter_abbrev;

	      if (zone_off_rule.var_format != NULL)
		{
		  snprintf (dst_format, sizeof (dst_format) - 1,
			    zone_off_rule.var_format,
			    (ds_abbr != NULL && *ds_abbr != '-')
			    ? ds_abbr : "");
		  dst_name = dst_format;
		}
	      else
		{
		  if (dst_save_time != 0)
		    {
		      dst_name = zone_off_rule.save_format;
		    }
		}
	      if (dst_name == NULL)
		{
		  dst_name = empty_string;
		}
	    }
	}

      /* Now put the daylight saving time offset and name */
      if (dst_name != NULL)
	{
	  tz_print_tz_offset (dst_offset, dst_save_time);
	  db_make_string_copy (&vals[idx++], dst_offset);
	  db_make_string_copy (&vals[idx++], dst_name);
	}
      else
	{
	  db_make_null (&vals[idx++]);
	  db_make_null (&vals[idx++]);
	}
    }

  *ptr = ctx;
  return NO_ERROR;

exit_on_error:
  if (ctx != NULL)
    {
      showstmt_free_array_context (thread_p, ctx);
    }

  return error;
}
#endif
