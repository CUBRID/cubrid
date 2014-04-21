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
 * broker_log_top.h -
 */

#ifndef	_BROKER_LOG_TOP_H_
#define	_BROKER_LOG_TOP_H_

#ident "$Id$"

#define LINE_BUF_SIZE           30000

typedef enum t_log_top_mode T_LOG_TOP_MODE;
enum t_log_top_mode
{
  MODE_PROC_TIME = 0,
  MODE_MAX_HANDLE = 1
};

enum log_top_error_code
{
  LT_NO_ERROR = 0,
  LT_INVAILD_VERSION = -1,
  LT_OTHER_ERROR = -2
};

extern int check_log_time (char *start_date, char *end_date);
extern int log_top_tran (int argc, char *argv[], int arg_start);
extern int get_file_offset (char *filename, long *start_offset,
			    long *end_offset);

extern T_LOG_TOP_MODE log_top_mode;

#endif /* _BROKER_LOG_TOP_H_ */
