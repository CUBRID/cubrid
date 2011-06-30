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
 * db_value_table.c -
 */

#include "config.h"
#include <string.h>
#include <assert.h>
#include "db_stub.h"
#include "api_util.h"
#include "dbi.h"
#include "db.h"

typedef struct value_bind_table_s_ VALUE_BIND_TABLE_;
struct value_bind_table_s_
{
  /* interface */
  VALUE_BIND_TABLE tbl;
  /* user supplied */
  void *impl;
  int auto_apply;
  BIND_HANDLE conn_handle;
  int (*get_index_by_name) (void *, const char *, int *);
  int (*get_db_value) (void *, int, DB_VALUE *);
  int (*set_db_value) (void *, int, DB_VALUE *);
  int (*init_domain) (void *, int, DB_VALUE *);
  /* implementation */
  VALUE_INDEXER *indexer;
};

typedef struct value_area_s_ VALUE_AREA_;
struct value_area_s_
{
  bool need_apply;
  bool val_read;
};

static int
value_to_db_value (CI_TYPE type, void *addr, size_t len,
		   DB_VALUE * val, bool domain_initialized);
static int db_value_to_value (BIND_HANDLE conn,
			      const DB_VALUE * val, CI_TYPE type,
			      void *addr, size_t len, size_t * outlen,
			      bool * isnull);
static int vbt_lazy_init_db_value (VALUE_BIND_TABLE_ * vbt, int index,
				   CHECK_PURPOSE pup, VALUE_AREA ** rva,
				   DB_VALUE ** rv);
static int vbt_api_get_value (VALUE_BIND_TABLE * tbl, int index,
			      CI_TYPE type, void *addr, size_t len,
			      size_t * outlen, bool * isnull);
static int vbt_api_set_value (VALUE_BIND_TABLE * tbl, int index,
			      CI_TYPE type, void *addr, size_t len);
static int vbt_api_get_value_by_name (VALUE_BIND_TABLE * tbl,
				      const char *name, CI_TYPE type,
				      void *addr, size_t len, size_t * outlen,
				      bool * isnull);
static int vbt_api_set_value_by_name (VALUE_BIND_TABLE * tbl,
				      const char *name, CI_TYPE type,
				      void *addr, size_t len);
static int vbt_apply_updatesf_map (void *arg, int index, VALUE_AREA * va,
				   API_VALUE * val);
static int vbt_api_apply_updates (VALUE_BIND_TABLE * tbl);
static int vbt_resetf_map (void *arg, int index, VALUE_AREA * va,
			   API_VALUE * val);
static int vbt_api_reset (VALUE_BIND_TABLE * tbl);
static void vbt_dtor (VALUE_AREA * v, API_VALUE * aval);
static void vbt_api_destroy (VALUE_BIND_TABLE * table);

/*
 * value_to_db_value -
 *    return:
 *    type():
 *    addr():
 *    len():
 *    dbval():
 *    domain_initalized():
 */
