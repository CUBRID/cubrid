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
 * always define this macro.
 * developer can move this macro definition to global 
 * configuration header if need to control this option
 */
#define SUPPORT_BROKER_JNI


#ifdef SUPPORT_BROKER_JNI

#include "cm_stat.h"
#include "cm_portable.h"

#include <jni.h>

/*
 * Report failed JNI call position (file name, line number) to STDOUT
 */
#define JNI_TRACE_ON 1

#define CHK_EXCEP(env) ((*env)->ExceptionOccurred(env) != NULL)

#define FAIL_RET_X(env, x, expr)									\
	do {															\
		if ((expr) == NULL || CHK_EXCEP(env)) {						\
			if (JNI_TRACE_ON)										\
				printf("jni error: %s (%d)\n", __FILE__, __LINE__);	\
			return (x);												\
		}															\
	} while (0)

#define FAIL_RET(env, expr)											\
	do {															\
		if ((expr) == NULL || CHK_EXCEP(env)) {						\
			if (JNI_TRACE_ON)										\
				printf("jni error: %s (%d)\n", __FILE__, __LINE__);	\
			return;													\
		}															\
	} while (0)

#define FAIL_RET_INT(env, expr)										\
	do {															\
		int __ret__ = (expr);										\
		if (__ret__ != 0 || CHK_EXCEP(env)) {						\
			if (JNI_TRACE_ON)										\
				printf("jni error: %s (%d)\n", __FILE__, __LINE__); \
			return (__ret__);										\
		}															\
	} while (0)

#define EXCEP_RET_X(env, x)											\
	do {															\
		if (CHK_EXCEP(env)) {										\
			if (JNI_TRACE_ON)										\
				printf("jni error: %s (%d)\n", __FILE__, __LINE__);	\
			return (x);												\
		}															\
	} while (0)

#define FAIL_GOTO(env, label, expr)									\
	do {															\
		if ((expr) == NULL || CHK_EXCEP(env)) {						\
			if (JNI_TRACE_ON)										\
				printf("jni error: %s (%d)\n", __FILE__, __LINE__);	\
			goto label;												\
		}															\
	} while (0)

#define EXCEP_GOTO(env, label)										\
	do {															\
		if (CHK_EXCEP(env)) {										\
			if (JNI_TRACE_ON)										\
				printf("jni error: %s (%d)\n", __FILE__, __LINE__);	\
			goto label;												\
		}															\
	} while (0)


