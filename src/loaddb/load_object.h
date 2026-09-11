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
 * load_object.h: simplified object definitions
 */

#ifndef _LOAD_OBJECT_H_
#define _LOAD_OBJECT_H_

#include <fcntl.h>

#include "dbtype_def.h"
#include "class_object.h"
#include <vector>

/*
 * DESC_OBJ
 *    This is a simplified description of an object that is used in cases
 *    where we do not want or need to build complete memory representations
 *    of an instance.  This was developed primarily to support the
 *    loader/import/export utility but could be used for other things as
 *    well.
 */


typedef struct dbvalue_buf
{
  char *buf;
  int buf_size;
} DBVALUE_BUF;

/*
 * How an OOS-backed attribute reaches the object descriptor.
 *
 * An OOS-backed attribute is stored as a 16-byte inline stub (OID + length) whose payload lives in the
 * class' OOS or Internal LOB file.  Who reads the record decides which form it needs:
 *
 *   DESC_OOS_EXPANDED    the server already replaced the stub with the payload bytes, so the ordinary
 *                        per-type reader sees a record that looks as if OOS had never been used.  Valid
 *                        only for types whose value IS its bytes; a BLOB/CLOB in the record is a
 *                        locator, so expanded payload bytes are not a value of that type at all.
 *   DESC_OOS_TEXT_LOCATOR unloaddb: turn an Internal LOB stub into the text locator it writes into the
 *                        object file, and stream the payload into the sidecar itself.
 *   DESC_OOS_KEEP_STUB   compactdb: keep the stub bytes as they are stored so the record can be written
 *                        back unchanged.  The payload is never read and never re-encoded.
 */
typedef enum
{
  DESC_OOS_EXPANDED,
  DESC_OOS_TEXT_LOCATOR,
  DESC_OOS_KEEP_STUB
} DESC_OOS_POLICY;

typedef struct desc_obj
{
  MOP classop;
  SM_CLASS *class_;
  int updated_flag;
  int count;
  SM_ATTRIBUTE **atts;
  DB_VALUE *values;
  DBVALUE_BUF *dbvalue_buf_ptr;	// Area for copying data of VARCHAR column
  /* DESC_OOS_KEEP_STUB only: the stored inline stub of values[i], to be written back verbatim.
   * oos_stubs holds count * OR_OOS_INLINE_SIZE bytes; entry i is meaningful when oos_stub_valid[i]. */
  bool *oos_stub_valid;
  char *oos_stubs;
} DESC_OBJ;



extern DESC_OBJ *make_desc_obj (SM_CLASS * class_, int pre_alloc_varchar_size);
extern int desc_obj_to_disk (DESC_OBJ * obj, RECDES * record, bool * index_flag);
extern int desc_disk_to_obj (MOP classop, SM_CLASS * class_, RECDES * record, DESC_OBJ * obj,
			     DESC_OOS_POLICY oos_policy);
extern void desc_free (DESC_OBJ * obj);

extern int er_filter_fileset (FILE * ef);
extern int er_filter_errid (bool ignore_warning);

/* *INDENT-OFF* */
extern void get_ignored_errors (std::vector<int> &vec);
/* *INDENT-ON* */

#endif /* _LOAD_OBJECT_H_ */
