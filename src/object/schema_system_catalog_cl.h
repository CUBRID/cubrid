/*
 *
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
 * schema_system_catalog_cl.h
 */

#ifndef _SCHEMA_SYSTEM_CATALOG_CL_H_
#define _SCHEMA_SYSTEM_CATALOG_CL_H_

#include "statistics.h"

typedef enum
{
  SM_CATALOG_TIMESTAMP_INIT,
  SM_CATALOG_TIMESTAMP_UPDATE,
  SM_CATALOG_TIMESTAMP_STATISTICS,
} SM_CATALOG_TIMESTAMP_TYPE;

int sm_set_class_catalog_timestamps (const char *class_name, SM_CATALOG_TIMESTAMP_TYPE type);
int sm_set_class_catalog_statistics_info (MOP _db_class, const char *class_name, CLASS_STATS * stats,
					  bool with_fullscan);
int sm_set_class_catalog_timestamps_all_classes (void);

#endif /* _SCHEMA_SYSTEM_CATALOG_CL_H_ */
