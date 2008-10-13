/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * qstsr.h -  
 */

#ifndef _STATISTICS_SR_H_
#define _STATISTICS_SR_H_

#ident "$Id$"

#include "dbtype.h"
#include "statistics.h"
#include "system_catalog.h"

extern char *xstats_get_statistics_from_server (THREAD_ENTRY * thread_p,
						OID * class_id,
						unsigned int timestamp,
						int *length);
extern int xstats_update_class_statistics (THREAD_ENTRY * thread_p,
					   OID * class_id);
extern int xstats_update_statistics (THREAD_ENTRY * thread_p);

#if defined(CUBRID_DEBUG)
extern void stats_dump_class_statistics (CLASS_STATS * class_stats,
					 FILE * fpp);
#endif /* CUBRID_DEBUG */

#endif /* _STATISTICS_SR_H_ */
