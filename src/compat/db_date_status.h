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

/* Internal date conversion status API. Legacy entry points alone publish errors. */
#ifndef _DB_DATE_STATUS_H_
#define _DB_DATE_STATUS_H_

#include "db_date.h"
#include "tz_support.h"
#include "error_manager.h"

/* No global error state is changed while computing a conversion. All recorded
 * date/time errors have no message arguments; keep their original location. */
struct date_conversion_error
{
  int code = NO_ERROR;
  int severity = ER_ERROR_SEVERITY;
  const char *file = nullptr;
  int line = 0;
  bool touched = false;

  void set (int level, const char *source_file, int source_line, int error_code, int nargs)
  {
    assert (nargs == 0);
    code = error_code;
    severity = level;
    file = source_file;
    line = source_line;
    touched = true;
  }

  void clear ()
  {
    code = NO_ERROR;
    touched = true;
  }

  void publish () const
  {
    if (touched)
      {
	if (code != NO_ERROR)
	  {
	    er_set (severity, file, line, code, 0);
	  }
	else
	  {
	    er_clear ();
	  }
      }
  }
};

int
db_add_int_to_datetime_core (DB_DATETIME * datetime, DB_BIGINT bi2, DB_DATETIME * result_datetime,
			     date_conversion_error * date_error);
int db_date_encode_core (DB_DATE * date, int month, int day, int year, date_conversion_error * date_error);
int db_date_parse_date_core (char const *str, int str_len, DB_DATE * date, date_conversion_error * date_error);
int
db_date_parse_datetime_core (char const *str, int str_len, DB_DATETIME * datetime, date_conversion_error * date_error);
int
db_date_parse_datetime_parts_core (char const *str, int str_len, DB_DATETIME * datetime, bool * has_explicit_time,
				   bool * has_explicit_msec, bool * fits_as_timestamp, char const **endp,
				   date_conversion_error * date_error);
int db_date_parse_time_core (char const *str, int str_len, DB_TIME * time, int *millisecond,
			     date_conversion_error * date_error);
int db_date_parse_timestamp_core (char const *str, int str_len, DB_TIMESTAMP * utime,
				  date_conversion_error * date_error);
int db_datetime_encode_core (DB_DATETIME * datetime, int month, int day, int year, int hour, int minute, int second,
			     int millisecond, date_conversion_error * date_error);
int db_datetimetz_to_string_core (char *buf, int bufsize, DB_DATETIME * dt, const TZ_ID * tz_id,
				  date_conversion_error * date_error);
int db_string_to_datetimetz_ex_core (const char *str, int str_len, DB_DATETIMETZ * dt_tz, bool * has_zone,
				     date_conversion_error * date_error);
int db_string_to_timestamptz_ex_core (const char *str, int str_len, DB_TIMESTAMPTZ * ts_tz, bool * has_zone,
				      bool is_cast, date_conversion_error * date_error);
int db_subtract_int_from_datetime_core (DB_DATETIME * dt1, DB_BIGINT bi2, DB_DATETIME * result_datetime,
					date_conversion_error * date_error);
int db_time_encode_core (DB_TIME * timeval, int hour, int minute, int second, date_conversion_error * date_error);
int
db_timestamp_decode_ses_core (const DB_TIMESTAMP * utime, DB_DATE * date, DB_TIME * timeval,
			      date_conversion_error * date_error);
int db_timestamp_decode_w_reg_core (const DB_TIMESTAMP * utime, const TZ_REGION * tz_region, DB_DATE * date,
				    DB_TIME * timeval, date_conversion_error * date_error);
int db_timestamp_decode_w_tz_id_core (const DB_TIMESTAMP * utime, const TZ_ID * tz_id, DB_DATE * date,
				      DB_TIME * timeval, date_conversion_error * date_error);
int db_timestamp_encode_ses_core (const DB_DATE * date, const DB_TIME * timeval, DB_TIMESTAMP * utime,
				  TZ_ID * dest_tz_id, date_conversion_error * date_error);
int db_timestamp_encode_utc_core (const DB_DATE * date, const DB_TIME * timeval, DB_TIMESTAMP * utime,
				  date_conversion_error * date_error);
int db_timestamp_to_string_core (char *buf, int bufsize, DB_TIMESTAMP * utime, date_conversion_error * date_error);
int
db_timestamptz_to_string_core (char *buf, int bufsize, DB_TIMESTAMP * utime, const TZ_ID * tz_id,
			       date_conversion_error * date_error);
int tz_conv_tz_datetime_w_region_core (const DB_DATETIME * src_dt, const TZ_REGION * src_tz_region,
				       const TZ_REGION * dest_tz_region, DB_DATETIME * dest_dt, TZ_ID * src_tz_id_out,
				       TZ_ID * dest_tz_id_out, date_conversion_error * date_error);
int tz_create_datetimetz_core (const DB_DATETIME * dt, const char *tz_str, const int tz_size,
			       const TZ_REGION * default_tz_region, DB_DATETIMETZ * dt_tz, const char **end_tz_str,
			       date_conversion_error * date_error);
int tz_create_datetimetz_from_ses_core (const DB_DATETIME * dt, DB_DATETIMETZ * dt_tz,
					date_conversion_error * date_error);
int tz_create_session_tzid_for_datetime_core (const DB_DATETIME * src_dt, bool src_is_utc, TZ_ID * tz_id,
					      date_conversion_error * date_error);
int tz_create_session_tzid_for_timestamp_core (const DB_UTIME * src_ts, TZ_ID * tz_id,
					       date_conversion_error * date_error);
int tz_create_timestamptz_core (const DB_DATE * date, const DB_TIME * time, const char *tz_str, const int tz_size,
				const TZ_REGION * default_tz_region, DB_TIMESTAMPTZ * ts_tz, const char **end_tz_str,
				date_conversion_error * date_error);
int tz_datetimeltz_to_local_core (const DB_DATETIME * dt_ltz, DB_DATETIME * dt_local,
				  date_conversion_error * date_error);
int tz_get_ds_change_julian_date_diff_core (const int src_julian_date, const TZ_DS_RULE * ds_rule, const int year,
					    int *ds_rule_julian_date, full_date_t * date_diff,
					    date_conversion_error * date_error);
int tz_utc_datetimetz_to_local_core (const DB_DATETIME * dt_utc, const TZ_ID * tz_id, DB_DATETIME * dt_local,
				     date_conversion_error * date_error);

#endif /* _DB_DATE_STATUS_H_ */
