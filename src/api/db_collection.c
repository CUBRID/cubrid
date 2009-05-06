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
 * db_collection.c -
 */

#include <assert.h>
#include "db_stub.h"
#include "api_util.h"
#include "dbi.h"

typedef struct collection_s_ COLLECTION_;
struct collection_s_
{
  API_COLLECTION col;
  DB_TYPE dt;
  VALUE_INDEXER *indexer;
};

static int col_api_length (API_COLLECTION * col, int *len);
static int col_api_insert (API_COLLECTION * col, long pos, CI_TYPE type,
			   void *ptr, size_t size);
static int col_api_update (API_COLLECTION * col, long pos, CI_TYPE type,
			   void *ptr, size_t size);
static int col_api_delete (API_COLLECTION * col, long pos);
static int col_api_get_elem_domain_info (API_COLLECTION * col, long pos,
					 CI_TYPE * type, int *precision,
					 int *scale);
static int col_api_get_elem (API_COLLECTION * col, long pos, CI_TYPE type,
			     void *addr, size_t len, size_t * outlen,
			     bool * isnull);
static void col_dtorf (VALUE_AREA * va, API_VALUE * val);
static void col_api_destroy (API_COLLECTION * col);

static int apif_collection_create (BIND_HANDLE conn, COLLECTION_ ** rc);
static int fill_collection (COLLECTION_ * co, DB_SET * set);


/*
 * col_api_length -
 *    return:
 *    col():
 *    len():
 */
static int
col_api_length (API_COLLECTION * col, int *len)
{
  COLLECTION_ *co = (COLLECTION_ *) col;
  int res;
  assert (co != NULL);
  assert (len != NULL);
  res = co->indexer->ifs->length (co->indexer, len);
  return res;
}

/*
 * col_api_insert -
 *    return:
 *    col():
 *    pos():
 *    type():
 *    ptr():
 *    size():
 */
static int
col_api_insert (API_COLLECTION * col, long pos, CI_TYPE type, void *ptr,
		size_t size)
{
  COLLECTION_ *co;
  DB_VALUE *val;
  int res;

  co = (COLLECTION_ *) col;
  assert (co != NULL);

  res = co->indexer->ifs->check (co->indexer, (int) pos, CHECK_FOR_INSERT);
  if (res != NO_ERROR)
    {
      return res;
    }

  val = db_value_create ();
  if (val == NULL)
    return ER_INTERFACE_NO_MORE_MEMORY;

  res = coerce_value_to_db_value (type, ptr, size, val, false);
  if (res != NO_ERROR)
    return res;

  res =
    co->indexer->ifs->insert (co->indexer, (int) pos, NULL,
			      (API_VALUE *) val);
  if (res != NO_ERROR)
    (void) db_value_free (val);

  return NO_ERROR;
}

/*
 * col_api_update -
 *    return:
 *    col():
 *    pos():
 *    type():
 *    ptr():
 *    size():
 */
static int
col_api_update (API_COLLECTION * col, long pos, CI_TYPE type, void *ptr,
		size_t size)
{
  COLLECTION_ *co = (COLLECTION_ *) col;
  int res;
  VALUE_AREA *va;
  DB_VALUE *val;

  assert (co != NULL);
  res =
    co->indexer->ifs->check (co->indexer, (int) pos,
			     CHECK_FOR_GET | CHECK_FOR_SET);
  if (res != NO_ERROR)
    return res;
  res =
    co->indexer->ifs->get (co->indexer, (int) pos, &va, (API_VALUE **) & val);
  if (res != NO_ERROR)
    return res;
  res = coerce_value_to_db_value (type, ptr, size, val, true);
  if (res != NO_ERROR)
    return res;
  return NO_ERROR;
}

/*
 * col_api_delete -
 *    return:
 *    col():
 *    pos():
 */
static int
col_api_delete (API_COLLECTION * col, long pos)
{
  COLLECTION_ *co = (COLLECTION_ *) col;
  int res;
  VALUE_AREA *va;
  DB_VALUE *val;

  assert (co != NULL);
  res = co->indexer->ifs->check (co->indexer, (int) pos, CHECK_FOR_DELETE);
  if (res != NO_ERROR)
    return res;
  res =
    co->indexer->ifs->delete (co->indexer, (int) pos, &va,
			      (API_VALUE **) & val);
  if (res != NO_ERROR)
    return res;
  assert (va == NULL);
  if (val)
    (void) db_value_free (val);
  return NO_ERROR;
}

