/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution. 
 *
 * Redistribution and use in source and binary forms, with or without modification, 
 * are permitted provided that the following conditions are met: 
 *
 * - Redistributions of source code must retain the above copyright notice, 
 *   this list of conditions and the following disclaimer. 
 *
 * - Redistributions in binary form must reproduce the above copyright notice, 
 *   this list of conditions and the following disclaimer in the documentation 
 *   and/or other materials provided with the distribution. 
 *
 * - Neither the name of the <ORGANIZATION> nor the names of its contributors 
 *   may be used to endorse or promote products derived from this software without 
 *   specific prior written permission. 
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND 
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED 
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. 
 * IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, 
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, 
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, 
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, 
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) 
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY 
 * OF SUCH DAMAGE. 
 *
 */

#include "cubrid.h"

extern PyObject *handle_error(int e, T_CCI_ERROR *error);

static PyObject* 
cursor_object_new(PyTypeObject *type, PyObject *args, PyObject *kwargs)
{
  Cursor_Object *self;

  self = (Cursor_Object*) type->tp_alloc(type, 0);
  if (!self) {
      return NULL;
  }

  return (PyObject*) self;
}

static int
cursor_object_init(Cursor_Object *self, PyObject *args, PyObject *kwargs)
{
  Connection_Object *conn;

  if (!PyArg_ParseTuple(args, "O!", &connection_object_type, &conn)) {
    return -1;
  }

  Py_INCREF(conn);

  self->handle = 0;
  self->connection = conn->handle;
  Py_INCREF(Py_None);
  self->description = Py_None;
  self->rowCount = -1;
  self->last_stmt = NULL;

  return 0;
}

static void
cursor_object_reset(Cursor_Object *self)
{
  int res;

  if (self->handle) {
    res = cci_close_req_handle(self->handle);
    self->handle = 0;

    if (self->description) {
      Py_DECREF(self->description);
      self->description = NULL;
    }

    if (self->last_stmt) {
      PyMem_Free(self->last_stmt);
      self->last_stmt = NULL;
    }
  }
}

static PyObject *
cursor_object_close(Cursor_Object *self, PyObject *args)
{
  cursor_object_reset(self);
  Py_INCREF(Py_None);
  return Py_None;
}

static int
cursor_object_set_description(Cursor_Object *self)
{
  PyObject	*desc;
  int i;
  char colName[128];
  int datatype, precision, scale, nullable;

  if (self->col_count == 0) {
    Py_XDECREF(self->description);
    self->description = PyTuple_New(0);
    return 1;
  }

  desc = (PyObject *) PyTuple_New(self->col_count);

  for ( i = 0 ; i < self->col_count ; i++ ) {
    PyObject *item;

    item = PyTuple_New(7);

    strcpy(colName, CCI_GET_RESULT_INFO_NAME(self->col_info, i+1));
    precision = CCI_GET_RESULT_INFO_PRECISION(self->col_info, i+1);
    scale     = CCI_GET_RESULT_INFO_SCALE(self->col_info, i+1);
    nullable  = CCI_GET_RESULT_INFO_IS_NON_NULL(self->col_info, i+1);

    datatype  = CCI_GET_RESULT_INFO_TYPE(self->col_info, i+1);
    /*
     * char 			1
     * string,varchar 	2
     * nchar 			3
     * varnchar			4
     * bit	 			5
     * varbit			6
     * numeric	 		7
     * int	 			8
     * short 			9
     * monetary 		10
     * float 			11
     * double 			12
     * date 			13
     * time 			14
     * timestamp		15
     * object 			19
     * set				32
     * multiset			64
     * sequence			96
     */
     
    PyTuple_SetItem(item, 0, PyString_FromString(colName));
    PyTuple_SetItem(item, 1, PyInt_FromLong(datatype));
    PyTuple_SetItem(item, 2, PyInt_FromLong(0));
    PyTuple_SetItem(item, 3, PyInt_FromLong(0));
    PyTuple_SetItem(item, 4, PyInt_FromLong(precision));
    PyTuple_SetItem(item, 5, PyInt_FromLong(scale));
    PyTuple_SetItem(item, 6, PyInt_FromLong(nullable));

    PyTuple_SetItem(desc, i, item);
  }

  Py_XDECREF(self->description);
  self->description = desc;

  return 1;
}

static int
cursor_object_bind_param(Cursor_Object *self, PyObject *params, T_CCI_PARAM_INFO *param_info)
{
  int i, res;
  PyObject	*paramVal;
  int param_cnt;
  char *str_val;

  param_cnt = PyTuple_Size(params);

  for(i = 0; i < param_cnt; i++) {
    paramVal = PyTuple_GetItem(params, i);
    if (PyString_Check(paramVal)) {
      str_val = PyString_AsString(paramVal);
      res = cci_bind_param(self->handle, i + 1, CCI_A_TYPE_STR, str_val, CCI_U_TYPE_STRING, 0);
    }
    else {
      res = CUBRID_ER_UNKNOWN_TYPE;
    }

    if (res < 0) {
      return res;
    }
  }

  return 0;
}

