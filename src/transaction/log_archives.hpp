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
// Log archives management
//

#ifndef _LOG_ARCHIVES_HPP_
#define _LOG_ARCHIVES_HPP_

#include "file_io.h"
#include "log_storage.hpp"
#include "storage_common.h"

typedef struct log_archives LOG_ARCHIVES;
struct log_archives
{
  int vdes;			/* Last archived accessed */
  LOG_ARV_HEADER hdr;		/* The log archive header */
  int max_unav;			/* Max size of unavailable array */
  int next_unav;		/* Last unavailable entry */
  int *unav_archives;		/* Unavailable archives */

  log_archives ()
    : vdes (NULL_VOLDES)
    , hdr ()
    , max_unav (0)
    , next_unav (0)
    , unav_archives (NULL)
  {
  }
};

//
// background archiving
//
typedef struct background_archiving_info BACKGROUND_ARCHIVING_INFO;
struct background_archiving_info
{
  LOG_PAGEID start_page_id;
  LOG_PAGEID current_page_id;
  LOG_PAGEID last_sync_pageid;
  int vdes;

  background_archiving_info ()
    : start_page_id (NULL_PAGEID)
    , current_page_id (NULL_PAGEID)
    , last_sync_pageid (NULL_PAGEID)
    , vdes (NULL_VOLDES)
  {
  }
};

// todo - move from log_impl.h

#endif // !_LOG_ARCHIVES_HPP_
