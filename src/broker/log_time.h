/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * log_time.h - 
 */

#ifndef _LOG_TIME_H_
#define _LOG_TIME_H_

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

#endif /* _LOG_TIME_H_ */
