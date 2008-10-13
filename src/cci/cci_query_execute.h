/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * cci_query_execute.h - 
 */

#ifndef	_CCI_QUERY_EXECUTE_H_
#define	_CCI_QUERY_EXECUTE_H_

#ident "$Id$"

/************************************************************************
 * IMPORTED SYSTEM HEADER FILES						*
 ************************************************************************/

/************************************************************************
 * IMPORTED OTHER HEADER FILES						*
 ************************************************************************/

#include "cas_cci.h"
#include "cci_handle_mng.h"

#ifdef CCI_XA
#include "cci_xa.h"
#endif

/************************************************************************
 * EXPORTED DEFINITIONS							*
 ************************************************************************/

#define BIND_PTR_STATIC		0
#define BIND_PTR_DYNAMIC	1

#define SIZE_INT	sizeof(int)
#define SIZE_DOUBLE	sizeof(double)
#define SIZE_FLOAT	sizeof(float)
#define SIZE_DATE	6
#define SIZE_TIME	6
#define SIZE_TIMESTAMP	12
#define SIZE_OBJECT	8

#define NET_STR_TO_INT(INT_VALUE, PTR)		\
	do {					\
	  int		macro_var_tmp_value;		\
	  memcpy((char*) &macro_var_tmp_value, PTR, 4);	\
	  macro_var_tmp_value = ntohl(macro_var_tmp_value);		\
	  INT_VALUE = macro_var_tmp_value;		\
	} while (0)

#define NET_STR_TO_SHORT(SHORT_VALUE, PTR)	\
	do {					\
	  short		macro_var_tmp_value;		\
	  memcpy((char*) &macro_var_tmp_value, PTR, 2);	\
	  macro_var_tmp_value = ntohs(macro_var_tmp_value);		\
	  SHORT_VALUE = macro_var_tmp_value;		\
	} while (0)

#define NET_STR_TO_FLOAT(FLOAT_VALUE, PTR)	\
	do {					\
	  float		macro_var_tmp_value;		\
	  memcpy((char*) &macro_var_tmp_value, PTR, 4);	\
	  macro_var_tmp_value = ntohf(macro_var_tmp_value);		\
	  FLOAT_VALUE = macro_var_tmp_value;		\
	} while (0)

#define NET_STR_TO_DOUBLE(DOUBLE_VALUE, PTR)	\
	do {					\
	  double	macro_var_tmp_value;		\
	  memcpy((char*) &macro_var_tmp_value, PTR, 8);	\
	  macro_var_tmp_value = ntohd(macro_var_tmp_value);		\
	  DOUBLE_VALUE = macro_var_tmp_value;		\
	} while (0)

#define NET_STR_TO_DATE(DATE_VAL, PTR)		\
	do {					\
	  short		macro_var_yr, macro_var_mon, macro_var_day;		\
	  NET_STR_TO_SHORT(macro_var_yr, PTR);		\
	  NET_STR_TO_SHORT(macro_var_mon, PTR + 2);	\
	  NET_STR_TO_SHORT(macro_var_day, PTR + 4);	\
	  (DATE_VAL).yr = macro_var_yr;			\
	  (DATE_VAL).mon = macro_var_mon;			\
	  (DATE_VAL).day = macro_var_day;			\
	} while (0)

#define NET_STR_TO_TIME(TIME_VAL, PTR)		\
	do {					\
	  short		macro_var_hh, macro_var_mm, macro_var_ss;	\
	  NET_STR_TO_SHORT(macro_var_hh, PTR);		\
	  NET_STR_TO_SHORT(macro_var_mm, PTR + 2);	\
	  NET_STR_TO_SHORT(macro_var_ss, PTR + 4);	\
	  (TIME_VAL).hh = macro_var_hh;			\
	  (TIME_VAL).mm = macro_var_mm;			\
	  (TIME_VAL).ss = macro_var_ss;			\
	} while (0)

