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

/*
 * cubrid.c - low level Python module for CUBRID
 */
 
#include "cubrid.h"

char *cci_client_name = "CCI"; /* for CCI library */

static PyObject	*cubrid_error;
static PyObject	*cubrid_warning;
static PyObject	*cubrid_interface_error;
static PyObject	*cubrid_database_error;
static PyObject	*cubrid_internal_error;
static PyObject	*cubrid_operational_error;
static PyObject	*cubrid_programming_error;
static PyObject	*cubrid_integrity_error;
static PyObject	*cubrid_data_error;
static PyObject	*cubrid_not_supported_error;

static struct _error_message {
  int   err;
  char* msg;
} cubrid_err_msgs[] = {
{-1,      "CUBRID database error"},
{-2,      "Invalid connection handle"},
{-3,      "Memory allocation error"},
{-4,      "Communication error"},
{-5,      "No more data"},
{-6,      "Unknown transaction type"},
{-7,      "Invalid string parameter"},
{-8,      "Type conversion error"},
{-9,      "Parameter binding error"},
{-10,     "Invalid type"},
{-11,     "Parameter binding error"},
{-12,     "Invalid database parameter name"},
{-13,     "Invalid column index"},
{-14,     "Invalid schema type"},
{-15,     "File open error"},
{-16,     "Connection error"},
{-17,     "Connection handle creation error"},
{-18,     "Invalid request handle"},
{-19,     "Invalid cursor position"},
{-20,     "Object is not valid"},
{-21,     "CAS error"},
{-22,     "Unknown host name"},
{-99,     "Not implemented"},
{-1000,   "Database connection error"},
{-1002,   "Memory allocation error"},
{-1003,   "Communication error"},
{-1004,   "Invalid argument"},
{-1005,   "Unknown transaction type"},
{-1007,   "Parameter binding error"},
{-1008,   "Parameter binding error"},
{-1009,   "Cannot make DB_VALUE"},
{-1010,   "Type conversion error"},
{-1011,   "Invalid database parameter name"},
{-1012,   "No more data"},
{-1013,   "Object is not valid"},
{-1014,   "File open error"},
{-1015,   "Invalid schema type"},
{-1016,   "Version mismatch"},
{-1017,   "Cannot process the request. Try again later."},
{-1018,   "Authorization error"},
{-1020,   "The attribute domain must be the set type."},
{-1021,   "The domain of a set must be the same data type."},
{-2001,   "Memory allocation error"},
{-2002,   "Invalid API call"},
{-2003,   "Cannot get column info"},
{-2004,   "Array initializing error"},
{-2005,   "Unknown column type"},
{-2006,   "Invalid parameter"},
{-2007,   "Invalid array type"},
{-2008,   "Invalid type"},
{-2009,   "File open error"},
{-2010,   "Temporary file open error"},
{-2011,   "Glo transfering error"},
{0, ""}
};

int 
get_error_msg(int err_code, char **err_msg)
{
  int i;

  for(i = 0; ; i++) {
    if (!cubrid_err_msgs[i].err) break;
    if (cubrid_err_msgs[i].err == err_code) {
      *err_msg = cubrid_err_msgs[i].msg;
      return 0;
    }
  }
  return -1;
}

PyObject *
handle_error(int e, T_CCI_ERROR *error)
{
  PyObject  *t;
  int       err_code;
  char      msg[1024];
  char      *err_msg = NULL, *facility_msg;

  if (e == CCI_ER_DBMS) {
    facility_msg = "DBMS";
    if (error) {
      err_code = error->err_code;
      err_msg = error->err_msg;
    } else {
      err_code = 0;
      err_msg = "Unknown DBMS Error";
    }
  } else {
    if (get_error_msg(e, &err_msg) < 0) {
      strcpy(err_msg, "Unknown Error");
    }
    err_code = e;

    if (e > -1000) {
      facility_msg = "CCI";
    } else if (e > -2000) {
      facility_msg = "CAS";
    } else if (e > -2000) {
      facility_msg = "CLIENT";
    } else {
      facility_msg = "UNKNOWN";
    }
  }

  sprintf(msg, "ERROR: %s, %d, %s", facility_msg, err_code, err_msg);

  if (!(t = PyTuple_New(2))) return NULL;

  PyTuple_SetItem(t, 0, PyInt_FromLong((long)e));
  PyTuple_SetItem(t, 1, PyString_FromString(msg));

  PyErr_SetObject(cubrid_error, t);
  Py_DECREF(t);

  return NULL;
}

