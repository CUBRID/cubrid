#include "Python.h"
#include "structmember.h"
#include "datetime.h"
#include "cas_cci.h"

#if PY_MAJOR_VERSION >= 3
#define PyString_FromString PyBytes_FromString
#define PyString_AsString PyBytes_AsString
#define PyString_Check PyBytes_Check
#endif

#define CUBRID_ER_NO_MORE_MEMORY	    -30001
#define CUBRID_ER_INVALID_SQL_TYPE	    -30002
#define CUBRID_ER_CANNOT_GET_COLUMN_INFO    -30003
#define CUBRID_ER_INIT_ARRAY_FAIL           -30004
#define CUBRID_ER_UNKNOWN_TYPE              -30005
#define CUBRID_ER_INVALID_PARAM             -30006
#define CUBRID_ER_INVALID_ARRAY_TYPE        -30007
#define CUBRID_ER_NOT_SUPPORTED_TYPE        -30008
#define CUBRID_ER_OPEN_FILE                 -30009
#define CUBRID_ER_CREATE_TEMP_FILE          -30010
#define CUBRID_ER_INVALID_CURSOR_POS	    -30012
#define CUBRID_ER_SQL_UNPREPARE		    -30013
#define CUBRID_ER_PARAM_UNBIND		    -30014
#define CUBRID_ER_SCHEMA_TYPE               -30015
#define CUBRID_ER_READ_FILE                 -30016
#define CUBRID_ER_WRITE_FILE                -30017
#define CUBRID_ER_LOB_NOT_EXIST             -30018
#define CUBRID_ER_END                       -31000

#define CUBRID_EXEC_ASYNC           CCI_EXEC_ASYNC
#define CUBRID_EXEC_QUERY_ALL       CCI_EXEC_QUERY_ALL
#define CUBRID_EXEC_QUERY_INFO      CCI_EXEC_QUERY_INFO
#define CUBRID_EXEC_ONLY_QUERY_PLAN CCI_EXEC_ONLY_QUERY_PLAN
#define CUBRID_EXEC_THREAD          CCI_EXEC_THREAD

#ifdef MS_WINDOWS
#define CUBRID_LONG_LONG _int64
#else
#define CUBRID_LONG_LONG long long
#endif

typedef struct
{
  PyObject_HEAD
  int handle;
  char *url;
  char *user;
  char *passwd;
  PyObject *autocommit;
  PyObject *isolation_level;
  PyObject *max_string_len;
  PyObject *lock_timeout;
} _cubrid_ConnectionObject;

typedef struct
{
  PyObject_HEAD
  int handle;
  int connection;  
  int col_count;
  int row_count;
  int bind_num;
  int cursor_pos;
  char charset[128];
  T_CCI_CUBRID_STMT sql_type;
  T_CCI_COL_INFO *col_info;
  PyObject *description;  
} _cubrid_CursorObject;

typedef struct
{
  PyObject_HEAD
  int connection;
  T_CCI_BLOB blob;
  T_CCI_CLOB clob;
  char type;
  CUBRID_LONG_LONG pos;
} _cubrid_LobObject;

extern PyTypeObject _cubrid_ConnectionObject_type;
extern PyTypeObject _cubrid_CursorObject_type;
extern PyTypeObject _cubrid_LobObject_type;

