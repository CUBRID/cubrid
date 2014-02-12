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
 * cci_stub.c -
 */

#include "config.h"

#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "api_util.h"
#include "api_common.h"
/* this is hack */
#define DBDEF_HEADER_
#include "cas_cci.h"
#undef DBDEF_HEADER_


/* ------------- */
/* API STRUCTURE */
/* ------------- */
typedef enum impl_handle_type IMPL_HANDLE_TYPE;
typedef struct connection_impl_s CONNECTION_IMPL;
typedef struct parameter_meta_impl_s PARAMETER_META_IMPL;
typedef struct batch_sql_item_s BATCH_SQL_ITEM;
typedef struct batch_ary_item_s BATCH_ARY_ITEM;
typedef struct batch_result_impl_s BATCH_RESULT_IMPL;
typedef struct statement_impl_s STATEMENT_IMPL;
typedef struct resultset_meta_impl_s RESULTSET_META_IMPL;
typedef struct resultset_impl_s RESULTSET_IMPL;
typedef struct api_val_s API_VAL;
typedef struct api_val_cci_bind_s API_VAL_CCI_BIND;
typedef struct cci_object_s CCI_OBJECT;
typedef struct cci_object_pool_s CCI_OBJECT_POOL;
typedef struct cci_collection_s CCI_COLLECTION;

struct connection_impl_s
{
  COMMON_API_STRUCTURE_HEADER;
  T_CCI_ERROR err_buf;
  int rid;
  CI_CONNECTION conn;
  int conn_handle;
  BH_INTERFACE *bh;
  /* option related */
  bool autocommit;
  int n_need_complete;
  /* object pool */
  CCI_OBJECT_POOL *opool;
};

struct parameter_meta_impl_s
{
  COMMON_API_STRUCTURE_HEADER;
  STATEMENT_IMPL *bptr;
};

struct batch_result_impl_s
{
  COMMON_API_STRUCTURE_HEADER;
  STATEMENT_IMPL *bptr;
};

struct batch_sql_item_s
{
  dlisth h;
  char sql[1];
};

struct batch_ary_item_s
{
  dlisth h;
  API_VAL *ary[1];
};

struct statement_impl_s
{
  COMMON_API_STRUCTURE_HEADER;
  CONNECTION_IMPL *pconn;
  int req_handle;
  int status;
  bool opt_updatable_result;
  bool opt_async_query;
  /*
   * parameter and parameter meta related fields
   * - PM : parameter meta data api structure header
   * - got_pm_handle : flag for indicating parameter related fields sanity
   * - pm_handle : handle
   * - num_col : number of column information
   * - col_info : column meta information
   * - params : parameter values
   */
  PARAMETER_META_IMPL PM;
  bool got_pm_handle;
  BIND_HANDLE pm_handle;
  int num_col;
  T_CCI_CUBRID_STMT cmd_type;
  T_CCI_COL_INFO *col_info;
  VALUE_INDEXER *params;
  /*
   * current result info (batch or multiple query in single statement)
   * - has_resultset : flag for field availablity
   * - res_handle : handle
   * - num_query : query_result array size
   * - curr_query_result_index
   * - query_result : query_result array
   * NOTE
   * cci_next_result() closes current result set
   */
  bool has_resultset;
  BIND_HANDLE res_handle;
  int num_query;
  int affected_rows;
  T_CCI_QUERY_RESULT *query_result;
  int curr_query_result_index;	/* 0 start */

  /*
   * fields for batch processing
   * batch : list of BATCH_SQL_ITEM or BATCH_ARY_ITEM w.r.t. status
   */
  int num_batch;
  dlisth batch;
  BATCH_RESULT_IMPL BR;
  BIND_HANDLE bres_handle;
};

struct resultset_meta_impl_s
{
  COMMON_RESULTSET_META_HEADER;
  RESULTSET_IMPL *bp;
};

struct resultset_impl_s
{
  COMMON_RESULTSET_HEADER;
  RESULTSET_META_IMPL RM;

  /*
   * provided (do not try to destroy them)
   * parent, bh, req_handle, err_buf, on_close
   */
  COMMON_API_STRUCTURE *parent;
  bool updatable;
  CI_CONNECTION conn;
  BH_INTERFACE *bh;
  int req_handle;
  T_CCI_ERROR *err_buf;
  void *arg;
  /* called when this structure is to be destroyed by resultset_impl_dtor */
  void (*on_close) (RESULTSET_IMPL * pres, void *arg);

  /*
   * result set cursor related
   */
  int offset;
  bool fetched;
  VALUE_INDEXER *updated_values;
  /*
   * resultset meta fields (lazy initialized)
   */
  bool got_rm_handle;
  BIND_HANDLE rm_handle;
  int num_col;
  T_CCI_CUBRID_STMT cmd_type;
  T_CCI_COL_INFO *col_info;
};

struct api_val_s
{
  CI_TYPE type;
  void *ptr;
  size_t len;
};

struct api_val_cci_bind_s
{
  CI_CONNECTION conn;
  int flag;
  T_CCI_A_TYPE atype;
  void *value;
  bool redirected;
  union
  {
    T_CCI_BIT bit_val;
    T_CCI_DATE data_val;
    int int_val;
    char *cp;
    char oid_buf[32];
    T_CCI_SET tset;
  } redirect;
};
#define API_VAL_CCI_BIND_FLAG_GET 0x01
#define API_VAL_CCI_BIND_FLAG_SET 0x02

struct cci_object_s
{
  CI_OID xoid;
  bool deleted;
  int req_handle;
  RESULTSET_IMPL *pres;
  CCI_OBJECT_POOL *pool;
};

struct cci_object_pool_s
{
  API_OBJECT_RESULTSET_POOL ifs;
  CONNECTION_IMPL *pconn;
  hash_table *ht;		/* CI_OID to CCI_OBJECT */
  hash_table *ght;		/* CI_OID to GLO_OFFSET */
  T_CCI_ERROR err_buf;
};

struct cci_collection_s
{
  API_COLLECTION col;
  CI_TYPE type;			/* only supports homogenious collection type */
  VALUE_INDEXER *indexer;	/* value is pointer to API_VAL */
  CI_CONNECTION conn;		/* CCI_U_TYPE_OBJECT */
};

/* ------------------- */
/* forward declaration */
/* ------------------- */
/* bind management */
static int bind_api_structure (BH_INTERFACE * bh, COMMON_API_STRUCTURE * s,
			       COMMON_API_STRUCTURE * parent,
			       BIND_HANDLE * handle);
/* error handling */
static int err_from_cci (int err);

/* connection */
static void connection_impl_dtor (BH_BIND * bind);
static void init_connection_impl (CONNECTION_IMPL * impl, int rid,
				  CI_CONNECTION conn,
				  BH_INTERFACE * bh, int conn_handle,
				  CCI_OBJECT_POOL * pool);
static int complete_connection (CONNECTION_IMPL * impl);
/* api value and cci value binding */
static void api_val_dtor (VALUE_AREA * va, API_VALUE * val);
static CI_TYPE cci_u_type_to_type (T_CCI_U_TYPE utype);
static T_CCI_U_TYPE type_to_cci_u_type (CI_TYPE type);
static int get_value_from_api_val (const API_VAL * pv, CI_TYPE type,
				   void *addr, size_t len, size_t * outlen,
				   bool * isnull);
static int set_value_to_api_val (API_VAL * pv, CI_TYPE type, void *addr,
				 size_t len);
static int get_type_value_size (CI_TYPE type, int *len);
static void api_val_cci_bind_init (API_VAL_CCI_BIND * bind,
				   CI_CONNECTION conn, int flag);
static int api_val_cci_bind_bind (CI_TYPE type, void *ptr, int len,
				  API_VAL_CCI_BIND * bind);
static void api_val_cci_bind_clear (API_VAL_CCI_BIND * bind);

static int api_val_bind_param (CI_CONNECTION conn, int req_handle,
			       API_VAL * pv, int index);
static int get_value_from_req_handle (CI_CONNECTION conn, int req_handle,
				      int index, CI_TYPE type, void *addr,
				      size_t len, size_t * outlen,
				      bool * is_null);
static int get_value_from_tset (T_CCI_U_TYPE utype, T_CCI_SET tset,
				CI_TYPE type, CI_CONNECTION conn, int i,
				API_VAL ** pv);
static int api_val_cursor_update (CI_CONNECTION conn, int req_handle,
				  int offset, int index, API_VAL * av,
				  T_CCI_ERROR * err_buf);
/* parameter meta */
/* resultset ifs impl */
static int lazy_bind_qres_rmeta (RESULTSET_IMPL * pres);
static int api_qres_get_resultset_metadata (API_RESULTSET * res,
					    API_RESULTSET_META ** rimpl);
static int api_qres_fetch (API_RESULTSET * res, int offset,
			   CI_FETCH_POSITION pos);
static int api_qres_tell (API_RESULTSET * res, int *offset);
static int api_qres_clear_updates (API_RESULTSET * res);
static int api_qres_delete_row (API_RESULTSET * res);
static int api_qres_get_value (API_RESULTSET * res, int index,
			       CI_TYPE type, void *addr, size_t len,
			       size_t * outlen, bool * is_null);
static int api_qres_get_value_by_name (API_RESULTSET * res, const char *name,
				       CI_TYPE type, void *addr,
				       size_t len, size_t * outlen,
				       bool * isnull);
static int api_qres_update_value (API_RESULTSET * res, int index,
				  CI_TYPE type, void *addr, size_t len);
static int api_qres_apply_update (API_RESULTSET * res);
static void api_qres_destroy (API_RESULTSET * res);
static int create_resultset_impl (CI_CONNECTION conn,
				  BH_INTERFACE * bh,
				  int req_handle,
				  T_CCI_ERROR * err_buf,
				  COMMON_API_STRUCTURE * parent,
				  bool updatable,
				  void *arg,
				  void (*on_close) (RESULTSET_IMPL *, void *),
				  API_RESULTSET_IFS * resifs,
				  API_RESULTSET_META_IFS * rmifs,
				  RESULTSET_IMPL ** rpres);
static void resultset_impl_dtor (BH_BIND * bind);

/* resultset meta ifs impl */
static int api_qrmeta_get_count (API_RESULTSET_META * rm, int *count);
static int api_qrmeta_get_info (API_RESULTSET_META * rm, int index,
				CI_RMETA_INFO_TYPE type, void *arg,
				size_t size);
/* statement */
static int lazy_bind_pstmt_pmeta (STATEMENT_IMPL * pstmt);
static void statement_impl_dtor (BH_BIND * bind);
static void init_statement_impl (STATEMENT_IMPL * pstmt,
				 CONNECTION_IMPL * pconn);
static int statement_execute (STATEMENT_IMPL * pstmt, T_CCI_ERROR * err_buf);
static int statement_get_reshandle_or_affectedrows (BH_INTERFACE * bh,
						    STATEMENT_IMPL * pstmt);
static int complete_statement (STATEMENT_IMPL * pstmt);
static void on_close_statement_res (RESULTSET_IMPL * impl, void *arg);

static int
add_batch_params_restore_mapf (void *arg, int index, VALUE_AREA * va,
			       API_VALUE * val);
static int
add_batch_params_mapf (void *arg, int index, VALUE_AREA * va,
		       API_VALUE * val);
static int stmt_execute_batch_sql (STATEMENT_IMPL * pstmt);
static int stmt_execute_batch_array (STATEMENT_IMPL * pstmt);
static int stmt_complete_batch (STATEMENT_IMPL * pstmt);

/* object resultset and resultset meta api */
static int api_ormeta_get_count (API_RESULTSET_META * rm, int *count);
static int api_ormeta_get_info (API_RESULTSET_META * rm, int index,
				CI_RMETA_INFO_TYPE type, void *arg,
				size_t size);
static int api_ores_get_resultset_metadata (API_RESULTSET * res,
					    API_RESULTSET_META ** rimpl);
static int api_ores_fetch (API_RESULTSET * res, int offset,
			   CI_FETCH_POSITION pos);
static int api_ores_tell (API_RESULTSET * res, int *offset);
static int api_ores_clear_updates (API_RESULTSET * res);
static int api_ores_delete_row (API_RESULTSET * res);
static int api_ores_get_value (API_RESULTSET * res, int index,
			       CI_TYPE type, void *addr, size_t len,
			       size_t * outlen, bool * is_null);
static int api_ores_get_value_by_name (API_RESULTSET * res, const char *name,
				       CI_TYPE type, void *addr,
				       size_t len, size_t * outlen,
				       bool * isnull);
static int api_ores_update_value (API_RESULTSET * res, int index,
				  CI_TYPE type, void *addr, size_t len);
static int api_ores_apply_update (API_RESULTSET * res);
static void api_ores_destroy (API_RESULTSET * res);


/* api object resultset pool implementation */
static int opool_get_object_resultset (API_OBJECT_RESULTSET_POOL * poo,
				       CI_OID * oid, API_RESULTSET ** rres);
static int opool_oid_delete (API_OBJECT_RESULTSET_POOL * poo, CI_OID * oid);
static int opool_oid_get_classname (API_OBJECT_RESULTSET_POOL * pool,
				    CI_OID * oid, char *name, size_t size);
static void opool_destroy (API_OBJECT_RESULTSET_POOL * poo);

/* object pool related functions */
static int create_cci_object (CCI_OBJECT_POOL * pool, CI_OID * oid,
			      CCI_OBJECT ** cobj);
static void destroy_cci_object (CCI_OBJECT * obj);
static void on_close_opool_res (RESULTSET_IMPL * impl, void *arg);
static void xoid2oidstr (const CI_OID * xoid, char *oidbuf);
static int oidstr2xoid (const char *oidstr, CI_CONNECTION conn,
			CI_OID * xoid);

static int opool_ht_comparef (void *key1, void *key2, int *r);
static int opool_ht_hashf (void *key, unsigned int *rv);
static int opool_ht_keyf (void *elem, void **rk);
static void opool_ht_elem_dtor (void *elem);
static int opool_ght_keyf (void *elem, void **rk);
static void opool_ght_elem_dtor (void *elem);
static int opool_create (CONNECTION_IMPL * pconn, CCI_OBJECT_POOL ** rpool);
/* collection interface related */
static int api_col_length (API_COLLECTION * col, int *len);
static int api_col_insert (API_COLLECTION * col, long pos, CI_TYPE type,
			   void *ptr, size_t size);
static int api_col_update (API_COLLECTION * col, long pos, CI_TYPE type,
			   void *ptr, size_t size);
static int api_col_delete (API_COLLECTION * col, long pos);
static int api_col_get_elem_domain_info (API_COLLECTION * col, long pos,
					 CI_TYPE * type, int *precision,
					 int *scale);
static int api_col_get_elem (API_COLLECTION * col, long pos, CI_TYPE type,
			     void *addr, size_t len, size_t * outlen,
			     bool * isnull);
static void api_col_destroy (API_COLLECTION * col);

/* collection releated */
static void xcol_elem_dtor (VALUE_AREA * va, API_VALUE * av);
static int xcol_create (CI_TYPE type, CI_CONNECTION conn,
			CI_COLLECTION * rcol);
static void xcol_destroy (CI_COLLECTION col);
static int xcol_elem_cci_bind_mapf (void *arg, int index, VALUE_AREA * va,
				    API_VALUE * av);
static int xcol_to_cci_set (CI_COLLECTION col, T_CCI_SET * tset);

static int cci_set_to_xcol (CI_CONNECTION conn, T_CCI_SET tset,
			    CI_COLLECTION * col);
static int xcol_copy (CI_COLLECTION col, CI_COLLECTION * rcol);



/*
 * bind_api_structure - make handle for COMMON_API_STRUCTURE s and
 *                      bind the handle to the handle of COMMON_API_STRUCTURE
 *                      parent
 *    return: NO_ERROR if successful, error code otherwise
 *    bh(in): BH_INTERFACE
 *    s(in): pointer to COMMON_API_STRUCTURE
 *    parent(in):pointer to COMMON_API_STRUCTURE
 *    handle(out): BIND_HANDLE
 */
static int
bind_api_structure (BH_INTERFACE * bh, COMMON_API_STRUCTURE * s,
		    COMMON_API_STRUCTURE * parent, BIND_HANDLE * handle)
{
  int res;

  assert (s != NULL);
  assert (handle != NULL);

  res = bh->alloc_handle (bh, (BH_BIND *) s, handle);
  if (res != NO_ERROR)
    {
      return res;
    }

  if (parent != NULL)
    {
      res = bh->bind_graft (bh, (BH_BIND *) s, (BH_BIND *) parent);
      if (res != NO_ERROR)
	{			/* destroy handle only */
	  bh_destroyf dtor = s->bind.dtor;
	  s->bind.dtor = NULL;
	  bh->destroy_handle (bh, *handle);
	  s->bind.dtor = dtor;
	}
      return res;
    }

  return NO_ERROR;
}

/*
 * err_from_cci - convert CCI error code to CUBRID API error code
 *    return: error code
 *    err(in): CCI error code
 */
static int
err_from_cci (int err)
{
  switch (err)
    {
    case CCI_ER_NO_ERROR:
      return NO_ERROR;
    case CCI_ER_DBMS:
      return ER_INTERFACE_DBMS;
    case CCI_ER_CON_HANDLE:
    case CCI_ER_NO_MORE_MEMORY:
    case CCI_ER_COMMUNICATION:
    case CCI_ER_NO_MORE_DATA:
    case CCI_ER_TRAN_TYPE:
    case CCI_ER_STRING_PARAM:
    case CCI_ER_TYPE_CONVERSION:
    case CCI_ER_BIND_INDEX:
    case CCI_ER_ATYPE:
    case CCI_ER_PARAM_NAME:
    case CCI_ER_COLUMN_INDEX:
    case CCI_ER_SCHEMA_TYPE:
    case CCI_ER_FILE:
    case CCI_ER_CONNECT:
    case CCI_ER_ALLOC_CON_HANDLE:
    case CCI_ER_REQ_HANDLE:
    case CCI_ER_INVALID_CURSOR_POS:
    case CCI_ER_HOSTNAME:
    case CCI_ER_OBJECT:
    case CCI_ER_SET_INDEX:
    case CCI_ER_DELETED_TUPLE:
    case CCI_ER_SAVEPOINT_CMD:
    case CCI_ER_THREAD_RUNNING:
    case CCI_ER_ISOLATION_LEVEL:
    case CCI_ER_BIND_ARRAY_SIZE:
    case CCI_ER_OID_CMD:
    case CCI_ER_NOT_BIND:
    case CCI_ER_CAS:
    case CAS_ER_INTERNAL:
    case CAS_ER_NO_MORE_MEMORY:
    case CAS_ER_COMMUNICATION:
    case CAS_ER_ARGS:
    case CAS_ER_TRAN_TYPE:
    case CAS_ER_SRV_HANDLE:
    case CAS_ER_NUM_BIND:
    case CAS_ER_UNKNOWN_U_TYPE:
    case CAS_ER_DB_VALUE:
    case CAS_ER_TYPE_CONVERSION:
    case CAS_ER_PARAM_NAME:
    case CAS_ER_NO_MORE_DATA:
    case CAS_ER_OBJECT:
    case CAS_ER_OPEN_FILE:
    case CAS_ER_SCHEMA_TYPE:
    case CAS_ER_VERSION:
    case CAS_ER_FREE_SERVER:
    case CAS_ER_NOT_AUTHORIZED_CLIENT:
    case CAS_ER_QUERY_CANCEL:
    case CAS_ER_NOT_COLLECTION:
    case CAS_ER_COLLECTION_DOMAIN:
    case CAS_ER_NO_MORE_RESULT_SET:
    case CAS_ER_INVALID_CALL_STMT:
    case CAS_ER_STMT_POOLING:
    case CAS_ER_DBSERVER_DISCONNECTED:
    case CAS_ER_MAX_CLIENT_EXCEEDED:
    case CAS_ER_IS:
    case CCI_ER_NOT_IMPLEMENTED:
      return ER_INTERFACE_BROKER;
    default:
      return ER_INTERFACE_GENERIC;
    }
}

