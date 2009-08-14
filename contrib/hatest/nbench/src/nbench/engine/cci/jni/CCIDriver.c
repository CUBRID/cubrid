#include "nbench_engine_cci_CCIDriver.h"
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <dlfcn.h>
#include <string.h>
#include <errno.h>
/**
 * 
 * TODO
 * 1. make some constants readable (from cas_cci.h)
 * 2. precise error reporting
 * 3. resolve cci_connext_<version> func by cci_get_version func
 * NOTE
 * 1. autocommit mode is not supported
 *
 **/
/* -------------------------------------------------------------------------- */
/* DEBUG RELATED */
/* -------------------------------------------------------------------------- */
//#define DEBUG_LIBNBENCHCCI
#ifdef DEBUG_LIBNBENCHCCI
#define debug_printf(format,args...) fprintf(stderr,"[CCI]" format, ##args)
#else
#define debug_printf(format,args...)
#endif

/* from cas_cci.h */
typedef struct {
  int type;
  int is_non_null;
  int precision;
  char *col_name;
  char *real_attr;
  char *class_name;
} T_CCI_COL_INFO;
  
/* -------------------------------------------------------------------------- */
/* VIRTUAL CCI DRIVER INTERFACE */
/* -------------------------------------------------------------------------- */
typedef struct cci_error_s_ cci_error_s;
typedef struct cci_conn_s_ cci_conn_s;
typedef struct cci_req_s_ cci_req_s;
typedef struct cci_driver_ifs_ cci_driver_ifs;

struct cci_error_s_ //T_CCI_ERROR (cas_cci.h)
{
  int error_code;
  char err_msg[1024];
};

struct cci_conn_s_
{
  int handle;
  cci_req_s *prepared;
  cci_error_s error;
};

struct cci_req_s_
{
  int handle;
  cci_req_s *next;
  cci_conn_s *conn;
  int fetched;
  cci_error_s error;
  char sql[1];
};

struct cci_driver_ifs_
{
  /* ------------------------------------------- */
  /* driver interface function (some of CCI API) */
  /* ------------------------------------------- */
  int (*connect)(char *ip, int port, char *db, char *user, char *passwd);
  int (*disconnect) (int conn_h, cci_error_s *err_buf);
  int (*end_tran) (int conn_h, char type, cci_error_s *err_buf);
  int (*prepare) (int conn_h, char *sql, char flag, cci_error_s *err_buf);
  int (*bind_param) (int req_h, int index, int a_type, void *value, int u_type,
		     char flag);
  int (*get_db_parameter) (int conn_h, int param, void *value, 
			   cci_error_s *err_buf);
  int (*set_db_parameter) (int conn_h, int param, void *value, 
			   cci_error_s *err_buf);
  int (*execute) (int req_h, char flag, int max_col_size, cci_error_s *err_buf);
  T_CCI_COL_INFO* (*get_result_info) (int req_h, int *cmd_type, int *num);
  int (*cursor) (int req_h, int offset, int orgin, cci_error_s *err_buf);
  int (*fetch) (int req_h, cci_error_s *err_buf);
  int (*get_data) (int req_h, int col_no, int type, void *value, int *indi);
  /* ---------- */
  /* some flags */
  /* ---------- */
  int need_reprepare; //end_tran destryes all reqest handle
  void *handle; //return value of dlopen
  /* ---------------------------- */
  /* cached java global reference */
  /* ---------------------------- */
  jclass java_lang_Exception;
  jclass java_lang_String;
  jclass java_lang_Integer;
  jclass java_math_BigDecimal;
  jclass java_sql_Timestamp;
  jclass nbench_engine_cci_CCIColInfo;
};

/* -------------------------------------------------------------------------- */
/* STATIC VARIABLE */
/* -------------------------------------------------------------------------- */
static cci_driver_ifs * DRIVER = NULL; 

/* -------------------------------------------------------------------------- */
/* -------------------------------------------------------------------------- */
static jclass x_get_exception_class(JNIEnv *env)
{
  jclass cls;
  if(DRIVER->java_lang_Exception)
    return DRIVER->java_lang_Exception;
  cls = (*env)->FindClass(env, "Ljava/lang/Exception;");
  if(!cls)
    return NULL;
  DRIVER->java_lang_Exception = (*env)->NewGlobalRef(env, cls);
  (*env)->DeleteLocalRef(env, cls);
  return DRIVER->java_lang_Exception;
}