/*
 * col_api_get_elem_domain_info -
 *    return:
 *    col():
 *    pos():
 *    type():
 */
static int
col_api_get_elem_domain_info (API_COLLECTION * col, long pos,
			      CI_TYPE * type, int *precision, int *scale)
{
  COLLECTION_ *co = (COLLECTION_ *) col;
  int res;
  VALUE_AREA *va;
  DB_VALUE *val;

  assert (co != NULL);
  res = co->indexer->ifs->check (co->indexer, (int) pos, CHECK_FOR_GET);
  if (res != NO_ERROR)
    return res;
  res =
    co->indexer->ifs->get (co->indexer, (int) pos, &va, (API_VALUE **) & val);
  if (res != NO_ERROR)
    return res;

  if (precision)
    {
      *precision = db_value_precision (val);
    }
  if (scale)
    {
      *scale = db_value_scale (val);
    }

  return db_type_to_type (db_value_domain_type (val), type);
}

/*
 * col_api_get_elem -
 *    return:
 *    col():
 *    pos():
 *    type():
 *    addr():
 *    len():
 *    outlen():
 *    isnull():
 */
static int
col_api_get_elem (API_COLLECTION * col, long pos, CI_TYPE type,
		  void *addr, size_t len, size_t * outlen, bool * isnull)
{
  COLLECTION_ *co = (COLLECTION_ *) col;
  int res;
  VALUE_AREA *va;
  DB_VALUE *val;

  assert (co != NULL);
  res = co->indexer->ifs->check (co->indexer, (int) pos, CHECK_FOR_GET);
  if (res != NO_ERROR)
    return res;
  res =
    co->indexer->ifs->get (co->indexer, (int) pos, &va, (API_VALUE **) & val);
  if (res != NO_ERROR)
    return res;
  assert (val != NULL);
  res =
    coerce_db_value_to_value (val, co->col.conn, type, addr, len, outlen,
			      isnull);
  return res;
}

/*
 * col_dtorf -
 *    return:
 *    va():
 *    aval():
 */
static void
col_dtorf (VALUE_AREA * va, API_VALUE * aval)
{
  DB_VALUE *val = (DB_VALUE *) aval;
  assert (va == NULL);
  if (val)
    db_value_free (val);
}

/*
 * col_api_destroy -
 *    return:
 *    col():
 */
static void
col_api_destroy (API_COLLECTION * col)
{
  COLLECTION_ *co = (COLLECTION_ *) col;
  assert (co != NULL);
  co->indexer->ifs->destroy (co->indexer, col_dtorf);
  API_FREE (co);
}

static API_COLLECTION_IFS COL_IFS_ = {
  col_api_length,
  col_api_insert,
  col_api_update,
  col_api_delete,
  col_api_get_elem_domain_info,
  col_api_get_elem,
  col_api_destroy
};

/*
 * apif_collection_create -
 *    return:
 *    conn():
 *    rc():
 */
static int
apif_collection_create (BIND_HANDLE conn, COLLECTION_ ** rc)
{
  COLLECTION_ *col;
  int res;
  assert (rc != NULL);
  col = API_MALLOC (sizeof (*col));
  if (col == NULL)
    return ER_INTERFACE_NO_MORE_MEMORY;
  col->col.ifs = &COL_IFS_;
  col->col.conn = conn;
  res = list_indexer_create (&col->indexer);
  if (res != NO_ERROR)
    {
      API_FREE (col);
    }
  *rc = col;
  return res;
}

/*
 * fill_collection -
 *    return:
 *    co():
 *    set():
 */