/*
 * connection_impl_dtor - CONNECTION_IMPL structure bind handle destructor
 *    return: void
 *    bind(in): pointer to CONNECTON_IMPL
 */
static void
connection_impl_dtor (BH_BIND * bind)
{
  CONNECTION_IMPL *impl = (CONNECTION_IMPL *) bind;

  if (impl)
    {
      if (impl->conn_handle >= 0)
	{
	  (void) cci_disconnect (impl->conn_handle, &impl->err_buf);
	}
      API_FREE (impl);
    }

  if (impl->opool)
    {
      opool_destroy ((API_OBJECT_RESULTSET_POOL *) impl->opool);
    }
}

/*
 * init_connection_impl - initialize CONNECTION_IMPL structure
 *    return: void
 *    pconn(int): pointer to CONNECTION_IMPL
 *    rid(int): root id
 *    conn(int): CI_CONNECTION
 *    bh(int): BH_INTERFACE
 *    conn_handle(int): CCI connection handle
 *    pool(in): CCI_OBJECT_POOL
 */
static void
init_connection_impl (CONNECTION_IMPL * pconn, int rid, CI_CONNECTION conn,
		      BH_INTERFACE * bh, int conn_handle,
		      CCI_OBJECT_POOL * pool)
{
  pconn->conn = conn;
  pconn->bind.dtor = connection_impl_dtor;
  pconn->handle_type = HANDLE_TYPE_CONNECTION;
  pconn->err_buf.err_code = 0;
  pconn->err_buf.err_msg[0] = 0;
  pconn->rid = rid;
  pconn->bh = bh;
  pconn->conn_handle = conn_handle;
  pconn->autocommit = false;
  pconn->n_need_complete = 0;
  pconn->opool = pool;
}

/*
 * completes all incompleted object bound to this connection
 */

/*
 * complete_connection - complete transaction boundary resources
 *    return: NO_ERROR if successful, error code otherwise
 *    pconn(in): pointer to CONNECTION_IMPL
 */
static int
complete_connection (CONNECTION_IMPL * pconn)
{
  COMMON_API_STRUCTURE *st;
  int res;

  res = pconn->bh->bind_get_first_child (pconn->bh, (BH_BIND *) pconn,
					 (BH_BIND **) & st);
  if (res != NO_ERROR)
    {
      return res;
    }

  while (st != NULL)
    {
      if (st->handle_type == HANDLE_TYPE_STATEMENT)
	{
	  res = complete_statement ((STATEMENT_IMPL *) st);
	  if (res != NO_ERROR)
	    {
	      return res;
	    }
	}
      res = pconn->bh->bind_get_next_sibling (pconn->bh, (BH_BIND *) st,
					      (BH_BIND **) & st);
      if (res != NO_ERROR)
	{
	  return res;
	}
    }

  return NO_ERROR;
}


/*
 * api_val_dtor - API_VAL VALUE_INDEXER destructor
 *    return: NO_ERROR if successful, error code otherwise
 *    va(in): VALUE_AREA
 *    val(in): API_VALUE
 */
static void
api_val_dtor (VALUE_AREA * va, API_VALUE * val)
{
  if (val != NULL)
    {
      API_VAL *pv = (API_VAL *) val;

      if (pv->type == CI_TYPE_COLLECTION)
	{
	  xcol_destroy (*(CI_COLLECTION *) pv->ptr);
	}

      if (pv->ptr)
	{
	  API_FREE (pv->ptr);
	}
      API_FREE (pv);
    }
}


/*
 * cci_u_type_to_type - map T_CCI_U_TYPE to CI_TYPE
 *    return: CI_TYPE
 *    utype(in): T_CCI_U_TYPE
 */
static CI_TYPE
cci_u_type_to_type (T_CCI_U_TYPE utype)
{
  switch (utype)
    {
    case CCI_U_TYPE_NULL:
      return CI_TYPE_NULL;
    case CCI_U_TYPE_CHAR:
      return CI_TYPE_CHAR;
    case CCI_U_TYPE_STRING:
      return CI_TYPE_VARCHAR;
    case CCI_U_TYPE_NCHAR:
      return CI_TYPE_NCHAR;
    case CCI_U_TYPE_VARNCHAR:
      return CI_TYPE_VARNCHAR;
    case CCI_U_TYPE_BIT:
      return CI_TYPE_BIT;
    case CCI_U_TYPE_VARBIT:
      return CI_TYPE_VARBIT;
    case CCI_U_TYPE_NUMERIC:
      return CI_TYPE_NUMERIC;
    case CCI_U_TYPE_BIGINT:
      return CI_TYPE_BIGINT;
    case CCI_U_TYPE_INT:
      return CI_TYPE_INT;
    case CCI_U_TYPE_SHORT:
      return CI_TYPE_SHORT;
    case CCI_U_TYPE_MONETARY:
      return CI_TYPE_MONETARY;
    case CCI_U_TYPE_FLOAT:
      return CI_TYPE_FLOAT;
    case CCI_U_TYPE_DOUBLE:
      return CI_TYPE_DOUBLE;
    case CCI_U_TYPE_DATE:
      return CI_TYPE_DATE;
    case CCI_U_TYPE_TIME:
      return CI_TYPE_TIME;
    case CCI_U_TYPE_TIMESTAMP:
      return CI_TYPE_TIMESTAMP;
    case CCI_U_TYPE_DATETIME:
      return CI_TYPE_DATETIME;
    case CCI_U_TYPE_SET:
    case CCI_U_TYPE_MULTISET:
    case CCI_U_TYPE_SEQUENCE:
      return CI_TYPE_COLLECTION;
    case CCI_U_TYPE_OBJECT:
      return CI_TYPE_OID;
    case CCI_U_TYPE_RESULTSET:
    default:
      return CI_TYPE_NULL;
    }
  return CI_TYPE_NULL;
}


/*
 * type_to_cci_u_type - map CI_TYPE to T_CCI_U_TYPE
 *    return: T_CCI_U_TYPE
 *    type(in): CI_TYPE
 */
static T_CCI_U_TYPE
type_to_cci_u_type (CI_TYPE type)
{
  switch (type)
    {
    case CI_TYPE_NULL:
      return CCI_U_TYPE_NULL;
    case CI_TYPE_BIGINT:
      return CCI_U_TYPE_BIGINT;
    case CI_TYPE_INT:
      return CCI_U_TYPE_INT;
    case CI_TYPE_SHORT:
      return CCI_U_TYPE_SHORT;
    case CI_TYPE_FLOAT:
      return CCI_U_TYPE_FLOAT;
    case CI_TYPE_DOUBLE:
      return CCI_U_TYPE_DOUBLE;
    case CI_TYPE_CHAR:
      return CCI_U_TYPE_CHAR;
    case CI_TYPE_VARCHAR:
      return CCI_U_TYPE_STRING;
    case CI_TYPE_NCHAR:
      return CCI_U_TYPE_NCHAR;
    case CI_TYPE_VARNCHAR:
      return CCI_U_TYPE_VARNCHAR;
    case CI_TYPE_BIT:
      return CCI_U_TYPE_BIT;
    case CI_TYPE_VARBIT:
      return CCI_U_TYPE_VARBIT;
    case CI_TYPE_TIME:
      return CCI_U_TYPE_TIME;
    case CI_TYPE_DATE:
      return CCI_U_TYPE_DATE;
    case CI_TYPE_TIMESTAMP:
      return CCI_U_TYPE_TIMESTAMP;
    case CI_TYPE_DATETIME:
      return CCI_U_TYPE_DATETIME;
    case CI_TYPE_MONETARY:
      return CCI_U_TYPE_MONETARY;
    case CI_TYPE_NUMERIC:
      return CCI_U_TYPE_NUMERIC;
    case CI_TYPE_OID:
      return CCI_U_TYPE_OBJECT;
    case CI_TYPE_COLLECTION:
      return CCI_U_TYPE_SEQUENCE;
    default:
      break;
    }
  return CCI_U_TYPE_NULL;
}

/*
 * get_value_from_api_val - get CUBRID C API value from API_VAL
 *    return: NO_ERROR if successful, error code otherwise
 *    pv(in): pointer to API_VAL
 *    type(in): CI_TYPE
 *    addr(out): address of the container of type value
 *    len(in): length of container pointed by addr field
 *    outlen(out): written size
 *    isnull(out): null indicator
 *
 * NOTE : no non-trvial conversion is allowed between CUBRID C API native
 *        type values
 */
static int
get_value_from_api_val (const API_VAL * pv, CI_TYPE type, void *addr,
			size_t len, size_t * outlen, bool * isnull)
{
  if (pv->type != type)
    return ER_INTERFACE_NOT_SUPPORTED_OPERATION;

  if (len < pv->len)
    return ER_INTERFACE_NOT_ENOUGH_DATA_SIZE;

  if (type == CI_TYPE_NULL)
    {
      *isnull = true;
      return NO_ERROR;
    }
  else if (type == CI_TYPE_COLLECTION)
    {
      CI_COLLECTION col;
      int res;

      res = xcol_copy (*(CI_COLLECTION *) pv->ptr, &col);
      if (res != NO_ERROR)
	return res;

      *(CI_COLLECTION *) addr = col;
      *outlen = sizeof (CI_COLLECTION);

      return NO_ERROR;
    }

  memcpy (addr, pv->ptr, pv->len);
  *outlen = pv->len;

  return NO_ERROR;
}

/*
 * set_value_to_api_val - set CUBRID C API type value to API_VAL structure
 *    return: NO_ERROR if successful, error code otherwise
 *    pv(in): pointer to API_VAL
 *    type(in): CI_TYPE
 *    addr(in): address of type value container
 *    len(in): length of type value container
 */
static int
set_value_to_api_val (API_VAL * pv, CI_TYPE type, void *addr, size_t len)
{
  void *ptr;

  /* clear previous value */
  if (pv->type == CI_TYPE_COLLECTION)
    {
      CI_COLLECTION col = *(CI_COLLECTION *) pv->ptr;

      if (col != NULL)
	xcol_destroy (col);
      pv->ptr = NULL;
    }

  /* reuse holder place if possible */
  if (pv->len >= len)
    {
      pv->type = type;
      memcpy (pv->ptr, addr, len);

      return NO_ERROR;
    }

  ptr = API_MALLOC (len + 1);
  if (!ptr)
    return ER_INTERFACE_NO_MORE_MEMORY;

  memcpy (ptr, addr, len);
  ((char *) ptr)[len] = 0;
  API_FREE (pv->ptr);
  pv->type = type;
  pv->ptr = ptr;
  pv->len = len;

  return NO_ERROR;
}

/*
 * get_type_value_size - get the size of container from CI_TYPE
 *    return: NO_ERROR if successful, error code otherwise
 *    type(in): CI_TYPE
 *    len(out): 0 if type is variable size
 *              >0 if type value is fixed size
 *              <0 on error
 */
static int
get_type_value_size (CI_TYPE type, int *len)
{
  assert (len != NULL);

  switch (type)
    {
    case CI_TYPE_BIGINT:
      *len = sizeof (INT64);
      break;

    case CI_TYPE_INT:
      *len = sizeof (int);
      break;

    case CI_TYPE_SHORT:
      *len = sizeof (short);
      break;

    case CI_TYPE_FLOAT:
      *len = sizeof (float);
      break;

    case CI_TYPE_DOUBLE:
    case CI_TYPE_MONETARY:
      *len = sizeof (double);
      break;

    case CI_TYPE_CHAR:
    case CI_TYPE_VARCHAR:
    case CI_TYPE_NCHAR:
    case CI_TYPE_VARNCHAR:
    case CI_TYPE_BIT:
    case CI_TYPE_VARBIT:
    case CI_TYPE_NUMERIC:
      *len = 0;
      break;

    case CI_TYPE_TIME:
    case CI_TYPE_DATE:
    case CI_TYPE_TIMESTAMP:
    case CI_TYPE_DATETIME:
      *len = sizeof (CI_TIME);
      break;

    case CI_TYPE_OID:
      *len = sizeof (CI_OID);
      break;

    case CI_TYPE_COLLECTION:
      *len = sizeof (CI_COLLECTION);
      break;

    case CI_TYPE_NULL:		/* should be checked before */
    default:
      *len = -1;
      return ER_INTERFACE_GENERIC;
    }

  return NO_ERROR;
}

/*
 * api_val_cci_bind_bind - set value, atype, directed fields of API_VAL_CCI_BIND
 *                         structure w.r.t. CI_TYPE
 *    return: NO_ERROR if successful, error code otherwise
 *    type(in): CI_TYPE
 *    ptr(in): address of value container
 *    len(in): length of value container
 *    bind(in): pointer to API_VAL_CCI_BIND structure
 */
static int
api_val_cci_bind_bind (CI_TYPE type, void *ptr, int len,
		       API_VAL_CCI_BIND * bind)
{
  bind->value = NULL;
  bind->atype = CCI_A_TYPE_STR;
  bind->redirected = false;

  if ((type != CI_TYPE_NULL && (ptr == NULL || len <= 0)))
    return ER_INTERFACE_INVALID_ARGUMENT;

  assert (bind != NULL);

  /* determine atype and value ptr */
  switch (type)
    {
    case CI_TYPE_NULL:
      bind->value = NULL;
      return NO_ERROR;

    case CI_TYPE_INT:
      if (len < SSIZEOF (int))
	{
	  return ER_INTERFACE_NOT_ENOUGH_DATA_SIZE;
	}
      bind->atype = CCI_A_TYPE_INT;
      bind->value = ptr;

      return NO_ERROR;

    case CI_TYPE_SHORT:
      if (len < SSIZEOF (short))
	{
	  return ER_INTERFACE_NOT_ENOUGH_DATA_SIZE;
	}

      bind->atype = CCI_A_TYPE_INT;
      bind->value = &bind->redirect.int_val;
      bind->redirected = true;
      if (bind->flag & API_VAL_CCI_BIND_FLAG_SET)
	{
	  bind->redirect.int_val = *(short *) ptr;
	}

      return NO_ERROR;

    case CI_TYPE_FLOAT:
      if (len < SSIZEOF (float))
	{
	  return ER_INTERFACE_NOT_ENOUGH_DATA_SIZE;
	}
      bind->atype = CCI_A_TYPE_FLOAT;
      bind->value = ptr;

      return NO_ERROR;

    case CI_TYPE_DOUBLE:
      if (len < SSIZEOF (float))
	{
	  return ER_INTERFACE_NOT_ENOUGH_DATA_SIZE;
	}

      bind->atype = CCI_A_TYPE_DOUBLE;
      bind->value = ptr;

      return NO_ERROR;

    case CI_TYPE_CHAR:
    case CI_TYPE_VARCHAR:
    case CI_TYPE_NCHAR:
    case CI_TYPE_VARNCHAR:
      if (len < 1)		/* need re-check after data get */
	{
	  return ER_INTERFACE_NOT_ENOUGH_DATA_SIZE;
	}

      bind->atype = CCI_A_TYPE_STR;
      if (bind->flag & API_VAL_CCI_BIND_FLAG_SET)
	bind->value = ptr;
      else
	{
	  bind->value = &bind->redirect.cp;
	  bind->redirected = true;
	}

      return NO_ERROR;

    case CI_TYPE_BIT:
    case CI_TYPE_VARBIT:
      bind->atype = CCI_A_TYPE_BIT;
      bind->redirected = true;

      if (bind->flag & API_VAL_CCI_BIND_FLAG_SET)
	{
	  bind->redirect.bit_val.size = (len + 7) / 8;
	  bind->redirect.bit_val.buf = (char *) ptr;
	  bind->value = &bind->redirect.bit_val;
	}
      else
	{
	  bind->value = &bind->redirect.bit_val;
	}

      return NO_ERROR;

    case CI_TYPE_TIME:
    case CI_TYPE_DATE:
    case CI_TYPE_TIMESTAMP:
    case CI_TYPE_DATETIME:
      if (len < SSIZEOF (CI_TIME))
	{
	  return ER_INTERFACE_NOT_ENOUGH_DATA_SIZE;
	}

      bind->atype = CCI_A_TYPE_DATE;
      bind->value = ptr;

      return NO_ERROR;

    case CI_TYPE_MONETARY:
      if (len < SSIZEOF (double))
	{
	  return ER_INTERFACE_NOT_ENOUGH_DATA_SIZE;
	}

      bind->atype = CCI_A_TYPE_DOUBLE;
      bind->value = ptr;

      return NO_ERROR;

    case CI_TYPE_NUMERIC:
      /* need re-check */
      if (len < 1)
	{
	  return ER_INTERFACE_NOT_ENOUGH_DATA_SIZE;
	}

      bind->atype = CCI_A_TYPE_STR;
      if (bind->flag & API_VAL_CCI_BIND_FLAG_SET)
	{
	  bind->value = ptr;
	}
      else
	{
	  bind->value = &bind->redirect.cp;
	  bind->redirected = true;
	}

      return NO_ERROR;

    case CI_TYPE_OID:
      /* need re-check (w.r.t. CI_TYPE) */
      if (len < 1)
	{
	  return ER_INTERFACE_NOT_ENOUGH_DATA_SIZE;
	}

      bind->atype = CCI_A_TYPE_STR;
      bind->redirected = true;
      bind->value = bind->redirect.oid_buf;
      if (bind->flag & API_VAL_CCI_BIND_FLAG_SET)
	{
	  xoid2oidstr ((CI_OID *) ptr, (char *) bind->value);
	}

      return NO_ERROR;

    case CI_TYPE_COLLECTION:
      if (len < SSIZEOF (CI_COLLECTION *))
	{
	  return ER_INTERFACE_NOT_ENOUGH_DATA_SIZE;
	}

      if (bind->flag & API_VAL_CCI_BIND_FLAG_SET)
	{
	  int res;
	  T_CCI_SET tset = NULL;

	  res = xcol_to_cci_set (*(CI_COLLECTION *) ptr, &tset);
	  if (res != NO_ERROR)
	    return res;

	  bind->atype = CCI_A_TYPE_SET;
	  bind->redirect.tset = tset;
	  bind->value = &bind->redirect.tset;
	  bind->redirected = true;
	}
      else
	{
	  bind->atype = CCI_A_TYPE_SET;
	  bind->redirect.tset = NULL;
	  bind->value = &bind->redirect.tset;
	}

      return NO_ERROR;

    default:
      return ER_INTERFACE_INVALID_ARGUMENT;
    }
}


/*
 * api_val_cci_bind_init - initialize API_VAL_CCI_BIND structure
 *    return: void
 *    bind(in/out): pointer to API_VAL_CCI_BIND
 *    conn(in): CI_CONNECTION handle
 *    flag(in): flag (get/set purpose)
 */
static void
api_val_cci_bind_init (API_VAL_CCI_BIND * bind, CI_CONNECTION conn, int flag)
{
  memset (bind, 0, sizeof (*bind));
  bind->conn = conn;
  bind->flag = flag;
}

/*
 * api_val_cci_bind_clear - clear API_VAL_CCI_BIND structure
 *    return: void
 *    bind(in/out): API_VAL_CCI_BIND structure
 */
static void
api_val_cci_bind_clear (API_VAL_CCI_BIND * bind)
{
  if (bind->redirected && bind->atype == CCI_A_TYPE_SET
      && bind->redirect.tset != NULL)
    {
      cci_set_free (bind->redirect.tset);
      bind->redirect.tset = NULL;
    }

  bind->redirected = false;
  bind->flag = 0;

  return;
}

/*
 * api_val_bind_param - call cci_bind_param() from API_VAL structure
 *    return: NO_ERROR if successful, error code otherwise
 *    conn(in): CI_CONNECTION handle
 *    req_handle(in): CCI request handle
 *    pv(in): pointer to API_VAL
 *    index(in): parameter index
 */