static jclass x_get_string_class(JNIEnv *env)
{
  jclass cls;
  if(DRIVER->java_lang_String)
    return DRIVER->java_lang_String;
  cls = (*env)->FindClass(env, "Ljava/lang/String;");
  if(!cls)
    return NULL;
  DRIVER->java_lang_String = (*env)->NewGlobalRef(env, cls);
  (*env)->DeleteLocalRef(env, cls);
  return DRIVER->java_lang_String;
}

static jclass x_get_integer_class(JNIEnv *env)
{
  jclass cls;
  if(DRIVER->java_lang_Integer)
    return DRIVER->java_lang_Integer;
  cls = (*env)->FindClass(env, "Ljava/lang/Integer;");
  if(!cls)
    return NULL;
  DRIVER->java_lang_Integer = (*env)->NewGlobalRef(env, cls);
  (*env)->DeleteLocalRef(env, cls);
  return DRIVER->java_lang_Integer;
}

static jclass x_get_bigdecimal_class(JNIEnv *env)
{
  jclass cls;
  if(DRIVER->java_math_BigDecimal)
    return DRIVER->java_math_BigDecimal;
  cls = (*env)->FindClass(env, "Ljava/math/BigDecimal;");
  if(!cls)
    return NULL;
  DRIVER->java_math_BigDecimal = (*env)->NewGlobalRef(env, cls);
  (*env)->DeleteLocalRef(env, cls);
  return DRIVER->java_math_BigDecimal;
}

static jclass x_get_timestamp_class(JNIEnv *env)
{
  jclass cls;
  if(DRIVER->java_sql_Timestamp)
    return DRIVER->java_sql_Timestamp;
  cls = (*env)->FindClass(env, "Ljava/sql/Timestamp;");
  if(!cls)
    return NULL;
  DRIVER->java_sql_Timestamp = (*env)->NewGlobalRef(env, cls);
  (*env)->DeleteLocalRef(env, cls);
  return DRIVER->java_sql_Timestamp;
}

static jclass x_get_ccicolinfo_class(JNIEnv *env)
{
  jclass cls;
  if(DRIVER->nbench_engine_cci_CCIColInfo)
    return DRIVER->nbench_engine_cci_CCIColInfo;
  cls = (*env)->FindClass(env, "Lnbench/engine/cci/CCIColInfo;");
  if(!cls)
    return NULL;
  DRIVER->nbench_engine_cci_CCIColInfo = (*env)->NewGlobalRef(env, cls);
  (*env)->DeleteLocalRef(env, cls);
  return DRIVER->nbench_engine_cci_CCIColInfo;
}