static int
fill_collection (COLLECTION_ * co, DB_SET * set)
{
  int set_size;
  int i, res;
  DB_VALUE *val;

  assert (co != NULL);
  if (set == NULL)
    return ER_INTERFACE_INVALID_ARGUMENT;

  set_size = db_col_size (set);
  for (i = 0; i < set_size; i++)
    {
      int li_idx = i - 1;

      res = co->indexer->ifs->check (co->indexer, li_idx, CHECK_FOR_INSERT);
      if (res != NO_ERROR)
	return res;
      val = db_value_create ();
      if (!val)
	return ER_INTERFACE_NO_MORE_MEMORY;
      res = db_col_get (set, i, val);
      if (res != NO_ERROR)
	{
	  db_value_free (val);
	  return ER_INTERFACE_GENERIC;
	}
      res =
	co->indexer->ifs->insert (co->indexer, li_idx, NULL,
				  (API_VALUE *) val);
      if (res != NO_ERROR)
	{
	  db_value_free (val);
	}
    }
  return NO_ERROR;
}

/* ------------------------------------------------------------------------- */
/* EXPORTED FUNCTION */

/*
 * api_collection_create_from_db_value -
 *    return:
 *    conn():
 *    val():
 *    rc():
 */
int
api_collection_create_from_db_value (BIND_HANDLE conn,
				     const DB_VALUE * val,
				     API_COLLECTION ** rc)
{
  DB_TYPE dt;
  COLLECTION_ *co;
  int res;

  if (val == NULL || rc == NULL)
    return ER_INTERFACE_INVALID_ARGUMENT;
  dt = db_value_domain_type (val);
  if (dt != DB_TYPE_SET && dt != DB_TYPE_MULTISET && dt != DB_TYPE_SEQUENCE)
    return ER_INTERFACE_INVALID_ARGUMENT;	/* not collection type */
  res = apif_collection_create (conn, &co);
  if (res != NO_ERROR)
    return res;

  res = fill_collection (co, db_get_set (val));
  if (res != NO_ERROR)
    co->col.ifs->destroy ((API_COLLECTION *) co);
  *rc = (API_COLLECTION *) co;
  return res;
}

/*
 * set_to_db_value_mapf -
 *    return:
 *    arg():
 *    idx():
 *    va():
 *    aval():
 */
static int
set_to_db_value_mapf (void *arg, int idx, VALUE_AREA * va, API_VALUE * aval)
{
  int res;
  DB_VALUE *val = (DB_VALUE *) aval;
  DB_COLLECTION *col = (DB_COLLECTION *) arg;

  assert (col != NULL);

  res = db_col_put (col, idx, val);
  if (res != NO_ERROR)
    {
      return ER_INTERFACE_GENERIC;
    }

  return NO_ERROR;
}

/*
 * api_collection_set_to_db_value -
 *    return:
 *    col():
 *    val():
 */
int
api_collection_set_to_db_value (API_COLLECTION * col, DB_VALUE * val)
{
  DB_TYPE dt;
  COLLECTION_ *co;
  DB_COLLECTION *dbcol;
  int size, res;

  if (col == NULL || val == NULL)
    return ER_INTERFACE_INVALID_ARGUMENT;
  co = (COLLECTION_ *) col;
  dt = db_value_domain_type (val);
  if (dt != DB_TYPE_SET && dt != DB_TYPE_MULTISET && dt != DB_TYPE_SEQUENCE)
    return ER_INTERFACE_INVALID_ARGUMENT;
  res = co->indexer->ifs->length (co->indexer, &size);
  if (res != NO_ERROR)
    return res;
  dbcol = db_col_create (dt, size, NULL);
  if (dbcol == NULL)
    return ER_INTERFACE_GENERIC;
  res = co->indexer->ifs->map (co->indexer, set_to_db_value_mapf, dbcol);
  if (res != NO_ERROR)
    {
      db_col_free (dbcol);
      return ER_INTERFACE_GENERIC;
    }
  res = db_value_put (val, DB_TYPE_C_SET, &dbcol, sizeof (DB_COLLECTION **));
  if (res != NO_ERROR)
    {
      db_col_free (dbcol);
      return ER_INTERFACE_GENERIC;
    }
  return NO_ERROR;
}

/*
 * api_collection_create -
 *    return:
 *    conn():
 *    rc():
 */
int
api_collection_create (BIND_HANDLE conn, API_COLLECTION ** rc)
{
  COLLECTION_ *co;
  int res;

  if (rc == NULL)
    return ER_INTERFACE_INVALID_ARGUMENT;

  res = apif_collection_create (conn, &co);
  if (res == NO_ERROR)
    {
      *rc = (API_COLLECTION *) co;
    }
  return res;
}
