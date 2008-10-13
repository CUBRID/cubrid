/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * get_psinfo.h - 
 */

#ifndef	_GET_PSINFO_H_
#define	_GET_PSINFO_H_

#ident "$Id$"

#ifdef GET_PSINFO
#if !defined(SOLARIS) && !defined(HPUX)
#error NOT IMPLEMENTED
#endif
#endif

typedef struct t_psinfo T_PSINFO;
struct t_psinfo
{
  int num_thr;
  int cpu_time;
  float pcpu;
};

int get_psinfo (int pid, T_PSINFO * ps_info);

#endif /* _GET_PSINFO_H_ */