/* -------------------------------------------------------------------------- */
/* -------------------------------------------------------------------------- */
static int x_init (char *driver)
{
  void *handle = NULL;
  cci_driver_ifs *ifs = malloc(sizeof(*ifs));
  void *tmp;
  int res;
  void (*initfunc)(void) = NULL;

  debug_printf(">x_init\n");
  if(!ifs)
    return -1;
  if((handle = dlopen(driver, RTLD_GLOBAL | RTLD_LAZY)) == NULL)
  {
    res = -2;
    goto error;
  }
  // fill member funcs
  *(void **)(&ifs->connect) = tmp = 
	dlsym(handle, "cci_connect_3_0"); 
  if(!tmp) { res = -11;  goto error; }
  *(void **)(&ifs->disconnect) = tmp = 
	dlsym(handle, "cci_disconnect");
  if(!tmp) { res = -12;  goto error; }
  *(void **)(&ifs->end_tran) = tmp = 
	dlsym(handle, "cci_end_tran");
  if(!tmp) { res = -13;  goto error; }
  *(void **)(&ifs->prepare) = tmp = 
	dlsym(handle, "cci_prepare");
  if(!tmp) { res = -14;  goto error; }
  *(void **)(&ifs->bind_param) = tmp = 
	dlsym(handle, "cci_bind_param");
  if(!tmp) { res = -15;  goto error; }
  *(void **)(&ifs->get_db_parameter) = tmp = 
	dlsym(handle, "cci_get_db_parameter");
  if(!tmp) { res = -16;  goto error; }
  *(void **)(&ifs->set_db_parameter) = tmp = 
	dlsym(handle, "cci_set_db_parameter");
  if(!tmp) { res = -17;  goto error; }
  *(void **)(&ifs->execute) = tmp = 
	dlsym(handle, "cci_execute");
  if(!tmp) { res = -18;  goto error; }
  *(void **)(&ifs->get_result_info) = tmp = 
	dlsym(handle, "cci_get_result_info");
  if(!tmp) { res = -19;  goto error; }
  *(void **)(&ifs->cursor) = tmp = 
	dlsym(handle, "cci_cursor");
  if(!tmp) { res = -20;  goto error; }
  *(void **)(&ifs->fetch) = tmp = 
	dlsym(handle, "cci_fetch");
  if(!tmp) { res = -21;  goto error; }
  *(void **)(&ifs->get_data) = tmp = 
	dlsym(handle, "cci_get_data");
  if(!tmp) { res = -21;  goto error; }
  // initializer
  initfunc = (void(*)())dlsym(handle, "cci_init");
  if(!initfunc) { res = -22; goto error; }
  initfunc();
  do // check flags
  {
    int (*get_version_func)(int*, int*, int*);
    int major, minor, patch;
    *(void **)(&get_version_func) = dlsym(handle, "cci_get_version");
    if(!get_version_func)
    {
      res = -22;
      goto error;
    } 
    res = get_version_func(&major, &minor, &patch);
    if(res < 0) 
    {
      res = -23;
      goto error;
    }
    if(major >= 3 && minor >= 6)
    {
      debug_printf("need_reprepare = 0\n"); 
      ifs->need_reprepare = 0;
    }
    else
    {
      debug_printf("need_reprepare = 1\n"); 
      ifs->need_reprepare = 1;
    }
  } while(0);
  ifs->handle = handle; 
  ifs->java_lang_Exception = NULL;
  ifs->java_lang_String = NULL;
  ifs->java_lang_Integer = NULL;
  ifs->java_math_BigDecimal = NULL;
  ifs->java_sql_Timestamp = NULL;
  ifs->nbench_engine_cci_CCIColInfo = NULL;
  DRIVER = ifs;
  return 0;
error:
  if(ifs)
    free(ifs);
  if(handle)
    dlclose(handle);
  return res;
}

/* -------------------------------------------------------------------------- */
/* CCI DRIVER IMPLEMENTATION */
/* -------------------------------------------------------------------------- */
/* 실패한 경우 0을 리턴한다.  */
static long x_connect(char *ip, int port, char *db, char *user, char *passwd)
{
  int conn_handle;
  cci_conn_s * conn;

  debug_printf(">x_connect\n");
  if(!DRIVER) 
    return 0;
  conn = malloc(sizeof(*conn));
  if(!conn)
    return 0;
  debug_printf(">>DRIVER->connect\n");
  conn_handle = DRIVER->connect(ip, port, db, user, passwd);
  debug_printf("<<conn_handle=%d\n", conn_handle);
  //__asm__ __volatile__("int3"); 
  if(conn_handle < 0)
  {
    free(conn);
    return 0;
  }
  conn->handle = conn_handle;
  conn->prepared = NULL;
  return (long)conn;
}

static int x_disconnect (long conn_h)
{
  cci_conn_s * conn = (cci_conn_s *)conn_h;
  int res;
  if(!conn || !DRIVER)
    return -1;
  debug_printf(">x_disconnect(conn_h=%ld)\n", conn_h);

  debug_printf(">>DRIVER->disconnect\n");
  res = DRIVER->disconnect(conn->handle, &conn->error);
  debug_printf("<<res=%d\n", res);
  return res;
}

static int x_commit (long conn_h)
{
  cci_conn_s * conn = (cci_conn_s *)conn_h;
  int res;
  if(!conn || !DRIVER)
    return -1;

  debug_printf(">x_commit(conn_h=%ld)\n", conn_h);
  //1: CCI_TRAN_COMMIT, 2: CCI_TRAN_ROLLBACK
  debug_printf(">>DRIVER->end_tran\n");
  res = DRIVER->end_tran(conn->handle, 1, &conn->error);
  debug_printf("<<res=%d\n", res);
  if(DRIVER->need_reprepare)
  {
    cci_req_s *req;
    for(req = conn->prepared; req; req = req->next)
    {
      if(req->handle > 0)
      {
	debug_printf(">>flush req->handle=%d\n", req->handle);
	req->handle = -1;
      }
    }
  }
  return res;
}