static int
value_to_db_value (CI_TYPE type, void *addr, size_t len,
		   DB_VALUE * dbval, bool domain_initalized)
{
  DB_VALUE val_s;
  DB_VALUE *val = &val_s;
  int res = NO_ERROR;

  db_make_null (val);
  if (!domain_initalized)
    {
      int precision, scale;

      DB_DOMAIN *dom;
      DB_TYPE dt;

      res = type_to_db_type (type, &dt);
      if (res != NO_ERROR)
	return res;
      dom = db_type_to_db_domain (dt);

      precision = scale = 0;
      if (dom != NULL)
	{
	  precision = db_domain_precision (dom);
	  scale = db_domain_scale (dom);
	}

      res = db_value_domain_init (val, dt, precision, scale);

      if (res != NO_ERROR)
	goto res_return;
    }
  else
    {
      val->domain = dbval->domain;
    }

  if (type == CI_TYPE_NULL || addr == NULL)
    {
      db_make_null (val);
      goto res_return;
    }

  switch (type)
    {
    case CI_TYPE_BIGINT:
      {
	if (len < sizeof (INT64))
	  return ER_INTERFACE_INVALID_ARGUMENT;
	res = db_value_put (val, DB_TYPE_C_BIGINT, addr, len);
	break;
      }
    case CI_TYPE_INT:
      {
	if (len < sizeof (int))
	  return ER_INTERFACE_INVALID_ARGUMENT;
	res = db_value_put (val, DB_TYPE_C_INT, addr, len);
	break;
      }
    case CI_TYPE_SHORT:
      {
	if (len < sizeof (short))
	  return ER_INTERFACE_INVALID_ARGUMENT;
	res = db_value_put (val, DB_TYPE_C_SHORT, addr, len);
	break;
      }
    case CI_TYPE_FLOAT:
      {
	if (len < sizeof (float))
	  return ER_INTERFACE_INVALID_ARGUMENT;
	res = db_value_put (val, DB_TYPE_C_FLOAT, addr, len);
	break;
      }
    case CI_TYPE_DOUBLE:
      {
	if (len < sizeof (double))
	  return ER_INTERFACE_INVALID_ARGUMENT;
	res = db_value_put (val, DB_TYPE_C_DOUBLE, addr, len);
	break;
      }
    case CI_TYPE_CHAR:
    case CI_TYPE_VARCHAR:
    case CI_TYPE_NCHAR:
    case CI_TYPE_VARNCHAR:
      {
	if (len <= 0)
	  return ER_INTERFACE_INVALID_ARGUMENT;
	if (type == CI_TYPE_CHAR)
	  res = db_value_put (val, DB_TYPE_C_CHAR, addr, len);
	else if (type == CI_TYPE_VARCHAR)
	  res = db_value_put (val, DB_TYPE_C_VARCHAR, addr, len);
	else if (type == CI_TYPE_NCHAR)
	  res = db_value_put (val, DB_TYPE_C_NCHAR, addr, len);
	else if (type == CI_TYPE_VARNCHAR)
	  res = db_value_put (val, DB_TYPE_C_VARNCHAR, addr, len);
	break;
      }
    case CI_TYPE_BIT:
    case CI_TYPE_VARBIT:
      {
	if (len <= 0)
	  return ER_INTERFACE_INVALID_ARGUMENT;
	if (type == CI_TYPE_BIT)
	  res = db_value_put (val, DB_TYPE_C_BIT, addr, len);
	else if (type == CI_TYPE_VARBIT)
	  res = db_value_put (val, DB_TYPE_C_VARBIT, addr, len);
	break;
      }
    case CI_TYPE_TIME:
      {
	CI_TIME *xtime;
	DB_C_TIME time;

	if (len < sizeof (*xtime))
	  return ER_INTERFACE_INVALID_ARGUMENT;
	xtime = (CI_TIME *) addr;
	time.hour = xtime->hour;
	time.minute = xtime->minute;
	time.second = xtime->second;
	res = db_value_put (val, DB_TYPE_C_TIME, &time, sizeof (time));
	break;
      }
    case CI_TYPE_DATE:
      {
	CI_TIME *xtime;
	DB_C_DATE date;

	if (len < sizeof (*xtime))
	  return ER_INTERFACE_INVALID_ARGUMENT;
	xtime = (CI_TIME *) addr;
	date.year = xtime->year;
	date.month = xtime->month;
	date.day = xtime->day;
	res = db_value_put (val, DB_TYPE_C_DATE, &date, sizeof (date));
	break;
      }
    case CI_TYPE_TIMESTAMP:
      {
	CI_TIME *xtime;
	DB_VALUE d, t;
	DB_TIMESTAMP ts;

	if (len < sizeof (*xtime))
	  return ER_INTERFACE_INVALID_ARGUMENT;

	xtime = (CI_TIME *) addr;
	(void) db_make_null (&d);
	(void) db_make_null (&t);
	res = db_make_date (&d, xtime->month, xtime->day, xtime->year);
	if (res != NO_ERROR)
	  break;
	res = db_make_time (&t, xtime->hour, xtime->minute, xtime->second);
	if (res != NO_ERROR)
	  {
	    (void) db_value_clear (&d);
	    break;
	  }
	res = db_timestamp_encode (&ts, db_get_date (&d), db_get_time (&t));
	if (res == NO_ERROR)
	  res = db_value_put (val, DB_TYPE_C_TIMESTAMP, &ts, sizeof (ts));
	(void) db_value_clear (&d);
	(void) db_value_clear (&t);
	break;
      }
    case CI_TYPE_DATETIME:
      {
	CI_TIME *xtime;
	DB_VALUE d, t;
	DB_DATETIME dt;

	if (len < sizeof (*xtime))
	  return ER_INTERFACE_INVALID_ARGUMENT;

	xtime = (CI_TIME *) addr;
	res = db_datetime_encode (&dt, xtime->month, xtime->day, xtime->year,
				  xtime->hour, xtime->minute, xtime->second,
				  xtime->millisecond);
	if (res == NO_ERROR)
	  res = db_value_put (val, DB_TYPE_C_DATETIME, &dt, sizeof (dt));
	break;
      }
    case CI_TYPE_MONETARY:
      {
	if (len < sizeof (double))
	  return ER_INTERFACE_INVALID_ARGUMENT;
	db_value_put (val, DB_TYPE_C_MONETARY, addr, len);
	res = db_make_monetary (val, DB_CURRENCY_DEFAULT, *(double *) addr);
	break;
      }
    case CI_TYPE_NUMERIC:
      {
	res = db_value_put (val, DB_TYPE_C_CHAR, addr, len);
	break;
      }
    case CI_TYPE_OID:
      {
	CI_OID *xoid;
	DB_OBJECT *obj;
	OID oid;
	if (len < sizeof (CI_OID))
	  return ER_INTERFACE_INVALID_ARGUMENT;
	xoid = (CI_OID *) addr;
	xoid2oid (xoid, &oid);
#if 1
	obj = db_object (&oid);
	if (obj == NULL || obj->lock == NULL_LOCK)
	  return ER_INTERFACE_GENERIC;	/* no such object */
	res =
	  db_value_put (val, DB_TYPE_C_OBJECT, &obj, sizeof (DB_OBJECT **));
#else
	res = db_make_oid (val, &oid);
#endif
	break;
      }
    case CI_TYPE_COLLECTION:
      {
	CI_COLLECTION *col;
	API_COLLECTION *col_;
	if (len < sizeof (CI_COLLECTION *))
	  return ER_INTERFACE_INVALID_ARGUMENT;
	col = (CI_COLLECTION *) addr;
	col_ = (API_COLLECTION *) * col;
	res = api_collection_set_to_db_value (col_, val);
	if (res == NO_ERROR)
	  res = NO_ERROR;
	break;
      }
    default:
      {
	api_er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		    ER_INTERFACE_INVALID_ARGUMENT, 0);
	return ER_INTERFACE_INVALID_ARGUMENT;
      }
    }

