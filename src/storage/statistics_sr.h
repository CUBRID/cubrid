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
 * statistics_sr.h -
 */

#ifndef _STATISTICS_SR_H_
#define _STATISTICS_SR_H_

#ident "$Id$"

#include "dbtype.h"
#include "statistics.h"
#include "system_catalog.h"

#if defined(CUBRID_DEBUG)
extern void stats_dump_class_statistics (CLASS_STATS * class_stats,
					 FILE * fpp);
#endif /* CUBRID_DEBUG */

#endif /* _STATISTICS_SR_H_ */