/* 실패하면 0을 리턴한다.  */
static long x_prepare (long conn_h, char *sql)
{
  cci_conn_s *conn = (cci_conn_s *)conn_h;
  cci_req_s *req;
  int res;
  if(!conn || !DRIVER || !sql)
    return 0;
  req = malloc(sizeof(*req) + strlen(sql));
  if(!req)
    return 0;

  debug_printf(">x_prepare\n");
  debug_printf(">>DRIVER->prepare\n");
  res = DRIVER->prepare(conn->handle, sql, 0, &conn->error);
  debug_printf("<<res=%d\n", res);
  if(res > 0)
  {
    strcpy(req->sql, sql);
    req->conn = conn;
    req->handle = res;
    req->fetched = 0;
    req->next = conn->prepared;
    conn->prepared = req;
    return (long)req;
  }
  else
  {
    free(req);
    return 0L;
  }
}

static int x_bind (long req_h, int index, char *val, int u_type)
{
  cci_req_s * req = (cci_req_s *)req_h;
  int res;
  if(!req || !DRIVER)
    return -1;
  debug_printf(">x_bind(req_h:%d,index:%d,val:%s)\n",req->handle, index, val ? val : "null");
  /* reprepare if needed */
  if(DRIVER->need_reprepare && req->handle == -1)
  {
    debug_printf(">>DRIVER->prepare(conn_h=%d)\n", req->conn->handle);
    res = DRIVER->prepare(req->conn->handle, req->sql, 0, &req->error);
    debug_printf("<<res=%d\n", res);
    if(res < 0)
    {
      return res;
    }
    req->handle = res;
  }
  /* 1: CCI_A_TYPE_STR */
  debug_printf(">>DRIVER->bind_param\n");
  res = DRIVER->bind_param(req->handle, index, /* a_type */ 1, val, u_type, 0);
  debug_printf("<<res=%d\n", res);
  return res;
}

static int x_execute (long req_h, int autocommit)
{
  cci_req_s * req = (cci_req_s *)req_h;
  int res;
  if(!req || !DRIVER)
    return -1;
  debug_printf(">x_execute(req_h:%d,autocommit:%s)\n", req->handle, autocommit ? "true" : "false");
  /* reprepare if needed */
  if(DRIVER->need_reprepare && req->handle == -1)
  {
    debug_printf(">>DRIVER->prepare\n");
    res = DRIVER->prepare(req->conn->handle, req->sql, 0, &req->error);
    debug_printf("<<res=%d\n",res);
    if(res < 0)
      return res;
    req->handle = res;
  }
  debug_printf(">>DRIVER->execute\n");
  res = DRIVER->execute(req->handle, 0, 0, &req->error);
  debug_printf("<<res=%d\n", res);
  if(res < 0)
    return res;
  return res;
}

static T_CCI_COL_INFO * x_collinfo (long req_h, int *size)
{
  cci_req_s * req = (cci_req_s *)req_h;
  int cmd_type;
  T_CCI_COL_INFO *res;
  if(!req || !DRIVER)
    return NULL;
  debug_printf(">x_collinfo\n");
  debug_printf(">>DRIVER->get_result_info\n");
  res = DRIVER->get_result_info(req->handle, &cmd_type, size);
  debug_printf("<<res=%p\n", res);
  return res;
}

static int x_cursor_first (long req_h)
{
  cci_req_s * req = (cci_req_s *)req_h;
  int res;
  if(!req || !DRIVER)
    return -1;
  debug_printf(">x_cursor_first\n");
  /* 0: first, 1: curr, 2:last */
  debug_printf(">>DRIVER->cursor\n");
  res = DRIVER->cursor(req->handle, 1, 0, &req->error);
  debug_printf("<<res=%d\n", res);
  req->fetched = 0;
  return res;
}

static int x_cursor_next (long req_h)
{
  cci_req_s * req = (cci_req_s *)req_h;
  int res;
  if(!req || !DRIVER)
    return -1;
  debug_printf(">x_cursor_next\n");
  /* 0: first, 1: curr, 2:last */
  debug_printf(">>DRIVER->cursor\n");
  res = DRIVER->cursor(req->handle, 1, 1, &req->error);
  debug_printf("<<res=%d\n", res);
  req->fetched = 0;
  return res;
}

static int x_fetch(long req_h, int index, int u_type)
{
  cci_req_s * req = (cci_req_s *)req_h;
  int res;
  if(!req || !DRIVER)
    return -1;
  if(req->fetched)
    return 1;
  debug_printf(">x_fetch\n");
  debug_printf(">>DRIVER->fetch\n");
  res = DRIVER->fetch(req->handle, &req->error);
  debug_printf("<<res=%d\n", res);
  if(res >= 0)
    req->fetched = 1;
  return res;
}

