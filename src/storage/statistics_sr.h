/*
 * Copyright 2008 Search Solution Corporation
 * Copyright 2016 CUBRID Corporation
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 */


/*
 * statistics_sr.h -
 */

#ifndef _STATISTICS_SR_H_
#define _STATISTICS_SR_H_

#ident "$Id$"

#if !defined (SERVER_MODE) && !defined (SA_MODE)
#error Belongs to server module
#endif /* !defined (SERVER_MODE) && !defined (SA_MODE) */

#include "dbtype_def.h"
#include "statistics.h"
#include "system_catalog.h"
#include "object_representation_sr.h"

extern unsigned int stats_get_time_stamp (void);
extern const BTREE_STATS *stats_find_inherited_index_stats (OR_CLASSREP * cls_rep, OR_CLASSREP * subcls_rep,
							    DISK_ATTR * subcls_attr, BTID * cls_btid);
#if defined(CUBRID_DEBUG)
extern void stats_dump_class_statistics (CLASS_STATS * class_stats, FILE * fpp);
#endif /* CUBRID_DEBUG */

#endif /* _STATISTICS_SR_H_ */
