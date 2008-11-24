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
 * broker_log_top.h - 
 */

#ifndef	_BROKER_LOG_TOP_H_
#define	_BROKER_LOG_TOP_H_

#ident "$Id$"

#define LINE_BUF_SIZE           30000

#define DATE_STR_LEN    14

#define IS_CAS_LOG_CMD(STR)     \
  ((strlen(STR) >= 19 && (STR)[2] == '/' && (STR)[5] == ' ' && (STR)[8] == ':' && (STR)[11] == ':' && (STR)[18] == ' ') ? 1 : 0)

#define GET_CUR_DATE_STR(BUF, LINEBUF)  \
        do  {                           \
          strncpy(BUF, LINEBUF, DATE_STR_LEN);  \
          BUF[DATE_STR_LEN] = '\0';             \
        } while (0)

typedef enum t_log_top_mode T_LOG_TOP_MODE;
enum t_log_top_mode
{
  MODE_PROC_TIME = 0,
  MODE_MAX_HANDLE = 1
};

extern int check_log_time (char *start_date, char *end_date);

extern T_LOG_TOP_MODE log_top_mode;

#endif /* _BROKER_LOG_TOP_H_ */
