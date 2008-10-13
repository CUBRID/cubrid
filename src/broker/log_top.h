/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * log_top.h - 
 */

#ifndef	_LOG_TOP_H_
#define	_LOG_TOP_H_

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

#endif /* _LOG_TOP_H_ */
