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
connection_object_new(PyTypeObject *type, PyObject *args, PyObject *kwargs)
{
  Connection_Object *self;

  self = (Connection_Object*) type->tp_alloc(type, 0);
  if (!self) {
      return NULL;
  }

  return (PyObject*) self;
}

static int
connection_object_init(Connection_Object *self, PyObject *args, PyObject *kwargs)
{
  static char *kwList[] = {"host", "port", "db", "user", "password", NULL};
  char *host	= "localhost";
  int  port	    = 30000;
  char *db	    = "demodb";
  char *user	= "public";
  char *passwd	= "";
  int  res, res2;
  char db_ver[16];
  T_CCI_ERROR error;

  if (!PyArg_ParseTupleAndKeywords(
        args, kwargs, "|sisss", kwList, &host, &port, &db, &user, &passwd)) {
    return -1;
  }

  self->handle = 0;
  self->host[0] = '\0';
  self->port = 0;
  self->db[0] = '\0';
  self->user[0] = '\0';

  res = cci_connect(host, port, db, user, passwd);

  if (res < 0) {
    handle_error(res, NULL);
    return -1;
  }

  res2 = cci_get_db_version(res, db_ver, sizeof(db_ver));
  if (res2 < 0) {
    cci_disconnect(res, &error);
    handle_error(res2, NULL);
    return -1;
  }

  res2 = cci_end_tran(res, CCI_TRAN_COMMIT, &error);
  if (res2 < 0) {
    cci_disconnect(res, &error);
    handle_error(res2, &error);
    return -1;
  }

  self->handle = res;
  strcpy(self->host, host);
  self->port = port;
  strcpy(self->db, db);
  strcpy(self->user, user);

  return 0;
} 

static PyObject *
connection_object_close(Connection_Object *self, PyObject *args)
{
  if (self->handle) {
    T_CCI_ERROR error;
    cci_disconnect(self->handle, &error);
    self->handle = 0;
  }

  Py_INCREF(Py_None);
  return Py_None;
}

static PyObject *
connection_object_cursor(Connection_Object *self, PyObject *args)
{
  PyObject *arg, *cursor;

  if (!self->handle) {
    handle_error(CCI_ER_CON_HANDLE, NULL);	
    return NULL;
  }

  arg = PyTuple_New(1);
  if (!arg) {
    return NULL;
  }
  
  Py_INCREF(self);
  PyTuple_SET_ITEM(arg, 0, (PyObject*) self);
  
  cursor = PyObject_Call((PyObject*) &cursor_object_type, arg, NULL);
  
  Py_DECREF(arg);

  return cursor;
}

static PyObject *
connection_object_end_tran(Connection_Object *self, int type)
{
  int res;
  T_CCI_ERROR error;

  res = cci_end_tran(self->handle, type, &error);
  if (res < 0){
    return handle_error(res, &error);
  }

  Py_INCREF(Py_None);
  return Py_None;
}

static PyObject *
connection_object_commit(Connection_Object *self, PyObject *args)
{
  return connection_object_end_tran(self, CCI_TRAN_COMMIT);
}

static PyObject *
connection_object_rollback(Connection_Object *self, PyObject *args)
{
  return connection_object_end_tran(self, CCI_TRAN_ROLLBACK);
}

static void
connection_object_dealloc(Connection_Object *self)
{
  PyObject *o;

  o = connection_object_close(self, NULL);
  Py_XDECREF(o);

  Py_TYPE(self)->tp_free((PyObject*) self);
}

static PyObject *
connection_object_repr(Connection_Object *self)
{
  char buf[1024];
  if (self->handle) {
    sprintf(buf,
        "<open CUBRID connection at %lx, "
        "host: %s, "
        "port: %d, "
        "db: %s "
        "user: %s"
        ">",
        (long)self,
        self->host,
        self->port,
        self->db,
        self->user
        );
  } else {
    sprintf(buf, "<closed connection at %lx>", (long)self);
  }
#if PY_MAJOR_VERSION >= 3
  return PyUnicode_FromString(buf);
#else
  return PyString_FromString(buf);
#endif
}

static PyMethodDef connection_object_methods[] = {
  {
    "close",
    (PyCFunction) connection_object_close,
    METH_VARARGS,
    "close connection"
  },
  {
    "cursor",
    (PyCFunction) connection_object_cursor,
    METH_VARARGS,
    "get cursor"
  },
  {
    "commit",
    (PyCFunction) connection_object_commit,
    METH_VARARGS,
    "commit transaction"
  },
  {
    "rollback",
    (PyCFunction) connection_object_rollback,
    METH_VARARGS,
    "rollback transaction"
  },
  {NULL, NULL}
};

static char cubrid_connect__doc__[] =
"Returns a CUBRID connection object.\n\n\
host\n\
  string, host to connect\n\n\
port\n\
  integer, TCP/IP port to connect to\n\n\
db\n\
  string, database to use\n\n\
user\n\
  string, user to connect as\n\n\
passwd\n\
  string, password to use\n\n";

PyTypeObject connection_object_type = {
#if PY_MAJOR_VERSION >= 3
    PyVarObject_HEAD_INIT(NULL, 0)
#else
    PyObject_HEAD_INIT(NULL)
    0,						/* ob_size */
#endif
    "cubrid.Connection",			/* tp_name */
    sizeof(Connection_Object),			/* tp_basicsize */
    0,						/* tp_itemsize */
    (destructor) connection_object_dealloc,	/* tp_dealloc */
    0,						/* tp_print */
    0,						/* tp_getattr */
    0,						/* tp_setattr */
    0,						/* tp_compare */
    (reprfunc) connection_object_repr,		/* tp_repr */
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
    cubrid_connect__doc__,			/* tp_doc */
    0,						/* tp_traverse */
    0,						/* tp_clear */
    0,						/* tp_richcompare */
    0,						/* tp_weaklistoffset */
    0,						/* tp_iter */
    0,						/* tp_iternext */
    connection_object_methods,			/* tp_methods */
    0,						/* tp_members */
    0,						/* tp_getset */
    0,						/* tp_base */
    0,						/* tp_dict */
    0,						/* tp_descr_get */
    0,						/* tp_descr_set */
    0,						/* tp_dictoffset */
    (initproc) connection_object_init,		/* tp_init */
    0,						/* tp_alloc */
    (newfunc) connection_object_new,		/* tp_new */
    0,						/* tp_free */ 
};