static int
api_val_bind_param (CI_CONNECTION conn, int req_handle, API_VAL * pv,
		    int index)
{
  T_CCI_U_TYPE utype;
  API_VAL_CCI_BIND bind;
  int res;

  utype = type_to_cci_u_type (pv->type);

  api_val_cci_bind_init (&bind, conn, API_VAL_CCI_BIND_FLAG_SET);
  res = api_val_cci_bind_bind (pv->type, pv->ptr, pv->len, &bind);
  if (res != NO_ERROR)
    {
      api_val_cci_bind_clear (&bind);
      return res;
    }

  res = cci_bind_param (req_handle, index, bind.atype, bind.value, utype,
			CCI_BIND_PTR);
  api_val_cci_bind_clear (&bind);

  if (res != 0)
    {
      return err_from_cci (res);
    }

  return NO_ERROR;
}

/*
 * get_value_from_req_handle - get CUBRID C API value from cci_get_dat()
 *    return: NO_ERROR if successful, error code otherwise
 *    conn(in): CI_CONNECTION handle
 *    req_handle(in): CCI request handle
 *    index(in): CCI column index
 *    type(in): CI_TYPE
 *    addr(out): address of value container
 *    len(in): length of value container
 *    outlen(out): actuall written size
 *    is_null(out): null indicator
 */
static int
get_value_from_req_handle (CI_CONNECTION conn, int req_handle,
			   int index, CI_TYPE type, void *addr,
			   size_t len, size_t * outlen, bool * is_null)
{
  API_VAL_CCI_BIND bind;
  int res;
  int indicator;

  api_val_cci_bind_init (&bind, conn, API_VAL_CCI_BIND_FLAG_GET);
  res = api_val_cci_bind_bind (type, addr, len, &bind);
  if (res != NO_ERROR)
    {
      api_val_cci_bind_clear (&bind);
      return res;
    }

  res = cci_get_data (req_handle, index, bind.atype, bind.value, &indicator);
  if (res != 0)
    {
      api_val_cci_bind_clear (&bind);
      return err_from_cci (res);
    }

  if (indicator == -1)
    {
      *is_null = true;
      api_val_cci_bind_clear (&bind);
      return NO_ERROR;
    }
  else
    {
      *is_null = false;
    }

  if (bind.redirected)
    {
      if (type == CI_TYPE_OID)
	{
	  /*
	   * this implies bind.atype == CCI_A_TYPE_STR so shoud be
	   * checked before other cases which binds results
	   * bind.atype == CCI_A_TYPE_STR
	   */
	  oidstr2xoid (bind.redirect.oid_buf, bind.conn, (CI_OID *) addr);
	}
      else if (bind.atype == CCI_A_TYPE_BIT)
	{
	  *outlen = bind.redirect.bit_val.size * 8;
	  if (*outlen > len)
	    {
	      return ER_INTERFACE_NOT_ENOUGH_DATA_SIZE;
	    }

	  memcpy (addr, bind.redirect.bit_val.buf,
		  bind.redirect.bit_val.size);
	}
      else if (bind.atype == CCI_A_TYPE_STR)
	{
	  if (indicator + 1 > (ssize_t) len)
	    {
	      return ER_INTERFACE_NOT_ENOUGH_DATA_SIZE;
	    }

	  *outlen = indicator;
	  memcpy (addr, bind.redirect.cp, indicator);
	  ((char *) addr)[indicator] = 0;
	}
      else if (bind.atype == CCI_A_TYPE_SET)
	{
	  CI_COLLECTION col;

	  res = cci_set_to_xcol (conn, bind.redirect.tset, &col);
	  if (res != NO_ERROR)
	    return res;

	  *(CI_COLLECTION *) addr = col;

	  return NO_ERROR;
	}
      else
	{
	  assert (0);
	  return ER_INTERFACE_GENERIC;
	}
    }

  api_val_cci_bind_clear (&bind);

  return NO_ERROR;
}

/*
 * get_value_from_tset - get value of CCI set structure
 *    return: NO_ERROR if successful, error code otherwise
 *    utype(in):
 *    tset(in): type of tset. precalcuated to prevent repeated function call
 *    type(in): dest type of pv
 *    conn(in): CI_CONNECTION handle
 *    i(in): index of element
 *    pv(out): API_VAL created
 */
static int
get_value_from_tset (T_CCI_U_TYPE utype, T_CCI_SET tset,
		     CI_TYPE type, CI_CONNECTION conn, int i, API_VAL ** pv)
{
  int res;
  void *ptr;
  int len;
  API_VAL *pval;
  API_VAL_CCI_BIND bind;
  int indicator;
  union
  {
    int ival;
    float fval;
    double dval;
    void *ptr;
    CI_TIME tval;
    CI_OID oidval;
    CI_COLLECTION col;
  } data;

  assert (pv != NULL);

  api_val_cci_bind_init (&bind, conn, API_VAL_CCI_BIND_FLAG_GET);

  res = api_val_cci_bind_bind (type, &data, sizeof (data), &bind);
  if (res != NO_ERROR)
    {
      return res;
    }

  res = cci_set_get (tset, i + 1, bind.atype, bind.value, &indicator);
  if (res != 0)
    {
      api_val_cci_bind_clear (&bind);
      return err_from_cci (res);
    }

  /* determine type, ptr, len for API_VAL creation */
  if (indicator == -1)
    {
      /* null value */
      type = CI_TYPE_NULL;
      ptr = NULL;
      len = 0;
    }
  else if (bind.redirected)
    {
      if (type == CI_TYPE_OID)
	{
	  CI_OID *xoid;

	  len = sizeof (*xoid);
	  xoid = API_MALLOC (len);
	  if (xoid == NULL)
	    {
	      api_val_cci_bind_clear (&bind);
	      return ER_INTERFACE_NO_MORE_MEMORY;
	    }
	  oidstr2xoid (bind.redirect.oid_buf, conn, xoid);
	  ptr = xoid;
	}
      else if (bind.atype == CCI_A_TYPE_BIT)
	{
	  len = bind.redirect.bit_val.size;
	  ptr = API_MALLOC (len);
	  if (!ptr)
	    {
	      api_val_cci_bind_clear (&bind);
	      return ER_INTERFACE_NO_MORE_MEMORY;
	    }
	  memcpy (ptr, bind.redirect.bit_val.buf, len);
	}
      else if (bind.atype == CCI_A_TYPE_STR)
	{
	  len = indicator + 1;
	  ptr = API_MALLOC (len);
	  if (!ptr)
	    {
	      api_val_cci_bind_clear (&bind);
	      return ER_INTERFACE_NO_MORE_MEMORY;
	    }
	  memcpy (ptr, bind.redirect.cp, len);
	  ((char *) ptr)[indicator] = 0;
	}
      else if (bind.atype == CCI_A_TYPE_SET)
	{
	  CI_COLLECTION col;

	  len = sizeof (CI_COLLECTION *);
	  ptr = API_MALLOC (len);
	  if (!ptr)
	    {
	      api_val_cci_bind_clear (&bind);
	      return ER_INTERFACE_NO_MORE_MEMORY;
	    }

	  res = cci_set_to_xcol (conn, bind.redirect.tset, &col);
	  if (res != NO_ERROR)
	    {
	      API_FREE (ptr);
	      api_val_cci_bind_clear (&bind);
	      return res;
	    }
	  *(CI_COLLECTION *) ptr = col;
	}
      else
	{
	  assert (0);		/* should not happen */
	  return ER_INTERFACE_GENERIC;
	}
    }
  else
    {
      res = get_type_value_size (type, &len);
      if (res != NO_ERROR)
	{
	  api_val_cci_bind_clear (&bind);
	  return res;
	}

      assert (len > 0);		/* variable size should be redirecteded */

      ptr = API_MALLOC (len);
      if (ptr == NULL)
	{
	  api_val_cci_bind_clear (&bind);
	  return ER_INTERFACE_NO_MORE_MEMORY;
	}
    }

  pval = API_MALLOC (sizeof (*pval));
  if (pval == NULL)
    {
      API_FREE (ptr);
      api_val_cci_bind_clear (&bind);
      return ER_INTERFACE_NO_MORE_MEMORY;
    }

  pval->type = type;
  pval->ptr = ptr;
  pval->len = len;
  *pv = pval;

  return NO_ERROR;
}

/* cci_cursor_update */

/*
 * api_val_cursor_update - call cci_cursor_update() whth API_VAL
 *    return: NO_ERROR if successful, error code otherwise
 *    conn(in): CI_CONNECTION
 *    req_handle(in): CCI request handle
 *    offset(in): cursor offset
 *    index(in): column index
 *    pv(in): pointer to API_VAL
 *    err_buf(out): CCI error buffer
 */
static int
api_val_cursor_update (CI_CONNECTION conn, int req_handle, int offset,
		       int index, API_VAL * pv, T_CCI_ERROR * err_buf)
{
  API_VAL_CCI_BIND bind;
  int res;

  api_val_cci_bind_init (&bind, conn, API_VAL_CCI_BIND_FLAG_SET);

  res = api_val_cci_bind_bind (pv->type, pv->ptr, pv->len, &bind);
  if (res != NO_ERROR)
    {
      api_val_cci_bind_clear (&bind);
      return res;
    }

  res = cci_cursor_update (req_handle, offset, index, bind.atype, bind.value,
			   err_buf);

  api_val_cci_bind_clear (&bind);

  if (res != 0)
    return err_from_cci (res);

  return NO_ERROR;
}

/*
 * lazy_bind_qres_rmeta - create and bind resultset meta handle of RESULTSET_IMPL
 *    return: NO_ERROR if successful, error code otherwise
 *    pres(in): pointer to RESULTSET_IMPL
 */
static int
lazy_bind_qres_rmeta (RESULTSET_IMPL * pres)
{
  API_RESULTSET_META *prm;
  BIND_HANDLE handle;
  int res;

  if (pres->got_rm_handle)
    return NO_ERROR;

  prm = (API_RESULTSET_META *) & pres->RM;
  res = bind_api_structure (pres->bh, (COMMON_API_STRUCTURE *) prm,
			    (COMMON_API_STRUCTURE *) pres, &handle);
  if (res != NO_ERROR)
    {
      return res;
    }

  pres->got_rm_handle = true;
  pres->rm_handle = handle;

  return NO_ERROR;
}

/*
 * api_qres_get_resultset_metadata - API_RESULTSET::get_resultset_metadata
 *                                   implementation of CCI query result
 *    return: NO_ERROR if successful, error code otherwise
 *    rs(in): pointer to API_RESULTSET
 *    rimpl(out): pointer to API_RESULTSET_META
 */
static int
api_qres_get_resultset_metadata (API_RESULTSET *
				 rs, API_RESULTSET_META ** rimpl)
{
  RESULTSET_IMPL *pres = (RESULTSET_IMPL *) rs;
  int res;

  assert (pres != NULL);
  assert (rimpl != NULL);

  /* lazy initialize result set meta */
  res = lazy_bind_qres_rmeta (pres);
  if (res != NO_ERROR)
    {
      return res;
    }

  assert (pres->got_rm_handle);

  *rimpl = (API_RESULTSET_META *) & pres->RM;

  return NO_ERROR;
}

/*
 * api_qres_fetch - API_RESULTSET_IFS::fetch implementation of
 *                  CCI query result
 *    return:NO_ERROR if successful, error code otherwise
 *    rs(in): pointer to API_RESULTSET
 *    offset(in): offset of cursor
 *    pos(in): start definition of cursor
 */
static int
api_qres_fetch (API_RESULTSET * rs, int offset, CI_FETCH_POSITION pos)
{
  RESULTSET_IMPL *pres = (RESULTSET_IMPL *) rs;
  int origin;
  int res;

  assert (pres != NULL);

  if (pos == CI_FETCH_POSITION_FIRST)
    {
      origin = 0;
    }
  else if (pos == CI_FETCH_POSITION_CURRENT)
    {
      origin = pres->offset;
    }
  else if (pos == CI_FETCH_POSITION_LAST)
    {
      return ER_INTERFACE_NOT_SUPPORTED_OPERATION;
    }
  else
    {
      return ER_INTERFACE_INVALID_ARGUMENT;
    }

  res = cci_cursor (pres->req_handle, origin + offset, CCI_CURSOR_FIRST,
		    pres->err_buf);
  if (res != 0)
    {
      return err_from_cci (res);
    }

  pres->offset = origin + offset;
  pres->fetched = false;

  return NO_ERROR;
}

/*
 * api_qres_tell - API_RESULTSET_IFS::tell() implementation of
 *                 CCI query result
 *    return: NO_ERROR if successful, error code otherwise
 *    rs(in): pointer to API_RESULTSET
 *    offset(out): cursor offset from the start
 */
static int
api_qres_tell (API_RESULTSET * rs, int *offset)
{
  RESULTSET_IMPL *pres = (RESULTSET_IMPL *) rs;

  *offset = pres->offset;

  return NO_ERROR;
}

/*
 * api_qres_clear_updates - API_RESULTSET::clear_updates() implementation of
 *                          CCI query result
 *    return: NO_ERROR if successful, error code otherwise
 *    rs(in): API_RESULTSET
 */
static int
api_qres_clear_updates (API_RESULTSET * rs)
{
  RESULTSET_IMPL *pres = (RESULTSET_IMPL *) rs;
  VALUE_INDEXER *indexer;
  int res, i, len;

  assert (pres != NULL);

  indexer = pres->updated_values;
  if (indexer == NULL)
    {
      /* no updates before this call */
      return NO_ERROR;
    }

  res = indexer->ifs->length (indexer, &len);
  if (res != NO_ERROR)
    {
      return res;
    }

  for (i = 0; i < len; i++)
    {
      VALUE_AREA *va;
      API_VAL *av;

      res = indexer->ifs->check (indexer, i, CHECK_FOR_GET | CHECK_FOR_SET);
      if (res != NO_ERROR)
	{
	  return res;
	}

      res = indexer->ifs->get (indexer, i, &va, (API_VALUE **) & av);
      if (res != NO_ERROR)
	{
	  return res;
	}

      if (av != NULL)
	{
	  api_val_dtor (va, (API_VALUE *) av);
	}

      res = indexer->ifs->set (indexer, i, NULL, NULL);
      if (res != NO_ERROR)
	{
	  return res;
	}
    }

  return NO_ERROR;
}

/*
 * api_qres_delete_row - API_RESULTSET::delete_row(), delete the current row
 *    return: NO_ERROR if successful, error code otherwise
 *    res(in): pointer to API_RESULTSET
 */
static int
api_qres_delete_row (API_RESULTSET * res)
{
  return ER_INTERFACE_NOT_SUPPORTED_OPERATION;
}

/*
 * api_qres_get_value - API_RESULTSET::get_value implementation of
 *                      CCI query result
 *    return: NO_ERROR if successful, error code otherwise
 *    rs(in): API_RESULTSET
 *    index(in): cloumn index
 *    type(in): CI_TYPE
 *    addr(out): address of value container
 *    len(in): length of value container
 *    outlen(out): output length
 *    is_null(out): null indicator
 */
static int
api_qres_get_value (API_RESULTSET * rs,
		    int index, CI_TYPE type,
		    void *addr, size_t len, size_t * outlen, bool * is_null)
{
  RESULTSET_IMPL *pres = (RESULTSET_IMPL *) rs;
  int res;

  assert (pres != NULL);

  if (!pres->fetched)
    {
      res = cci_fetch (pres->req_handle, pres->err_buf);
      if (res != 0)
	{
	  return err_from_cci (res);
	}
      pres->fetched = true;
    }

  assert (pres->fetched);

  if (pres->updated_values != NULL)
    {
      VALUE_INDEXER *indexer = pres->updated_values;
      VALUE_AREA *va;
      API_VAL *av;

      res = indexer->ifs->check (indexer, index - 1,
				 (CHECK_FOR_GET | CHECK_FOR_SET));
      if (res != NO_ERROR)
	{
	  return res;
	}

      res = indexer->ifs->get (indexer, index - 1, &va, (API_VALUE **) & av);
      if (res != NO_ERROR)
	{
	  return res;
	}

      if (av != NULL)
	{
	  res = get_value_from_api_val (av, type, addr, len, outlen, is_null);
	}
      else
	{
	  res = get_value_from_req_handle (pres->conn, pres->req_handle,
					   index, type, addr, len, outlen,
					   is_null);
	}
    }
  else
    {
      res = get_value_from_req_handle (pres->conn, pres->req_handle, index,
				       type, addr, len, outlen, is_null);
    }

  return res;
}

/*
 * api_qres_get_value_by_name - API_RESULSET::get_value_by_name implementation
 *                              of CCI query result
 *    return: NO_ERROR if successful, error code otherwise
 *    rs(in): API_RESULTSET
 *    name(in): cloumn name
 *    type(in): CI_TYPE
 *    addr(out): address of value container
 *    len(in): length of value container
 *    outlen(out): output length
 *    is_null(out): null indicator
 */
static int
api_qres_get_value_by_name (API_RESULTSET * rs,
			    const char *name,
			    CI_TYPE type,
			    void *addr,
			    size_t len, size_t * outlen, bool * isnull)
{
  RESULTSET_IMPL *pres = (RESULTSET_IMPL *) rs;
  int i;

  assert (pres != NULL);

  for (i = 0; i < pres->num_col; i++)
    {
      if (strcmp (name, pres->col_info[i].col_name) == 0)
	{
	  return api_qres_get_value (rs, i + 1, type, addr, len, outlen,
				     isnull);
	}
    }

  return ER_INTERFACE_INVALID_NAME;
}

/*
 * api_qres_update_value - API_RESULTSET::update_value implementation of
 *                         CCI query result
 *    return: NO_ERROR if successful, error code otherwise
 *    rs(in): API_RESULTSET
 *    index(in): column index
 *    type(in): CI_TYPE
 *    addr(in): address of the value container
 *    len(in): length of the value container
 */
static int
api_qres_update_value (API_RESULTSET * rs,
		       int index, CI_TYPE type, void *addr, size_t len)
{
  RESULTSET_IMPL *pres = (RESULTSET_IMPL *) rs;
  int res;
  VALUE_INDEXER *indexer;
  VALUE_AREA *va;
  API_VAL *av;
  bool val_created = false;

  if (!pres->updatable)
    {
      return ER_INTERFACE_RESULTSET_NOT_UPDATABLE;
    }

  if (pres->updated_values == NULL)
    {
      res = array_indexer_create (pres->num_col, &pres->updated_values);
      if (res != NO_ERROR)
	{
	  return res;
	}
    }

  assert (pres->updated_values != NULL);

  indexer = pres->updated_values;
  res = indexer->ifs->check (indexer, index - 1,
			     (CHECK_FOR_GET | CHECK_FOR_SET));

  if (res != NO_ERROR)
    {
      return res;
    }

  res = indexer->ifs->get (indexer, index - 1, &va, (API_VALUE **) & av);
  if (res != NO_ERROR)
    {
      return res;
    }

  if (av == NULL)
    {
      av = API_CALLOC (1, sizeof (*av));
      if (av == NULL)
	{
	  return ER_INTERFACE_NO_MORE_MEMORY;
	}

      val_created = true;
    }

  res = set_value_to_api_val (av, type, addr, len);
  if (res != NO_ERROR)
    {
      if (val_created)
	{
	  api_val_dtor (NULL, (API_VALUE *) av);
	}
      return res;
    }

  if (val_created)
    {
      (void) indexer->ifs->set (indexer, index - 1, NULL, (API_VALUE *) av);
    }

  return NO_ERROR;
}

/*
 * qres_apply_updatef - api_qres_apply_update worker function.
 *    return: NO_ERROR if successful, error code otherwise
 *    arg(in): pointer to RESULTSET_IMPL
 *    index(in): column index
 *    va(in): pointer to VALUE_AREA
 *    v(in): pointer to API_VALUE
 */
static int
qres_apply_updatef (void *arg, int index, VALUE_AREA * va, API_VALUE * v)
{
  RESULTSET_IMPL *pres = (RESULTSET_IMPL *) arg;
  API_VAL *av = (API_VAL *) v;
  int res;

  if (av == NULL)
    {
      return NO_ERROR;
    }

  res = api_val_cursor_update (pres->conn, pres->req_handle, pres->offset,
			       index, av, pres->err_buf);
  return res;
}

