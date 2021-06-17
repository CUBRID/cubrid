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

#ifndef _METHOD_SCAN_HPP_
#define _METHOD_SCAN_HPP_

#ident "$Id$"

#if !defined (SERVER_MODE) && !defined (SA_MODE)
#error Belongs to server module
#endif /* !defined (SERVER_MODE) && !defined (SA_MODE) */

#include <vector>

#include "dbtype_def.h" /* DB_VALUE */
#include "method_def.hpp" /* method_sig_list */
#include "object_domain.h" /* TP_DOMAIN */
#include "query_list.h" /* qfile_list_id, qfile_list_scan_id */

#if defined (SA_MODE)
#include "query_method.h"
#endif

/* forward declarations */
struct val_list_node;
struct qproc_db_value_list;

// thread_entry.hpp
namespace cubthread
{
  class entry;
}

namespace cubscan
{
  namespace method
  {
    class scanner
    {
      public:

	scanner ();

//////////////////////////////////////////////////////////////////////////
// Main SCAN routines
//////////////////////////////////////////////////////////////////////////

	int init (cubthread::entry *thread_p, method_sig_list *sig_list, qfile_list_id *list_id);
	int open ();
	SCAN_CODE next_scan (val_list_node &vl);
	int close ();

      protected:

//////////////////////////////////////////////////////////////////////////
// Common interface to send args and receive values
//////////////////////////////////////////////////////////////////////////

	int request ();
	int receive (DB_VALUE &return_val);

#if defined(SERVER_MODE)
//////////////////////////////////////////////////////////////////////////
// Communication with CAS
//////////////////////////////////////////////////////////////////////////

	int xs_send ();
	int xs_receive (DB_VALUE &val);
#endif

//////////////////////////////////////////////////////////////////////////
// Value array scanning declarations
//////////////////////////////////////////////////////////////////////////

	int open_value_array ();
	void next_value_array (val_list_node &vl);
	int close_value_array ();

//////////////////////////////////////////////////////////////////////////
// argument declarations
//////////////////////////////////////////////////////////////////////////

	SCAN_CODE get_single_tuple ();

      private:

	cubthread::entry *m_thread_p; /* thread entry */

	// TODO: method signature list will be interpret according to the method types in the future
	method_sig_list *m_method_sig_list;	/* method signatures */

	qfile_list_id *m_list_id; 		/* list file from cselect */
	qfile_list_scan_id m_scan_id;	/* for scanning list file */

	std::vector<TP_DOMAIN *> m_arg_dom_vector; /* placeholder for arg value's domain */
	std::vector<DB_VALUE> m_arg_vector;        /* placeholder for arg value */

#if defined (SA_MODE)
	std::vector<DB_VALUE> m_result_vector;     /* placeholder for result value */
#endif

	qproc_db_value_list *m_dbval_list; /* result */
    };
  }
} // namespace cubscan

// naming convention of SCAN_ID's
using METHOD_SCAN_ID = cubscan::method::scanner;

#endif // _METHOD_SCAN_HPP_
