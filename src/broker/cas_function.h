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

typedef int (*T_SERVER_FUNC) (SOCKET, int, void **, T_NET_BUF *,
			      T_REQ_INFO *);

#define CAS_FN_ARG_SOCK_FD		sock_fd
#define CAS_FN_ARG_ARGC			argc
#define CAS_FN_ARG_ARGV			argv
#define CAS_FN_ARG_NET_BUF		net_buf
#define CAS_FN_ARG_REQ_INFO		req_info

extern int fn_end_tran (SOCKET CAS_FN_ARG_SOCK_FD, int CAS_FN_ARG_ARGC,
			void **CAS_FN_ARG_ARGV,
			T_NET_BUF * CAS_FN_ARG_NET_BUF,
			T_REQ_INFO * CAS_FN_ARG_REQ_INFO);
extern int fn_prepare (SOCKET CAS_FN_ARG_SOCK_FD, int CAS_FN_ARG_ARGC,
		       void **CAS_FN_ARG_ARGV,
		       T_NET_BUF * CAS_FN_ARG_NET_BUF,
		       T_REQ_INFO * CAS_FN_ARG_REQ_INFO);
extern int fn_execute (SOCKET CAS_FN_ARG_SOCK_FD, int CAS_FN_ARG_ARGC,
		       void **CAS_FN_ARG_ARGV,
		       T_NET_BUF * CAS_FN_ARG_NET_BUF,
		       T_REQ_INFO * CAS_FN_ARG_REQ_INFO);
#if !defined(CAS_FOR_ORACLE) && !defined(CAS_FOR_MYSQL)
extern int fn_get_db_parameter (SOCKET CAS_FN_ARG_SOCK_FD,
				int CAS_FN_ARG_ARGC, void **CAS_FN_ARG_ARGV,
				T_NET_BUF * CAS_FN_ARG_NET_BUF,
				T_REQ_INFO * CAS_FN_ARG_REQ_INFO);
extern int fn_set_db_parameter (SOCKET CAS_FN_ARG_SOCK_FD,
				int CAS_FN_ARG_ARGC, void **CAS_FN_ARG_ARGV,
				T_NET_BUF * CAS_FN_ARG_NET_BUF,
				T_REQ_INFO * CAS_FN_ARG_REQ_INFO);
#endif
extern int fn_close_req_handle (SOCKET CAS_FN_ARG_SOCK_FD,
				int CAS_FN_ARG_ARGC, void **CAS_FN_ARG_ARGV,
				T_NET_BUF * CAS_FN_ARG_NET_BUF,
				T_REQ_INFO * CAS_FN_ARG_REQ_INFO);
extern int fn_cursor (SOCKET CAS_FN_ARG_SOCK_FD, int CAS_FN_ARG_ARGC,
		      void **CAS_FN_ARG_ARGV, T_NET_BUF * CAS_FN_ARG_NET_BUF,
		      T_REQ_INFO * CAS_FN_ARG_REQ_INFO);
extern int fn_fetch (SOCKET CAS_FN_ARG_SOCK_FD, int CAS_FN_ARG_ARGC,
		     void **CAS_FN_ARG_ARGV, T_NET_BUF * CAS_FN_ARG_NET_BUF,
		     T_REQ_INFO * CAS_FN_ARG_REQ_INFO);
#if !defined(CAS_FOR_ORACLE) && !defined(CAS_FOR_MYSQL)
extern int fn_schema_info (SOCKET CAS_FN_ARG_SOCK_FD, int CAS_FN_ARG_ARGC,
			   void **CAS_FN_ARG_ARGV,
			   T_NET_BUF * CAS_FN_ARG_NET_BUF,
			   T_REQ_INFO * CAS_FN_ARG_REQ_INFO);
extern int fn_glo_new (SOCKET CAS_FN_ARG_SOCK_FD, int CAS_FN_ARG_ARGC,
		       void **CAS_FN_ARG_ARGV,
		       T_NET_BUF * CAS_FN_ARG_NET_BUF,
		       T_REQ_INFO * CAS_FN_ARG_REQ_INFO);
extern int fn_glo_save (SOCKET CAS_FN_ARG_SOCK_FD, int CAS_FN_ARG_ARGC,
			void **CAS_FN_ARG_ARGV,
			T_NET_BUF * CAS_FN_ARG_NET_BUF,
			T_REQ_INFO * CAS_FN_ARG_REQ_INFO);