/*
 * api_qres_apply_update - API_RESULTSET::apply_updates() implmentation of
 *                         CCI query result
 *    return: NO_ERROR if successful, error code otherwise
 *    rs():
 */
static int
api_qres_apply_update (API_RESULTSET * rs)
{
  RESULTSET_IMPL *pres = (RESULTSET_IMPL *) rs;
  int res;

  if (pres->updated_values == NULL)
    {
      return NO_ERROR;
    }

  res = pres->updated_values->ifs->map (pres->updated_values,
					qres_apply_updatef, (void *) pres);
  return res;
}

/*
 * api_qres_destroy - API_RESULTSET::destroy() implementation of
 *                    CCI query result set
 *    return: void
 *    rs():
 */
static void
api_qres_destroy (API_RESULTSET * rs)
{
  return;
}

static API_RESULTSET_IFS QRES_IFS_ = {
  api_qres_get_resultset_metadata,
  api_qres_fetch,
  api_qres_tell,
  api_qres_clear_updates,
  api_qres_delete_row,
  api_qres_get_value,
  api_qres_get_value_by_name,
  api_qres_update_value,
  api_qres_apply_update,
  api_qres_destroy
};

/*
 * create_resultset_impl - create RESULTSET_IMPL
 *    return: NO_ERROR if successful, error code otherwise
 *    conn(in): CI_CONNECTION
 *    bh(in): BH_INTERFACE
 *    req_handle(in): CCI request handle
 *    err_buf(in): CCI error buffer
 *    parent(in): parent (in binding relationship) COMMON_API_STRUCTURE
 *    updatable(in): indicator of updatable resultset
 *    arg(in): on_close function argument
 *    on_close(in): notification fucntion to parent COMMON_API_STRUCTURE called
 *                  when this RESULTSET_IMPL is to be destroyed
 *    resifs(in): API_RESULTSET_IFS implementation
 *    rmifs(in): API_RESULTSET_META_IFS implementation
 *    rpres(in): return pointer
 */
static int
create_resultset_impl (CI_CONNECTION conn,
		       BH_INTERFACE * bh,
		       int req_handle,
		       T_CCI_ERROR * err_buf,
		       COMMON_API_STRUCTURE * parent,
		       bool updatable,
		       void *arg,
		       void (*on_close) (RESULTSET_IMPL *, void *),
		       API_RESULTSET_IFS * resifs,
		       API_RESULTSET_META_IFS * rmifs,
		       RESULTSET_IMPL ** rpres)
{
  T_CCI_COL_INFO *col_info;
  T_CCI_CUBRID_STMT cmd_type;
  int res, num_col;
  BIND_HANDLE handle;
  RESULTSET_IMPL *pres;

  col_info = cci_get_result_info (req_handle, &cmd_type, &num_col);
  if (col_info == NULL)
    return ER_INTERFACE_GENERIC;

  pres = (RESULTSET_IMPL *) API_CALLOC (1, sizeof (*pres));
  if (pres == NULL)
    {
      return ER_INTERFACE_NO_MORE_MEMORY;
    }

  /* initialize fields */
  pres->bind.dtor = resultset_impl_dtor;
  pres->handle_type = HANDLE_TYPE_RESULTSET;
  pres->ifs = resifs;
  pres->RM.bind.dtor = NULL;
  pres->RM.handle_type = HANDLE_TYPE_RMETA;
  pres->RM.ifs = rmifs;
  pres->RM.bp = pres;
  pres->conn = conn;
  pres->bh = bh;
  pres->req_handle = req_handle;
  pres->err_buf = err_buf;
  pres->parent = parent;
  pres->updatable = updatable;
  pres->arg = arg;
  pres->on_close = on_close;
  pres->offset = 0;
  pres->fetched = false;
  pres->got_rm_handle = false;
  pres->rm_handle = -1LL;
  pres->num_col = num_col;
  pres->cmd_type = cmd_type;
  pres->col_info = col_info;

  res = bind_api_structure (bh, (COMMON_API_STRUCTURE *) pres, parent,
			    &handle);
  if (res != NO_ERROR)
    {
      API_FREE (pres);
      return res;
    }

  *rpres = pres;

  return NO_ERROR;
}

/*
 * resultset_impl_dtor - RESULTSET_IMPL bind handle destructor
 *    return: void
 *    bind(in): pointer to RESULTSET_IMPL
 */
static void
resultset_impl_dtor (BH_BIND * bind)
{
  RESULTSET_IMPL *pres = (RESULTSET_IMPL *) bind;

  if (pres == NULL)
    return;

  if (pres->on_close != NULL)
    {
      pres->on_close (pres, pres->arg);
    }

  if (pres->updated_values)
    {
      pres->updated_values->ifs->destroy (pres->updated_values, api_val_dtor);
    }

  API_FREE (pres);
}

/*
 * api_qrmeta_get_count - API_RESULTSET_META::get_count implementation of
 *                        CCI query result
 *    return: NO_ERROR if successful, error code otherwise
 *    rm(in): API_RESULTSET_META
 *    count(out): number of rows
 */
static int
api_qrmeta_get_count (API_RESULTSET_META * rm, int *count)
{
  RESULTSET_META_IMPL *prmeta = (RESULTSET_META_IMPL *) rm;

  assert (prmeta != NULL);
  assert (count != NULL);

  *count = prmeta->bp->num_col;

  return NO_ERROR;
}

/*
 * api_qrmeta_get_info -
 *    return: NO_ERROR if successful, error code otherwise
 *    rm():
 *    index():
 *    type():
 *    arg():
 *    size():
 */
static int
api_qrmeta_get_info (API_RESULTSET_META * rm,
		     int index,
		     CI_RMETA_INFO_TYPE type, void *arg, size_t size)
{
  RESULTSET_META_IMPL *prmeta = (RESULTSET_META_IMPL *) rm;
  size_t len = 0;
  char *p;

  assert (prmeta != NULL);

  if (index <= 0 || index > prmeta->bp->num_col)
    {
      return ER_INTERFACE_INVALID_ARGUMENT;
    }

  switch (type)
    {
    case CI_RMETA_INFO_COL_LABEL:
      {
	p = prmeta->bp->col_info[index - 1].real_attr;
	p = p ? p : (char *) "";
	len = strlen (p);
	if (size < len + 1)
	  {
	    return ER_INTERFACE_NOT_ENOUGH_DATA_SIZE;
	  }

	strcpy ((char *) arg, p);
	return NO_ERROR;
      }
    case CI_RMETA_INFO_COL_NAME:
      {
	p = prmeta->bp->col_info[index - 1].col_name;
	p = p ? p : (char *) "";
	len = strlen (p);
	if (size < len + 1)
	  {
	    return ER_INTERFACE_NOT_ENOUGH_DATA_SIZE;
	  }

	strcpy ((char *) arg, p);
	return NO_ERROR;
      }
    case CI_RMETA_INFO_COL_TYPE:
      {
	T_CCI_U_TYPE utype = prmeta->bp->col_info[index - 1].type;

	if (len < sizeof (CI_TYPE))
	  {
	    return ER_INTERFACE_NOT_ENOUGH_DATA_SIZE;
	  }

	*(CI_TYPE *) arg = cci_u_type_to_type (utype);
	return NO_ERROR;
      }
    case CI_RMETA_INFO_PRECISION:
      {
	if (len < sizeof (int))
	  {
	    return ER_INTERFACE_NOT_ENOUGH_DATA_SIZE;
	  }

	*(int *) arg = prmeta->bp->col_info[index - 1].precision;
	return NO_ERROR;
      }
    case CI_RMETA_INFO_SCALE:
      {
	if (len < sizeof (int))
	  {
	    return ER_INTERFACE_NOT_ENOUGH_DATA_SIZE;
	  }

	*(int *) arg = prmeta->bp->col_info[index - 1].scale;
	return NO_ERROR;
      }
    case CI_RMETA_INFO_TABLE_NAME:
      {
	p = prmeta->bp->col_info[index - 1].class_name;
	p = p ? p : (char *) "";
	if (size < len + 1)
	  {
	    return ER_INTERFACE_NOT_ENOUGH_DATA_SIZE;
	  }

	strcpy ((char *) arg, p);
	return NO_ERROR;
      }
    case CI_RMETA_INFO_IS_AUTO_INCREMENT:
      {
	return ER_INTERFACE_NOT_SUPPORTED_OPERATION;
      }
    case CI_RMETA_INFO_IS_NULLABLE:
      {
	if (len < sizeof (int))
	  {
	    return ER_INTERFACE_NOT_ENOUGH_DATA_SIZE;
	  }

	*(int *) arg = prmeta->bp->col_info[index - 1].is_non_null ? 0 : 1;
	return NO_ERROR;
      }
    case CI_RMETA_INFO_IS_WRITABLE:
      {
	if (len < sizeof (int))
	  {
	    return ER_INTERFACE_NOT_ENOUGH_DATA_SIZE;
	  }

	*(int *) arg = prmeta->bp->updatable ? 1 : 0;
	return NO_ERROR;
      }
    default:
      return ER_INTERFACE_INVALID_ARGUMENT;
    }

  return NO_ERROR;
}

static API_RESULTSET_META_IFS QRMETA_IFS_ = {
  api_qrmeta_get_count,
  api_qrmeta_get_info
};

/*
 * lazy_bind_pstmt_pmeta -
 *    return: NO_ERROR if successful, error code otherwise
 *    pstmt():
 */
static int
lazy_bind_pstmt_pmeta (STATEMENT_IMPL * pstmt)
{
  int res;
  BIND_HANDLE handle;

  if (pstmt->got_pm_handle)
    return NO_ERROR;

  res = bind_api_structure (pstmt->pconn->bh,
			    (COMMON_API_STRUCTURE *) & pstmt->PM,
			    (COMMON_API_STRUCTURE *) pstmt, &handle);
  if (res != NO_ERROR)
    return res;

  pstmt->got_pm_handle = true;
  pstmt->pm_handle = handle;

  return NO_ERROR;
}

/*
 * statement_impl_dtor -
 *    return: NO_ERROR if successful, error code otherwise
 *    bind():
 */
static void
statement_impl_dtor (BH_BIND * bind)
{
  STATEMENT_IMPL *pstmt = (STATEMENT_IMPL *) bind;

  if (pstmt == NULL)
    return;

  if (pstmt->params != NULL)
    {
      pstmt->params->ifs->destroy (pstmt->params, api_val_dtor);
    }

  if (pstmt->req_handle >= 0)
    {
      (void) cci_close_req_handle (pstmt->req_handle);
    }

  API_FREE (pstmt);
}

/*
 * init_statement_impl -
 *    return: void
 *    pstmt():
 *    pconn():
 */
static void
init_statement_impl (STATEMENT_IMPL * pstmt, CONNECTION_IMPL * pconn)
{
  memset (pstmt, 0, sizeof (*pstmt));

  pstmt->bind.dtor = statement_impl_dtor;
  pstmt->handle_type = HANDLE_TYPE_STATEMENT;
  pstmt->pconn = pconn;
  pstmt->req_handle = -1;
  pstmt->PM.bind.dtor = NULL;
  pstmt->PM.handle_type = HANDLE_TYPE_PMETA;
  pstmt->PM.bptr = pstmt;
  pstmt->BR.bind.dtor = NULL;
  pstmt->BR.bptr = pstmt;
  pstmt->BR.handle_type = HANDLE_TYPE_BATCH_RESULT;
  pstmt->status = CI_STMT_STATUS_INITIALIZED;
  dlisth_init (&pstmt->batch);
  pstmt->num_batch = 0;
}

/*
 * statement_execute -
 *    return: NO_ERROR if successful, error code otherwise
 *    pstmt():
 *    err_buf():
 */
static int
statement_execute (STATEMENT_IMPL * pstmt, T_CCI_ERROR * err_buf)
{
  char ex_flag;
  int res, nres, num_query;
  T_CCI_QUERY_RESULT *query_result;

  ex_flag = CCI_EXEC_QUERY_ALL;
  if (pstmt->opt_async_query)
    {
      ex_flag |= CCI_EXEC_ASYNC;
    }

  res = cci_execute (pstmt->req_handle, ex_flag, 0, err_buf);
  if (res < 0)
    {
      return err_from_cci (err_buf->err_code);
    }
  nres = res;

  res = cci_execute_result (pstmt->req_handle, &query_result, err_buf);
  if (res < 0)
    {
      return err_from_cci (err_buf->err_code);
    }
  num_query = res;

  if (pstmt->query_result)
    {
      cci_query_result_free (pstmt->query_result, pstmt->num_query);
      pstmt->query_result = NULL;
      pstmt->num_query = 0;
    }

  pstmt->has_resultset = false;
  pstmt->res_handle = -1LL;
  pstmt->affected_rows = -1;
  pstmt->num_query = num_query;
  pstmt->query_result = query_result;
  pstmt->curr_query_result_index = 0;

  return NO_ERROR;
}

/*
 * get result of pstmt->curr_query_result_idx
 */

/*
 * statement_get_reshandle_or_affectedrows -
 *    return: NO_ERROR if successful, error code otherwise
 *    bh():
 *    pstmt():
 */
static int
statement_get_reshandle_or_affectedrows (BH_INTERFACE * bh,
					 STATEMENT_IMPL * pstmt)
{
  int idx, res;

  idx = pstmt->curr_query_result_index + 1;
  if (idx < 1 || idx > pstmt->num_query)
    {
      assert (0);
      return ER_INTERFACE_GENERIC;
    }

  /*
   * call cci_cursor () for resutlset test. (hack)
   * It will be good if CCI API provides function something like
   * CCI_QUERY_RESULT_HAS_RESULTSET ()
   */
  res = cci_cursor (pstmt->req_handle, 1, CCI_CURSOR_FIRST,
		    &pstmt->pconn->err_buf);
  if (res == 0)
    {
      RESULTSET_IMPL *pres;

      res = create_resultset_impl (pstmt->pconn->conn,
				   pstmt->pconn->bh, pstmt->req_handle,
				   &pstmt->pconn->err_buf,
				   (COMMON_API_STRUCTURE *) pstmt,
				   pstmt->opt_updatable_result,
				   pstmt,
				   on_close_statement_res,
				   &QRES_IFS_, &QRMETA_IFS_, &pres);
      if (res != NO_ERROR)
	{
	  return res;
	}

      pstmt->has_resultset = true;
      (void) pstmt->pconn->bh->bind_to_handle (pstmt->pconn->bh,
					       (BH_BIND *) pres,
					       &pstmt->res_handle);
      pstmt->affected_rows = CCI_QUERY_RESULT_RESULT (pstmt->query_result,
						      idx);

      return NO_ERROR;
    }
  else if (res == CCI_ER_NO_MORE_DATA)
    {
      pstmt->has_resultset = false;
      pstmt->res_handle = -1LL;
      pstmt->affected_rows = CCI_QUERY_RESULT_RESULT (pstmt->query_result,
						      idx);
      return NO_ERROR;
    }
  else
    {
      return err_from_cci (res);
    }
}

/*
 * complete_statement -
 *    return: NO_ERROR if successful, error code otherwise
 *    pstmt():
 */
static int
complete_statement (STATEMENT_IMPL * pstmt)
{
  int res;

  assert (pstmt->status & CI_STMT_STATUS_EXECUTED);

  if (pstmt->status & CI_STMT_STATUS_BATCH_ADDED)
    {
      res = stmt_complete_batch (pstmt);
      if (res != NO_ERROR)
	{
	  return res;
	}
    }
  else if (pstmt->query_result != NULL)
    {
      cci_query_result_free (pstmt->query_result, pstmt->num_query);
      pstmt->query_result = NULL;
      pstmt->num_query = 0;
    }

  pstmt->curr_query_result_index = 0;

  if (pstmt->has_resultset)
    {
      res = pstmt->pconn->bh->destroy_handle (pstmt->pconn->bh,
					      pstmt->res_handle);
      if (res != NO_ERROR)
	{
	  return res;
	}
    }

  if (pstmt->status & CI_STMT_STATUS_PREPARED)
    {
      /* reset parameter table */
      if (pstmt->params != NULL)
	{
	  int i;

	  for (i = 0; pstmt->num_col; i++)
	    {
	      VALUE_AREA *va;
	      API_VAL *av;

	      res = pstmt->params->ifs->check (pstmt->params, i,
					       CHECK_FOR_GET | CHECK_FOR_SET);
	      if (res != NO_ERROR)
		{
		  return res;
		}

	      res = pstmt->params->ifs->get (pstmt->params, i, &va,
					     (API_VALUE **) & av);
	      if (res != NO_ERROR)
		{
		  return res;
		}

	      res = pstmt->params->ifs->set (pstmt->params, i, NULL, NULL);
	      if (res != NO_ERROR)
		{
		  return res;
		}

	      if (av != NULL)
		{
		  api_val_dtor (NULL, (API_VALUE *) av);
		}
	    }
	}
    }

  return NO_ERROR;
}

/*
 * on_close_statement_res -
 *    return: NO_ERROR if successful, error code otherwise
 *    impl():
 *    arg():
 */
static void
on_close_statement_res (RESULTSET_IMPL * impl, void *arg)
{
  STATEMENT_IMPL *pstmt = (STATEMENT_IMPL *) arg;

  pstmt->has_resultset = false;
  pstmt->res_handle = -1LL;
  pstmt->affected_rows = -1;
}

struct add_batch_params_arg
{
  int nadded;
  BATCH_ARY_ITEM *bai;
  VALUE_INDEXER *indexer;
};

/*
 * add_batch_params_restore_mapf -
 *    return: NO_ERROR if successful, error code otherwise
 *    arg():
 *    index():
 *    va():
 *    val():
 */
static int
add_batch_params_restore_mapf (void *arg, int index, VALUE_AREA * va,
			       API_VALUE * val)
{
  struct add_batch_params_arg *parg = (struct add_batch_params_arg *) arg;

  if (index < parg->nadded)
    {
      (void) parg->indexer->ifs->set (parg->indexer, index, NULL,
				      (API_VALUE *) parg->bai->ary[index]);
    }

  return NO_ERROR;
}


/*
 * add_batch_params_mapf -
 *    return: NO_ERROR if successful, error code otherwise
 *    arg():
 *    index():
 *    va():
 *    val():
 */
static int
add_batch_params_mapf (void *arg, int index, VALUE_AREA * va, API_VALUE * val)
{
  int res;
  struct add_batch_params_arg *parg = (struct add_batch_params_arg *) arg;

  API_VAL *pval = (API_VAL *) val;

  if (val == NULL)
    {
      return ER_INTERFACE_PARAM_IS_NOT_SET;
    }

  parg->bai->ary[index] = pval;
  res = parg->indexer->ifs->set (parg->indexer, index, NULL, NULL);
  if (res != NO_ERROR)
    {
      return res;
    }
  parg->nadded++;

  return res;
}

/*
 * stmt_execute_batch_sql -
 *    return: NO_ERROR if successful, error code otherwise
 *    pstmt():
 */
static int
stmt_execute_batch_sql (STATEMENT_IMPL * pstmt)
{
  int res;
  T_CCI_QUERY_RESULT *query_result;
  char **sql_stmt;
  int i;
  dlisth *h;

  assert (pstmt->num_batch > 0);
  assert (pstmt->num_query == 0);
  assert (pstmt->query_result == NULL);

  sql_stmt = (char **) API_MALLOC (pstmt->num_batch * sizeof (char *));
  if (sql_stmt == NULL)
    {
      return ER_INTERFACE_NO_MORE_MEMORY;
    }

  for (h = pstmt->batch.next, i = 0; (h != &pstmt->batch)
       && i < pstmt->num_batch; h = h->next, i++)
    {
      BATCH_SQL_ITEM *bi = (BATCH_SQL_ITEM *) h;

      sql_stmt[i] = bi->sql;
    }

  assert (i = pstmt->num_batch);

  res = cci_execute_batch (pstmt->pconn->conn_handle, pstmt->num_batch,
			   sql_stmt, &query_result, &pstmt->pconn->err_buf);

  API_FREE (sql_stmt);
  if (res != 0)
    {
      return err_from_cci (res);
    }

  pstmt->num_query = pstmt->num_batch;
  pstmt->query_result = query_result;

  return NO_ERROR;
}