static char * x_req_error_msg(long req_h)
{
  cci_req_s * req = (cci_req_s *)req_h;
  if(!req)
    return "error error(req_h)";
  return req->error.err_msg;
}

static char * x_conn_error_msg(long conn_h)
{
  cci_conn_s * conn = (cci_conn_s *)conn_h;
  if(!conn)
    return "error error(conn_h)";
  return conn->error.err_msg;
}

/* -------------------------------------------------------------------------- */
/* JNI IMPLEMENTATION */
/* -------------------------------------------------------------------------- */

#define THROW_EXCEPTION(str)                                      \
do {                                                              \
  jclass exception;                                               \
  exception = x_get_exception_class(env);                         \
  if(!exception)                                                  \
    break;  /* give up */                                         \
  (*env)->ThrowNew(env, exception, str);                          \
} while(0)

/*
 */
void JNICALL Java_nbench_engine_cci_CCIDriver_init
  (JNIEnv *env, jclass cls, jstring driver)
{
  const char *str;
  int res;
  char err_buf[128];
  str = (*env)->GetStringUTFChars(env, driver, NULL);
  if(!str)
    return; /* OutOfMemoryError already thrown */
  res = x_init((char *)str);
  (*env)->ReleaseStringUTFChars(env, driver, str);
  if(res < 0)
  {
    sprintf(err_buf, "init failed (res:%d)", res);
    THROW_EXCEPTION(err_buf);
  }
}

/*
 */
jlong JNICALL Java_nbench_engine_cci_CCIDriver_connect 
(JNIEnv * env, jobject obj, jstring ip, jint port, jstring db, jstring user, 
 jstring passwd)
{
  const char *ip_s = NULL;
  const char *db_s = NULL;
  const char *user_s = NULL;
  const char *passwd_s = NULL;
  long conn_h;
  if(ip)
  {
    ip_s = (*env)->GetStringUTFChars(env, ip, NULL);
    if(!ip_s) 
      return 0; //already thrown
  }
  if(db)
  {
    db_s = (*env)->GetStringUTFChars(env, db, NULL);
    if(!db_s)
      return 0;
  }
  if(user)
  {
    user_s = (*env)->GetStringUTFChars(env, user, NULL);
    if(!user_s)
      return 0;
  }
  if(passwd)
  {
    passwd_s = (*env)->GetStringUTFChars(env, passwd, NULL);
    if(!passwd_s)
      return 0;
  }
  conn_h = x_connect((char *)ip_s, port, (char *)db_s, (char *)user_s, 
	(char *)passwd_s);
  if(ip_s)
    (*env)->ReleaseStringUTFChars(env, ip, ip_s);
  if(db_s)
    (*env)->ReleaseStringUTFChars(env, db, db_s);
  if(user_s)
    (*env)->ReleaseStringUTFChars(env, user, user_s);
  if(passwd_s)
    (*env)->ReleaseStringUTFChars(env, passwd, passwd_s);
  if(conn_h == 0) 
    THROW_EXCEPTION("connect failed");
  return (jlong)conn_h;
}

/*
 */
void JNICALL Java_nbench_engine_cci_CCIDriver_disconnect
  (JNIEnv *env, jobject obj, jlong conn_h)
{
  int res;
  res = x_disconnect((long)conn_h);
  if(res < 0)
    THROW_EXCEPTION(x_conn_error_msg((long)conn_h));
}

/*
 */
void JNICALL Java_nbench_engine_cci_CCIDriver_commit
  (JNIEnv *env, jobject obj, jlong conn_h)
{
  int res;
  res = x_commit((long)conn_h);
  if(res < 0)
    THROW_EXCEPTION(x_conn_error_msg((long)conn_h));
}

/*
 */
jlong JNICALL Java_nbench_engine_cci_CCIDriver_prepare
  (JNIEnv *env, jobject obj, jlong conn_h, jstring sql)
{
  long res;
  const char *sql_s;
  if(!sql)
  {
    THROW_EXCEPTION("sql is null");
    return 0;
  }
  sql_s = (*env)->GetStringUTFChars(env, sql, NULL);
  if(!sql_s)
    return 0;
  res = x_prepare((long)conn_h, (char *)sql_s);
  (*env)->ReleaseStringUTFChars(env, sql, sql_s);
  if(res == 0)
    THROW_EXCEPTION(x_conn_error_msg((long)conn_h));
  return (jlong)res;
}

/*
 */