static PyObject *
cursor_object_execute(Cursor_Object *self, PyObject *args)
{
  T_CCI_ERROR error;
  T_CCI_COL_INFO *res_col_info;
  T_CCI_SQLX_CMD res_sql_type;
  T_CCI_PARAM_INFO *res_param_info = NULL;
  char 		*stmt = "";
  PyObject	*params = NULL;
  int 		res, res_col_count;

  if (!PyArg_ParseTuple(args, "s|O", &stmt, &params)) {
    return NULL;
  }

  if (!self->last_stmt || strcmp(self->last_stmt, stmt) != 0) {
    cursor_object_reset(self);

    res = cci_prepare(self->connection, stmt, 0 /*CCI_PREPARE_INCLUDE_OID*/, &error);
    if (res < 0) {
      return handle_error(res, &error);
    }
    self->handle = res;
    if (self->last_stmt) {
      PyMem_Free(self->last_stmt);
      self->last_stmt = NULL;
    }
    self->last_stmt = PyMem_Malloc(strlen(stmt) + 1);
    strcpy(self->last_stmt, stmt);
  }

  if (params) {
    res = cursor_object_bind_param(self, params, res_param_info);
    if (res < 0) {
      return handle_error(res, NULL);
    } 
  }

  res = cci_execute(self->handle, 0/*CCI_EXEC_ASYNC*/, 0, &error);
  if (res < 0) {
    return handle_error(res, &error);
  }

  res_col_info = cci_get_result_info(self->handle, &res_sql_type, &res_col_count);
  if (res_sql_type == SQLX_CMD_SELECT && !res_col_info) {
    return handle_error(CUBRID_ER_CANNOT_GET_COLUMN_INFO, NULL);
  }

  self->col_info = res_col_info;
  self->col_count = res_col_count;
  self->async_mode = 2;

  switch(res_sql_type) {
    case SQLX_CMD_SELECT:
      self->rowCount = res;
      break;
    case SQLX_CMD_INSERT:
    case SQLX_CMD_UPDATE:
    case SQLX_CMD_DELETE:
      self->affected_rows = res;
      break;
    case SQLX_CMD_CALL:
      self->rowCount = res;

    default:
      break;
  }

  if (res_sql_type == SQLX_CMD_SELECT) {
    cursor_object_set_description(self);

    res = cci_cursor(self->handle, 1, CCI_CURSOR_CURRENT, &error);
    if (res < 0 && res != CCI_ER_NO_MORE_DATA) {
      return handle_error(res, &error);
    }
  }

  return Py_BuildValue("i", res);
}

/* DB type to Python type mapping
 * 
 * int, short 				-> Integer
 * float, double, numeric 	-> Float
 * another type				-> String
 */
static PyObject *
cursor_object_dbval_to_pyvalue(Cursor_Object *self, int type, int index)
{
  int res, ind;
  PyObject *val, *tmpval;
  char *res_buf;
  int int_val;

  switch (type) {
    case CCI_U_TYPE_INT:
    case CCI_U_TYPE_SHORT:
      res = cci_get_data(self->handle, index, CCI_A_TYPE_INT, &int_val, &ind);
      if (res < 0) {
        return handle_error(res, NULL);
      }
      if (ind < 0) {
        Py_INCREF(Py_None);
        val = Py_None;
      } else {
        val = PyInt_FromLong(int_val);
      }
      break;

    case CCI_U_TYPE_FLOAT:
    case CCI_U_TYPE_DOUBLE:
    case CCI_U_TYPE_NUMERIC:
      res = cci_get_data(self->handle, index, CCI_A_TYPE_STR, &res_buf, &ind);
      if (res < 0) {
        return handle_error(res, NULL);
      }
      if (ind < 0) {
	Py_INCREF(Py_None);
	val = Py_None;
      } else {
        tmpval = PyString_FromString(res_buf);
#if PY_MAJOR_VERSION >= 3
        val = PyFloat_FromString(tmpval);
#else
        val = PyFloat_FromString(tmpval, NULL);
#endif
        Py_DECREF(tmpval);
      }
      break;

    default:
      res = cci_get_data(self->handle, index, CCI_A_TYPE_STR, &res_buf, &ind);
      if (res < 0) {
	return handle_error(res, NULL);
      }
      if (ind < 0) {
	Py_INCREF(Py_None);
	val = Py_None;
      } else {
	val = PyString_FromString(res_buf);
      }
      break;
  }

  return val;
}

/* Collection(set, multiset, sequence) 	-> List, 
 * Collection' item 					-> String 
 */
