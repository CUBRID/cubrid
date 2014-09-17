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
#ifndef _TZ_COMPILE_H_
#define _TZ_COMPILE_H_

#include "timezone_lib_common.h"

#ifdef __cplusplus
extern "C"
{
#endif
  extern int timezone_compile_data (const char *input_folder,
				    const TZ_GEN_TYPE tz_gen_type);
  extern void tzc_dump_summary (const TZ_DATA * tzd);
  extern void tzc_dump_countries (const TZ_DATA * tzd);
  extern void tzc_dump_timezones (const TZ_DATA * tzd);
  extern void tzc_dump_one_timezone (const TZ_DATA * tzd, const int zone_id);
  extern void tzc_dump_leap_sec (const TZ_DATA * tzd);
#ifdef __cplusplus
}
#endif

#endif				/* _TZ_COMPILE_H_ */