jint JNICALL Java_nbench_engine_cci_CCIDriver_bind
  (JNIEnv *env, jobject obj, jlong req_h, jint index, jint u_type, jobject val)
{
  int res = -1;
  jclass from_class;
  jmethodID mid;
  const char *str;
  if(!val)
  {
    THROW_EXCEPTION("value is null");
    return 0;
  }
  switch(u_type)
  {
    case 1:  /* U_TYPE_CHAR */
    case 2:  /* U_TYPE_STRING, U_TYPE_VARCHAR */
    case 3:  /* U_TYPE_NCHAR */
    case 4:  /* U_TYPE_VARNCHAR */
    { // from:java.lang.String --> to:char * 
#if 0 
      from_class = x_get_string_class(env);
      if(!from_class)
	break;
      if((*env)->IsInstanceOf(env, val, from_class) == JNI_FALSE)
      {
	THROW_EXCEPTION("java.lang.String expected");
	break;
      }
#endif
      str = (*env)->GetStringUTFChars(env, val, NULL);
      if(!str)
	break;
      res = x_bind((long)req_h, (int)index, (char *)str, (int)u_type);
      (*env)->ReleaseStringUTFChars(env, val, str);
      break;
    }
    case 7:  /* U_TYPE_NUMERIC */
    { // from:java.lang.BigDeciam --> to:char *
      jobject jo;

      from_class = x_get_bigdecimal_class(env);
      if(!from_class)
	break;
#if 0
      if((*env)->IsInstanceOf(env, val, from_class) == JNI_FALSE)
      {
	THROW_EXCEPTION("java.math.BigDecimal expected");
	break;
      }
#endif
      mid = (*env)->GetMethodID(env, from_class, "toPlainString", 
	"()Ljava/lang/String;");
      if(mid == NULL)
	break;
      jo = (*env)->CallObjectMethod(env, val, mid);
      if(jo == NULL)
	break;
      str = (*env)->GetStringUTFChars(env, jo, NULL);
      if(!str)
	break;
      res = x_bind((long)req_h, (int)index, (char *)str, (int)u_type);
      (*env)->ReleaseStringUTFChars(env, jo, str);
      break;
    }
    case 8:  /* U_TYPE_INTEGER */
    { // from:java.lang.Integer --> to:int *
      jint ival;
      char i_buf[16];

      from_class = x_get_integer_class(env);
      if(!from_class)
	break;
#if 0
      if((*env)->IsInstanceOf(env, val, from_class) == JNI_FALSE)
      {
	THROW_EXCEPTION("java.lang.Integer expected");
	break;
      }
#endif
      mid = (*env)->GetMethodID(env, from_class, "intValue", "()I");
      if(mid == NULL)
	break;
      ival = (*env)->CallIntMethod(env, val, mid);
      if((*env)->ExceptionCheck(env) == JNI_TRUE)
	break;
      sprintf(i_buf, "%d", (int)ival);
      res = x_bind((long)req_h, (int)index, i_buf, (int)u_type);
      break;
    }
    case 15: /* U_TYPE_TIMESTAMP */
    { // from:java.sql.Timestamp --> to:char * (YYYY/MM/DD HH:MM:SS) -_-
      jobject jo;
      char tmp_buf[32]; /* yyyy-mm-dd hh:mm:ss.fffffffff (from java) */

      from_class = x_get_timestamp_class(env);
      if(!from_class)
	break;
      mid = (*env)->GetMethodID(env, from_class, "toString", 
	"()Ljava/lang/String;");
      if(mid == NULL)
	break;
      jo = (*env)->CallObjectMethod(env, val, mid);
      if(jo == NULL)
        break;
      str = (*env)->GetStringUTFChars(env, jo, NULL);
      if(!str)
	break;
      strncpy(tmp_buf, str, 32);
      (*env)->ReleaseStringUTFChars(env, jo, str);
      /* fast convert */
      tmp_buf[4] = tmp_buf[7] = '/';
      tmp_buf[19] = 0;
      res = x_bind((long)req_h, (int)index, tmp_buf, (int)u_type);
      break;
    }
    default:
      THROW_EXCEPTION("unsupported data type");
      return 0;
  }
  if(res < 0)
    THROW_EXCEPTION("bind error");
  return 0;
}

/*
 */
jint JNICALL Java_nbench_engine_cci_CCIDriver_execute
  (JNIEnv *env, jobject obj, jlong req_h, jboolean autocommit)
{
  int res = x_execute((long)req_h, (int)autocommit);
  if(res < 0)
    THROW_EXCEPTION("execute error");
  return 0;
}