#define NET_STR_TO_TIMESTAMP(TS_VAL, PTR)	\
	do {					\
	  NET_STR_TO_DATE(TS_VAL, PTR);		\
	  NET_STR_TO_TIME(TS_VAL, PTR + 6);	\
	} while (0)

#define NET_STR_TO_OBJECT(OBJ_VAL, PTR)		\
	do {					\
	  int		macro_var_pageid;			\
	  short 	macro_var_volid, macro_var_slotid;		\
	  NET_STR_TO_INT(macro_var_pageid, PTR);		\
	  NET_STR_TO_SHORT(macro_var_slotid, PTR + 4);	\
	  NET_STR_TO_SHORT(macro_var_volid, PTR + 6);	\
	  (OBJ_VAL).pageid = macro_var_pageid;		\
	  (OBJ_VAL).slotid = macro_var_slotid;		\
	  (OBJ_VAL).volid = macro_var_volid;		\
	} while (0)

#define ADD_ARG_INT(BUF, VALUE)			\
	do {					\
	  net_buf_cp_int(BUF, 4);		\
	  net_buf_cp_int(BUF, VALUE);		\
	} while (0)

#ifdef UNICODE_DATA
#define ADD_ARG_STR(BUF, STR, SIZE)				\
	do {							\
	  char *_macro_tmp_str = STR;				\
	  int _macro_tmp_int = 0;				\
	  _macro_tmp_str = ut_ansi_to_unicode(STR);		\
	  if (_macro_tmp_str != NULL)				\
	    _macro_tmp_int = strlen(_macro_tmp_str) + 1;	\
	  net_buf_cp_int(BUF, _macro_tmp_int);			\
	  net_buf_cp_str(BUF, _macro_tmp_str, _macro_tmp_int);	\
	  FREE_MEM(_macro_tmp_str);				\
	} while (0)
#else
#define ADD_ARG_STR(BUF, STR, SIZE)				\
  	ADD_ARG_BYTES(BUF, STR, SIZE)
#endif

#define ADD_ARG_BYTES(BUF, STR, SIZE)		\
	do {					\
	  net_buf_cp_int(BUF, SIZE);		\
	  net_buf_cp_str(BUF, (char*) STR, SIZE);	\
	} while (0)

#define ADD_ARG_FLOAT(BUF, VALUE)		\
	do {					\
	  net_buf_cp_int(BUF, 4);		\
	  net_buf_cp_float(BUF, VALUE);		\
	} while (0)

#define ADD_ARG_DOUBLE(BUF, VALUE)		\
	do {					\
	  net_buf_cp_int(BUF, 8);		\
	  net_buf_cp_double(BUF, VALUE);	\
	} while (0)

#define ADD_ARG_TIMESTAMP(BUF, VALUE_P)		\
	do {					\
	  T_CCI_DATE	*macro_var_date_p = (T_CCI_DATE*) (VALUE_P);	\
	  net_buf_cp_int(BUF, SIZE_TIMESTAMP);	\
	  net_buf_cp_short(BUF, macro_var_date_p->yr);	\
	  net_buf_cp_short(BUF, macro_var_date_p->mon);	\
	  net_buf_cp_short(BUF, macro_var_date_p->day);	\
	  net_buf_cp_short(BUF, macro_var_date_p->hh);	\
	  net_buf_cp_short(BUF, macro_var_date_p->mm);	\
	  net_buf_cp_short(BUF, macro_var_date_p->ss);	\
	} while (0)

#define ADD_ARG_OBJECT(BUF, OID_P)			\
	do {						\
	  T_OBJECT	*macro_var_obj_p = (T_OBJECT*) (OID_P);	\
	  net_buf_cp_int(BUF, SIZE_OBJECT);		\
	  net_buf_cp_int(BUF, macro_var_obj_p->pageid);		\
	  net_buf_cp_short(BUF, macro_var_obj_p->slotid);		\
	  net_buf_cp_short(BUF, macro_var_obj_p->volid);		\
	} while (0)