extern int fn_glo_load (SOCKET CAS_FN_ARG_SOCK_FD, int CAS_FN_ARG_ARGC,
			void **CAS_FN_ARG_ARGV,
			T_NET_BUF * CAS_FN_ARG_NET_BUF,
			T_REQ_INFO * CAS_FN_ARG_REQ_INFO);
#endif
extern int fn_get_db_version (SOCKET CAS_FN_ARG_SOCK_FD, int CAS_FN_ARG_ARGC,
			      void **CAS_FN_ARG_ARGV,
			      T_NET_BUF * CAS_FN_ARG_NET_BUF,
			      T_REQ_INFO * CAS_FN_ARG_REQ_INFO);
extern int fn_next_result (SOCKET CAS_FN_ARG_SOCK_FD, int CAS_FN_ARG_ARGC,
			   void **CAS_FN_ARG_ARGV,
			   T_NET_BUF * CAS_FN_ARG_NET_BUF,
			   T_REQ_INFO * CAS_FN_ARG_REQ_INFO);
extern int fn_execute_batch (SOCKET CAS_FN_ARG_SOCK_FD, int CAS_FN_ARG_ARGC,
			     void **CAS_FN_ARG_ARGV,
			     T_NET_BUF * CAS_FN_ARG_NET_BUF,
			     T_REQ_INFO * CAS_FN_ARG_REQ_INFO);
extern int fn_execute_array (SOCKET CAS_FN_ARG_SOCK_FD, int CAS_FN_ARG_ARGC,
			     void **CAS_FN_ARG_ARGV,
			     T_NET_BUF * CAS_FN_ARG_NET_BUF,
			     T_REQ_INFO * CAS_FN_ARG_REQ_INFO);
extern int fn_get_attr_type_str (SOCKET CAS_FN_ARG_SOCK_FD,
				 int CAS_FN_ARG_ARGC, void **CAS_FN_ARG_ARGV,
				 T_NET_BUF * CAS_FN_ARG_NET_BUF,
				 T_REQ_INFO * CAS_FN_ARG_REQ_INFO);
extern int fn_xa_prepare (SOCKET CAS_FN_ARG_SOCK_FD, int CAS_FN_ARG_ARGC,
			  void **CAS_FN_ARG_ARGV,
			  T_NET_BUF * CAS_FN_ARG_NET_BUF,
			  T_REQ_INFO * CAS_FN_ARG_REQ_INFO);
extern int fn_xa_recover (SOCKET CAS_FN_ARG_SOCK_FD, int CAS_FN_ARG_ARGC,
			  void **CAS_FN_ARG_ARGV,
			  T_NET_BUF * CAS_FN_ARG_NET_BUF,
			  T_REQ_INFO * CAS_FN_ARG_REQ_INFO);
extern int fn_xa_end_tran (SOCKET CAS_FN_ARG_SOCK_FD, int CAS_FN_ARG_ARGC,
			   void **CAS_FN_ARG_ARGV,
			   T_NET_BUF * CAS_FN_ARG_NET_BUF,
			   T_REQ_INFO * CAS_FN_ARG_REQ_INFO);
extern int fn_con_close (SOCKET CAS_FN_ARG_SOCK_FD, int CAS_FN_ARG_ARGC,
			 void **CAS_FN_ARG_ARGV,
			 T_NET_BUF * CAS_FN_ARG_NET_BUF,
			 T_REQ_INFO * CAS_FN_ARG_REQ_INFO);
extern int fn_check_cas (SOCKET CAS_FN_ARG_SOCK_FD, int CAS_FN_ARG_ARGC,
			 void **CAS_FN_ARG_ARGV,
			 T_NET_BUF * CAS_FN_ARG_NET_BUF,
			 T_REQ_INFO * CAS_FN_ARG_REQ_INFO);
extern int fn_make_out_rs (SOCKET CAS_FN_ARG_SOCK_FD, int CAS_FN_ARG_ARGC,
			   void **CAS_FN_ARG_ARGV,
			   T_NET_BUF * CAS_FN_ARG_NET_BUF,
			   T_REQ_INFO * CAS_FN_ARG_REQ_INFO);
extern int fn_get_generated_keys (SOCKET CAS_FN_ARG_SOCK_FD,
				  int CAS_FN_ARG_ARGC, void **CAS_FN_ARG_ARGV,
				  T_NET_BUF * CAS_FN_ARG_NET_BUF,
				  T_REQ_INFO * CAS_FN_ARG_REQ_INFO);