static PyObject *
cubrid_connect(PyObject *self, PyObject *args, PyObject *kwargs)
{
  return PyObject_Call((PyObject*) &connection_object_type, args, kwargs);
} 

static struct PyMethodDef cubrid_methods[] = { 
  {
    "connect", 
    (PyCFunction) cubrid_connect, 
    METH_VARARGS | METH_KEYWORDS, 
    "connect to CUBRID server"
  }, 
  {NULL, NULL} 
};             

void 
init_exceptions(PyObject *dict)
{
  cubrid_error = PyErr_NewException("cubrid.Error", PyExc_BaseException, NULL);
  PyDict_SetItemString(dict, "Error", cubrid_error);

  cubrid_warning = PyErr_NewException("cubrid.Warning", PyExc_BaseException, NULL);
  PyDict_SetItemString(dict, "Warning", cubrid_warning);

  cubrid_interface_error = PyErr_NewException("cubrid.InterfaceError", cubrid_error, NULL);
  PyDict_SetItemString(dict, "InterfaceError",cubrid_interface_error);

  cubrid_database_error = PyErr_NewException("cubrid.DatabaseError", cubrid_error, NULL);
  PyDict_SetItemString(dict, "DatabaseError", cubrid_database_error);

  cubrid_internal_error = PyErr_NewException("cubrid.InternalError", cubrid_database_error, NULL);
  PyDict_SetItemString(dict, "InternalError", cubrid_internal_error);

  cubrid_operational_error = PyErr_NewException("cubrid.OperationalError", cubrid_database_error, NULL);
  PyDict_SetItemString(dict, "OperationalError", cubrid_operational_error);

  cubrid_programming_error = PyErr_NewException("cubrid.ProgrammingError", cubrid_database_error, NULL);
  PyDict_SetItemString(dict, "ProgrammingError", cubrid_programming_error);

  cubrid_integrity_error = PyErr_NewException("cubrid.IntegrityError", cubrid_database_error, NULL);
  PyDict_SetItemString(dict, "IntegrityError", cubrid_integrity_error);

  cubrid_data_error = PyErr_NewException("cubrid.DataError", cubrid_database_error, NULL);
  PyDict_SetItemString(dict, "DataError", cubrid_data_error);

  cubrid_not_supported_error = PyErr_NewException("cubrid.NotSupportedError", cubrid_database_error, NULL);
  PyDict_SetItemString(dict, "NotSupportedError", cubrid_not_supported_error);
}

#define _CUBRID_VERSION_	"0.5.0"
static char cubrid_doc[] = "CUBRID API Module for Python";

#if PY_MAJOR_VERSION >= 3
static struct PyModuleDef cubriddef = {
  PyModuleDef_HEAD_INIT,
  "cubrid",
  NULL,
  0,
  cubrid_methods,
  NULL,
  NULL,
  NULL,
  NULL
};
#endif

void 
init_cubrid(void)
{
  return;
}

#if PY_MAJOR_VERSION >= 3
PyObject *
PyInit_cubrid(void)
#else
void 
initcubrid(void) 
#endif
{ 
  PyObject *dict, *module;

#if PY_MAJOR_VERSION >= 3
  module = PyModule_Create(&cubriddef);
#else
  module = Py_InitModule4("cubrid", cubrid_methods, cubrid_doc, (PyObject *)NULL, PYTHON_API_VERSION);
#endif

  if (!(dict = PyModule_GetDict(module))) {
    goto Error;
  }

  init_exceptions(dict);
  PyDict_SetItemString(dict, "__version__", PyString_FromString(_CUBRID_VERSION_));

  if (PyType_Ready(&connection_object_type) < 0) {
    goto Error;
  }

  Py_INCREF(&connection_object_type); 
  if (PyModule_AddObject(module, "Connection", (PyObject*) &connection_object_type) < 0) {
    goto Error;
  }

  if (PyType_Ready(&cursor_object_type) < 0) {
    goto Error;
  }

  Py_INCREF(&cursor_object_type); 
  if (PyModule_AddObject(module, "Cursor", (PyObject*) &cursor_object_type) < 0) {
    goto Error;
  }

#if PY_MAJOR_VERSION >= 3
    return module;
#else
    return;
#endif

Error:
  if (PyErr_Occurred()) {
    PyErr_SetString(PyExc_ImportError, "cubrid: init failure");
  }

#if PY_MAJOR_VERSION >= 3
  return NULL;
#else
  return;
#endif
} 