#define ADD_ARG_CACHE_TIME(BUF, SEC, USEC)	\
	do {					\
	  net_buf_cp_int(BUF, 8);		\
	  net_buf_cp_int(BUF, SEC);		\
	  net_buf_cp_int(BUF, USEC);		\
	} while (0)

/************************************************************************
 * EXPORTED TYPE DEFINITIONS						*
 ************************************************************************/

/************************************************************************
 * EXPORTED FUNCTION PROTOTYPES						*
 ************************************************************************/

extern int qe_con_close (T_CON_HANDLE * con_handle);
extern int qe_prepare (T_REQ_HANDLE * req_handle,
		       int sock_fd,
		       char *sql_stmt,
		       char flag, T_CCI_ERROR * err_buf, int reuse);
extern void qe_bind_value_free (int num_bind, T_BIND_VALUE * bind_value);
extern int qe_bind_param (T_REQ_HANDLE * req_handle,
			  int index,
			  T_CCI_A_TYPE a_type,
			  void *value, T_CCI_U_TYPE u_type, char flag);
extern int qe_execute (T_REQ_HANDLE * req_handle,
		       int sock_fd,
		       char flag, int max_col_size, T_CCI_ERROR * err_buf);
extern int qe_end_tran (T_CON_HANDLE * con_handle,
			char type, T_CCI_ERROR * err_buf);
extern int qe_get_db_parameter (T_CON_HANDLE * con_handle,
				T_CCI_DB_PARAM param_name,
				void *value, T_CCI_ERROR * err_buf);
extern int qe_set_db_parameter (T_CON_HANDLE * con_handle,
				T_CCI_DB_PARAM param_name,
				void *value, T_CCI_ERROR * err_buf);
extern int qe_close_req_handle (T_REQ_HANDLE * req_handle, int sock_fd);
extern int qe_cursor (T_REQ_HANDLE * req_handle,
		      int sock_fd,
		      int offset, char origin, T_CCI_ERROR * err_buf);
extern int qe_fetch (T_REQ_HANDLE * req_handle,
		     int sock_fd,
		     char flag, int result_set_index, T_CCI_ERROR * err_buf);
extern int qe_get_data (T_REQ_HANDLE * req_handle,
			int col_no, int a_type, void *value, int *indicator);
extern int qe_get_cur_oid (T_REQ_HANDLE * req_handle, char *oid_str_buf);
extern int qe_glo_new (T_CON_HANDLE * con_handle,
		       char *class_name,
		       char *filename, char *oid_str, T_CCI_ERROR * err_buf);
extern int qe_glo_save (T_CON_HANDLE * con_handle,
			char *oid_str, char *filename, T_CCI_ERROR * err_buf);
extern int qe_glo_load (T_CON_HANDLE * con_handle,
			char *oid_str, int out_fd, T_CCI_ERROR * err_buf);
extern int qe_schema_info (T_REQ_HANDLE * req_handle,
			   int sock_fd,
			   int type,
			   char *class_name,
			   char *attr_name, char flag, T_CCI_ERROR * err_buf);
extern int qe_oid_get (T_REQ_HANDLE * req_handle,
		       int sock_fd,
		       char *oid_str,
		       char **attr_name, T_CCI_ERROR * err_buf);
extern int qe_oid_put (int sock_fd,
		       char *oid_str,
		       char **attr_name,
		       char **new_val, T_CCI_ERROR * err_buf);
extern int qe_oid_put2 (int sock_fd,
			char *oid_str,
			char **attr_name,
			void **new_val, int *a_type, T_CCI_ERROR * err_buf);
extern int qe_get_db_version (T_CON_HANDLE * con_handle,
			      char *out_buf, int buf_size);
extern int qe_get_class_num_objs (T_CON_HANDLE * con_handle,
				  char *class_name,
				  char flag,
				  int *num_objs,
				  int *num_pages, T_CCI_ERROR * err_buf);
extern int qe_oid_cmd (int sock_fd,
		       char cmd,
		       char *oid_str,
		       char *out_buf,
		       int out_buf_size, T_CCI_ERROR * err_buf);
