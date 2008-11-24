/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution. 
 *
 *   This program is free software; you can redistribute it and/or modify 
 *   it under the terms of the GNU General Public License as published by 
 *   the Free Software Foundation; version 2 of the License. 
 *
 *  This program is distributed in the hope that it will be useful, 
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of 
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the 
 *  GNU General Public License for more details. 
 *
 *  You should have received a copy of the GNU General Public License 
 *  along with this program; if not, write to the Free Software 
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA 
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
#include "thread_impl.h"

extern int db_floor_dbval (DB_VALUE * result, DB_VALUE * value);
extern int db_ceil_dbval (DB_VALUE * result, DB_VALUE * value);
extern int db_sign_dbval (DB_VALUE * result, DB_VALUE * value);
extern int db_abs_dbval (DB_VALUE * result, DB_VALUE * value);
extern int db_exp_dbval (DB_VALUE * result, DB_VALUE * value);
extern int db_sqrt_dbval (DB_VALUE * result, DB_VALUE * value);
extern int db_power_dbval (DB_VALUE * result, TP_DOMAIN * domain,
			   DB_VALUE * value1, DB_VALUE * value2);
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

/* TODO: M2 - move these to qp_serial.h */
extern int xqp_get_serial_current_value (THREAD_ENTRY * thread_p,
					 const DB_VALUE * oid_str_val,
					 DB_VALUE * result_num);
extern int xqp_get_serial_next_value (THREAD_ENTRY * thread_p,
				      const DB_VALUE * oid_str_val,
				      DB_VALUE * result_num);

#endif /* _ARITHMETIC_H_ */
