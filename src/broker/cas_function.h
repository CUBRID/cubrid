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


/*
 * cas_function.h -
 */

#ifndef	_CAS_FUNCTION_H_
#define	_CAS_FUNCTION_H_

#ident "$Id$"

#include "cas_net_buf.h"

typedef int (*T_SERVER_FUNC) (int, int, void **, T_NET_BUF *, T_REQ_INFO *);

#define CAS_FN_ARG_SOCK_FD		sock_fd
#define CAS_FN_ARG_ARGC			argc
#define CAS_FN_ARG_ARGV			argv
#define CAS_FN_ARG_NET_BUF		net_buf
#define CAS_FN_ARG_REQ_INFO		req_info

extern int fn_end_tran (int CAS_FN_ARG_SOCK_FD, int CAS_FN_ARG_ARGC,
			void **CAS_FN_ARG_ARGV,
			T_NET_BUF * CAS_FN_ARG_NET_BUF,
			T_REQ_INFO * CAS_FN_ARG_REQ_INFO);
extern int fn_prepare (int CAS_FN_ARG_SOCK_FD, int CAS_FN_ARG_ARGC,
		       void **CAS_FN_ARG_ARGV,
		       T_NET_BUF * CAS_FN_ARG_NET_BUF,
		       T_REQ_INFO * CAS_FN_ARG_REQ_INFO);
extern int fn_execute (int CAS_FN_ARG_SOCK_FD, int CAS_FN_ARG_ARGC,
		       void **CAS_FN_ARG_ARGV,
		       T_NET_BUF * CAS_FN_ARG_NET_BUF,
		       T_REQ_INFO * CAS_FN_ARG_REQ_INFO);
extern int fn_get_db_parameter (int CAS_FN_ARG_SOCK_FD, int CAS_FN_ARG_ARGC,
				void **CAS_FN_ARG_ARGV,
				T_NET_BUF * CAS_FN_ARG_NET_BUF,
				T_REQ_INFO * CAS_FN_ARG_REQ_INFO);
extern int fn_set_db_parameter (int CAS_FN_ARG_SOCK_FD, int CAS_FN_ARG_ARGC,
				void **CAS_FN_ARG_ARGV,
				T_NET_BUF * CAS_FN_ARG_NET_BUF,
				T_REQ_INFO * CAS_FN_ARG_REQ_INFO);
extern int fn_close_req_handle (int CAS_FN_ARG_SOCK_FD, int CAS_FN_ARG_ARGC,
				void **CAS_FN_ARG_ARGV,
				T_NET_BUF * CAS_FN_ARG_NET_BUF,
				T_REQ_INFO * CAS_FN_ARG_REQ_INFO);
extern int fn_cursor (int CAS_FN_ARG_SOCK_FD, int CAS_FN_ARG_ARGC,
		      void **CAS_FN_ARG_ARGV,
		      T_NET_BUF * CAS_FN_ARG_NET_BUF,
		      T_REQ_INFO * CAS_FN_ARG_REQ_INFO);
extern int fn_fetch (int CAS_FN_ARG_SOCK_FD, int CAS_FN_ARG_ARGC,
		     void **CAS_FN_ARG_ARGV,
		     T_NET_BUF * CAS_FN_ARG_NET_BUF,
		     T_REQ_INFO * CAS_FN_ARG_REQ_INFO);
extern int fn_schema_info (int CAS_FN_ARG_SOCK_FD, int CAS_FN_ARG_ARGC,
			   void **CAS_FN_ARG_ARGV,
			   T_NET_BUF * CAS_FN_ARG_NET_BUF,
			   T_REQ_INFO * CAS_FN_ARG_REQ_INFO);
extern int fn_glo_new (int CAS_FN_ARG_SOCK_FD, int CAS_FN_ARG_ARGC,
		       void **CAS_FN_ARG_ARGV,
		       T_NET_BUF * CAS_FN_ARG_NET_BUF,
		       T_REQ_INFO * CAS_FN_ARG_REQ_INFO);