/*
 */
jobjectArray JNICALL Java_nbench_engine_cci_CCIDriver_collinfo
  (JNIEnv *env, jobject self, jlong req_h)
{
  T_CCI_COL_INFO *info;
  int size;
  jobjectArray ary;
  jclass cls;
  jstring jstr;
  int i;
  jfieldID f_index, f_u_type, f_is_non_null, f_col_name, f_real_attr;
  jmethodID cid;
  
  
  info = x_collinfo((long)req_h, &size);
  if(info == NULL || size < 0)
  {
    THROW_EXCEPTION("collinfo error");
    return NULL;
  }
  cls = x_get_ccicolinfo_class(env);
  if(cls == NULL)
    return NULL;
  /* find fields */
  f_index = (*env)->GetFieldID(env, cls, "index", "I");
  if(!f_index) return NULL;
  f_u_type = (*env)->GetFieldID(env, cls, "u_type", "I");
  if(!f_u_type) return NULL;
  f_is_non_null = (*env)->GetFieldID(env, cls, "is_non_null", "Z");
  if(!f_is_non_null) return NULL;
  f_col_name = (*env)->GetFieldID(env, cls, "col_name", "Ljava/lang/String;");
  if(!f_col_name) return NULL;
  f_real_attr = (*env)->GetFieldID(env, cls, "real_attr", "Ljava/lang/String;");
  if(!f_real_attr) return NULL;
  /* make object array */
  ary = (*env)->NewObjectArray(env, size, cls, NULL);
  if(ary == NULL)
    return NULL;
  cid = (*env)->GetMethodID(env, cls, "<init>", "()V");
  if(!cid)
    return NULL;
  for(i = 0; i < size; i++)
  {
    jobject obj = (*env)->NewObject(env, cls, cid);
    if(!obj)
      return NULL;
    /* set fields of new obj */
    (*env)->SetIntField(env, obj, f_index, (jint)(i+1)); //0 or 1?
    if((*env)->ExceptionCheck(env) == JNI_TRUE) 
      return NULL;
    (*env)->SetIntField(env, obj, f_u_type, (jint)info[i].type);
    if((*env)->ExceptionCheck(env) == JNI_TRUE) 
      return NULL;
    (*env)->SetBooleanField(env, obj, f_is_non_null, 
	info[i].is_non_null ? 1 : 0);
    if((*env)->ExceptionCheck(env) == JNI_TRUE) 
      return NULL;
    if(info[i].col_name)
    {
      jstr = (*env)->NewStringUTF(env, info[i].col_name);
      if(!jstr)
	return NULL;
      (*env)->SetObjectField(env, obj, f_col_name, jstr);
      if((*env)->ExceptionCheck(env) == JNI_TRUE)
	return NULL;
    }
    if(info[i].real_attr)
    {
      jstr = (*env)->NewStringUTF(env, info[i].real_attr);
      if(!jstr)
	return NULL;
      (*env)->SetObjectField(env, obj, f_real_attr, jstr);
      if((*env)->ExceptionCheck(env) == JNI_TRUE)
	return NULL;
    }
    /* set object to ary */
    (*env)->SetObjectArrayElement(env, ary, i, obj);
    if((*env)->ExceptionCheck(env) == JNI_TRUE)
      return NULL;
    /* delete local ref of object */
    (*env)->DeleteLocalRef(env, obj);
    if((*env)->ExceptionCheck(env) == JNI_TRUE)
      return NULL;
  }
  return ary;
}

/*
 */
jboolean JNICALL Java_nbench_engine_cci_CCIDriver_cursor_1first
  (JNIEnv *env, jobject obj, jlong req_h)
{
  int res = x_cursor_first((long)req_h);
  if(res < 0)
  {
    if(res == -5) // CCI_ER_NO_MORE_DATA
      return JNI_FALSE;
    else
      THROW_EXCEPTION(x_req_error_msg(req_h));
  }
  return JNI_TRUE;
}

/*
 */
jboolean JNICALL Java_nbench_engine_cci_CCIDriver_cursor_1next
  (JNIEnv *env, jobject obj, jlong req_h)
{
  int res = x_cursor_next((long)req_h);
  if(res < 0)
  {
    if(res == -5) // CCI_ER_NO_MORE_DATA
      return JNI_FALSE;
    else
      THROW_EXCEPTION(x_req_error_msg(req_h));
  }
  return JNI_TRUE;
}