res_return:
  if (res != NO_ERROR)
    return ER_INTERFACE_GENERIC;
  else
    res = pr_clone_value (val, dbval);

  (void) db_value_clear (val);

  if (res == NO_ERROR)
    return NO_ERROR;
  return ER_INTERFACE_GENERIC;
}

/*
 * db_value_to_value -
 *    return:
 *    conn():
 *    val():
 *    type():
 *    addr():
 *    len():
 *    out_len():
 *    is_null():
 */
static int
db_value_to_value (BIND_HANDLE conn, const DB_VALUE * val,
		   CI_TYPE type, void *addr, size_t len, size_t * out_len,
		   bool * is_null)
{
  DB_TYPE dbtype;
  int buflen, xflen, outlen;
  int res;

  assert (val != NULL);
  assert (out_len != NULL);
  assert (is_null != NULL);

  dbtype = db_value_type (val);
  if (dbtype == DB_TYPE_NULL)
    {
      *is_null = true;
      return NO_ERROR;
    }

  buflen = (int) len;
  outlen = 0;

  switch (type)
    {
    case CI_TYPE_INT:
      {
	if (len < sizeof (int))
	  return ER_INTERFACE_INVALID_ARGUMENT;

	res = db_value_get ((DB_VALUE *) val, DB_TYPE_C_INT, addr, buflen,
			    &xflen, &outlen);
	break;
      }
    case CI_TYPE_SHORT:
      {
	if (len < sizeof (short))
	  return ER_INTERFACE_INVALID_ARGUMENT;

	res = db_value_get ((DB_VALUE *) val, DB_TYPE_C_SHORT, addr, buflen,
			    &xflen, &outlen);
	break;
      }
    case CI_TYPE_FLOAT:
      {
	if (len < sizeof (float))
	  return ER_INTERFACE_INVALID_ARGUMENT;

	res = db_value_get ((DB_VALUE *) val, DB_TYPE_C_FLOAT, addr, buflen,
			    &xflen, &outlen);
	break;
      }
    case CI_TYPE_DOUBLE:
      {
	if (len < sizeof (double))
	  return ER_INTERFACE_INVALID_ARGUMENT;

	res = db_value_get ((DB_VALUE *) val, DB_TYPE_C_DOUBLE, addr, buflen,
			    &xflen, &outlen);
	break;
      }
    case CI_TYPE_CHAR:
      {
	res = db_value_get ((DB_VALUE *) val, DB_TYPE_C_CHAR, addr, buflen,
			    &xflen, &outlen);
	break;
      }
    case CI_TYPE_VARCHAR:
      {
	res = db_value_get ((DB_VALUE *) val, DB_TYPE_C_VARCHAR, addr, buflen,
			    &xflen, &outlen);
	break;
      }
    case CI_TYPE_NCHAR:
      {
	res = db_value_get ((DB_VALUE *) val, DB_TYPE_C_NCHAR, addr, buflen,
			    &xflen, &outlen);
	break;
      }
    case CI_TYPE_VARNCHAR:
      {
	res = db_value_get ((DB_VALUE *) val, DB_TYPE_C_VARNCHAR, addr,
			    buflen, &xflen, &outlen);
	break;
      }
    case CI_TYPE_BIT:
      {
	res = db_value_get ((DB_VALUE *) val, DB_TYPE_C_BIT, addr, buflen,
			    &xflen, &outlen);
	break;
      }
    case CI_TYPE_VARBIT:
      {
	res = db_value_get ((DB_VALUE *) val, DB_TYPE_C_VARBIT, addr, buflen,
			    &xflen, &outlen);
	break;
      }
    case CI_TYPE_TIME:
      {
	CI_TIME *xtime = (CI_TIME *) addr;
	DB_TIME t;
	int h, m, s;

	if (len < sizeof (*xtime))
	  return ER_INTERFACE_INVALID_ARGUMENT;

	res = db_value_get ((DB_VALUE *) val, DB_TYPE_C_TIME, &t, sizeof (t),
			    &xflen, &outlen);
	if (res != NO_ERROR)
	  return ER_INTERFACE_GENERIC;

	db_time_decode (&t, &h, &m, &s);
	xtime->hour = h;
	xtime->minute = m;
	xtime->second = s;
	xflen = sizeof (*xtime);
	break;
      }
    case CI_TYPE_DATE:
      {
	CI_TIME *xtime = (CI_TIME *) addr;
	DB_DATE d;
	int M, D, Y;

	if (len < sizeof (*xtime))
	  return ER_INTERFACE_INVALID_ARGUMENT;

	res = db_value_get ((DB_VALUE *) val, DB_TYPE_C_DATE, &d, sizeof (d),
			    &xflen, &outlen);
	if (res != NO_ERROR)
	  return ER_INTERFACE_GENERIC;

	db_date_decode (&d, &M, &D, &Y);
	xtime->year = Y;
	xtime->month = M;
	xtime->day = D;
	xflen = sizeof (*xtime);
	break;
      }
    case CI_TYPE_TIMESTAMP:
      {
	CI_TIME *xtime = (CI_TIME *) addr;
	DB_TIMESTAMP ts;
	DB_DATE date;
	DB_TIME time;
	int D, M, Y, h, m, s;

	if (len < sizeof (*xtime))
	  return ER_INTERFACE_INVALID_ARGUMENT;

	res = db_value_get ((DB_VALUE *) val, DB_TYPE_C_TIMESTAMP, &ts,
			    sizeof (ts), &xflen, &outlen);
	if (res != NO_ERROR)
	  return ER_INTERFACE_GENERIC;

	db_timestamp_decode ((DB_TIMESTAMP *) & ts, &date, &time);
	db_date_decode (&date, &M, &D, &Y);
	db_time_decode (&time, &h, &m, &s);

	xtime->year = Y;
	xtime->month = M;
	xtime->day = D;
	xtime->hour = h;
	xtime->minute = m;
	xtime->second = s;
	xflen = sizeof (*xtime);
	break;
      }
    case CI_TYPE_MONETARY:
      {
	if (len < sizeof (double))
	  return ER_INTERFACE_INVALID_ARGUMENT;

	res = db_value_get ((DB_VALUE *) val, DB_TYPE_C_MONETARY, addr,
			    buflen, &xflen, &outlen);
	break;
      }
    case CI_TYPE_NUMERIC:
      {
	if (dbtype == DB_TYPE_INTEGER || dbtype == DB_TYPE_SHORT
	    || dbtype == DB_TYPE_BIGINT || dbtype == DB_TYPE_FLOAT
	    || dbtype == DB_TYPE_DOUBLE || dbtype == DB_TYPE_MONETARY
	    || dbtype == DB_TYPE_NUMERIC)
	  {
	    res = db_value_get ((DB_VALUE *) val, DB_TYPE_C_CHAR, addr,
				buflen, &xflen, &outlen);
	  }
	else if (dbtype == DB_TYPE_CHAR || dbtype == DB_TYPE_VARCHAR
		 || dbtype == DB_TYPE_NCHAR || dbtype == DB_TYPE_VARNCHAR)
	  {
	    DB_VALUE nval;
	    char *nstr;

	    /* need check if the string is numeric string */
	    db_make_null (&nval);
	    nstr = db_get_string (val);

	    assert (nstr != NULL);

	    res = numeric_coerce_string_to_num (nstr, &nval);
	    (void) db_value_clear (&nval);
	    if (res != NO_ERROR)
	      return ER_INTERFACE_GENERIC;

	    res = db_value_get ((DB_VALUE *) val, DB_TYPE_C_CHAR, addr,
				buflen, &xflen, &outlen);
	  }
	else
	  {
	    return ER_INTERFACE_NOT_SUPPORTED_OPERATION;
	  }
	break;
      }
    case CI_TYPE_OID:
      {
	CI_OID *xoid = (CI_OID *) addr;
	DB_TYPE dt;
	OID *oid;

	if (len < sizeof (CI_OID))
	  return ER_INTERFACE_INVALID_ARGUMENT;

	dt = db_value_domain_type (val);
	oid = NULL;

	if (dt == DB_TYPE_OBJECT)
	  {
	    DB_OBJECT *object = db_get_object (val);
	    if (object == NULL)
	      {
		*is_null = true;
		return NO_ERROR;
	      }
	    oid = db_identifier (object);
	  }
	else if (dt == DB_TYPE_OID)
	  {
	    oid = db_get_oid (val);
	  }
	if (oid == NULL)
	  {
	    *is_null = true;
	    return NO_ERROR;
	  }

	oid2xoid (oid, conn, xoid);
	xflen = sizeof (*xoid);
	res = NO_ERROR;
	break;
      }
    case CI_TYPE_COLLECTION:
      {
	CI_COLLECTION *col;
	API_COLLECTION *col_;

	if (len < sizeof (CI_COLLECTION *))
	  return ER_INTERFACE_INVALID_ARGUMENT;

	col = (CI_COLLECTION *) addr;
	res = api_collection_create_from_db_value (conn, val, &col_);
	if (res == NO_ERROR)
	  {
	    *col = col_;
	    xflen = sizeof (API_COLLECTION *);
	    res = NO_ERROR;
	  }
	else
	  {
	    res = ER_GENERIC_ERROR;
	  }
	break;
      }
    default:
      {
	return ER_INTERFACE_INVALID_ARGUMENT;
      }
    }

  if (res == NO_ERROR)
    {
      *out_len = ((outlen == 0) ? xflen : outlen);
      *is_null = false;
      return NO_ERROR;
    }

  return ER_INTERFACE_GENERIC;
}