#if !defined(CAS_FOR_ORACLE) && !defined(CAS_FOR_MYSQL)
extern int fn_oid_get (SOCKET CAS_FN_ARG_SOCK_FD, int CAS_FN_ARG_ARGC,
		       void **CAS_FN_ARG_ARGV,
		       T_NET_BUF * CAS_FN_ARG_NET_BUF,
		       T_REQ_INFO * CAS_FN_ARG_REQ_INFO);
extern int fn_oid_put (SOCKET CAS_FN_ARG_SOCK_FD, int CAS_FN_ARG_ARGC,
		       void **CAS_FN_ARG_ARGV,
		       T_NET_BUF * CAS_FN_ARG_NET_BUF,
		       T_REQ_INFO * CAS_FN_ARG_REQ_INFO);
extern int fn_get_class_num_objs (SOCKET CAS_FN_ARG_SOCK_FD,
				  int CAS_FN_ARG_ARGC, void **CAS_FN_ARG_ARGV,
				  T_NET_BUF * CAS_FN_ARG_NET_BUF,
				  T_REQ_INFO * CAS_FN_ARG_REQ_INFO);
extern int fn_oid (SOCKET CAS_FN_ARG_SOCK_FD, int CAS_FN_ARG_ARGC,
		   void **CAS_FN_ARG_ARGV, T_NET_BUF * CAS_FN_ARG_NET_BUF,
		   T_REQ_INFO * CAS_FN_ARG_REQ_INFO);
extern int fn_collection (SOCKET CAS_FN_ARG_SOCK_FD, int CAS_FN_ARG_ARGC,
			  void **CAS_FN_ARG_ARGV,
			  T_NET_BUF * CAS_FN_ARG_NET_BUF,
			  T_REQ_INFO * CAS_FN_ARG_REQ_INFO);
#endif
extern int fn_cursor_update (SOCKET CAS_FN_ARG_SOCK_FD, int CAS_FN_ARG_ARGC,
			     void **CAS_FN_ARG_ARGV,
			     T_NET_BUF * CAS_FN_ARG_NET_BUF,
			     T_REQ_INFO * CAS_FN_ARG_REQ_INFO);
extern int fn_get_query_info (SOCKET CAS_FN_ARG_SOCK_FD, int CAS_FN_ARG_ARGC,
			      void **CAS_FN_ARG_ARGV,
			      T_NET_BUF * CAS_FN_ARG_NET_BUF,
			      T_REQ_INFO * CAS_FN_ARG_REQ_INFO);
#if !defined(CAS_FOR_ORACLE) && !defined(CAS_FOR_MYSQL)
extern int fn_savepoint (SOCKET CAS_FN_ARG_SOCK_FD, int CAS_FN_ARG_ARGC,
			 void **CAS_FN_ARG_ARGV,
			 T_NET_BUF * CAS_FN_ARG_NET_BUF,
			 T_REQ_INFO * CAS_FN_ARG_REQ_INFO);
#endif
extern int fn_parameter_info (SOCKET CAS_FN_ARG_SOCK_FD, int CAS_FN_ARG_ARGC,
			      void **CAS_FN_ARG_ARGV,
			      T_NET_BUF * CAS_FN_ARG_NET_BUF,
			      T_REQ_INFO * CAS_FN_ARG_REQ_INFO);
#if !defined(CAS_FOR_ORACLE) && !defined(CAS_FOR_MYSQL)
extern int fn_glo_cmd (SOCKET CAS_FN_ARG_SOCK_FD, int CAS_FN_ARG_ARGC,
		       void **CAS_FN_ARG_ARGV,
		       T_NET_BUF * CAS_FN_ARG_NET_BUF,
		       T_REQ_INFO * CAS_FN_ARG_REQ_INFO);
#endif
#if defined(CAS_FOR_ORACLE) || defined(CAS_FOR_MYSQL)
extern int fn_not_supported (SOCKET CAS_FN_ARG_SOCK_FD, int CAS_FN_ARG_ARGC,
			     void **CAS_FN_ARG_ARGV,
			     T_NET_BUF * CAS_FN_ARG_NET_BUF,
			     T_REQ_INFO * CAS_FN_ARG_REQ_INFO);
#endif

#endif /* _CAS_FUNCTION_H_ */