extern int fn_glo_save (int CAS_FN_ARG_SOCK_FD, int CAS_FN_ARG_ARGC,
			void **CAS_FN_ARG_ARGV,
			T_NET_BUF * CAS_FN_ARG_NET_BUF,
			T_REQ_INFO * CAS_FN_ARG_REQ_INFO);
extern int fn_glo_load (int CAS_FN_ARG_SOCK_FD, int CAS_FN_ARG_ARGC,
			void **CAS_FN_ARG_ARGV,
			T_NET_BUF * CAS_FN_ARG_NET_BUF,
			T_REQ_INFO * CAS_FN_ARG_REQ_INFO);
extern int fn_get_db_version (int CAS_FN_ARG_SOCK_FD, int CAS_FN_ARG_ARGC,
			      void **CAS_FN_ARG_ARGV,
			      T_NET_BUF * CAS_FN_ARG_NET_BUF,
			      T_REQ_INFO * CAS_FN_ARG_REQ_INFO);
extern int fn_next_result (int CAS_FN_ARG_SOCK_FD, int CAS_FN_ARG_ARGC,
			   void **CAS_FN_ARG_ARGV,
			   T_NET_BUF * CAS_FN_ARG_NET_BUF,
			   T_REQ_INFO * CAS_FN_ARG_REQ_INFO);
extern int fn_execute_batch (int CAS_FN_ARG_SOCK_FD, int CAS_FN_ARG_ARGC,
			     void **CAS_FN_ARG_ARGV,
			     T_NET_BUF * CAS_FN_ARG_NET_BUF,
			     T_REQ_INFO * CAS_FN_ARG_REQ_INFO);
extern int fn_execute_array (int CAS_FN_ARG_SOCK_FD, int CAS_FN_ARG_ARGC,
			     void **CAS_FN_ARG_ARGV,
			     T_NET_BUF * CAS_FN_ARG_NET_BUF,
			     T_REQ_INFO * CAS_FN_ARG_REQ_INFO);
extern int fn_get_attr_type_str (int CAS_FN_ARG_SOCK_FD, int CAS_FN_ARG_ARGC,
				 void **CAS_FN_ARG_ARGV,
				 T_NET_BUF * CAS_FN_ARG_NET_BUF,
				 T_REQ_INFO * CAS_FN_ARG_REQ_INFO);
extern int fn_xa_prepare (int CAS_FN_ARG_SOCK_FD, int CAS_FN_ARG_ARGC,
			  void **CAS_FN_ARG_ARGV,
			  T_NET_BUF * CAS_FN_ARG_NET_BUF,
			  T_REQ_INFO * CAS_FN_ARG_REQ_INFO);
extern int fn_xa_recover (int CAS_FN_ARG_SOCK_FD, int CAS_FN_ARG_ARGC,
			  void **CAS_FN_ARG_ARGV,
			  T_NET_BUF * CAS_FN_ARG_NET_BUF,
			  T_REQ_INFO * CAS_FN_ARG_REQ_INFO);
extern int fn_xa_end_tran (int CAS_FN_ARG_SOCK_FD, int CAS_FN_ARG_ARGC,
			   void **CAS_FN_ARG_ARGV,
			   T_NET_BUF * CAS_FN_ARG_NET_BUF,
			   T_REQ_INFO * CAS_FN_ARG_REQ_INFO);
extern int fn_con_close (int CAS_FN_ARG_SOCK_FD, int CAS_FN_ARG_ARGC,
			 void **CAS_FN_ARG_ARGV,
			 T_NET_BUF * CAS_FN_ARG_NET_BUF,
			 T_REQ_INFO * CAS_FN_ARG_REQ_INFO);
extern int fn_check_cas (int CAS_FN_ARG_SOCK_FD, int CAS_FN_ARG_ARGC,
			 void **CAS_FN_ARG_ARGV,
			 T_NET_BUF * CAS_FN_ARG_NET_BUF,
			 T_REQ_INFO * CAS_FN_ARG_REQ_INFO);
