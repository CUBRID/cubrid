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
#ifndef _TZ_COMPILE_H_
#define _TZ_COMPILE_H_

#include "timezone_lib_common.h"

#ifdef __cplusplus
extern "C"
{
#endif
#if defined (SA_MODE)
  extern int timezone_compile_data (const char *input_folder, const TZ_GEN_TYPE tz_gen_type, char *database_name,
				    const char *output_file_path, char *checksum);
#endif
  extern void tzc_dump_summary (const TZ_DATA * tzd);
  extern void tzc_dump_countries (const TZ_DATA * tzd);
  extern void tzc_dump_timezones (const TZ_DATA * tzd);
  extern void tzc_dump_one_timezone (const TZ_DATA * tzd, const int zone_id);
  extern void tzc_dump_leap_sec (const TZ_DATA * tzd);
#ifdef __cplusplus
}
#endif

#endif				/* _TZ_COMPILE_H_ */
