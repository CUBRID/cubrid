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
 * statistics_sr.h -  
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
