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

#ifndef _INTERNAL_LOB_FILE_HPP_
#define _INTERNAL_LOB_FILE_HPP_

#include <cstdio>
#include <limits.h>
#include <string>

#include "dbtype_def.h"
#include "internal_lob_marker.h"
#include "oos_file.hpp"
#include "object_domain.h"
#include "oid.h"

extern int internal_lob_create_file (THREAD_ENTRY *thread_p, const HFID &heap_hfid, const OID &class_oid,
				     VFID &lob_vfid);
#if defined (CUBRID_UNIT_TEST_ENABLED)
/* Storage-level tests create Internal LOB files with a synthetic owner descriptor. */
extern int internal_lob_create_file (THREAD_ENTRY *thread_p, VFID &lob_vfid);
#endif /* CUBRID_UNIT_TEST_ENABLED */
extern int internal_lob_remove_file (THREAD_ENTRY *thread_p, const VFID &lob_vfid);
#endif /* _INTERNAL_LOB_FILE_HPP_ */