/*
 * stmt_execute_batch_array -
 *    return: NO_ERROR if successful, error code otherwise
 *    pstmt():
 */
static int
stmt_execute_batch_array (STATEMENT_IMPL * pstmt)
{
  int res;
  T_CCI_QUERY_RESULT *query_result;
  int index;
  dlisth *h;
  int num_query;

  index = 0;
  for (h = pstmt->batch.next; h != &pstmt->batch; h = h->next)
    {
      BATCH_ARY_ITEM *bi = (BATCH_ARY_ITEM *) h;
      int i;

      for (i = 0; i < pstmt->num_col; i++)
	{
	  res = api_val_bind_param (pstmt->pconn->conn, pstmt->req_handle,
				    bi->ary[i], index + i + 1);
	  if (res != NO_ERROR)
	    {
	      return res;
	    }
	}
    }

  res = cci_execute_array (pstmt->req_handle, &query_result,
			   &pstmt->pconn->err_buf);
  if (res < 0)
    {
      return err_from_cci (res);
    }

  num_query = res;
  pstmt->num_query = num_query;
  pstmt->query_result = query_result;

  return NO_ERROR;
}


/*
 * stmt_complete_batch -
 *    return: NO_ERROR if successful, error code otherwise
 *    pstmt():
 */
static int
stmt_complete_batch (STATEMENT_IMPL * pstmt)
{
  int res;
  int num_batch, i;
  dlisth h;

  if (pstmt->query_result == NULL)
    {
      assert (dlisth_is_empty (&pstmt->batch));
      assert (pstmt->num_query == 0);

      return NO_ERROR;
    }

  if (pstmt->query_result)
    {
      res = cci_query_result_free (pstmt->query_result, pstmt->num_query);
    }

  if (res != 0)
    {
      return err_from_cci (res);
    }

  pstmt->query_result = NULL;
  pstmt->num_query = 0;

  /* clear batch data also */
  num_batch = pstmt->num_batch;
  dlisth_init (&h);
  dlisth_insert_after (&h, &pstmt->batch);
  dlisth_delete (&pstmt->batch);
  pstmt->num_batch = 0;

  while (dlisth_is_empty (&h))
    {
      dlisth *tmp = h.next;

      dlisth_delete (tmp);
      if (pstmt->status & CI_STMT_STATUS_PREPARED)
	{
	  BATCH_ARY_ITEM *bi = (BATCH_ARY_ITEM *) tmp;

	  for (i = 0; i < pstmt->num_col; i++)
	    {
	      api_val_dtor (NULL, (API_VALUE *) bi->ary[i]);
	    }
	  API_FREE (bi);
	}
      else
	{
	  API_FREE (tmp);
	}
    }

  return NO_ERROR;
}

/*
 * api_ormeta_get_count -
 *    return: NO_ERROR if successful, error code otherwise
 *    rm():
 *    count():
 */
static int
api_ormeta_get_count (API_RESULTSET_META * rm, int *count)
{
  RESULTSET_META_IMPL *prmeta = (RESULTSET_META_IMPL *) rm;
  CCI_OBJECT *obj;

  assert (prmeta != NULL);

  obj = (CCI_OBJECT *) prmeta->bp->arg;
  if (obj->deleted)
    {
      return ER_INTERFACE_RESULTSET_CLOSED;
    }

  return api_qrmeta_get_count (rm, count);
}

/*
 * api_ormeta_get_info -
 *    return: NO_ERROR if successful, error code otherwise
 *    rm():
 *    index():
 *    type():
 *    arg():
 *    size():
 */
static int
api_ormeta_get_info (API_RESULTSET_META * rm, int index,
		     CI_RMETA_INFO_TYPE type, void *arg, size_t size)
{
  RESULTSET_META_IMPL *prmeta = (RESULTSET_META_IMPL *) rm;
  CCI_OBJECT *obj;

  assert (prmeta != NULL);

  obj = (CCI_OBJECT *) prmeta->bp->arg;
  if (obj->deleted)
    {
      return ER_INTERFACE_RESULTSET_CLOSED;
    }

  return api_qrmeta_get_info (rm, index, type, arg, size);
}

static API_RESULTSET_META_IFS ORMETA_IFS_ = {
  api_ormeta_get_count,
  api_ormeta_get_info
};

/*
 * api_ores_get_resultset_metadata -
 *    return: NO_ERROR if successful, error code otherwise
 *    res():
 *    rimpl():
 */
static int
api_ores_get_resultset_metadata (API_RESULTSET * res,
				 API_RESULTSET_META ** rimpl)
{
  RESULTSET_IMPL *pres = (RESULTSET_IMPL *) res;
  CCI_OBJECT *obj;

  assert (pres != NULL);

  obj = (CCI_OBJECT *) pres->arg;
  if (obj->deleted)
    {
      return ER_INTERFACE_RESULTSET_CLOSED;
    }

  return api_qres_get_resultset_metadata (res, rimpl);
}

/*
 * api_ores_fetch -
 *    return: NO_ERROR if successful, error code otherwise
 *    res():
 *    offset():
 *    pos():
 */
static int
api_ores_fetch (API_RESULTSET * res, int offset, CI_FETCH_POSITION pos)
{
  RESULTSET_IMPL *pres = (RESULTSET_IMPL *) res;
  CCI_OBJECT *obj;

  assert (pres != NULL);

  obj = (CCI_OBJECT *) pres->arg;
  if (obj->deleted)
    {
      return ER_INTERFACE_RESULTSET_CLOSED;
    }

  return api_qres_fetch (res, offset, pos);
}

/*
 * api_ores_tell -
 *    return: NO_ERROR if successful, error code otherwise
 *    res():
 *    offset():
 */
static int
api_ores_tell (API_RESULTSET * res, int *offset)
{
  RESULTSET_IMPL *pres = (RESULTSET_IMPL *) res;
  CCI_OBJECT *obj;

  assert (pres != NULL);

  obj = (CCI_OBJECT *) pres->arg;
  if (obj->deleted)
    {
      return ER_INTERFACE_RESULTSET_CLOSED;
    }

  return api_qres_tell (res, offset);
}

/*
 * api_ores_clear_updates -
 *    return: NO_ERROR if successful, error code otherwise
 *    res():
 */
static int
api_ores_clear_updates (API_RESULTSET * res)
{
  RESULTSET_IMPL *pres = (RESULTSET_IMPL *) res;
  CCI_OBJECT *obj;

  assert (pres != NULL);

  obj = (CCI_OBJECT *) pres->arg;
  if (obj->deleted)
    {
      return ER_INTERFACE_RESULTSET_CLOSED;
    }

  return api_qres_clear_updates (res);
}

/*
 * api_ores_delete_row -
 *    return: NO_ERROR if successful, error code otherwise
 *    rs():
 */
static int
api_ores_delete_row (API_RESULTSET * rs)
{
  RESULTSET_IMPL *pres = (RESULTSET_IMPL *) rs;
  CCI_OBJECT *obj;
  int res;
  char oid_buf[32];

  assert (pres != NULL);
  if (pres == NULL)
    {
      return ER_INTERFACE_GENERIC;
    }

  obj = (CCI_OBJECT *) pres->arg;
  xoid2oidstr (&obj->xoid, oid_buf);

  res = cci_oid (obj->pool->pconn->conn_handle, CCI_OID_DROP, oid_buf,
		 &obj->pool->pconn->err_buf);
  if (res != 0)
    {
      return err_from_cci (res);
    }

  obj->deleted = true;

  return NO_ERROR;
}

/*
 * api_ores_get_value -
 *    return: NO_ERROR if successful, error code otherwise
 *    res():
 *    index():
 *    type():
 *    addr():
 *    len():
 *    outlen():
 *    is_null():
 */
static int
api_ores_get_value (API_RESULTSET * res, int index,
		    CI_TYPE type, void *addr, size_t len,
		    size_t * outlen, bool * is_null)
{
  RESULTSET_IMPL *pres = (RESULTSET_IMPL *) res;
  CCI_OBJECT *obj;

  assert (pres != NULL);

  obj = (CCI_OBJECT *) pres->arg;
  if (obj->deleted)
    {
      return ER_INTERFACE_RESULTSET_CLOSED;
    }

  return api_qres_get_value (res, index, type, addr, len, outlen, is_null);
}

/*
 * api_ores_get_value_by_name -
 *    return: NO_ERROR if successful, error code otherwise
 *    res():
 *    name():
 *    type():
 *    addr():
 *    len():
 *    outlen():
 *    isnull():
 */
static int
api_ores_get_value_by_name (API_RESULTSET * res, const char *name,
			    CI_TYPE type, void *addr,
			    size_t len, size_t * outlen, bool * isnull)
{
  RESULTSET_IMPL *pres = (RESULTSET_IMPL *) res;
  CCI_OBJECT *obj;

  assert (pres != NULL);

  obj = (CCI_OBJECT *) pres->arg;
  if (obj->deleted)
    {
      return ER_INTERFACE_RESULTSET_CLOSED;
    }

  return api_qres_get_value_by_name (res, name, type, addr, len, outlen,
				     isnull);
}

/*
 * api_ores_update_value -
 *    return: NO_ERROR if successful, error code otherwise
 *    res():
 *    index():
 *    type():
 *    addr():
 *    len():
 */
static int
api_ores_update_value (API_RESULTSET * res, int index,
		       CI_TYPE type, void *addr, size_t len)
{
  RESULTSET_IMPL *pres = (RESULTSET_IMPL *) res;
  CCI_OBJECT *obj;

  if (!pres->updatable)
    {
      return ER_INTERFACE_RESULTSET_NOT_UPDATABLE;
    }

  obj = (CCI_OBJECT *) pres->arg;
  if (obj->deleted)
    {
      return ER_INTERFACE_RESULTSET_CLOSED;
    }

  return api_qres_update_value (res, index, type, addr, len);
}

struct ores_nvt_s
{
  char *name;
  char *value;
  T_CCI_A_TYPE atype;
};

struct ores_apply_updatef_s
{
  RESULTSET_IMPL *pres;
  int nattrs;
  int nupdates;
  char **names;
  API_VAL_CCI_BIND *binds;
};

/*
 * ores_apply_updatef -
 *    return: NO_ERROR if successful, error code otherwise
 *    arg():
 *    index():
 *    va():
 *    v():
 */
static int
ores_apply_updatef (void *arg, int index, VALUE_AREA * va, API_VALUE * v)
{
  struct ores_apply_updatef_s *ARG;
  API_VAL *pv;
  int res;

  if (v == NULL)
    {
      return NO_ERROR;
    }

  ARG = (struct ores_apply_updatef_s *) arg;
  pv = (API_VAL *) v;
  ARG->names[index] = CCI_GET_RESULT_INFO_ATTR_NAME (ARG->pres->col_info,
						     index + 1);

  res = api_val_cci_bind_bind (pv->type, pv->ptr, pv->len,
			       &ARG->binds[index]);
  if (res != NO_ERROR)
    {
      return res;
    }

  ARG->nupdates++;

  return NO_ERROR;
}

/*
 * api_ores_apply_update -
 *    return: NO_ERROR if successful, error code otherwise
 *    rs():
 */
static int
api_ores_apply_update (API_RESULTSET * rs)
{
  RESULTSET_IMPL *pres = (RESULTSET_IMPL *) rs;
  CCI_OBJECT *obj;
  struct ores_apply_updatef_s ARG;
  int i, res, nattrs;
  char oid_buf[32];
  char **names;
  void **values;
  int *atypes;
  API_VAL_CCI_BIND *binds;

  obj = (CCI_OBJECT *) pres->arg;
  if (obj->deleted)
    {
      return ER_INTERFACE_RESULTSET_CLOSED;
    }

  if (pres->updated_values == NULL)
    {
      return NO_ERROR;
    }

  nattrs = pres->num_col;
  names = API_CALLOC (1, sizeof (char *) * (nattrs + 1));
  binds = API_MALLOC (sizeof (API_VAL_CCI_BIND) * nattrs);
  values = API_MALLOC (sizeof (void *) * nattrs);
  atypes = API_MALLOC (sizeof (int) * nattrs);
  if (names == NULL || binds == NULL || values == NULL || atypes == NULL)
    {
      res = ER_INTERFACE_NO_MORE_MEMORY;
      goto res_return;
    }

  for (i = 0; i < nattrs; i++)
    {
      api_val_cci_bind_init (&binds[i], pres->conn,
			     API_VAL_CCI_BIND_FLAG_SET);
    }

  ARG.pres = pres;
  ARG.nattrs = nattrs;
  ARG.nupdates = 0;
  ARG.names = names;
  ARG.binds = binds;

  res = pres->updated_values->ifs->map (pres->updated_values,
					ores_apply_updatef, &ARG);
  if (res != NO_ERROR)
    {
      goto res_return;
    }

  xoid2oidstr (&obj->xoid, oid_buf);

  names[ARG.nupdates] = NULL;

  for (i = 0; i < ARG.nupdates; i++)
    {
      values[i] = binds[i].value;
      atypes[i] = binds[i].atype;
    }

  res = cci_oid_put2 (obj->pool->pconn->conn_handle, oid_buf, names, values,
		      atypes, &obj->pool->pconn->err_buf);

res_return:
  if (names)
    {
      API_FREE (names);
    }

  if (values)
    {
      API_FREE (values);
    }

  if (atypes)
    {
      API_FREE (atypes);
    }

  if (binds)
    {
      int i;

      for (i = 0; i < nattrs; i++)
	api_val_cci_bind_clear (&binds[i]);
      API_FREE (binds);
    }

  return res;
}

/*
 * api_ores_destroy -
 *    return: NO_ERROR if successful, error code otherwise
 *    res():
 */
static void
api_ores_destroy (API_RESULTSET * res)
{
  assert (0);
  /* should not be called */
}

static API_RESULTSET_IFS ORES_IFS_ = {
  api_ores_get_resultset_metadata,
  api_ores_fetch,
  api_ores_tell,
  api_ores_clear_updates,
  api_ores_delete_row,		/* this calls cci_oid() */
  api_ores_get_value,
  api_ores_get_value_by_name,
  api_ores_update_value,	/* this calls cci_oid_put() */
  api_ores_apply_update,
  api_ores_destroy
};

/*
 * opool_get_object_resultset -
 *    return: NO_ERROR if successful, error code otherwise
 *    poo():
 *    oid():
 *    rres():
 */
static int
opool_get_object_resultset (API_OBJECT_RESULTSET_POOL * poo,
			    CI_OID * oid, API_RESULTSET ** rres)
{
  CCI_OBJECT_POOL *pool = (CCI_OBJECT_POOL *) poo;
  CCI_OBJECT *obj;
  int res;

  assert (pool != NULL);
  assert (oid != NULL);
  assert (rres != NULL);

  obj = NULL;
  res = hash_lookup (pool->ht, oid, (void **) &obj);
  if (res != NO_ERROR)
    {
      return res;
    }

  if (obj != NULL)
    {
      res = create_cci_object (pool, oid, &obj);
      if (res != NO_ERROR)
	{
	  return res;
	}

      assert (obj != NULL);
      assert (obj->pres != NULL);

      res = hash_insert (pool->ht, obj);
      if (res != NO_ERROR)
	{
	  destroy_cci_object (obj);
	  return res;
	}
    }

  *rres = (API_RESULTSET *) obj->pres;

  return NO_ERROR;
}

/*
 * opool_oid_delete -
 *    return: NO_ERROR if successful, error code otherwise
 *    poo():
 *    oid():
 */
static int
opool_oid_delete (API_OBJECT_RESULTSET_POOL * poo, CI_OID * oid)
{
  CCI_OBJECT_POOL *pool = (CCI_OBJECT_POOL *) poo;
  int res;
  CCI_OBJECT *obj;
  char oid_buf[32];

  assert (pool != NULL);
  assert (oid != NULL);

  res = hash_delete (pool->ht, oid, (void **) &obj);
  if (res != NO_ERROR)
    {
      return res;
    }

  if (obj != NULL)
    {
      destroy_cci_object (obj);
    }

  xoid2oidstr (oid, oid_buf);

  res = cci_oid (pool->pconn->conn_handle, CCI_OID_DROP, oid_buf,
		 &pool->pconn->err_buf);
  return err_from_cci (res);
}

/*
 * opool_oid_get_classname -
 *    return: NO_ERROR if successful, error code otherwise
 *    poo():
 *    oid():
 */
static int
opool_oid_get_classname (API_OBJECT_RESULTSET_POOL * poo, CI_OID * oid,
			 char *name, size_t size)
{
  return ER_INTERFACE_NOT_SUPPORTED_OPERATION;
}

/*
 * opool_destroy -
 *    return: void
 *    poo():
 */
static void
opool_destroy (API_OBJECT_RESULTSET_POOL * poo)
{
  CCI_OBJECT_POOL *pool = (CCI_OBJECT_POOL *) poo;

  assert (pool != NULL);

  hash_destroy (pool->ht, opool_ht_elem_dtor);
  hash_destroy (pool->ght, opool_ght_elem_dtor);

  API_FREE (pool);

  return;
}

/*
 * create_cci_object -
 *    return: NO_ERROR if successful, error code otherwise
 *    pool():
 *    oid():
 *    cobj():
 */
static int
create_cci_object (CCI_OBJECT_POOL * pool, CI_OID * oid, CCI_OBJECT ** cobj)
{
  int res, req_handle;
  char oid_buf[32];
  CCI_OBJECT *obj;
  RESULTSET_IMPL *pres;

  obj = API_MALLOC (sizeof (*obj));
  if (obj == NULL)
    {
      return ER_INTERFACE_NO_MORE_MEMORY;
    }

  xoid2oidstr (oid, oid_buf);
  res = cci_oid_get (pool->pconn->conn_handle, oid_buf, NULL,
		     &pool->pconn->err_buf);
  if (res < 0)
    {
      API_FREE (obj);
      return err_from_cci (res);
    }

  req_handle = res;

  pres = NULL;
  res = create_resultset_impl (pool->pconn->conn,
			       pool->pconn->bh, req_handle,
			       &pool->pconn->err_buf, NULL, true, obj,
			       on_close_opool_res, &ORES_IFS_, &ORMETA_IFS_,
			       &pres);
  if (res == NO_ERROR)
    {
      obj->xoid = *oid;
      obj->deleted = false;
      obj->req_handle = req_handle;
      obj->pres = pres;
      obj->pool = pool;
    }
  else
    {
      API_FREE (obj);
    }

  return res;
}

/*
 * destroy_cci_object -
 *    return: void
 *    obj():
 */
static void
destroy_cci_object (CCI_OBJECT * obj)
{
  if (obj)
    {
      if (obj->pres)
	{
	  resultset_impl_dtor ((BH_BIND *) obj->pres);
	}
      API_FREE (obj);
    }
}

/*
 * on_close_opool_res -
 *    return: NO_ERROR if successful, error code otherwise
 *    impl():
 *    arg():
 */
static void
on_close_opool_res (RESULTSET_IMPL * impl, void *arg)
{
  CCI_OBJECT *obj = (CCI_OBJECT *) arg;

  if (obj->req_handle >= 0)
    {
      cci_close_req_handle (obj->req_handle);
    }
  obj->req_handle = -1;
  obj->pres = NULL;
}

/*
 * xoid2oidstr -
 *    return: void
 *    xoid():
 *    oidstr():
 */
static void
xoid2oidstr (const CI_OID * xoid, char *oidstr)
{
  int pageid, slotid, volid;

  assert (xoid != NULL);
  assert (oidstr != NULL);

  pageid = xoid->d1;
  slotid = (xoid->d2 >> 16) & 0xffff;
  volid = xoid->d2 & 0xffff;

  sprintf (oidstr, "@%d|%d|%d", pageid, slotid, volid);
}

/* copy from ut_str_to_oid() */

/*
 * oidstr2xoid -
 *    return: NO_ERROR if successful, error code otherwise
 *    oidstr():
 *    conn():
 *    xoid():
 */