static PyObject *
cursor_object_dbset_to_pyvalue(Cursor_Object *self, int index)
{
  int i, res, ind;
  PyObject *val;
  T_CCI_SET set;
  int set_size;
  PyObject *e;
  char *buffer;

  res = cci_get_data(self->handle, index, CCI_A_TYPE_SET, &set, &ind);
  if (res < 0) {
    return handle_error(res, NULL);
  }

  if (set == NULL) {
    Py_INCREF(Py_None);
    return Py_None;
  }
  
  set_size = cci_set_size(set);
  val = PyList_New(set_size);

  for (i = 0; i < set_size; i++) {
    res = cci_set_get(set, i + 1, CCI_A_TYPE_STR, &buffer, &ind);
    if (res < 0) {
      return handle_error(res, NULL);
    }
    e =  PyString_FromString(buffer);
    PyList_SetItem(val, i, e);
  } 
  cci_set_free(set); 

  return val;
}

static PyObject *
cursor_object_fetch_a_row(Cursor_Object *self)
{
  int i, type;
  PyObject *row; 
  PyObject *val;

  row = PyList_New(self->col_count);

  for (i = 0; i < self->col_count; i++) {
    type = CCI_GET_RESULT_INFO_TYPE(self->col_info, i + 1);

    if (CCI_IS_COLLECTION_TYPE(type)) {
      val = cursor_object_dbset_to_pyvalue(self, i + 1);
    } else {
      val = cursor_object_dbval_to_pyvalue(self, type, i + 1);
    }
    PyList_SetItem(row, i, val);
  }

  return row;
}

static PyObject *
cursor_object_fetch(Cursor_Object *self, PyObject *args)
{
  int res;
  T_CCI_ERROR error;
  PyObject *row;

  res = cci_cursor(self->handle, 0, CCI_CURSOR_CURRENT, &error);
  if (res == CCI_ER_NO_MORE_DATA) {
    Py_INCREF(Py_None);
    return Py_None;
  } else if (res < 0) {
    return handle_error(res, &error);
  }

  res = cci_fetch(self->handle, &error);
  if (res < 0) {
    return handle_error(res, &error);
  }

  row = cursor_object_fetch_a_row(self);

  res = cci_cursor(self->handle, 1, CCI_CURSOR_CURRENT, &error);
  if (res < 0 && res != CCI_ER_NO_MORE_DATA) {
    return handle_error(res, &error);
  }

  return row;
}

static struct PyMemberDef cursor_object_members[] = {
  {
    "description", 
    T_OBJECT, 
    offsetof(Cursor_Object, description), 
    0, 
    "connection"
  },
  { 
    "rowcount", 
    T_INT, 
    offsetof(Cursor_Object, rowCount), 
    0, 
    "row count" 
  },
  { NULL }
};

static PyMethodDef cursor_object_methods[] = {
  {
    "close",
    (PyCFunction) cursor_object_close,
    METH_VARARGS, 
    "close cursor"
  },
  {
    "execute",
    (PyCFunction) cursor_object_execute,
    METH_VARARGS,
    "execute query"
  },
  {
    "fetch",
    (PyCFunction) cursor_object_fetch,
    METH_VARARGS,
    "fetch result"
  },
  {NULL, NULL} 
};

static void
cursor_object_dealloc(Cursor_Object *self)
{
  cursor_object_reset(self);
  Py_TYPE(self)->tp_free((PyObject*) self);
}

static char cursor_object__doc__[] = "";

PyTypeObject cursor_object_type = {
#if PY_MAJOR_VERSION >= 3
    PyVarObject_HEAD_INIT(NULL, 0)
#else
    PyObject_HEAD_INIT(NULL)
    0,						/* ob_size */
#endif
    "cubrid.Cursor",				/* tp_name */
    sizeof(Cursor_Object),			/* tp_basicsize */
    0,						/* tp_itemsize */
    (destructor) cursor_object_dealloc,		/* tp_dealloc */
    0,						/* tp_print */
    0,						/* tp_getattr */
    0,						/* tp_setattr */
    0,						/* tp_compare */
    0,						/* tp_repr */
    0,						/* tp_as_number */
    0,						/* tp_as_sequence */
    0,						/* tp_as_mapping */
    0,						/* tp_hash */
    0,						/* tp_call */
    0,						/* tp_str */
    0,						/* tp_getattro */
    0,						/* tp_setattro */
    0,						/* tp_as_buffer */
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,	/* tp_flags */
    cursor_object__doc__,			/* tp_doc */
    0,						/* tp_traverse */
    0,						/* tp_clear */
    0,						/* tp_richcompare */
    0,						/* tp_weaklistoffset */
    0,						/* tp_iter */
    0,						/* tp_iternext */
    cursor_object_methods,			/* tp_methods */
    cursor_object_members,			/* tp_members */
    0,						/* tp_getset */
    0,						/* tp_base */
    0,						/* tp_dict */
    0,						/* tp_descr_get */
    0,						/* tp_descr_set */
    0,						/* tp_dictoffset */
    (initproc) cursor_object_init,		/* tp_init */
    0,						/* tp_alloc */
    (newfunc) cursor_object_new,		/* tp_new */
    0,						/* tp_free */ 
};
 