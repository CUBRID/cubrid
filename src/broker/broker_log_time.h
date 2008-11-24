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
 * broker_log_time.h - 
 */

#ifndef _BROKER_LOG_TIME_H_
#define _BROKER_LOG_TIME_H_

#ident "$Id$"

typedef struct t_log_time T_LOG_TIME;
struct t_log_time
{
  int hour;
  int min;
  int sec;
  int msec;
};

extern int log_time_make (char *str, T_LOG_TIME * ltm);
extern int log_time_diff (T_LOG_TIME * t1, T_LOG_TIME * t2);

#endif /* _BROKER_LOG_TIME_H_ */