static int
oidstr2xoid (const char *oidstr, CI_CONNECTION conn, CI_OID * xoid)
{
  char *p = (char *) oidstr, *end_p;
  int pageid, slotid, volid;
  int result = 0;

  if (p == NULL)
    {
      return ER_INTERFACE_GENERIC;	/* CCI_ER_CONVERSION */
    }

  if (*p != '@')
    {
      return ER_INTERFACE_GENERIC;
    }

  p++;
  result = str_to_int32 (&pageid, &end_p, p, 10);
  if (result != 0 || *end_p != '|')
    {
      return ER_INTERFACE_GENERIC;
    }

  p = end_p + 1;
  result = str_to_int32 (&slotid, &end_p, p, 10);
  if (result != 0 || *end_p != '|')
    {
      return ER_INTERFACE_GENERIC;
    }

  p = end_p + 1;
  result = str_to_int32 (&volid, &end_p, p, 10);
  if (result != 0 || *end_p != '\0')
    {
      return ER_INTERFACE_GENERIC;
    }

  xoid->d1 = pageid;
  xoid->d2 = ((slotid << 16) & 0xffff0000) | volid;
  xoid->conn = conn;

  return NO_ERROR;
}

/*
 * opool_ht_comparef -
 *    return: NO_ERROR if successful, error code otherwise
 *    key1():
 *    key2():
 *    r():
 */
static int
opool_ht_comparef (void *key1, void *key2, int *r)
{
  CI_OID *oid1, *oid2;

  assert (key1 != NULL);
  assert (key2 != NULL);
  assert (r != NULL);

  oid1 = (CI_OID *) key1;
  oid2 = (CI_OID *) key2;

  if (oid1->d1 > oid2->d1)
    {
      *r = 1;
    }
  else if (oid1->d1 == oid2->d1)
    {
      if (oid1->d2 > oid2->d2)
	{
	  *r = 1;
	}
      else if (oid1->d2 == oid2->d2)
	{
	  *r = 0;
	}
      else
	{
	  *r = -1;
	}
    }
  else
    {
      *r = -1;
    }

  return NO_ERROR;
}

/*
 * opool_ht_hashf -
 *    return: NO_ERROR if successful, error code otherwise
 *    key():
 *    rv():
 */
static int
opool_ht_hashf (void *key, unsigned int *rv)
{
  CI_OID *oid = (CI_OID *) key;

  assert (oid != NULL);
  assert (rv != NULL);

  *rv = (unsigned int) (oid->d1 + oid->d2);

  return NO_ERROR;
}

/*
 * opool_ht_keyf -
 *    return: NO_ERROR if successful, error code otherwise
 *    elem():
 *    rk():
 */
static int
opool_ht_keyf (void *elem, void **rk)
{
  CCI_OBJECT *cobj = (CCI_OBJECT *) elem;

  assert (cobj != NULL);
  assert (rk != NULL);

  *rk = &cobj->xoid;

  return NO_ERROR;
}

/*
 * opool_ht_elem_dtor -
 *    return: NO_ERROR if successful, error code otherwise
 *    elem():
 */
static void
opool_ht_elem_dtor (void *elem)
{
  CCI_OBJECT *obj = (CCI_OBJECT *) elem;

  destroy_cci_object (obj);
}

/*
 * opool_ght_keyf -
 *    return: NO_ERROR if successful, error code otherwise
 *    elem():
 *    rk():
 */
static int
opool_ght_keyf (void *elem, void **rk)
{
  GLO_OFFSET *obj = (GLO_OFFSET *) elem;

  assert (obj != NULL);
  assert (rk != NULL);

  *rk = &obj->xoid;

  return NO_ERROR;
}

/*
 * opool_ght_elem_dtor -
 *    return: NO_ERROR if successful, error code otherwise
 *    elem():
 */
static void
opool_ght_elem_dtor (void *elem)
{
  GLO_OFFSET *obj = (GLO_OFFSET *) elem;

  API_FREE (obj);
}

/*
 * opool_create -
 *    return: NO_ERROR if successful, error code otherwise
 *    pconn():
 *    rpool():
 */
static int
opool_create (CONNECTION_IMPL * pconn, CCI_OBJECT_POOL ** rpool)
{
  CCI_OBJECT_POOL *pool;
  hash_table *ht, *ght;
  int res;

  assert (pconn != NULL);
  assert (rpool != NULL);

  pool = API_MALLOC (sizeof (*pool));
  if (pool == NULL)
    {
      return ER_INTERFACE_NO_MORE_MEMORY;

    }

  ht = NULL;
  res = hash_new (64, opool_ht_hashf, opool_ht_keyf, opool_ht_comparef, &ht);
  if (res != NO_ERROR)
    {
      API_FREE (pool);
      return res;
    }

  res = hash_new (32, opool_ht_hashf, opool_ght_keyf, opool_ht_comparef,
		  &ght);
  if (res != NO_ERROR)
    {
      API_FREE (pool);
      hash_destroy (ht, NULL);
    }

  pool->ifs.get_object_resultset = opool_get_object_resultset;
  pool->ifs.oid_delete = opool_oid_delete;
  pool->ifs.oid_get_classname = opool_oid_get_classname;
  pool->ifs.destroy = opool_destroy;

  pool->pconn = pconn;
  pool->ht = ht;
  pool->ght = ght;

  *rpool = pool;

  return NO_ERROR;
}

/*
 * api_col_length -
 *    return: NO_ERROR if successful, error code otherwise
 *    coo():
 *    length():
 */
static int
api_col_length (API_COLLECTION * coo, int *length)
{
  CCI_COLLECTION *col = (CCI_COLLECTION *) coo;
  int res, len;

  res = col->indexer->ifs->length (col->indexer, &len);
  if (res == NO_ERROR)
    *length = len;
  return res;
}

/*
 * api_col_insert -
 *    return: NO_ERROR if successful, error code otherwise
 *    coo():
 *    pos():
 *    type():
 *    ptr():
 *    size():
 */
static int
api_col_insert (API_COLLECTION * coo, long pos, CI_TYPE type,
		void *ptr, size_t size)
{
  CCI_COLLECTION *col = (CCI_COLLECTION *) coo;
  API_VAL *val;
  int res;

  if (col->type != CI_TYPE_NULL && type != CI_TYPE_NULL && col->type != type)
    {
      return ER_INTERFACE_INVALID_ARGUMENT;
    }

  res = col->indexer->ifs->check (col->indexer, (int) pos, CHECK_FOR_INSERT);
  if (res != NO_ERROR)
    {
      return res;
    }

  val = API_CALLOC (1, sizeof (*val));
  if (val == NULL)
    {
      return ER_INTERFACE_NO_MORE_MEMORY;
    }

  res = set_value_to_api_val (val, type, ptr, size);
  if (res != NO_ERROR)
    {
      API_FREE (val);
      return res;
    }

  res = col->indexer->ifs->insert (col->indexer, (int) pos, NULL,
				   (API_VALUE *) val);
  if (res != NO_ERROR)
    {
      api_val_dtor (NULL, (API_VALUE *) val);
    }

  if (type != CI_TYPE_NULL && col->type == CI_TYPE_NULL)
    {
      col->type = type;
    }

  return res;
}

/*
 * api_col_update -
 *    return: NO_ERROR if successful, error code otherwise
 *    coo():
 *    pos():
 *    type():
 *    ptr():
 *    size():
 */
static int
api_col_update (API_COLLECTION * coo, long pos, CI_TYPE type,
		void *ptr, size_t size)
{
  CCI_COLLECTION *col = (CCI_COLLECTION *) coo;
  API_VAL *val;
  VALUE_AREA *va;
  int res;

  if (col->type != CI_TYPE_NULL && type != CI_TYPE_NULL && col->type != type)
    {
      return ER_INTERFACE_INVALID_ARGUMENT;
    }
  res = col->indexer->ifs->check (col->indexer, (int) pos,
				  CHECK_FOR_GET | CHECK_FOR_SET);
  if (res != NO_ERROR)
    {
      return res;
    }

  res = col->indexer->ifs->get (col->indexer, (int) pos, &va,
				(API_VALUE **) & val);
  if (res != NO_ERROR)
    {
      return res;
    }

  res = set_value_to_api_val (val, type, ptr, size);

  return res;
}

/*
 * api_col_delete -
 *    return: NO_ERROR if successful, error code otherwise
 *    coo():
 *    pos():
 */
static int
api_col_delete (API_COLLECTION * coo, long pos)
{
  CCI_COLLECTION *col = (CCI_COLLECTION *) coo;
  API_VAL *val;
  VALUE_AREA *va;
  int res;

  res = col->indexer->ifs->check (col->indexer, (int) pos, CHECK_FOR_DELETE);
  if (res != NO_ERROR)
    {
      return res;
    }

  res = col->indexer->ifs->delete (col->indexer, (int) pos, &va,
				   (API_VALUE **) & val);
  if (res != NO_ERROR)
    {
      return res;
    }

  xcol_elem_dtor (va, (API_VALUE *) val);
  return NO_ERROR;
}

/*
 * api_col_get_elem_domain_info -
 *    return: NO_ERROR if successful, error code otherwise
 *    coo():
 *    pos():
 *    type():
 *    precision():
 *    scale():
 */
static int
api_col_get_elem_domain_info (API_COLLECTION * coo, long pos,
			      CI_TYPE * type, int *precision, int *scale)
{
  CCI_COLLECTION *col = (CCI_COLLECTION *) coo;
  API_VAL *val;
  VALUE_AREA *va;
  int res;

  res = col->indexer->ifs->check (col->indexer, (int) pos, CHECK_FOR_GET);
  if (res != NO_ERROR)
    {
      return res;
    }

  res = col->indexer->ifs->get (col->indexer, (int) pos, &va,
				(API_VALUE **) & val);
  if (res != NO_ERROR)
    {
      return res;
    }

  *type = val->type;

  if (precision)
    {
      *precision = 0;
    }
  if (scale)
    {
      *scale = 0;
    }

  return res;
}

/*
 * api_col_get_elem -
 *    return: NO_ERROR if successful, error code otherwise
 *    coo():
 *    pos():
 *    type():
 *    addr():
 *    len():
 *    outlen():
 *    isnull():
 */
static int
api_col_get_elem (API_COLLECTION * coo, long pos, CI_TYPE type,
		  void *addr, size_t len, size_t * outlen, bool * isnull)
{
  CCI_COLLECTION *col = (CCI_COLLECTION *) coo;
  API_VAL *val;
  VALUE_AREA *va;
  int res;

  res = col->indexer->ifs->check (col->indexer, (int) pos, CHECK_FOR_GET);
  if (res != NO_ERROR)
    {
      return res;
    }

  res = col->indexer->ifs->get (col->indexer, (int) pos, &va,
				(API_VALUE **) & val);
  if (res != NO_ERROR)
    {
      return res;
    }

  res = get_value_from_api_val (val, type, addr, len, outlen, isnull);

  return res;
}

/*
 * api_col_destroy -
 *    return: void
 *    coo():
 */
static void
api_col_destroy (API_COLLECTION * coo)
{
  CCI_COLLECTION *col = (CCI_COLLECTION *) coo;

  xcol_destroy ((CI_COLLECTION) col);
}

static API_COLLECTION_IFS COL_IFS_ = {
  api_col_length,
  api_col_insert,
  api_col_update,
  api_col_delete,
  api_col_get_elem_domain_info,
  api_col_get_elem,
  api_col_destroy
};

/*
 * xcol_elem_dtor -
 *    return: NO_ERROR if successful, error code otherwise
 *    va():
 *    av():
 */
static void
xcol_elem_dtor (VALUE_AREA * va, API_VALUE * av)
{
  API_VAL *pv = (API_VAL *) av;

  if (pv)
    {
      api_val_dtor (va, av);
    }
}

/*
 * xcol_create -
 *    return: NO_ERROR if successful, error code otherwise
 *    type():
 *    conn():
 *    rcol():
 */
static int
xcol_create (CI_TYPE type, CI_CONNECTION conn, CI_COLLECTION * rcol)
{
  int res;
  CCI_COLLECTION *col;

  col = API_MALLOC (sizeof (*col));
  if (col == NULL)
    {
      return ER_INTERFACE_NO_MORE_MEMORY;
    }

  col->col.conn = conn;
  col->col.ifs = &COL_IFS_;
  col->type = type;

  res = list_indexer_create (&col->indexer);
  if (res != NO_ERROR)
    {
      API_FREE (col);
      return res;
    }

  *rcol = col;

  return NO_ERROR;
}

/*
 * xcol_destroy -
 *    return: void
 *    col():
 */
static void
xcol_destroy (CI_COLLECTION col)
{
  CCI_COLLECTION *co = (CCI_COLLECTION *) col;

  if (co)
    {
      co->indexer->ifs->destroy (co->indexer, xcol_elem_dtor);
      API_FREE (co);
    }
}

/*
 * xcol_elem_cci_bind_mapf -
 *    return: NO_ERROR if successful, error code otherwise
 *    arg():
 *    index():
 *    va():
 *    av():
 */
static int
xcol_elem_cci_bind_mapf (void *arg, int index, VALUE_AREA * va,
			 API_VALUE * av)
{
  API_VAL_CCI_BIND *binds = (API_VAL_CCI_BIND *) arg;
  API_VAL *pv = (API_VAL *) av;
  API_VAL_CCI_BIND *bind;
  int res;

  assert (va == NULL);

  bind = &binds[index];
  api_val_cci_bind_init (bind, -1LL, API_VAL_CCI_BIND_FLAG_SET);
  res = api_val_cci_bind_bind (pv->type, pv->ptr, pv->len, bind);

  return res;
}

/*
 * convert to string. this is the only safe way when col is heterogeneous
 */

/*
 * xcol_to_cci_set -
 *    return: NO_ERROR if successful, error code otherwise
 *    col():
 *    rtset():
 */
static int
xcol_to_cci_set (CI_COLLECTION col, T_CCI_SET * rtset)
{
  CCI_COLLECTION *co;
  T_CCI_SET tset;
  T_CCI_U_TYPE utype;
  int res, size, i;
  void **values;		/* values array */
  int *indicators;		/* null indicator array */
  API_VAL_CCI_BIND *binds;	/* bind array */

  assert (col != NULL);
  assert (tset != NULL);

  co = (CCI_COLLECTION *) col;
  res = co->indexer->ifs->length (co->indexer, &size);
  if (res != NO_ERROR)
    {
      return res;
    }

  values = NULL;
  indicators = NULL;
  binds = NULL;

  if (size > 0)
    {
      values = API_CALLOC (size, sizeof (void *));
      indicators = API_CALLOC (size, sizeof (int));
      binds = API_CALLOC (size, sizeof (API_VAL_CCI_BIND));

      if (values == NULL || indicators == NULL || binds == NULL)
	{
	  res = ER_INTERFACE_NO_MORE_MEMORY;
	  goto res_return;
	}
    }
  else
    {
      /* empty collection is not null value */
      ;
    }

  res = co->indexer->ifs->map (co->indexer, xcol_elem_cci_bind_mapf, binds);
  if (res != NO_ERROR)
    {
      return res;
    }

  for (i = 0; i < size; i++)
    {
      values[i] = binds[i].value;
      indicators[i] = (binds[i].atype == CCI_U_TYPE_NULL);
    }

  utype = type_to_cci_u_type (co->type);

  res = cci_set_make (&tset, utype, size, values, indicators);
  if (res != 0)
    {
      res = err_from_cci (res);
      goto res_return;
    }

  *rtset = tset;
  res = NO_ERROR;

res_return:
  if (indicators)
    {
      API_FREE (indicators);
    }

  if (values)
    {
      API_FREE (values);
    }

  if (binds)
    {
      /*
       * calloc'ed API_VAL_CCI_BIND is safe to pass to api_val_cci_bind_clear()
       */
      for (i = 0; i < size; i++)
	{
	  api_val_cci_bind_clear (&binds[i]);
	}
      API_FREE (binds);
    }

  return res;
}

/*
 * cci_set_to_xcol -
 *    return: NO_ERROR if successful, error code otherwise
 *    conn():
 *    tset():
 *    rcol():
 */
static int
cci_set_to_xcol (CI_CONNECTION conn, T_CCI_SET tset, CI_COLLECTION * rcol)
{
  T_CCI_U_TYPE utype;
  CI_TYPE type;
  CI_COLLECTION col;
  CCI_COLLECTION *co;
  API_VAL *pv;
  int i, size = 0;
  int res;

  assert (tset != NULL);
  assert (rcol != NULL);

  utype = cci_set_element_type (tset);
  type = cci_u_type_to_type (utype);
  res = xcol_create (type, conn, &col);
  if (res != NO_ERROR)
    {
      return res;
    }

  co = (CCI_COLLECTION *) col;

  for (i = 0; i < size; i++)
    {
      pv = NULL;

      res = get_value_from_tset (utype, tset, type, conn, i, &pv);
      if (res != NO_ERROR)
	{
	  goto res_return;
	}

      assert (pv != NULL);

      res = co->indexer->ifs->insert (co->indexer, i - 1, NULL,
				      (API_VALUE *) pv);
      if (res != NO_ERROR)
	{
	  api_val_dtor (NULL, (API_VALUE *) pv);
	  goto res_return;
	}
    }

res_return:
  if (col != NULL)
    {
      xcol_destroy (col);
    }

  return res;
}

/*
 * xcol_copy -
 *    return: NO_ERROR if successful, error code otherwise
 *    col():
 *    rcol():
 */
static int
xcol_copy (CI_COLLECTION col, CI_COLLECTION * rcol)
{
  int size, res, i;
  CI_COLLECTION rc;
  CCI_COLLECTION *co;
  CCI_COLLECTION *rco;

  assert (col != NULL);
  assert (rcol != NULL);

  co = (CCI_COLLECTION *) col;

  res = xcol_create (co->type, co->col.conn, &rc);
  if (res != NO_ERROR)
    {
      return res;
    }
  rco = (CCI_COLLECTION *) rc;

  res = co->indexer->ifs->length (co->indexer, &size);
  if (res != NO_ERROR)
    {
      xcol_destroy (rco);
      return res;
    }

  for (i = 0; i < size; i++)
    {
      VALUE_AREA *va;
      API_VAL *pv, *pvc;

      res = co->indexer->ifs->get (co->indexer, i, &va, (API_VALUE **) & pv);
      if (res != NO_ERROR)
	{
	  xcol_destroy (rco);
	  return res;
	}

      pvc = API_CALLOC (1, sizeof (*pvc));
      if (pvc == NULL)
	{
	  xcol_destroy (rco);
	  return ER_INTERFACE_NO_MORE_MEMORY;
	}

      res = set_value_to_api_val (pvc, pv->type, pv->ptr, pv->len);
      if (res != NO_ERROR)
	{
	  API_FREE (pvc);
	  xcol_destroy (rco);
	  return res;
	}

      res = rco->indexer->ifs->insert (rco->indexer, i - 1, NULL,
				       (API_VALUE *) pvc);
      if (res != NO_ERROR)
	{
	  api_val_dtor (NULL, (API_VALUE *) pvc);
	  xcol_destroy (rco);
	  return res;
	}
    }

  *rcol = (CI_COLLECTION) rco;
  return NO_ERROR;
}

/*
 * ci_create_connection_impl -
 *    return: NO_ERROR if successful, error code otherwise
 *    conn():
 */