/*
 * internal use. create HashMap array. and put new HashMap object to array
 */
  static jobjectArray new_hash_map_array (JNIEnv * env, int len, int create)
  {
    jclass map_class;
    jobjectArray res;
    int i;
    jmethodID map_constructor_id;

      FAIL_RET_X (env, NULL, map_class =
		  (*env)->FindClass (env, "java/util/HashMap"));
      FAIL_RET_X (env, NULL, map_constructor_id =
		  (*env)->GetMethodID (env, map_class, "<init>", "()V"));
      FAIL_RET_X (env, NULL, res =
		  (*env)->NewObjectArray (env, len, map_class, NULL));

    if (create)
      {
	/* init each element */
	for (i = 0; i < len; i++)
	  {
	    jobject map =
	      (*env)->NewObject (env, map_class, map_constructor_id);
	      FAIL_RET_X (env, NULL, map);
	      (*env)->SetObjectArrayElement (env, res, i, map);
	      EXCEP_RET_X (env, NULL);
	  }
      }

    return res;
  }

  static jobjectArray reduce_array (JNIEnv * env, jobjectArray array)
  {
    int i, offset, len, sum = 0;
    jobjectArray array2;
    jobject obj;

    len = (*env)->GetArrayLength (env, array);
    EXCEP_RET_X (env, NULL);

    for (i = 0; i < len; i++)
      {
	obj = (*env)->GetObjectArrayElement (env, array, i);
	EXCEP_RET_X (env, NULL);

	if (obj != NULL)
	  {
	    sum++;
	  }
      }

    if (sum == len)
      {
	return array;
      }

    FAIL_RET_X (env, NULL, array2 = new_hash_map_array (env, sum, 0));
    offset = 0;

    for (i = 0; i < len; i++)
      {
	obj = (*env)->GetObjectArrayElement (env, array, i);
	EXCEP_RET_X (env, NULL);

	if (obj != NULL)
	  {
	    (*env)->SetObjectArrayElement (env, array2, offset++, obj);
	    EXCEP_RET_X (env, NULL);
	  }
      }

    return array2;
  }

  static int throw_runtime_exception (JNIEnv * env, const char *msg)
  {
    jclass exception_clazz =
      (*env)->FindClass (env, "java/lang/RuntimeException");
    FAIL_RET_X (env, -1, exception_clazz);
    return (*env)->ThrowNew (env, exception_clazz, msg);
  }

  static jobject put_object (JNIEnv * env, jobject map, const char *key,
			     jobject val)
  {
    jclass clazz;
    jmethodID put_method_id;
    jstring key0;

    clazz = (*env)->GetObjectClass (env, map);
    FAIL_RET_X (env, NULL, put_method_id =
		(*env)->GetMethodID (env, clazz, "put",
				     "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;"));
    FAIL_RET_X (env, NULL, key0 = (*env)->NewStringUTF (env, key));
    return (*env)->CallObjectMethod (env, map, put_method_id, key0, val);
  }

  static jobject put_string_utf8 (JNIEnv * env, jobject map, const char *key,
				  const char *val)
  {
    jstring val0;
    FAIL_RET_X (env, NULL, val0 = (*env)->NewStringUTF (env, val));
    return put_object (env, map, key, val0);
  }

  static jobject put_char_as_string (JNIEnv * env, jobject map,
				     const char *key, char val)
  {
    char str[4] = { 0 };
    str[0] = val;
    return put_string_utf8 (env, map, key, str);
  }

  static jobject put_int (JNIEnv * env, jobject map, const char *key, int val)
  {
    jclass clazz;
    jmethodID constructor_id;
    jobject val0;

    FAIL_RET_X (env, NULL, clazz =
		(*env)->FindClass (env, "java/lang/Integer"));
    FAIL_RET_X (env, NULL, constructor_id =
		(*env)->GetMethodID (env, clazz, "<init>", "(I)V"));
    FAIL_RET_X (env, NULL, val0 =
		(*env)->NewObject (env, clazz, constructor_id, val));
    return put_object (env, map, key, val0);
  }

  static jobject put_long (JNIEnv * env, jobject map, const char *key,
			   int64_t val)
  {
    jclass clazz;
    jmethodID constructor_id;
    jobject val0;

    FAIL_RET_X (env, NULL, clazz = (*env)->FindClass (env, "java/lang/Long"));
    FAIL_RET_X (env, NULL, constructor_id =
		(*env)->GetMethodID (env, clazz, "<init>", "(J)V"));
    FAIL_RET_X (env, NULL, val0 =
		(*env)->NewObject (env, clazz, constructor_id, val));
    return put_object (env, map, key, val0);
  }

  static jobject put_float (JNIEnv * env, jobject map, const char *key,
			    float val)
  {
    jclass clazz;
    jmethodID constructor_id;
    jobject val0;

    FAIL_RET_X (env, NULL, clazz =
		(*env)->FindClass (env, "java/lang/Float"));
    FAIL_RET_X (env, NULL, constructor_id =
		(*env)->GetMethodID (env, clazz, "<init>", "(F)V"));
    FAIL_RET_X (env, NULL, val0 =
		(*env)->NewObject (env, clazz, constructor_id, val));
    return put_object (env, map, key, val0);
  }