/*
 * vbt_lazy_init_db_value -
 *    return:
 *    vbt():
 *    index():
 *    pup():
 *    rva():
 *    rv():
 */
static int
vbt_lazy_init_db_value (VALUE_BIND_TABLE_ * vbt, int index, CHECK_PURPOSE pup,
			VALUE_AREA ** rva, DB_VALUE ** rv)
{
  int res;
  VALUE_AREA_ *va;
  DB_VALUE *val;

  assert (vbt != NULL);
  assert (rva != NULL);
  assert (rv != NULL);

  res = vbt->indexer->ifs->check (vbt->indexer, index, pup);
  if (res != NO_ERROR)
    return res;

  res = vbt->indexer->ifs->get (vbt->indexer, index, (VALUE_AREA **) (&va),
				(API_VALUE **) (&val));
  if (res != NO_ERROR)
    return res;

  if (val == NULL)
    {
      assert (va == NULL);
      val = db_value_create ();
      if (val == NULL)
	return ER_INTERFACE_NO_MORE_MEMORY;
      res = vbt->init_domain (vbt->impl, index, val);
      if (res != NO_ERROR)
	{
	  (void) db_value_free (val);
	  return res;
	}
      va = API_CALLOC (1, sizeof (*va));
      if (va == NULL)
	{
	  (void) db_value_free (val);
	  return ER_INTERFACE_NO_MORE_MEMORY;
	}
      res =
	vbt->indexer->ifs->set (vbt->indexer, index, (VALUE_AREA *) va,
				(API_VALUE *) val);
      if (res != NO_ERROR)
	{
	  (void) db_value_free (val);
	  API_FREE (va);
	  return res;
	}
    }
  *rva = (VALUE_AREA *) va;
  *rv = val;
  return NO_ERROR;
}