/*
 */
jobject JNICALL Java_nbench_engine_cci_CCIDriver_fetch
  (JNIEnv *env, jobject self, jlong req_h, jint index, jint u_type)
{
  int res;
  char *str;
  int indi;
  jclass cls;
  jobject obj;
  jstring jstr;
  cci_req_s * req = (cci_req_s *)(long)req_h;

  if(!req || !DRIVER)
  {
    THROW_EXCEPTION("invalid argument");
    return NULL;
  }
  res = x_fetch((long)req_h, (int)index, (int)u_type);
  if(res < 0)
  {
    THROW_EXCEPTION("fetch error");
    return NULL;
  }
  
  if((int)u_type == 8)
  { //fast process for U_TYPE_INTEGER(8)
    int ival, indi;
    debug_printf(">r_fetch calls get_data(index:%d, atype=%d)\n", index, 2);
    /* CCI_A_TYPE_INT(2) */
    res = DRIVER->get_data(req->handle, (int)index, 2, &ival, &indi);
    debug_printf("<<res=%d\n", res);
    if(res == 0) /* no error */
    {
      jmethodID cid;
      cls = x_get_integer_class(env);
      if(!cls)
        return NULL;
      cid = (*env)->GetMethodID(env, cls, "<init>", "(I)V");
      if(!cid)
        return NULL;
      obj = (*env)->NewObject(env, cls, cid, (jint)ival);
      return obj;
    }
    /* fall through : try as string */
  }
  debug_printf(">r_fetch calls get_data(index:%d, atype=%d)\n", index, 1);
  /* first get data as a string: 1 -> CCI_A_TYPE_STR */
  res = DRIVER->get_data(req->handle, (int)index, 1, &str, &indi);
  debug_printf("<<res=%d\n", res);
  if(res < 0)
  {
    debug_printf(">r_fetch res:%d, indi:%d\n", res, indi);
    THROW_EXCEPTION("fetch error: get_data");
    return NULL;
  }
  if(indi == -1) //NULL
  {
    return NULL; //java (null)
  }
  switch((int)u_type)
  {
    case 1:  /* U_TYPE_CHAR */
    case 2:  /* U_TYPE_STRING, U_TYPE_VARCHAR */
    case 3:  /* U_TYPE_NCHAR */
    case 4:  /* U_TYPE_VARNCHAR */
    { // to java.lang.String 
      jstr = (*env)->NewStringUTF(env, str);
      return jstr;
    }
    case 7:  /* U_TYPE_NUMERIC */
    { // to java.lang.BigDecimal
      jmethodID cid;
      cls = x_get_bigdecimal_class(env);
      if(!cls)
	return NULL;
      cid = (*env)->GetMethodID(env, cls, "<init>", "(Ljava/lang/String;)V");
      if(!cid)
	return NULL;
      jstr = (*env)->NewStringUTF(env, str);
      if(!jstr)
	return NULL;
      obj = (*env)->NewObject(env, cls, cid, jstr);
      return obj; 
    }
    case 8:  /* U_TYPE_INTEGER */
    { // to java.lang.Integer
      jmethodID cid;
      cls = x_get_integer_class(env);
      if(!cls)
	return NULL;
      cid = (*env)->GetMethodID(env, cls, "<init>", "(Ljava/lang/String;)V");
      if(!cid)
	return NULL;
      jstr = (*env)->NewStringUTF(env, str);
      if(!jstr)
	return NULL;
      obj = (*env)->NewObject(env, cls, cid, jstr);
      return obj;
    }
    case 15: /* U_TYPE_TIMESTAMP */
    { // to java.sql.Timestamp  (yyyy-mm-dd hh:mm:ss) format (-_- see prev)
      jmethodID cid;
      char tmp_buf[32];
      strncpy(tmp_buf, str, 20); //쪼개 쓰자..
      strncat(tmp_buf, ".000000000", 12);
      cls = x_get_timestamp_class(env);
      if(!cls)
	return NULL;
      cid = (*env)->GetStaticMethodID(env, cls, "valueOf", 
	"(Ljava/lang/String;)Ljava/sql/Timestamp;");
      if(!cid)
	return NULL;
      jstr = (*env)->NewStringUTF(env, tmp_buf);
      if(!jstr)
	return NULL;
      obj = (*env)->CallStaticObjectMethod(env, cls, cid, jstr);
      return obj;
    }
    default:
      THROW_EXCEPTION("unsupported data type");
      return 0;
  }
  return 0;
}