/*
 * Class:     com_cubrid_jni_BrokerJni
 * Method:    getAllBrokerInfo0
 * Signature: ()[Ljava/util/Map;
 */
  JNIEXPORT jobjectArray JNICALL
    Java_com_cubrid_jni_BrokerJni_getAllBrokerInfo0 (JNIEnv * env,
						     jclass clazz)
  {
    jobjectArray res;
    T_CM_BROKER_INFO_ALL br_info_all;
    int count;
    T_CM_ERROR err_buf;
    int i;

    count = cm_get_broker_info (&br_info_all, &err_buf);

    if (count < 0)
      {
	throw_runtime_exception (env, err_buf.err_msg);
	return NULL;
      }

    FAIL_GOTO (env, fail, res = new_hash_map_array (env, count, 1));

    for (i = 0; i < count; i++)
      {
	T_CM_BROKER_INFO *br_info;
	jobject obj;

	FAIL_GOTO (env, fail, obj =
		   (*env)->GetObjectArrayElement (env, res, i));
	br_info = br_info_all.br_info + i;

	put_string_utf8 (env, obj, "name", br_info->name);
	EXCEP_GOTO (env, fail);
	put_string_utf8 (env, obj, "as_type", br_info->as_type);
	EXCEP_GOTO (env, fail);
	put_int (env, obj, "pid", br_info->pid);
	EXCEP_GOTO (env, fail);
	put_int (env, obj, "port", br_info->port);
	EXCEP_GOTO (env, fail);
	put_int (env, obj, "shm_id", br_info->shm_id);
	EXCEP_GOTO (env, fail);
	put_int (env, obj, "num_as", br_info->num_as);
	EXCEP_GOTO (env, fail);
	put_int (env, obj, "max_as", br_info->max_as);
	EXCEP_GOTO (env, fail);
	put_int (env, obj, "min_as", br_info->min_as);
	EXCEP_GOTO (env, fail);
	put_int (env, obj, "num_job_q", br_info->num_job_q);
	EXCEP_GOTO (env, fail);
	put_int (env, obj, "num_thr", br_info->num_thr);
	EXCEP_GOTO (env, fail);
	put_float (env, obj, "pcpu", br_info->pcpu);
	EXCEP_GOTO (env, fail);
	put_int (env, obj, "cpu_time", br_info->cpu_time);
	EXCEP_GOTO (env, fail);
	put_int (env, obj, "num_busy_count", br_info->num_busy_count);
	EXCEP_GOTO (env, fail);
	put_long (env, obj, "num_req", br_info->num_req);
	EXCEP_GOTO (env, fail);
	put_long (env, obj, "num_tran", br_info->num_tran);
	EXCEP_GOTO (env, fail);
	put_long (env, obj, "num_query", br_info->num_query);
	EXCEP_GOTO (env, fail);
	put_long (env, obj, "num_long_tran", br_info->num_long_tran);
	EXCEP_GOTO (env, fail);
	put_long (env, obj, "num_long_query", br_info->num_long_query);
	EXCEP_GOTO (env, fail);
	put_long (env, obj, "num_error_query", br_info->num_error_query);
	EXCEP_GOTO (env, fail);
	put_int (env, obj, "long_query_time", br_info->long_query_time);
	EXCEP_GOTO (env, fail);
	put_int (env, obj, "long_transaction_time",
		 br_info->long_transaction_time);
	EXCEP_GOTO (env, fail);
	put_string_utf8 (env, obj, "session_timeout",
			 br_info->session_timeout);
	EXCEP_GOTO (env, fail);
	put_int (env, obj, "as_max_size", br_info->as_max_size);
	EXCEP_GOTO (env, fail);
	put_string_utf8 (env, obj, "keep_connection",
			 br_info->keep_connection);
	EXCEP_GOTO (env, fail);
	put_string_utf8 (env, obj, "sql_log_mode", br_info->sql_log_mode);
	EXCEP_GOTO (env, fail);
	put_char_as_string (env, obj, "log_backup", br_info->log_backup);
	EXCEP_GOTO (env, fail);
	put_char_as_string (env, obj, "source_env_flag",
			    br_info->source_env_flag);
	EXCEP_GOTO (env, fail);
	put_char_as_string (env, obj, "access_list_flag",
			    br_info->access_list_flag);
	EXCEP_GOTO (env, fail);
	put_int (env, obj, "time_to_kill", br_info->time_to_kill);
	EXCEP_GOTO (env, fail);
	put_string_utf8 (env, obj, "status", br_info->status);
	EXCEP_GOTO (env, fail);
	put_string_utf8 (env, obj, "auto_add", br_info->auto_add);
	EXCEP_GOTO (env, fail);
	put_string_utf8 (env, obj, "log_dir", br_info->log_dir);
	EXCEP_GOTO (env, fail);
	put_string_utf8 (env, obj, "access_mode", br_info->access_mode);
	EXCEP_GOTO (env, fail);
      }

    cm_broker_info_free (&br_info_all);
    return res;

  fail:
    cm_broker_info_free (&br_info_all);
    return NULL;
  }