/*
 * vbt_api_get_value -
 *    return:
 *    tbl():
 *    index():
 *    type():
 *    addr():
 *    len():
 *    outlen():
 *    isnull():
 */
static int
vbt_api_get_value (VALUE_BIND_TABLE * tbl, int index, CI_TYPE type,
		   void *addr, size_t len, size_t * outlen, bool * isnull)
{
  VALUE_BIND_TABLE_ *vbt;
  VALUE_AREA_ *va;
  DB_VALUE *val;
  int res;

  assert (tbl != NULL);
  assert (outlen != NULL);
  assert (isnull != NULL);

  vbt = (VALUE_BIND_TABLE_ *) tbl;

  res =
    vbt_lazy_init_db_value (vbt, index, CHECK_FOR_GET, (VALUE_AREA **) & va,
			    &val);
  if (res != NO_ERROR)
    return res;
  assert (val != NULL);
  assert (va != NULL);
  if (!va->val_read)
    {
      res = vbt->get_db_value (vbt->impl, index, val);
      if (res != NO_ERROR)
	return res;
      va->val_read = true;
    }
  return db_value_to_value (vbt->conn_handle, val, type,
			    addr, len, outlen, isnull);
}

/*
 * vbt_api_set_value -
 *    return:
 *    tbl():
 *    index():
 *    type():
 *    addr():
 *    len():
 */
static int
vbt_api_set_value (VALUE_BIND_TABLE * tbl, int index, CI_TYPE type,
		   void *addr, size_t len)
{
  VALUE_BIND_TABLE_ *vbt;
  VALUE_AREA_ *va;
  DB_VALUE *val;
  int res;

  assert (tbl != NULL);
  vbt = (VALUE_BIND_TABLE_ *) tbl;

  res =
    vbt_lazy_init_db_value (vbt, index, CHECK_FOR_SET, (VALUE_AREA **) & va,
			    &val);
  if (res != NO_ERROR)
    return res;

  assert (val != NULL);
  assert (va != NULL);
  res = value_to_db_value (type, addr, len, val, true);

  if (res != NO_ERROR)
    return res;

  if (vbt->auto_apply)
    {
      res = vbt->set_db_value (vbt->impl, index, val);
      if (res != NO_ERROR)
	return res;
      va->need_apply = false;
    }
  else
    {
      va->need_apply = true;
    }
  return res;
}

