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

#include <functional>
#include <vector>

#include "dbtype_def.h" /* DB_VALUE */
#include "method_def.hpp" /* method_sig_list */
#include "object_domain.h" /* TP_DOMAIN */
#include "query_list.h" /* qfile_list_id, qfile_list_scan_id */

#if defined (SERVER_MODE)
#include "jsp_struct.hpp"
#endif

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

#if defined (SERVER_MODE)
	#if 0
	class external_caller_group
	{
		public:
			external_caller_group ()
			{
				caller.
			}
			void prepare ();
			void execute ();
			void end ();
			
		private:
			std::array <external_caller *, METHOD_TYPE> caller;
		void addCaller (external_caller& caller);

	};

	class external_caller
	{
		public:
		  external_caller ();
		  virtual ~external_caller();

		  void connect ();
		  void disconnect ();

		  bool is_connected ();


		private:
	};
	#endif

    class javasp_caller
    {
      public:
	void connect ();
	void disconnect ();

	// bool is_connected ();

	int request (METHOD_SIG *&method_sig, std::vector<DB_VALUE> &arg_base);
	int receive (DB_VALUE &returnval);
	// int callback_dispatch (int code);
      private:
	int alloc_response (cubmem::extensible_block &blk);
	int receive_result (cubmem::extensible_block &blk, DB_VALUE &returnval);
	int receive_error (cubmem::extensible_block &blk, DB_VALUE &returnval);

	SOCKET m_sock_fd = INVALID_SOCKET;
	bool is_connected = false;
    };
#endif

    class scanner
    {
      public:
	using xs_callback_func = std::function<int (cubmem::block &)>;
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

	int request (method_sig_node *method_sig);
	int receive (METHOD_TYPE method_type, DB_VALUE &return_val);

#if defined(SERVER_MODE)
//////////////////////////////////////////////////////////////////////////
// Communication with CAS
//////////////////////////////////////////////////////////////////////////

	int xs_send (cubmem::block &mem);
	int xs_receive (const xs_callback_func &func);
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
#if defined (SERVER_MODE)
	javasp_caller m_caller;
#endif

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