/*
 * Class:     com_cubrid_jni_BrokerJni
 * Method:    getAllCasInfo0
 * Signature: (Ljava/lang/String;)[Ljava/util/Map;
 */
  JNIEXPORT jobjectArray JNICALL
    Java_com_cubrid_jni_BrokerJni_getAllCasInfo0 (JNIEnv * env, jclass clazz,
						  jstring name,
						  jboolean only_active)
  {
    jobjectArray res;
    T_CM_CAS_INFO_ALL cas_info_all;
    T_CM_JOB_INFO_ALL dummy;
    int count;
    const char *br_name;
    T_CM_ERROR err_buf;
    int i;

    br_name = (*env)->GetStringUTFChars (env, name, NULL);
    count = cm_get_cas_info (br_name, &cas_info_all, &dummy, &err_buf);
    (*env)->ReleaseStringUTFChars (env, name, br_name);

    if (count < 0)
      {
	throw_runtime_exception (env, err_buf.err_msg);
	return NULL;
      }

    FAIL_GOTO (env, fail, res = new_hash_map_array (env, count, 1));

    for (i = 0; i < count; i++)
      {
	T_CM_CAS_INFO *cas_info;
	jobject obj;

	FAIL_GOTO (env, fail, obj =
		   (*env)->GetObjectArrayElement (env, res, i));
	cas_info = cas_info_all.as_info + i;

	if (only_active && strcasecmp ("ON", cas_info->service_flag) != 0)
	  {
	    (*env)->SetObjectArrayElement (env, res, i, NULL);
	    EXCEP_GOTO (env, fail);
	    continue;
	  }

	put_int (env, obj, "id", cas_info->id);
	EXCEP_GOTO (env, fail);
	put_int (env, obj, "pid", cas_info->pid);
	EXCEP_GOTO (env, fail);
	put_int (env, obj, "num_request", cas_info->num_request);
	EXCEP_GOTO (env, fail);
	put_int (env, obj, "as_port", cas_info->as_port);
	EXCEP_GOTO (env, fail);
	put_string_utf8 (env, obj, "service_flag", cas_info->service_flag);
	EXCEP_GOTO (env, fail);
	put_string_utf8 (env, obj, "status", cas_info->status);
	EXCEP_GOTO (env, fail);
	put_long (env, obj, "last_access_time",
		  cas_info->last_access_time * 1000LL);
	EXCEP_GOTO (env, fail);
	put_int (env, obj, "psize", cas_info->psize);
	EXCEP_GOTO (env, fail);
	put_int (env, obj, "num_thr", cas_info->num_thr);
	EXCEP_GOTO (env, fail);
	put_int (env, obj, "cpu_time", cas_info->cpu_time);
	EXCEP_GOTO (env, fail);
	put_float (env, obj, "pcpu", cas_info->pcpu);
	EXCEP_GOTO (env, fail);
	put_string_utf8 (env, obj, "clt_ip_addr", cas_info->clt_ip_addr);
	EXCEP_GOTO (env, fail);
	put_string_utf8 (env, obj, "clt_appl_name", cas_info->clt_appl_name);
	EXCEP_GOTO (env, fail);
	put_string_utf8 (env, obj, "request_file", cas_info->request_file);
	EXCEP_GOTO (env, fail);
	put_string_utf8 (env, obj, "log_msg", cas_info->log_msg);
	EXCEP_GOTO (env, fail);
	put_string_utf8 (env, obj, "database_name", cas_info->database_name);
	EXCEP_GOTO (env, fail);
	put_string_utf8 (env, obj, "database_host", cas_info->database_host);
	EXCEP_GOTO (env, fail);
	put_long (env, obj, "last_connect_time",
		  cas_info->last_connect_time * 1000LL);
	EXCEP_GOTO (env, fail);
	put_long (env, obj, "num_requests_received",
		  cas_info->num_requests_received);
	EXCEP_GOTO (env, fail);
	put_long (env, obj, "num_queries_processed",
		  cas_info->num_queries_processed);
	EXCEP_GOTO (env, fail);
	put_long (env, obj, "num_transactions_processed",
		  cas_info->num_transactions_processed);
	EXCEP_GOTO (env, fail);
	put_long (env, obj, "num_long_queries", cas_info->num_long_queries);
	EXCEP_GOTO (env, fail);
	put_long (env, obj, "num_long_transactions",
		  cas_info->num_long_transactions);
	EXCEP_GOTO (env, fail);
	put_long (env, obj, "num_error_queries", cas_info->num_error_queries);
	EXCEP_GOTO (env, fail);
      }

    cm_cas_info_free (&cas_info_all, &dummy);

    if (only_active)
      {
	FAIL_RET_X (env, NULL, res = reduce_array (env, res));
      }

    return res;

  fail:
    cm_cas_info_free (&cas_info_all, &dummy);
    return NULL;
  }