/*
 * vbt_api_get_value_by_name -
 *    return:
 *    tbl():
 *    name():
 *    type():
 *    addr():
 *    len():
 *    outlen():
 *    isnull():
 */
static int
vbt_api_get_value_by_name (VALUE_BIND_TABLE * tbl, const char *name,
			   CI_TYPE type, void *addr, size_t len,
			   size_t * outlen, bool * isnull)
{
  VALUE_BIND_TABLE_ *vbt;
  int idx;
  int res;
  vbt = (VALUE_BIND_TABLE_ *) tbl;
  assert (vbt != NULL);
  res = vbt->get_index_by_name (vbt->impl, name, &idx);
  if (res != NO_ERROR)
    return res;
  return vbt->tbl.ifs->get_value (tbl, idx, type, addr, len, outlen, isnull);
}

/*
 * vbt_api_set_value_by_name -
 *    return:
 *    tbl():
 *    name():
 *    type():
 *    addr():
 *    len():
 */
static int
vbt_api_set_value_by_name (VALUE_BIND_TABLE * tbl, const char *name,
			   CI_TYPE type, void *addr, size_t len)
{
  VALUE_BIND_TABLE_ *vbt;
  int idx;
  int res;
  vbt = (VALUE_BIND_TABLE_ *) tbl;
  assert (vbt != NULL);
  res = vbt->get_index_by_name (vbt->impl, name, &idx);
  if (res != NO_ERROR)
    return res;
  return vbt->tbl.ifs->set_value (tbl, idx, type, addr, len);
}


/*
 * vbt_apply_updatesf_map -
 *    return:
 *    arg():
 *    index():
 *    v():
 *    aval():
 */
static int
vbt_apply_updatesf_map (void *arg, int index, VALUE_AREA * v,
			API_VALUE * aval)
{
  VALUE_BIND_TABLE_ *vbt;
  VALUE_AREA_ *va;
  DB_VALUE *val = (DB_VALUE *) aval;
  int res;

  vbt = (VALUE_BIND_TABLE_ *) arg;
  assert (vbt != NULL);
  if (val == NULL)
    return NO_ERROR;
  va = (VALUE_AREA_ *) v;
  assert (va != NULL);
  if (va->need_apply)
    {
      res = vbt->set_db_value (vbt->impl, index, val);
      if (res != NO_ERROR)
	return res;
      va->need_apply = false;
    }
  return NO_ERROR;
}

/*
 * vbt_api_apply_updates -
 *    return:
 *    tbl():
 */
static int
vbt_api_apply_updates (VALUE_BIND_TABLE * tbl)
{
  VALUE_BIND_TABLE_ *vbt;
  vbt = (VALUE_BIND_TABLE_ *) tbl;
  assert (vbt != NULL);

  if (vbt->auto_apply)
    return NO_ERROR;

  return vbt->indexer->ifs->map (vbt->indexer, vbt_apply_updatesf_map, vbt);
}

/*
 * vbt_resetf_map -
 *    return:
 *    arg():
 *    index():
 *    v():
 *    aval():
 */
static int
vbt_resetf_map (void *arg, int index, VALUE_AREA * v, API_VALUE * aval)
{
  VALUE_BIND_TABLE_ *vbt;
  VALUE_AREA_ *va;
  DB_VALUE *val = (DB_VALUE *) aval;
  int res;

  vbt = (VALUE_BIND_TABLE_ *) arg;


  assert (vbt != NULL);
  if (val == NULL)
    return NO_ERROR;
  va = (VALUE_AREA_ *) v;
  assert (va != NULL);
  /* caution : do not call delete api */
  res = vbt->indexer->ifs->set (vbt->indexer, index, NULL, NULL);
  if (res != NO_ERROR)
    return res;
  if (va)
    API_FREE (va);
  (void) db_value_free (val);
  return NO_ERROR;
}

/*
 * vbt_api_reset -
 *    return:
 *    tbl():
 */
static int
vbt_api_reset (VALUE_BIND_TABLE * tbl)
{
  VALUE_BIND_TABLE_ *vbt;
  int res;

  vbt = (VALUE_BIND_TABLE_ *) tbl;
  assert (vbt != NULL);

  res = vbt->indexer->ifs->map (vbt->indexer, vbt_resetf_map, vbt);
  return res;
}

/*
 * vbt_dtor -
 *    return:
 *    v():
 *    aval():
 */