static int
ci_create_connection_impl (CI_CONNECTION * conn)
{
  int res;
  int rid;
  CONNECTION_IMPL *pconn;
  BH_INTERFACE *bh;
  CCI_OBJECT_POOL *pool = NULL;

  pconn = (CONNECTION_IMPL *) API_CALLOC (1, sizeof (*pconn));
  if (pconn == NULL)
    {
      return ER_INTERFACE_NO_MORE_MEMORY;
    }

  res = opool_create (pconn, &pool);
  if (res != NO_ERROR)
    {
      opool_destroy ((API_OBJECT_RESULTSET_POOL *) pool);
      API_FREE (pconn);
      return res;
    }

  res = bh_root_acquire (&rid, BH_ROOT_TYPE_STATIC_HASH_64);
  if (res != NO_ERROR)
    {
      API_FREE (pconn);
      return res;
    }

  res = bh_root_lock (rid, &bh);
  if (res != NO_ERROR)
    {
      API_FREE (pconn);
      return res;
    }

  res = bind_api_structure (bh, (COMMON_API_STRUCTURE *) pconn, NULL, conn);
  if (res != NO_ERROR)
    {
      (void) bh_root_unlock (rid);
      (void) bh_root_release (rid);
      API_FREE (pconn);
      opool_destroy ((API_OBJECT_RESULTSET_POOL *) pool);
      return res;
    }

  init_connection_impl (pconn, rid, *conn, bh, -1, pool);
  (void) bh_root_unlock (rid);
  return NO_ERROR;
}

/*
 * ci_err_set_impl -
 *    return: NO_ERROR if successful, error code otherwise
 *    err_code():
 */
static int
ci_err_set_impl (int err_code)
{
  return NO_ERROR;
}

/*
 * ci_conn_connect_impl -
 *    return: NO_ERROR if successful, error code otherwise
 *    conn():
 *    host():
 *    port():
 *    databasename():
 *    user_name():
 *    password():
 */
static int
ci_conn_connect_impl (COMMON_API_STRUCTURE * conn,
		      const char *host,
		      unsigned short port,
		      const char *databasename,
		      const char *user_name, const char *password)
{
  CONNECTION_IMPL *pconn = (CONNECTION_IMPL *) conn;
  int res;

  res = cci_connect ((char *) host, port,
		     (char *) databasename,
		     (char *) user_name, (char *) password);
  if (res < 0)
    {
      return err_from_cci (res);
    }

  pconn->conn_handle = res;
  return NO_ERROR;
}

/*
 * ci_conn_close_impl -
 *    return:
 *    conn():
 */
static int
ci_conn_close_impl (COMMON_API_STRUCTURE * conn)
{
  CONNECTION_IMPL *pconn = (CONNECTION_IMPL *) conn;

  (void) cci_disconnect (pconn->conn_handle, &pconn->err_buf);
  return NO_ERROR;
}

/*
 * ci_conn_create_statement_impl -
 *    return:
 *    conn():
 *    stmt():
 */
static int
ci_conn_create_statement_impl (COMMON_API_STRUCTURE * conn,
			       CI_STATEMENT * stmt)
{
  CONNECTION_IMPL *pconn = (CONNECTION_IMPL *) conn;
  STATEMENT_IMPL *pstmt;
  int res;

  pstmt = (STATEMENT_IMPL *) API_MALLOC (sizeof (*pstmt));
  if (pstmt == NULL)
    {
      return ER_INTERFACE_NO_MORE_MEMORY;
    }

  init_statement_impl (pstmt, pconn);

  res = bind_api_structure (pconn->bh,
			    (COMMON_API_STRUCTURE *)
			    pstmt, (COMMON_API_STRUCTURE *) pconn, stmt);
  if (res != NO_ERROR)
    {
      API_FREE (pstmt);
      return res;
    }

  return NO_ERROR;
}

/*
 * ci_conn_set_option_impl -
 *    return:
 *    conn():
 *    option():
 *    arg():
 *    size():
 */
static int
ci_conn_set_option_impl (COMMON_API_STRUCTURE * conn,
			 CI_CONNECTION_OPTION option, void *arg, size_t size)
{
  CONNECTION_IMPL *pconn = (CONNECTION_IMPL *) conn;
  int res = 0;

  switch (option)
    {
    case CI_CONNECTION_OPTION_LOCK_TIMEOUT:
    case CI_CONNECTION_OPTION_TRAN_ISOLATION_LV:
    case CI_CONNECTION_OPTION_AUTOCOMMIT:
      if (size != sizeof (int))
	{
	  return ER_INTERFACE_INVALID_ARGUMENT;
	}

      if (option == CI_CONNECTION_OPTION_LOCK_TIMEOUT)
	{
	  res = cci_set_db_parameter (pconn->conn_handle,
				      CCI_PARAM_LOCK_TIMEOUT,
				      arg, &pconn->err_buf);
	}
      else if (option == CI_CONNECTION_OPTION_TRAN_ISOLATION_LV)
	{
	  res = cci_set_db_parameter (pconn->conn_handle,
				      CCI_PARAM_ISOLATION_LEVEL,
				      arg, &pconn->err_buf);
	}
      else if (option == CI_CONNECTION_OPTION_AUTOCOMMIT)
	{
	  res = cci_set_db_parameter (pconn->conn_handle,
				      CCI_PARAM_AUTO_COMMIT,
				      arg, &pconn->err_buf);
	}

      if (res != 0)
	{
	  return err_from_cci (pconn->err_buf.err_code);
	}

      if (option == CI_CONNECTION_OPTION_AUTOCOMMIT)
	{
	  pconn->autocommit = *(int *) arg != 0;
	}

      break;

    case CI_CONNECTION_OPTION_CLIENT_VERSION:
    case CI_CONNECTION_OPTION_SERVER_VERSION:
    default:
      return ER_INTERFACE_INVALID_ARGUMENT;
    }

  return NO_ERROR;
}

/*
 * ci_conn_get_option_impl -
 *    return:
 *    conn():
 *    option():
 *    arg():
 *    size():
 */
static int
ci_conn_get_option_impl (COMMON_API_STRUCTURE * conn,
			 CI_CONNECTION_OPTION option, void *arg, size_t size)
{
  CONNECTION_IMPL *pconn = (CONNECTION_IMPL *) conn;
  int res = 0;

  switch (option)
    {
    case CI_CONNECTION_OPTION_LOCK_TIMEOUT:
    case CI_CONNECTION_OPTION_TRAN_ISOLATION_LV:
    case CI_CONNECTION_OPTION_AUTOCOMMIT:
      if (size != sizeof (int))
	{
	  return ER_INTERFACE_INVALID_ARGUMENT;
	}

      if (option == CI_CONNECTION_OPTION_LOCK_TIMEOUT)
	{
	  res = cci_get_db_parameter (pconn->conn_handle,
				      CCI_PARAM_LOCK_TIMEOUT,
				      arg, &pconn->err_buf);
	}
      else if (option == CI_CONNECTION_OPTION_TRAN_ISOLATION_LV)
	{
	  res = cci_get_db_parameter (pconn->conn_handle,
				      CCI_PARAM_ISOLATION_LEVEL,
				      arg, &pconn->err_buf);
	}
      else if (option == CI_CONNECTION_OPTION_AUTOCOMMIT)
	{
	  res = cci_get_db_parameter (pconn->conn_handle,
				      CCI_PARAM_AUTO_COMMIT,
				      arg, &pconn->err_buf);
	}

      if (res != 0)
	{
	  return err_from_cci (pconn->err_buf.err_code);
	}
      break;

    case CI_CONNECTION_OPTION_CLIENT_VERSION:
      {
	int mj, mi, pa, res;
	char buf[48];

	res = cci_get_version (&mj, &mi, &pa);
	if (res != 0)
	  {
	    return err_from_cci (pconn->err_buf.err_code);
	  }

	sprintf (buf, "cci %d.%d.%d", mj, mi, pa);
	strncpy (arg, buf, size);
	break;
      }

    case CI_CONNECTION_OPTION_SERVER_VERSION:
    default:
      return ER_INTERFACE_INVALID_ARGUMENT;
    }

  return NO_ERROR;
}

/*
 * ci_conn_commit_impl -
 *    return:
 *    conn():
 */
static int
ci_conn_commit_impl (COMMON_API_STRUCTURE * conn)
{
  CONNECTION_IMPL *pconn = (CONNECTION_IMPL *) conn;
  int res;

  res = cci_end_tran (pconn->conn_handle, CCI_TRAN_COMMIT, &pconn->err_buf);
  if (res != 0)
    {
      return err_from_cci (pconn->err_buf.err_code);
    }

  res = complete_connection (pconn);

  return res;
}

/*
 * ci_conn_rollback_impl -
 *    return:
 *    conn():
 */
static int
ci_conn_rollback_impl (COMMON_API_STRUCTURE * conn)
{
  CONNECTION_IMPL *pconn = (CONNECTION_IMPL *) conn;
  int res;

  res = cci_end_tran (pconn->conn_handle, CCI_TRAN_ROLLBACK, &pconn->err_buf);
  if (res != 0)
    {
      return err_from_cci (pconn->err_buf.err_code);
    }

  res = complete_connection (pconn);

  return res;
}

/*
 * ci_conn_get_error_impl -
 *    return:
 *    conn():
 *    err():
 *    msg():
 *    size():
 */
static int
ci_conn_get_error_impl (COMMON_API_STRUCTURE * conn, int *err,
			char *msg, size_t size)
{
  CONNECTION_IMPL *pconn = (CONNECTION_IMPL *) conn;

  *err = err_from_cci (pconn->err_buf.err_code);
  strncpy (msg, pconn->err_buf.err_msg, size);

  return NO_ERROR;
}

/*
 * ci_stmt_add_batch_query_impl -
 *    return:
 *    stmt():
 *    sql():
 *    len():
 */
static int
ci_stmt_add_batch_query_impl (COMMON_API_STRUCTURE * stmt,
			      const char *sql, size_t len)
{
  STATEMENT_IMPL *pstmt = (STATEMENT_IMPL *) stmt;
  BATCH_SQL_ITEM *bsi;

  if (pstmt->status & CI_STMT_STATUS_PREPARED)
    {
      return ER_INTERFACE_IS_PREPARED_STATEMENT;
    }

  if (!(pstmt->status & CI_STMT_STATUS_BATCH_ADDED)
      && (pstmt->status & CI_STMT_STATUS_EXECUTED))
    {
      return ER_INTERFACE_IS_NOT_BATCH_STATEMENT;
    }

  bsi = API_MALLOC (sizeof (*bsi) + len);
  if (bsi == NULL)
    {
      return ER_INTERFACE_NO_MORE_MEMORY;
    }

  dlisth_init (&bsi->h);
  memcpy (bsi->sql, sql, len);
  bsi->sql[len] = 0;
  dlisth_insert_before ((dlisth *) bsi, &pstmt->batch);
  pstmt->num_batch++;

  return NO_ERROR;
}

/*
 * ci_stmt_add_batch_impl -
 *    return:
 *    stmt():
 */
static int
ci_stmt_add_batch_impl (COMMON_API_STRUCTURE * stmt)
{
  STATEMENT_IMPL *pstmt = (STATEMENT_IMPL *) stmt;
  BATCH_ARY_ITEM *bai;
  int res;
  struct add_batch_params_arg arg;

  if (!(pstmt->status & CI_STMT_STATUS_PREPARED))
    {
      return ER_INTERFACE_IS_NOT_PREPARED_STATEMENT;
    }

  if (!(pstmt->status & CI_STMT_STATUS_BATCH_ADDED)
      && (pstmt->status & CI_STMT_STATUS_EXECUTED))
    {
      return ER_INTERFACE_IS_NOT_BATCH_STATEMENT;
    }

  bai = API_MALLOC (sizeof (*bai)
		    + sizeof (API_VAL *) * (pstmt->num_col - 1));
  if (bai == NULL)
    {
      return ER_INTERFACE_NO_MORE_MEMORY;
    }

  dlisth_init (&bai->h);
  arg.nadded = 0;
  arg.bai = bai;
  arg.indexer = pstmt->params;

  res = pstmt->params->ifs->map (pstmt->params, add_batch_params_mapf, &arg);
  if (res != NO_ERROR)
    {
      (void) pstmt->params->ifs->map (pstmt->params,
				      add_batch_params_restore_mapf, &arg);
      API_FREE (bai);
      return res;
    }

  dlisth_insert_before ((dlisth *) bai, &pstmt->batch);
  pstmt->num_batch++;

  return NO_ERROR;
}

/*
 * ci_stmt_clear_batch_impl -
 *    return:
 *    stmt():
 */
static int
ci_stmt_clear_batch_impl (COMMON_API_STRUCTURE * stmt)
{
  STATEMENT_IMPL *pstmt = (STATEMENT_IMPL *) stmt;
  int res;

  if ((!(pstmt->status & CI_STMT_STATUS_BATCH_ADDED)
       && (pstmt->status & CI_STMT_STATUS_EXECUTED)))
    {
      return ER_INTERFACE_CANNOT_CLEAR_BATCH;
    }

  res = stmt_complete_batch (pstmt);

  return res;
}

#define API_EXEC_PRE()                                                \
do {                                                                  \
  if (pstmt->status & CI_STMT_STATUS_EXECUTED)                     \
    {                                                                 \
      res = complete_statement (pstmt);                               \
      if (res != NO_ERROR)                                            \
        return res;                                             \
      pstmt->status &= ~CI_STMT_STATUS_EXECUTED;                   \
    }                                                                 \
                                                                      \
  if (pstmt->pconn->autocommit && pstmt->pconn->n_need_complete > 0)  \
    {                                                                 \
      res = complete_connection (pstmt->pconn);                       \
      if (res != NO_ERROR)                                            \
        return res;                                             \
      assert (pstmt->pconn->n_need_complete == 0);                    \
    }                                                                 \
} while (0)

#define API_EXEC_POST(has_result)                                     \
do {                                                                  \
  if (pstmt->pconn->autocommit && !(has_result))                      \
    {                                                                 \
      res =                                                           \
        cci_end_tran (pstmt->pconn->conn_handle, CCI_TRAN_COMMIT,     \
                      &pstmt->pconn->err_buf);                        \
      if (r != 0)                                                     \
        return err_from_cci (res);                              \
    }                                                                 \
} while (0)

/*
 * ci_stmt_execute_immediate_impl -
 *    return:
 *    stmt():
 *    sql():
 *    len():
 *    rs():
 *    r():
 */
static int
ci_stmt_execute_immediate_impl (COMMON_API_STRUCTURE * stmt, char *sql,
				size_t len, CI_RESULTSET * rs, int *r)
{
  STATEMENT_IMPL *pstmt = (STATEMENT_IMPL *) stmt;
  int res;
  int req_handle = -1;
  char pp_flag = 0;

  /* check status */
  if (pstmt->status & CI_STMT_STATUS_PREPARED)
    {
      return ER_INTERFACE_IS_PREPARED_STATEMENT;
    }

  if (pstmt->status & CI_STMT_STATUS_BATCH_ADDED)
    {
      return ER_INTERFACE_IS_BATCH_STATEMENT;
    }

  API_EXEC_PRE ();

  /* flag is inherited */
  if (pstmt->opt_updatable_result)
    {
      pp_flag |= CCI_PREPARE_UPDATABLE;
    }

  req_handle = cci_prepare (pstmt->pconn->conn_handle, sql,
			    pp_flag, &pstmt->pconn->err_buf);
  if (req_handle < 0)
    {
      return err_from_cci (pstmt->pconn->err_buf.err_code);
    }

  pstmt->req_handle = req_handle;
  res = statement_execute (pstmt, &pstmt->pconn->err_buf);
  if (res != NO_ERROR)
    {
      (void) cci_close_req_handle (req_handle);
      pstmt->req_handle = -1;
      return res;
    }

  res = statement_get_reshandle_or_affectedrows (pstmt->pconn->bh, pstmt);
  if (res != NO_ERROR)
    {
      (void) cci_close_req_handle (req_handle);
      pstmt->req_handle = -1;
      return res;
    }

  if (pstmt->has_resultset)
    {
      *rs = pstmt->res_handle;
      pstmt->pconn->n_need_complete++;
    }
  else
    {
      *r = pstmt->affected_rows;
    }

  pstmt->status |= CI_STMT_STATUS_EXECUTED;
  API_EXEC_POST (pstmt->has_resultset);

  return NO_ERROR;
}

/*
 * ci_stmt_execute_impl -
 *    return:
 *    stmt():
 *    rs():
 *    r():
 */
static int
ci_stmt_execute_impl (COMMON_API_STRUCTURE * stmt, CI_RESULTSET * rs, int *r)
{
  STATEMENT_IMPL *pstmt = (STATEMENT_IMPL *) stmt;
  int res;
  int i;

  if (!(pstmt->status & CI_STMT_STATUS_PREPARED))
    {
      return ER_INTERFACE_IS_NOT_PREPARED_STATEMENT;
    }

  API_EXEC_PRE ();

  for (i = 0; i < pstmt->num_col; i++)
    {
      VALUE_AREA *va;
      API_VALUE *v;

      res = pstmt->params->ifs->check (pstmt->params, i, CHECK_FOR_GET);
      if (res != NO_ERROR)
	{
	  return res;
	}

      res = pstmt->params->ifs->get (pstmt->params, i, &va, &v);
      if (res != NO_ERROR)
	{
	  return res;
	}

      if (v == NULL)
	{
	  return ER_INTERFACE_PARAM_IS_NOT_SET;
	}

      res = api_val_bind_param (pstmt->pconn->conn, pstmt->req_handle,
				(API_VAL *) v, i + 1);
      if (res != NO_ERROR)
	{
	  return res;
	}
    }

  res = statement_execute (pstmt, &pstmt->pconn->err_buf);
  if (res != NO_ERROR)
    {
      return res;
    }

  res = statement_get_reshandle_or_affectedrows (pstmt->pconn->bh, pstmt);
  if (res != NO_ERROR)
    {
      return res;
    }

  if (pstmt->has_resultset)
    {
      *rs = pstmt->res_handle;
      pstmt->pconn->n_need_complete++;
    }
  else
    {
      *r = pstmt->affected_rows;
    }

  pstmt->status |= CI_STMT_STATUS_EXECUTED;
  API_EXEC_POST (pstmt->has_resultset);

  return NO_ERROR;
}

/*
 * ci_stmt_execute_batch_impl -
 *    return:
 *    stmt():
 *    br():
 */
static int
ci_stmt_execute_batch_impl (COMMON_API_STRUCTURE * stmt, CI_BATCH_RESULT * br)
{
  STATEMENT_IMPL *pstmt = (STATEMENT_IMPL *) stmt;
  int res;
  BIND_HANDLE handle;

  if (!(pstmt->status & CI_STMT_STATUS_BATCH_ADDED))
    {
      return ER_INTERFACE_CANNOT_BATCH_EXECUTE;
    }

  res = bind_api_structure (pstmt->pconn->bh,
			    ((COMMON_API_STRUCTURE *) & pstmt->BR),
			    (COMMON_API_STRUCTURE *) pstmt, &handle);
  if (res != NO_ERROR)
    {
      return res;
    }

  pstmt->bres_handle = handle;
  if (pstmt->status & CI_STMT_STATUS_PREPARED)
    {
      res = stmt_execute_batch_sql (pstmt);
    }
  else
    {
      res = stmt_execute_batch_array (pstmt);
    }

  if (res != NO_ERROR)
    {
      (void) pstmt->pconn->bh->destroy_handle (pstmt->pconn->bh, handle);
      pstmt->bres_handle = -1LL;
      return res;
    }

  *br = handle;
  return NO_ERROR;
}

/*
 * ci_stmt_get_option_impl -
 *    return:
 *    stmt():
 *    option():
 *    arg():
 *    size():
 */
static int
ci_stmt_get_option_impl (COMMON_API_STRUCTURE * stmt,
			 CI_STATEMENT_OPTION option, void *arg, size_t size)
{
  STATEMENT_IMPL *pstmt = (STATEMENT_IMPL *) stmt;

  switch (option)
    {
    case CI_STATEMENT_OPTION_HOLD_CURSORS_OVER_COMMIT:
      *(int *) arg = 0;		/* not implemented */
      break;

    case CI_STATEMENT_OPTION_UPDATABLE_RESULT:
      *(int *) arg = (pstmt->opt_updatable_result) ? 1 : 0;
      break;

    case CI_STATEMENT_OPTION_GET_GENERATED_KEYS:
      *(int *) arg = 0;		/* not implemented */
      break;

    case CI_STATEMENT_OPTION_ASYNC_QUERY:
      *(int *) arg = (pstmt->opt_async_query) ? 1 : 0;
      break;

    case CI_STATEMENT_OPTION_EXEC_CONTINUE_ON_ERROR:
    case CI_STATEMENT_OPTION_LAZY_EXEC:
      *(int *) arg = 0;
      break;

    default:
      return ER_INTERFACE_INVALID_ARGUMENT;
      break;
    }

  return NO_ERROR;
}