/*
 * Class:     com_cubrid_jni_BrokerJni
 * Method:    getAllJobInfo0
 * Signature: (Ljava/lang/String;)[Ljava/util/Map;
 */
  JNIEXPORT jobjectArray JNICALL
    Java_com_cubrid_jni_BrokerJni_getAllJobInfo0 (JNIEnv * env, jclass clazz,
						  jstring name,
						  jboolean only_active)
  {
    jobjectArray res;
    T_CM_JOB_INFO_ALL job_info_all;
    T_CM_CAS_INFO_ALL dummy;
    const char *br_name;
    T_CM_ERROR err_buf;
    int i, ret;

    br_name = (*env)->GetStringUTFChars (env, name, NULL);
    ret = cm_get_cas_info (br_name, &dummy, &job_info_all, &err_buf);
    (*env)->ReleaseStringUTFChars (env, name, br_name);

    if (ret < 0)
      {
	throw_runtime_exception (env, err_buf.err_msg);
	return NULL;
      }

    FAIL_GOTO (env, fail, res =
	       new_hash_map_array (env, job_info_all.num_info, 1));

    for (i = 0; i < job_info_all.num_info; i++)
      {
	T_CM_JOB_INFO *job_info;
	jobject obj;

	FAIL_GOTO (env, fail, obj =
		   (*env)->GetObjectArrayElement (env, res, i));
	job_info = job_info_all.job_info + i;

	put_int (env, obj, "id", job_info->id);
	EXCEP_GOTO (env, fail);
	put_int (env, obj, "priority", job_info->priority);
	EXCEP_GOTO (env, fail);
	put_string_utf8 (env, obj, "ipstr", job_info->ipstr);
	EXCEP_GOTO (env, fail);
	put_long (env, obj, "recv_time", job_info->recv_time * 1000LL);
	EXCEP_GOTO (env, fail);
	put_string_utf8 (env, obj, "script", job_info->script);
	EXCEP_GOTO (env, fail);
	put_string_utf8 (env, obj, "prgname", job_info->prgname);
	EXCEP_GOTO (env, fail);
      }

    cm_cas_info_free (&dummy, &job_info_all);
    return res;

  fail:
    cm_cas_info_free (&dummy, &job_info_all);
    return NULL;
  }

#endif /* SUPPORT_BROKER_JNI */
