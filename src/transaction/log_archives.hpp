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
