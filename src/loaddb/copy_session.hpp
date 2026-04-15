/*
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
 * copy_session.hpp - Server-side COPY FROM STDIN session
 */

#ifndef _COPY_SESSION_HPP_
#define _COPY_SESSION_HPP_

#include "dbtype_def.h"
#include "heap_file.h"
#include "oid.h"
#include "thread_compat.hpp"

#include <vector>

class copy_session
{
public:
  copy_session ();
  ~copy_session ();

  int init (THREAD_ENTRY *thread_p, const OID *class_oid, const DB_TYPE *col_types, int num_cols);
  int receive_data (THREAD_ENTRY *thread_p, const char *data, int data_len);
  int finish (THREAD_ENTRY *thread_p, int *rows_loaded);
  void abort (THREAD_ENTRY *thread_p);

private:
  OID m_class_oid;
  HFID m_hfid;
  HEAP_SCANCACHE m_scancache;
  bool m_scancache_started;

  std::vector<DB_TYPE> m_col_types;
  int m_num_cols;
  int m_rows_loaded;
};

#endif /* _COPY_SESSION_HPP_ */