static void
vbt_dtor (VALUE_AREA * v, API_VALUE * aval)
{
  VALUE_AREA_ *va = (VALUE_AREA_ *) v;
  DB_VALUE *val = (DB_VALUE *) aval;
  if (va)
    API_FREE (va);
  if (val)
    (void) db_value_free (val);
}

/*
 * vbt_api_destroy -
 *    return:
 *    table():
 */
static void
vbt_api_destroy (VALUE_BIND_TABLE * table)
{
  VALUE_BIND_TABLE_ *vbt = (VALUE_BIND_TABLE_ *) table;
  assert (table != NULL);
  vbt->indexer->ifs->destroy (vbt->indexer, vbt_dtor);
  API_FREE (vbt);
}

static VALUE_BIND_TABLE_IFS VB_IFS_ = {
  vbt_api_get_value,
  vbt_api_set_value,
  vbt_api_get_value_by_name,
  vbt_api_set_value_by_name,
  vbt_api_apply_updates,
  vbt_api_reset,
  vbt_api_destroy
};

/* ------------------------------------------------------------------------- */
/* VALUE_BIND_TABLE interface implementation */

/*
 * db_type_to_type -
 *    return:
 *    dt():
 *    xt():
 */
int
db_type_to_type (DB_TYPE dt, CI_TYPE * xt)
{
  if (xt == NULL)
    return ER_INTERFACE_INVALID_ARGUMENT;

  switch (dt)
    {
    case DB_TYPE_INTEGER:
      *xt = CI_TYPE_INT;
      break;
    case DB_TYPE_BIGINT:
      *xt = CI_TYPE_BIGINT;
      break;
    case DB_TYPE_FLOAT:
      *xt = CI_TYPE_FLOAT;
      break;
    case DB_TYPE_DOUBLE:
      *xt = CI_TYPE_DOUBLE;
      break;
    case DB_TYPE_STRING:
      *xt = CI_TYPE_VARCHAR;
      break;
    case DB_TYPE_OBJECT:
    case DB_TYPE_ELO:
    case DB_TYPE_BLOB:
    case DB_TYPE_CLOB:
    case DB_TYPE_OID:
      *xt = CI_TYPE_OID;
      break;
    case DB_TYPE_SET:
    case DB_TYPE_MULTISET:
    case DB_TYPE_SEQUENCE:
      *xt = CI_TYPE_COLLECTION;
      break;
    case DB_TYPE_TIME:
      *xt = CI_TYPE_TIME;
      break;
    case DB_TYPE_TIMESTAMP:
      *xt = CI_TYPE_TIMESTAMP;
      break;
    case DB_TYPE_DATETIME:
      *xt = CI_TYPE_DATETIME;
      break;
    case DB_TYPE_DATE:
      *xt = CI_TYPE_DATE;
      break;
    case DB_TYPE_MONETARY:
      *xt = CI_TYPE_MONETARY;
      break;
    case DB_TYPE_SHORT:
      *xt = CI_TYPE_SHORT;
      break;
    case DB_TYPE_NUMERIC:
      *xt = CI_TYPE_NUMERIC;
      break;
    case DB_TYPE_BIT:
      *xt = CI_TYPE_BIT;
      break;
    case DB_TYPE_VARBIT:
      *xt = CI_TYPE_VARBIT;
      break;
    case DB_TYPE_CHAR:
      *xt = CI_TYPE_CHAR;
      break;
    case DB_TYPE_NCHAR:
      *xt = CI_TYPE_NCHAR;
      break;
    case DB_TYPE_VARNCHAR:
      *xt = CI_TYPE_VARNCHAR;
      break;
    case DB_TYPE_NULL:
      *xt = CI_TYPE_NULL;
      break;
    default:
      return ER_INTERFACE_GENERIC;	/* unsupported db type */
    }
  return NO_ERROR;
}

/*
 * type_to_db_type -
 *    return:
 *    xt():
 *    dt():
 */
int
type_to_db_type (CI_TYPE xt, DB_TYPE * dt)
{
  if (dt == NULL)
    return ER_INTERFACE_INVALID_ARGUMENT;
  switch (xt)
    {
    case CI_TYPE_NULL:
      *dt = DB_TYPE_NULL;
      break;
    case CI_TYPE_INT:
      *dt = DB_TYPE_INTEGER;
      break;
    case CI_TYPE_SHORT:
      *dt = DB_TYPE_SHORT;
      break;
    case CI_TYPE_BIGINT:
      *dt = DB_TYPE_BIGINT;
      break;
    case CI_TYPE_FLOAT:
      *dt = DB_TYPE_FLOAT;
      break;
    case CI_TYPE_DOUBLE:
      *dt = DB_TYPE_DOUBLE;
      break;
    case CI_TYPE_CHAR:
      *dt = DB_TYPE_CHAR;
      break;
    case CI_TYPE_VARCHAR:
      *dt = DB_TYPE_VARCHAR;
      break;
    case CI_TYPE_NCHAR:
      *dt = DB_TYPE_NCHAR;
      break;
    case CI_TYPE_BIT:
      *dt = DB_TYPE_BIT;
      break;
    case CI_TYPE_VARBIT:
      *dt = DB_TYPE_VARBIT;
      break;
    case CI_TYPE_TIME:
      *dt = DB_TYPE_TIME;
      break;
    case CI_TYPE_DATE:
      *dt = DB_TYPE_DATE;
      break;
    case CI_TYPE_TIMESTAMP:
      *dt = DB_TYPE_TIMESTAMP;
      break;
    case CI_TYPE_DATETIME:
      *dt = DB_TYPE_DATETIME;
      break;
    case CI_TYPE_MONETARY:
      *dt = DB_TYPE_MONETARY;
      break;
    case CI_TYPE_NUMERIC:
      *dt = DB_TYPE_NUMERIC;
      break;
    case CI_TYPE_OID:
      *dt = DB_TYPE_OBJECT;
      break;
    case CI_TYPE_COLLECTION:
      *dt = DB_TYPE_SEQUENCE;
      break;
    default:
      return ER_INTERFACE_GENERIC;
    }
  return NO_ERROR;
}

