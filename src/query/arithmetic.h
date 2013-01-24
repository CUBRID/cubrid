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
 * arithmetic.h: interface for arithmetic functions
 */

#ifndef _ARITHMETIC_H_
#define _ARITHMETIC_H_

#ident "$Id$"

#include "dbtype.h"
#include "object_domain.h"
#include "thread.h"

#define PI ((double) (3.14159265358979323846264338))

extern int db_floor_dbval (DB_VALUE * result, DB_VALUE * value);
extern int db_ceil_dbval (DB_VALUE * result, DB_VALUE * value);
extern int db_sign_dbval (DB_VALUE * result, DB_VALUE * value);
extern int db_abs_dbval (DB_VALUE * result, DB_VALUE * value);
extern int db_exp_dbval (DB_VALUE * result, DB_VALUE * value);
extern int db_sqrt_dbval (DB_VALUE * result, DB_VALUE * value);
extern int db_power_dbval (DB_VALUE * result, DB_VALUE * value1,
			   DB_VALUE * value2);
extern int db_mod_dbval (DB_VALUE * result, DB_VALUE * value1,
			 DB_VALUE * value2);
extern int db_round_dbval (DB_VALUE * result, DB_VALUE * value1,
			   DB_VALUE * value2);
extern int db_log_dbval (DB_VALUE * result, DB_VALUE * value1,
			 DB_VALUE * value2);
extern int db_trunc_dbval (DB_VALUE * result, DB_VALUE * value1,
			   DB_VALUE * value2);
extern int db_random_dbval (DB_VALUE * result);
extern int db_drandom_dbval (DB_VALUE * result);
extern int db_bit_count_dbval (DB_VALUE * result, DB_VALUE * value);
extern int db_cos_dbval (DB_VALUE * result, DB_VALUE * value);
extern int db_sin_dbval (DB_VALUE * result, DB_VALUE * value);
extern int db_tan_dbval (DB_VALUE * result, DB_VALUE * value);
extern int db_cot_dbval (DB_VALUE * result, DB_VALUE * value);
extern int db_acos_dbval (DB_VALUE * result, DB_VALUE * value);
extern int db_asin_dbval (DB_VALUE * result, DB_VALUE * value);
extern int db_atan_dbval (DB_VALUE * result, DB_VALUE * value1);
extern int db_atan2_dbval (DB_VALUE * result, DB_VALUE * value,
			   DB_VALUE * value2);
extern int db_degrees_dbval (DB_VALUE * result, DB_VALUE * value);
extern int db_radians_dbval (DB_VALUE * result, DB_VALUE * value);
extern int db_log_generic_dbval (DB_VALUE * result, DB_VALUE * value, long b);
extern int db_typeof_dbval (DB_VALUE * result, DB_VALUE * value);
extern int db_width_bucket (DB_VALUE * result, const DB_VALUE * value1,
			    const DB_VALUE * value2, const DB_VALUE * value3,
			    const DB_VALUE * value4);
#endif /* _ARITHMETIC_H_ */
