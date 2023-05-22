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

//
// db_set structure
//

#ifndef _DB_SET_H_
#define _DB_SET_H_

#include "dbtype_def.h"

#include <stdio.h>

#ifdef __cplusplus
extern "C"
{
#endif				// __cplusplus

  struct db_set
  {
    /*
     * a garbage collector ticket is not required for the "owner" field as
     * the entire set references area is registered for scanning in area_grow.
     */
    struct db_object *owner;
    struct db_set *ref_link;
    struct setobj *set;
    char *disk_set;
    DB_DOMAIN *disk_domain;
    int attribute;
    int ref_count;
    int disk_size;
    need_clear_type need_clear;
  };

#ifdef __cplusplus
}				// extern "C"
#endif				// __cplusplus

#endif				// !_DB_SET_H_