extern int qe_col_get (T_REQ_HANDLE * req_handle,
		       int sock_fd,
		       char *oid_str,
		       char *col_attr,
		       int *col_size, int *col_type, T_CCI_ERROR * err_buf);
extern int qe_col_size (int sock_fd,
			char *oid_str,
			char *col_attr, int *col_size, T_CCI_ERROR * err_buf);
extern int qe_col_set_add_drop (int sock_fd,
				char col_cmd,
				char *oid_str,
				char *col_attr,
				char *value, T_CCI_ERROR * err_buf);
extern int qe_col_seq_op (int sock_fd,
			  char col_cmd,
			  char *oid_str,
			  char *col_attr,
			  int index, char *value, T_CCI_ERROR * err_buf);

extern int qe_next_result (T_REQ_HANDLE * req_handle,
			   char flag, int sock_fd, T_CCI_ERROR * err_buf);
extern int qe_execute_array (T_REQ_HANDLE * req_handle,
			     int sock_fd,
			     T_CCI_QUERY_RESULT ** qr, T_CCI_ERROR * err_buf);
extern void qe_query_result_free (int num_q, T_CCI_QUERY_RESULT * qr);
extern int qe_cursor_update (T_REQ_HANDLE * req_handle,
			     int sock_fd,
			     int cursor_pos,
			     int index,
			     T_CCI_A_TYPE a_type,
			     void *value, T_CCI_ERROR * err_buf);
extern int qe_execute_batch (int sock_fd,
			     int num_query,
			     char **sql_stmt,
			     T_CCI_QUERY_RESULT ** qr, T_CCI_ERROR * err_buf);
extern int qe_query_result_copy (T_REQ_HANDLE * req_handle,
				 T_CCI_QUERY_RESULT ** res_qr);

extern int qe_get_data_str (T_VALUE_BUF * conv_val_buf,
			    T_CCI_U_TYPE u_type,
			    char *col_value_p,
			    int col_val_size, void *value, int *indicator);
extern int qe_get_data_int (T_CCI_U_TYPE u_type,
			    char *col_value_p, void *value);
extern int qe_get_data_float (T_CCI_U_TYPE u_type,
			      char *col_value_p, void *value);
extern int qe_get_data_double (T_CCI_U_TYPE u_type,
			       char *col_value_p, void *value);
extern int qe_get_data_date (T_CCI_U_TYPE u_type,
			     char *col_value_p, void *value);
extern int qe_get_data_bit (T_CCI_U_TYPE u_type,
			    char *col_value_p, int col_val_size, void *value);
extern int qe_get_attr_type_str (int sock_fd,
				 char *class_name,
				 char *attr_name,
				 char *buf,
				 int buf_size, T_CCI_ERROR * err_buf);
extern int qe_get_query_info (T_REQ_HANDLE * req_handle,
			      int sock_fd, char log_type, char **out_buf);
extern int qe_savepoint_cmd (int sock_fd,
			     char cmd,
			     char *savepoint_name, T_CCI_ERROR * err_buf);
extern int qe_get_param_info (T_REQ_HANDLE * req_handle,
			      int sock_fd,
			      T_CCI_PARAM_INFO ** param,
			      T_CCI_ERROR * err_buf);
extern void qe_param_info_free (T_CCI_PARAM_INFO * param);

#ifdef CCI_XA
extern int qe_xa_prepare (T_CON_HANDLE * con_handle,
			  XID * xid, T_CCI_ERROR * err_buf);
extern int qe_xa_recover (T_CON_HANDLE * con_handle,
			  XID * xid, int num_xid, T_CCI_ERROR * err_buf);
extern int qe_xa_end_tran (T_CON_HANDLE * con_handle,
			   XID * xid, char type, T_CCI_ERROR * err_buf);
#endif

/************************************************************************
 * EXPORTED VARIABLES							*
 ************************************************************************/

#endif /* _CCI_QUERY_EXECUTE_H_ */