extern int fn_make_out_rs (int CAS_FN_ARG_SOCK_FD, int CAS_FN_ARG_ARGC,
			   void **CAS_FN_ARG_ARGV,
			   T_NET_BUF * CAS_FN_ARG_NET_BUF,
			   T_REQ_INFO * CAS_FN_ARG_REQ_INFO);
extern int fn_get_generated_keys (int CAS_FN_ARG_SOCK_FD, int CAS_FN_ARG_ARGC,
				  void **CAS_FN_ARG_ARGV,
				  T_NET_BUF * CAS_FN_ARG_NET_BUF,
				  T_REQ_INFO * CAS_FN_ARG_REQ_INFO);
extern int fn_oid_get (int CAS_FN_ARG_SOCK_FD, int CAS_FN_ARG_ARGC,
		       void **CAS_FN_ARG_ARGV,
		       T_NET_BUF * CAS_FN_ARG_NET_BUF,
		       T_REQ_INFO * CAS_FN_ARG_REQ_INFO);
extern int fn_oid_put (int CAS_FN_ARG_SOCK_FD, int CAS_FN_ARG_ARGC,
		       void **CAS_FN_ARG_ARGV,
		       T_NET_BUF * CAS_FN_ARG_NET_BUF,
		       T_REQ_INFO * CAS_FN_ARG_REQ_INFO);
extern int fn_get_class_num_objs (int CAS_FN_ARG_SOCK_FD, int CAS_FN_ARG_ARGC,
				  void **CAS_FN_ARG_ARGV,
				  T_NET_BUF * CAS_FN_ARG_NET_BUF,
				  T_REQ_INFO * CAS_FN_ARG_REQ_INFO);
extern int fn_oid (int CAS_FN_ARG_SOCK_FD, int CAS_FN_ARG_ARGC,
		   void **CAS_FN_ARG_ARGV,
		   T_NET_BUF * CAS_FN_ARG_NET_BUF,
		   T_REQ_INFO * CAS_FN_ARG_REQ_INFO);
extern int fn_collection (int CAS_FN_ARG_SOCK_FD, int CAS_FN_ARG_ARGC,
			  void **CAS_FN_ARG_ARGV,
			  T_NET_BUF * CAS_FN_ARG_NET_BUF,
			  T_REQ_INFO * CAS_FN_ARG_REQ_INFO);
extern int fn_cursor_update (int CAS_FN_ARG_SOCK_FD, int CAS_FN_ARG_ARGC,
			     void **CAS_FN_ARG_ARGV,
			     T_NET_BUF * CAS_FN_ARG_NET_BUF,
			     T_REQ_INFO * CAS_FN_ARG_REQ_INFO);
extern int fn_get_query_info (int CAS_FN_ARG_SOCK_FD, int CAS_FN_ARG_ARGC,
			      void **CAS_FN_ARG_ARGV,
			      T_NET_BUF * CAS_FN_ARG_NET_BUF,
			      T_REQ_INFO * CAS_FN_ARG_REQ_INFO);
extern int fn_savepoint (int CAS_FN_ARG_SOCK_FD, int CAS_FN_ARG_ARGC,
			 void **CAS_FN_ARG_ARGV,
			 T_NET_BUF * CAS_FN_ARG_NET_BUF,
			 T_REQ_INFO * CAS_FN_ARG_REQ_INFO);
extern int fn_parameter_info (int CAS_FN_ARG_SOCK_FD, int CAS_FN_ARG_ARGC,
			      void **CAS_FN_ARG_ARGV,
			      T_NET_BUF * CAS_FN_ARG_NET_BUF,
			      T_REQ_INFO * CAS_FN_ARG_REQ_INFO);
extern int fn_glo_cmd (int CAS_FN_ARG_SOCK_FD, int CAS_FN_ARG_ARGC,
		       void **CAS_FN_ARG_ARGV,
		       T_NET_BUF * CAS_FN_ARG_NET_BUF,
		       T_REQ_INFO * CAS_FN_ARG_REQ_INFO);

#endif /* _CAS_FUNCTION_H_ */