/*
 * ci_stmt_set_option_impl -
 *    return:
 *    stmt():
 *    option():
 *    arg():
 *    size():
 */
static int
ci_stmt_set_option_impl (COMMON_API_STRUCTURE * stmt,
			 CI_STATEMENT_OPTION option, void *arg, size_t size)
{
  STATEMENT_IMPL *pstmt = (STATEMENT_IMPL *) stmt;

  switch (option)
    {

    case CI_STATEMENT_OPTION_UPDATABLE_RESULT:
      pstmt->opt_updatable_result = *(int *) arg != 0 ? true : false;
      break;

    case CI_STATEMENT_OPTION_ASYNC_QUERY:
      pstmt->opt_async_query = *(int *) arg != 0 ? true : false;
      break;

    case CI_STATEMENT_OPTION_EXEC_CONTINUE_ON_ERROR:
    case CI_STATEMENT_OPTION_LAZY_EXEC:
    case CI_STATEMENT_OPTION_GET_GENERATED_KEYS:
    case CI_STATEMENT_OPTION_HOLD_CURSORS_OVER_COMMIT:
      return ER_INTERFACE_NOT_SUPPORTED_OPERATION;

    default:
      return ER_INTERFACE_INVALID_ARGUMENT;
      break;
    }

  return NO_ERROR;
}

/*
 * ci_stmt_prepare_impl -
 *    return:
 *    stmt():
 *    sql():
 *    len():
 */
static int
ci_stmt_prepare_impl (COMMON_API_STRUCTURE * stmt, const char *sql,
		      size_t len)
{
  STATEMENT_IMPL *pstmt = (STATEMENT_IMPL *) stmt;
  int res;
  char pp_flag = 0;
  int req_handle;
  T_CCI_COL_INFO *col_info;
  T_CCI_CUBRID_STMT cmd_type;
  int num_col;

  /* check status */
  if (pstmt->status & CI_STMT_STATUS_PREPARED)
    {
      return ER_INTERFACE_GENERIC;	/* already prepared */
    }

  if (pstmt->status & CI_STMT_STATUS_EXECUTED)
    {
      return ER_INTERFACE_IS_NOT_PREPARED_STATEMENT;
    }

  if (pstmt->opt_updatable_result)
    {
      pp_flag = CCI_PREPARE_UPDATABLE;
    }

  res = cci_prepare (pstmt->pconn->conn_handle,
		     (char *) sql, pp_flag, &pstmt->pconn->err_buf);
  req_handle = res;
  if (res < 0)
    {
      return err_from_cci (res);
    }

  col_info = cci_get_result_info (pstmt->req_handle, &cmd_type, &num_col);
  if (col_info == NULL)
    {
      return ER_INTERFACE_GENERIC;
    }

  pstmt->req_handle = req_handle;
  pstmt->got_pm_handle = false;
  pstmt->pm_handle = -1LL;
  pstmt->num_col = num_col;
  pstmt->cmd_type = cmd_type;
  pstmt->col_info = col_info;
  pstmt->status |= CI_STMT_STATUS_PREPARED;

  return NO_ERROR;
}

/*
 * ci_stmt_register_out_parameter_impl -
 *    return:
 *    stmt():
 *    index():
 */
static int
ci_stmt_register_out_parameter_impl (COMMON_API_STRUCTURE * stmt, int index)
{
  return ER_INTERFACE_NOT_SUPPORTED_OPERATION;
}

/*
 * ci_stmt_get_resultset_metadata_impl -
 *    return:
 *    stmt():
 *    r():
 */
static int
ci_stmt_get_resultset_metadata_impl (COMMON_API_STRUCTURE * stmt,
				     CI_RESULTSET_METADATA * r)
{
  STATEMENT_IMPL *pstmt = (STATEMENT_IMPL *) stmt;
  RESULTSET_IMPL *pres;
  int res;

  if (!(pstmt->status & CI_STMT_STATUS_EXECUTED))
    {
      return ER_INTERFACE_NOT_EXECUTED;
    }

  if (!pstmt->has_resultset)
    {
      return ER_INTERFACE_HAS_NO_RESULT_SET;
    }

  pres = NULL;
  res = pstmt->pconn->bh->lookup (pstmt->pconn->bh, pstmt->res_handle,
				  (BH_BIND **) & pres);
  if (res != NO_ERROR)
    {
      return res;
    }

  assert (pres != NULL);

  if (pres == NULL)
    {
      return ER_INTERFACE_GENERIC;
    }

  res = lazy_bind_qres_rmeta (pres);
  if (res != NO_ERROR)
    {
      return res;
    }

  *r = pres->rm_handle;
  return NO_ERROR;
}

/*
 * ci_stmt_get_parameter_metadata_impl -
 *    return:
 *    stmt():
 *    r():
 */
static int
ci_stmt_get_parameter_metadata_impl (COMMON_API_STRUCTURE * stmt,
				     CI_PARAMETER_METADATA * r)
{
  STATEMENT_IMPL *pstmt = (STATEMENT_IMPL *) stmt;
  int res;

  if (!(pstmt->status & CI_STMT_STATUS_PREPARED))
    {
      return ER_INTERFACE_IS_NOT_PREPARED_STATEMENT;
    }

  res = lazy_bind_pstmt_pmeta (pstmt);
  if (res == NO_ERROR)
    {
      *r = pstmt->pm_handle;
    }

  return res;
}

/*
 * ci_stmt_get_parameter_impl -
 *    return:
 *    stmt():
 *    index():
 *    type():
 *    addr():
 *    len():
 *    outlen():
 *    isnull():
 */
static int
ci_stmt_get_parameter_impl (COMMON_API_STRUCTURE * stmt,
			    int index,
			    CI_TYPE type,
			    void *addr,
			    size_t len, size_t * outlen, bool * isnull)
{
  STATEMENT_IMPL *pstmt = (STATEMENT_IMPL *) stmt;
  int res;
  VALUE_AREA *va;
  API_VAL *pv;

  if (!(pstmt->status & CI_STMT_STATUS_PREPARED))
    {
      return ER_INTERFACE_IS_NOT_PREPARED_STATEMENT;
    }

  if (pstmt->params == NULL)
    {
      return ER_INTERFACE_PARAM_IS_NOT_SET;
    }

  res = pstmt->params->ifs->check (pstmt->params, index - 1, CHECK_FOR_GET);
  if (res != NO_ERROR)
    {
      return res;
    }

  res = pstmt->params->ifs->get (pstmt->params,
				 index - 1, &va, (API_VALUE **) & pv);
  if (res != NO_ERROR)
    {
      return res;
    }

  if (pv != NULL)
    {
      res = get_value_from_api_val (pv, type, addr, len, outlen, isnull);
      return res;
    }
  else
    {
      return ER_INTERFACE_PARAM_IS_NOT_SET;
    }
}

/*
 * ci_stmt_set_parameter_impl -
 *    return:
 *    stmt():
 *    index():
 *    type():
 *    val():
 *    size():
 */
static int
ci_stmt_set_parameter_impl (COMMON_API_STRUCTURE * stmt,
			    int index, CI_TYPE type, void *val, size_t size)
{
  STATEMENT_IMPL *pstmt = (STATEMENT_IMPL *) stmt;
  int res;
  VALUE_AREA *va;
  API_VAL *pv;

  if (!(pstmt->status & CI_STMT_STATUS_PREPARED))
    {
      return ER_INTERFACE_IS_NOT_PREPARED_STATEMENT;
    }

  if (pstmt->params == NULL)
    {
      VALUE_INDEXER *vi;

      res = array_indexer_create (pstmt->num_col, &vi);
      if (res != NO_ERROR)
	{
	  return res;
	}
      pstmt->params = vi;
    }

  res = pstmt->params->ifs->check (pstmt->params,
				   index - 1, CHECK_FOR_GET | CHECK_FOR_SET);
  if (res != NO_ERROR)
    {
      return res;
    }

  res = pstmt->params->ifs->get (pstmt->params,
				 index - 1, &va, (API_VALUE **) & pv);
  if (res != NO_ERROR)
    {
      return res;
    }

  if (pv != NULL)
    {
      res = set_value_to_api_val (pv, type, val, size);
      return res;
    }

  assert (pv == NULL);

  pv = API_CALLOC (1, sizeof (*pv));
  if (pv == NULL)
    {
      return ER_INTERFACE_NO_MORE_MEMORY;
    }

  pv->type = CI_TYPE_NULL;

  res = set_value_to_api_val (pv, type, val, size);
  if (res != NO_ERROR)
    {
      api_val_dtor (NULL, (API_VALUE *) pv);
      return res;
    }

  res = pstmt->params->ifs->set (pstmt->params,
				 index - 1, NULL, (API_VALUE *) pv);
  if (res != NO_ERROR)
    {
      api_val_dtor (NULL, (API_VALUE *) pv);
    }

  return res;
}

/*
 * ci_stmt_get_resultset_impl -
 *    return:
 *    stmt():
 *    r():
 */
static int
ci_stmt_get_resultset_impl (COMMON_API_STRUCTURE * stmt, CI_RESULTSET * r)
{
  STATEMENT_IMPL *pstmt = (STATEMENT_IMPL *) stmt;

  if (!(pstmt->status & CI_STMT_STATUS_EXECUTED))
    {
      return ER_INTERFACE_NOT_EXECUTED;
    }

  if (!pstmt->has_resultset)
    {
      return ER_INTERFACE_HAS_NO_RESULT_SET;
    }

  *r = pstmt->res_handle;

  return NO_ERROR;
}

/*
 * ci_stmt_affected_rows_impl -
 *    return:
 *    stmt():
 *    out():
 */
static int
ci_stmt_affected_rows_impl (COMMON_API_STRUCTURE * stmt, int *out)
{
  STATEMENT_IMPL *pstmt = (STATEMENT_IMPL *) stmt;

  if (!(pstmt->status & CI_STMT_STATUS_EXECUTED))
    {
      return ER_INTERFACE_NOT_EXECUTED;
    }

  *out = pstmt->affected_rows;
  return NO_ERROR;
}

/*
 * ci_stmt_next_result_impl -
 *    return:
 *    stmt():
 *    exist_result():
 */
static int
ci_stmt_next_result_impl (COMMON_API_STRUCTURE * stmt, bool * exist_result)
{
  STATEMENT_IMPL *pstmt = (STATEMENT_IMPL *) stmt;
  int res;

  if (!(pstmt->status & CI_STMT_STATUS_EXECUTED))
    {
      return ER_INTERFACE_NOT_EXECUTED;
    }

  if (pstmt->curr_query_result_index + 1 > pstmt->num_query)
    {
      return ER_INTERFACE_NO_MORE_RESULT;
    }

  pstmt->curr_query_result_index++;
  if (pstmt->has_resultset)
    {
      res = pstmt->pconn->bh->destroy_handle (pstmt->pconn->bh,
					      pstmt->res_handle);
      if (res != NO_ERROR)
	{
	  return res;
	}
    }

  res = cci_next_result (pstmt->req_handle, &pstmt->pconn->err_buf);
  if (res != 0)
    {
      if (res == CAS_ER_NO_MORE_RESULT_SET)
	{
	  return ER_INTERFACE_NO_MORE_RESULT;
	}
      else
	{
	  return err_from_cci (res);
	}
    }

  res = statement_get_reshandle_or_affectedrows (pstmt->pconn->bh, pstmt);
  return res;
}

/*
 * ci_stmt_get_first_error_impl -
 *    return:
 *    stmt():
 *    line():
 *    col():
 *    errcode():
 *    err_msg():
 *    size():
 */
static int
ci_stmt_get_first_error_impl (COMMON_API_STRUCTURE * stmt, int *line,
			      int *col, int *errcode, char *err_msg,
			      size_t size)
{
  return ER_INTERFACE_NOT_SUPPORTED_OPERATION;
}

/*
 * ci_stmt_get_next_error_impl -
 *    return:
 *    stmt():
 *    line():
 *    col():
 *    errcode():
 *    err_msg():
 *    size():
 */
static int
ci_stmt_get_next_error_impl (COMMON_API_STRUCTURE * stmt,
			     int *line, int *col,
			     int *errcode, char *err_msg, size_t size)
{
  return ER_INTERFACE_NOT_SUPPORTED_OPERATION;
}

/*
 * ci_stmt_get_query_type_impl -
 *    return:
 *    stmt():
 *    type(out):
 */
static int
ci_stmt_get_query_type_impl (COMMON_API_STRUCTURE * stmt,
			     CUBRID_STMT_TYPE * type)
{
  return ER_INTERFACE_NOT_SUPPORTED_OPERATION;
}

/*
 * ci_stmt_get_start_line_impl -
 *    return:
 *    stmt():
 *    line(out):
 */
static int
ci_stmt_get_start_line_impl (COMMON_API_STRUCTURE * stmt, int *line)
{
  return ER_INTERFACE_NOT_SUPPORTED_OPERATION;
}

/*
 * ci_batch_res_query_count_impl -
 *    return:
 *    br():
 *    count():
 */
static int
ci_batch_res_query_count_impl (COMMON_API_STRUCTURE * br, int *count)
{
  BATCH_RESULT_IMPL *pbr = (BATCH_RESULT_IMPL *) br;

  *count = pbr->bptr->num_query;
  return NO_ERROR;
}

/*
 * ci_batch_res_get_result_impl -
 *    return:
 *    br():
 *    index():
 *    ret():
 *    nr():
 */
static int
ci_batch_res_get_result_impl (COMMON_API_STRUCTURE * br, int index,
			      int *ret, int *nr)
{
  BATCH_RESULT_IMPL *pbr = (BATCH_RESULT_IMPL *) br;
  int r;

  if (index > pbr->bptr->num_query)
    {
      return ER_INTERFACE_INVALID_ARGUMENT;
    }

  r = CCI_QUERY_RESULT_RESULT (pbr->bptr->query_result, index);
  if (r >= 0)
    {
      *nr = r;
    }

  *ret = r < 0 ? err_from_cci (r) : NO_ERROR;
  return NO_ERROR;
}

/*
 * ci_batch_res_get_error_impl -
 *    return:
 *    br():
 *    index():
 *    err_code():
 *    err_msg():
 *    buf_size():
 */
static int
ci_batch_res_get_error_impl (COMMON_API_STRUCTURE * br, int index,
			     int *err_code, char *err_msg, size_t buf_size)
{
  BATCH_RESULT_IMPL *pbr = (BATCH_RESULT_IMPL *) br;
  int r;
  const char *msg;

  if (index > pbr->bptr->num_query)
    {
      return ER_INTERFACE_INVALID_ARGUMENT;
    }

  r = CCI_QUERY_RESULT_RESULT (pbr->bptr->query_result, index);
  msg = CCI_QUERY_RESULT_ERR_MSG (pbr->bptr->query_result, index);
  if (err_code != NULL)
    {
      *err_code = r < 0 ? err_from_cci (r) : NO_ERROR;
    }

  if (err_msg != NULL)
    {
      if (buf_size <= 0)
	{
	  return ER_INTERFACE_INVALID_ARGUMENT;
	}
      strncpy (err_msg, msg, buf_size);
    }

  return NO_ERROR;
}

/*
 * ci_pmeta_get_count_impl -
 *    return:
 *    pmeta():
 *    count():
 */
static int
ci_pmeta_get_count_impl (COMMON_API_STRUCTURE * pmeta, int *count)
{
  PARAMETER_META_IMPL *pm = (PARAMETER_META_IMPL *) pmeta;

  *count = pm->bptr->num_col;

  return NO_ERROR;
}

/*
 * ci_pmeta_get_info_impl -
 *    return:
 *    pmeta():
 *    index():
 *    type():
 *    arg():
 *    size():
 */
static int
ci_pmeta_get_info_impl (COMMON_API_STRUCTURE * pmeta, int index,
			CI_PMETA_INFO_TYPE type, void *arg, size_t size)
{
  PARAMETER_META_IMPL *pm = (PARAMETER_META_IMPL *) pmeta;

  switch (type)
    {
    case CI_PMETA_INFO_MODE:
      if (size < sizeof (int))
	{
	  return ER_INTERFACE_NOT_ENOUGH_DATA_SIZE;
	}

      *(int *) arg = 0;
      break;

    case CI_PMETA_INFO_COL_TYPE:
      if (size < sizeof (int))
	{
	  return ER_INTERFACE_NOT_ENOUGH_DATA_SIZE;
	}

      *(int *) arg = cci_u_type_to_type (pm->bptr->col_info[index - 1].type);
      break;

    case CI_PMETA_INFO_PRECISION:
      if (size < sizeof (int))
	{
	  return ER_INTERFACE_NOT_ENOUGH_DATA_SIZE;
	}

      *(int *) arg = pm->bptr->col_info[index - 1].precision;
      break;

    case CI_PMETA_INFO_SCALE:
      if (size < sizeof (int))
	{
	  return ER_INTERFACE_NOT_ENOUGH_DATA_SIZE;
	}

      *(int *) arg = pm->bptr->col_info[index - 1].scale;
      break;

    case CI_PMETA_INFO_NULLABLE:
      if (size < sizeof (int))
	{
	  return ER_INTERFACE_NOT_ENOUGH_DATA_SIZE;
	}

      *(int *) arg = pm->bptr->col_info[index - 1].is_non_null ? 0 : 1;
      break;

    default:
      return ER_INTERFACE_INVALID_ARGUMENT;
    }

  return NO_ERROR;
}


/*
 * ci_get_connection_opool_impl -
 *    return:
 *    pst():
 *    rpool():
 */
static int
ci_get_connection_opool_impl (COMMON_API_STRUCTURE * pst,
			      API_OBJECT_RESULTSET_POOL ** rpool)
{
  CONNECTION_IMPL *pconn = (CONNECTION_IMPL *) pst;

  *rpool = (API_OBJECT_RESULTSET_POOL *) pconn->opool;
  return NO_ERROR;
}

/*
 * ci_collection_new_impl -
 *    return:
 *    conn():
 *    coll():
 */
static int
ci_collection_new_impl (CI_CONNECTION conn, CI_COLLECTION * coll)
{
  return xcol_create (CI_TYPE_NULL, conn, coll);
}

/* The only exported one */
CUBRID_API_FUNCTION_TABLE Cubrid_api_function_table = {
  ci_create_connection_impl,
  ci_err_set_impl,
  ci_conn_connect_impl,
  ci_conn_close_impl,
  ci_conn_create_statement_impl,
  ci_conn_set_option_impl,
  ci_conn_get_option_impl,
  ci_conn_commit_impl,
  ci_conn_rollback_impl,
  ci_conn_get_error_impl,
  ci_stmt_add_batch_query_impl,
  ci_stmt_add_batch_impl,
  ci_stmt_clear_batch_impl,
  ci_stmt_execute_immediate_impl,
  ci_stmt_execute_impl,
  ci_stmt_execute_batch_impl,
  ci_stmt_get_option_impl,
  ci_stmt_set_option_impl,
  ci_stmt_prepare_impl,
  ci_stmt_register_out_parameter_impl,
  ci_stmt_get_resultset_metadata_impl,
  ci_stmt_get_parameter_metadata_impl,
  ci_stmt_get_parameter_impl,
  ci_stmt_set_parameter_impl,
  ci_stmt_get_resultset_impl,
  ci_stmt_affected_rows_impl,
  ci_stmt_get_query_type_impl,
  ci_stmt_get_start_line_impl,
  ci_stmt_next_result_impl,
  ci_stmt_get_first_error_impl,
  ci_stmt_get_next_error_impl,
  ci_batch_res_query_count_impl,
  ci_batch_res_get_result_impl,
  ci_batch_res_get_error_impl,
  ci_pmeta_get_count_impl,
  ci_pmeta_get_info_impl,
  ci_get_connection_opool_impl,
  ci_collection_new_impl
};