/*
 * xoid2oid -
 *    return:
 *    xoid():
 *    oid():
 */
void
xoid2oid (CI_OID * xoid, OID * oid)
{
  assert (xoid != NULL);
  assert (oid != NULL);
  oid->pageid = xoid->d1;
  oid->slotid = (xoid->d2 >> 16) & 0xffff;
  oid->volid = xoid->d2 & 0xffff;
}

/*
 * oid2xoid -
 *    return:
 *    oid():
 *    conn():
 *    xoid():
 */
void
oid2xoid (OID * oid, BIND_HANDLE conn, CI_OID * xoid)
{
  xoid->d1 = oid->pageid;
  xoid->d2 = ((oid->slotid << 16) & 0xffff0000) | oid->volid;
  xoid->conn = conn;
}

/*
 * coerce_value_to_db_value -
 *    return:
 *    type():
 *    addr():
 *    len():
 *    dbval():
 *    domain_initialized():
 */
extern int
coerce_value_to_db_value (CI_TYPE type, void *addr, size_t len,
			  DB_VALUE * dbval, bool domain_initialized)
{
  if (dbval == NULL || (type != CI_TYPE_NULL && (addr == NULL || len <= 0)))
    {
      return ER_INTERFACE_INVALID_ARGUMENT;
    }

  return value_to_db_value (type, addr, len, dbval, domain_initialized);
}

/*
 * coerce_db_value_to_value -
 *    return:
 *    dbval():
 *    conn():
 *    type():
 *    addr():
 *    len():
 *    outlen():
 *    isnull():
 */
extern int
coerce_db_value_to_value (const DB_VALUE * dbval,
			  BIND_HANDLE conn, CI_TYPE type, void *addr,
			  size_t len, size_t * outlen, bool * isnull)
{
  if (dbval == NULL || addr == NULL || len <= 0 || outlen == NULL
      || isnull == NULL)
    {
      return ER_INTERFACE_INVALID_ARGUMENT;
    }

  return db_value_to_value (conn, dbval, type, addr, len, outlen, isnull);
}

/*
 * create_db_value_bind_table -
 *    return:
 *    nvalue():
 *    impl():
 *    auto_apply():
 *    conn_handle():
 *    get_index_by_name():
 *    )():
 *    get_db_value():
 *    )():
 *    set_db_value():
 *    )():
 *    init_domain():
 *    )():
 *    rtable():
 */
int
create_db_value_bind_table (int nvalue, void *impl, int auto_apply,
			    BIND_HANDLE conn_handle,
			    int (*get_index_by_name) (void *,
						      const char
						      *, int *),
			    int (*get_db_value) (void *,
						 int,
						 DB_VALUE *),
			    int (*set_db_value) (void *,
						 int,
						 DB_VALUE *),
			    int (*init_domain) (void *,
						int,
						DB_VALUE *),
			    VALUE_BIND_TABLE ** rtable)
{
  VALUE_BIND_TABLE_ *vbt;
  int res;

  assert (nvalue > 0);

  vbt = API_MALLOC (sizeof (*vbt));
  if (vbt == NULL)
    return ER_INTERFACE_NO_MORE_MEMORY;

  res = array_indexer_create (nvalue, &vbt->indexer);
  if (res != NO_ERROR)
    {
      API_FREE (vbt);
      return res;
    }

  vbt->tbl.ifs = &VB_IFS_;
  vbt->impl = impl;
  vbt->auto_apply = auto_apply;
  vbt->conn_handle = conn_handle;
  vbt->get_index_by_name = get_index_by_name;
  vbt->get_db_value = get_db_value;
  vbt->set_db_value = set_db_value;
  vbt->init_domain = init_domain;
  *rtable = (VALUE_BIND_TABLE *) vbt;
  return NO_ERROR;
}
