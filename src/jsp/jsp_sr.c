/*
 * Copyright 2008 Search Solution Corporation
 * Copyright 2016 CUBRID Corporation
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 */

/*
 * jsp_sr.c - Java Stored Procedure Server Module Source
 */

#ident "$Id$"

#include "config.h"

#if defined(WINDOWS)
#include <windows.h>
#define DELAYIMP_INSECURE_WRITABLE_HOOKS
#include <Delayimp.h>
#pragma comment(lib, "delayimp")
#pragma comment(lib, "jvm")
#else /* WINDOWS */
#include <dlfcn.h>
#endif /* !WINDOWS */

#include <jni.h>
#include <locale.h>
#include <assert.h>
#include <vector>
#include <string>
#include <sstream>
#include <iterator>

#include "jsp_sr.h"
#include "jsp_file.h"
#include "jsp_comm.h"

#include "boot_sr.h"
#include "environment_variable.h"
#include "system_parameter.h"
#include "release_string.h"
#include "memory_alloc.h"
#include "error_manager.h"
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

#if defined(sparc)
#define JVM_LIB_PATH "jre/lib/sparc/client"
#define JVM_LIB_PATH_JDK11 "lib/server"
#elif defined(WINDOWS)
#if __WORDSIZE == 32
#define JVM_LIB_PATH_JDK "jre\\bin\\client"
#define JVM_LIB_PATH_JRE "bin\\client"
#define JVM_LIB_PATH_JDK11 ""	/* JDK 11 does not support for Windows x64 */
#else
#define JVM_LIB_PATH_JDK "jre\\bin\\server"
#define JVM_LIB_PATH_JRE "bin\\server"
#define JVM_LIB_PATH_JDK11 "bin\\server"
#endif
#elif defined(HPUX) && defined(IA64)
#define JVM_LIB_PATH "jre/lib/IA64N/hotspot"
#define JVM_LIB_PATH_JDK11 "lib/IA64N/server"
#elif defined(HPUX) && !defined(IA64)
#define JVM_LIB_PATH "jre/lib/PA_RISC2.0/hotspot"
#define JVM_LIB_PATH_JDK11 "lib/PA_RISC2.0/server"
#elif defined(AIX)
#if __WORDSIZE == 32
#define JVM_LIB_PATH "jre/bin/classic"
#define JVM_LIB_PATH_JDK11 "lib/server"
#elif __WORDSIZE == 64
#define JVM_LIB_PATH "jre/lib/ppc64/classic"
#define JVM_LIB_PATH_JDK11 "lib/server"
#endif
#elif defined(__i386) || defined(__x86_64)
#if __WORDSIZE == 32
#define JVM_LIB_PATH "jre/lib/i386/client"
#define JVM_LIB_PATH_JDK11 "lib/server"
#else
#define JVM_LIB_PATH "jre/lib/amd64/server"
#define JVM_LIB_PATH_JDK11 "lib/server"
#endif
#else /* ETC */
#define JVM_LIB_PATH ""
#define JVM_LIB_PATH_JDK11 ""
#endif /* ETC */

#if !defined(WINDOWS)
#if defined(AIX)
#define JVM_LIB_FILE "libjvm.so"
#elif defined(HPUX) && !defined(IA64)
#define JVM_LIB_FILE "libjvm.sl"
#else /* not AIX , not ( HPUX && (not IA64)) */
#define JVM_LIB_FILE "libjvm.so"
#endif /* not AIX , not ( HPUX && (not IA64)) */
#endif /* !WINDOWS */

#if defined(WINDOWS)
#define REGKEY_JAVA     "Software\\JavaSoft\\Java Runtime Environment"
#endif /* WINDOWS */

#define BUF_SIZE        2048
typedef jint (*CREATE_VM_FUNC) (JavaVM **, void **, void *);

#define JVM_GetEnv(JVM, ENV, VER)	\
	(JVM)->GetEnv(ENV, VER)
#define JVM_AttachCurrentThread(JVM, ENV, ARGS)	\
	(JVM)->AttachCurrentThread(ENV, ARGS)
#define JVM_DetachCurrentThread(JVM)	\
	(JVM)->DetachCurrentThread()
#define JVM_ExceptionOccurred(ENV)	\
	(ENV)->ExceptionOccurred()
#define JVM_FindClass(ENV, NAME)	\
	(ENV)->FindClass(NAME)
#define JVM_GetStaticMethodID(ENV, CLAZZ, NAME, SIG)	\
	(ENV)->GetStaticMethodID(CLAZZ, NAME, SIG)
#define JVM_NewStringUTF(ENV, BYTES)	\
	(ENV)->NewStringUTF(BYTES);
#define JVM_NewObjectArray(ENV, LENGTH, ELEMENTCLASS, INITIALCLASS)	\
	(ENV)->NewObjectArray(LENGTH, ELEMENTCLASS, INITIALCLASS)
#define JVM_SetObjectArrayElement(ENV, ARRAY, INDEX, VALUE)	\
	(ENV)->SetObjectArrayElement(ARRAY, INDEX, VALUE)
#define JVM_CallStaticVoidMethod(ENV, CLAZZ, METHODID, ARGS)	\
	(ENV)->CallStaticVoidMethod(CLAZZ, METHODID, ARGS)
#define JVM_CallStaticIntMethod(ENV, CLAZZ, METHODID, ARGS)	\
	(ENV)->CallStaticIntMethod(CLAZZ, METHODID, ARGS)
#define JVM_CallStaticObjectMethod(ENV, CLAZZ, METHODID, ARGS)	\
	(ENV)->CallStaticObjectMethod(CLAZZ, METHODID, ARGS)
#define JVM_GetStringUTF(ENV, STRING)	\
	(ENV)->GetStringUTFChars(STRING, NULL)
#define JVM_ReleaseStringUTF(ENV, JSTRING, CSTRING)	\
	(ENV)->ReleaseStringUTFChars(JSTRING, CSTRING)
#define JVM_GetStringUTFLength(ENV, STRING)	\
	(ENV)->GetStringUTFLength(STRING)

static JavaVM *jvm = NULL;
static jint sp_port = -1;
// *INDENT-OFF*
static std::string err_msgs;
// *INDENT-ON*

#if defined(WINDOWS)
int get_java_root_path (char *path);
FARPROC WINAPI delay_load_hook (unsigned dliNotify, PDelayLoadInfo pdli);
LONG WINAPI delay_load_dll_exception_filter (PEXCEPTION_POINTERS pep);

extern PfnDliHook __pfnDliNotifyHook2 = delay_load_hook;
extern PfnDliHook __pfnDliFailureHook2 = delay_load_hook;

#else /* WINDOWS */
static void *jsp_get_create_java_vm_function_ptr (void);
#endif /* !WINDOWS */

#if defined(WINDOWS)

/*
 * get_java_root_path()
 *   return: return FALSE on error othrewise true
 *   path(in/out): get java root path
 *
 * Note:
 */

int
get_java_root_path (char *path)
{
  DWORD rc;
  DWORD len;
  DWORD dwType;
  char currentVersion[16];
  char regkey_java_current_version[BUF_SIZE];
  char java_root_path[BUF_SIZE];
  HKEY hKeyReg;

  if (!path)
    {
      return false;
    }

  rc = RegOpenKeyEx (HKEY_LOCAL_MACHINE, REGKEY_JAVA, 0, KEY_QUERY_VALUE, &hKeyReg);
  if (rc != ERROR_SUCCESS)
    {
      return false;
    }

  len = sizeof (currentVersion);
  rc = RegQueryValueEx (hKeyReg, "CurrentVersion", 0, &dwType, (LPBYTE) currentVersion, &len);

  if (hKeyReg)
    {
      RegCloseKey (hKeyReg);
    }

  if (rc != ERROR_SUCCESS)
    {
      return false;
    }

  hKeyReg = NULL;
  sprintf (regkey_java_current_version, "%s\\%s", REGKEY_JAVA, currentVersion);
  rc = RegOpenKeyEx (HKEY_LOCAL_MACHINE, regkey_java_current_version, 0, KEY_QUERY_VALUE, &hKeyReg);

  if (rc != ERROR_SUCCESS)
    {
      return false;
    }

  len = sizeof (java_root_path);
  rc = RegQueryValueEx (hKeyReg, "JavaHome", 0, &dwType, (LPBYTE) java_root_path, &len);

  if (hKeyReg)
    {
      RegCloseKey (hKeyReg);
    }

  if (rc != ERROR_SUCCESS)
    {
      return false;
    }

  strcpy (path, java_root_path);
  return true;
}

/*
 * delay_load_hook -
 *   return:
 *   dliNotify(in):
 *   pdli(in):
 *
 * Note:
 */

FARPROC WINAPI
delay_load_hook (unsigned dliNotify, PDelayLoadInfo pdli)
{
  FARPROC fp = NULL;

  switch (dliNotify)
    {
    case dliFailLoadLib:
      {
	char *java_home = NULL, *jvm_path = NULL, *tmp = NULL, *tail;
	void *libVM = NULL;

	jvm_path = getenv ("JVM_PATH");
	java_home = getenv ("JAVA_HOME");

	if (jvm_path)
	  {
	    err_msgs.append ("\n\tFailed to load libjvm from 'JVM_PATH' environment variable: ");
	    err_msgs.append ("\n\t\t");
	    err_msgs.append (jvm_path);

	    libVM = LoadLibrary (jvm_path);
	    if (libVM)
	      {
		fp = (FARPROC) (HMODULE) libVM;
		return fp;
	      }
	  }
	else
	  {
	    err_msgs.append ("\n\tFailed to get 'JVM_PATH' environment variable");
	  }

	tail = JVM_LIB_PATH_JDK;
	if (java_home == NULL)
	  {
	    tmp = (char *) malloc (BUF_SIZE);
	    if (tmp)
	      {
		if (get_java_root_path (tmp))
		  {
		    java_home = tmp;
		    tail = JVM_LIB_PATH_JRE;
		  }
	      }
	  }

	if (java_home)
	  {
	    err_msgs.append ("\n\tFailed to load libjvm from 'JAVA_HOME' environment variable: ");

	    char jvm_lib_path[BUF_SIZE];
	    sprintf (jvm_lib_path, "%s\\%s\\jvm.dll", java_home, tail);

	    err_msgs.append ("\n\t\t");
	    err_msgs.append (jvm_lib_path);

	    libVM = LoadLibrary (jvm_lib_path);

	    if (libVM)
	      {
		fp = (FARPROC) (HMODULE) libVM;
	      }
	    else
	      {
		tail = JVM_LIB_PATH_JDK11;

		memset (jvm_lib_path, BUF_SIZE, 0);
		sprintf (jvm_lib_path, "%s\\%s\\jvm.dll", java_home, tail);

		err_msgs.append ("\n\t\t");
		err_msgs.append (jvm_lib_path);

		libVM = LoadLibrary (jvm_lib_path);
		fp = (FARPROC) (HMODULE) libVM;
	      }
	  }
	else
	  {
	    err_msgs.append ("\n\tFailed to get 'JAVA_HOME' environment variable");
	  }

	if (tmp)
	  {
	    free_and_init (tmp);
	  }
      }
      break;

    default:
      break;
    }

  return fp;
}

/*
 * delay_load_dll_exception_filter -
 *   return:
 *   pep(in):
 *
 * Note:
 */

LONG WINAPI
delay_load_dll_exception_filter (PEXCEPTION_POINTERS pep)
{
  switch (pep->ExceptionRecord->ExceptionCode)
    {
    case VcppException (ERROR_SEVERITY_ERROR, ERROR_MOD_NOT_FOUND):
    case VcppException (ERROR_SEVERITY_ERROR, ERROR_PROC_NOT_FOUND):
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_JVM_LIB_NOT_FOUND, 1, err_msgs.c_str ());
      break;

    default:
      break;
    }

  return EXCEPTION_EXECUTE_HANDLER;
}

#else /* WINDOWS */

/*
 * jsp_get_create_java_vm_func_ptr
 *   return: return java vm function pointer
 *
 * Note:
 */

static void *
jsp_get_create_java_vm_function_ptr ()
{
  void *libVM_p = NULL;

  char *jvm_path = getenv ("JVM_PATH");
  if (jvm_path != NULL)
    {
      libVM_p = dlopen (jvm_path, RTLD_NOW | RTLD_LOCAL);
      if (libVM_p != NULL)
	{
	  return dlsym (libVM_p, "JNI_CreateJavaVM");
	}
      else
	{
	  err_msgs.append ("\n\tFailed to load libjvm from 'JVM_PATH' environment variable: ");
	  err_msgs.append ("\n\t\t");
	  err_msgs.append (dlerror ());
	}
    }
  else
    {
      err_msgs.append ("\n\tFailed to get 'JVM_PATH' environment variable");
    }

  char *java_home = getenv ("JAVA_HOME");
  if (java_home != NULL)
    {
      char jvm_library_path[PATH_MAX];
      err_msgs.append ("\n\tFailed to load libjvm from 'JAVA_HOME' environment variable: ");

      // under jdk 11
      snprintf (jvm_library_path, PATH_MAX - 1, "%s/%s/%s", java_home, JVM_LIB_PATH, JVM_LIB_FILE);
      libVM_p = dlopen (jvm_library_path, RTLD_NOW | RTLD_LOCAL);
      if (libVM_p != NULL)
	{
	  return dlsym (libVM_p, "JNI_CreateJavaVM");
	}
      else
	{
	  err_msgs.append ("\n\t\t");
	  err_msgs.append (dlerror ());
	}

      // from jdk 11
      snprintf (jvm_library_path, PATH_MAX - 1, "%s/%s/%s", java_home, JVM_LIB_PATH_JDK11, JVM_LIB_FILE);
      libVM_p = dlopen (jvm_library_path, RTLD_NOW | RTLD_LOCAL);
      if (libVM_p != NULL)
	{
	  return dlsym (libVM_p, "JNI_CreateJavaVM");
	}
      else
	{
	  err_msgs.append ("\n\t\t");
	  err_msgs.append (dlerror ());
	}
    }
  else
    {
      err_msgs.append ("\n\tFailed to get 'JAVA_HOME' environment variable");
    }

  return NULL;
}

#endif /* !WINDOWS */


/*
 * jsp_create_java_vm
 *   return: create java vm
 *
 * Note:
 */
static int
jsp_create_java_vm (JNIEnv ** env_p, JavaVMInitArgs * vm_arguments)
{
  int res;
#if defined(WINDOWS)
  __try
  {
    res = JNI_CreateJavaVM (&jvm, (void **) env_p, vm_arguments);
  }
  __except (delay_load_dll_exception_filter (GetExceptionInformation ()))
  {
    res = -1;
  }
#else /* WINDOWS */
  CREATE_VM_FUNC create_vm_func = (CREATE_VM_FUNC) jsp_get_create_java_vm_function_ptr ();
  if (create_vm_func)
    {
      res = (*create_vm_func) (&jvm, (void **) env_p, (void *) vm_arguments);
    }
  else
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_JVM_LIB_NOT_FOUND, 1, err_msgs.c_str ());
      res = -1;
    }
#endif /* WINDOWS */
  err_msgs.clear ();
  return res;
}

/*
 * jsp_tokenize_jvm_options
 *  return: tokenized array of string
 *
 */

// *INDENT-OFF*
static std::vector <std::string>
jsp_tokenize_jvm_options (char *opt_str)
{
  std::string str (opt_str);
  std::istringstream iss (str);
  std::vector <std::string> options;
  std::copy (std::istream_iterator <std::string> (iss),
	     std::istream_iterator <std::string> (), std::back_inserter (options));
  return options;
}
// *INDENT-ON*

/*
 * jsp_start_server -
 *   return: Error Code
 *   db_name(in): db name
 *   path(in): path
 *
 * Note:
 */

int
jsp_start_server (const char *db_name, const char *path, int port)
{
  jint res;
  jclass cls, string_cls;
  JNIEnv *env_p = NULL;
  jmethodID mid;
  jstring jstr_dbname, jstr_path, jstr_version, jstr_envroot, jstr_port, jstr_uds_path;
  jobjectArray args;
  JavaVMInitArgs vm_arguments;
  JavaVMOption *options;
  int vm_n_default_options = 2;
  int vm_n_ext_options = 0;
  char classpath[PATH_MAX + 32] = { 0 };
  char logging_prop[PATH_MAX + 32] = { 0 };
  char option_debug[70];
  char debug_flag[] = "-Xdebug";
  char debug_jdwp[] = "-agentlib:jdwp=transport=dt_socket,server=y,address=%d,suspend=n";
  const char *envroot;
  const char *uds_path;
  char jsp_file_path[PATH_MAX];
  char port_str[6] = { 0 };
  char *loc_p, *locale;
  char *jvm_opt_sysprm = NULL;
  int debug_port = -1;
  const bool is_uds_mode = (port == JAVASP_PORT_UDS_MODE);
  {
    if (jvm != NULL)
      {
	return ER_SP_ALREADY_EXIST;	/* already created */
      }

    envroot = envvar_root ();

    if (is_uds_mode)
      {
	uds_path = jsp_get_socket_file_path (db_name);
      }
    else
      {
	uds_path = "";
      }

    snprintf (classpath, sizeof (classpath) - 1, "-Djava.class.path=%s",
	      envvar_javadir_file (jsp_file_path, PATH_MAX, "jspserver.jar"));

    snprintf (logging_prop, sizeof (logging_prop) - 1, "-Djava.util.logging.config.file=%s",
	      envvar_javadir_file (jsp_file_path, PATH_MAX, "logging.properties"));

    debug_port = prm_get_integer_value (PRM_ID_JAVA_STORED_PROCEDURE_DEBUG);
    if (debug_port != -1)
      {
	vm_n_default_options += 2;	/* set debug flag and debugging port */
      }

    jvm_opt_sysprm = (char *) prm_get_string_value (PRM_ID_JAVA_STORED_PROCEDURE_JVM_OPTIONS);
  // *INDENT-OFF*
  std::vector <std::string> opts = jsp_tokenize_jvm_options (jvm_opt_sysprm);
  // *INDENT-ON*
    vm_n_ext_options += (int) opts.size ();
    options = new JavaVMOption[vm_n_default_options + vm_n_ext_options];
    if (options == NULL)
      {
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 0);
	goto error;
      }

    int ext_idx = vm_n_default_options;
    options[0].optionString = classpath;
    options[1].optionString = logging_prop;
    if (debug_port != -1)
      {
	snprintf (option_debug, sizeof (option_debug) - 1, debug_jdwp, debug_port);
	options[2].optionString = debug_flag;
	options[3].optionString = option_debug;
      }

    for (auto it = opts.begin (); it != opts.end (); ++it)
      {
      // *INDENT-OFF*
      options[ext_idx++].optionString = const_cast <char*> (it->c_str ());
      // *INDENT-ON*
      }

    vm_arguments.version = JNI_VERSION_1_6;
    vm_arguments.nOptions = vm_n_default_options + vm_n_ext_options;
    vm_arguments.options = options;
    vm_arguments.ignoreUnrecognized = JNI_TRUE;

    locale = NULL;
    loc_p = setlocale (LC_TIME, NULL);
    if (loc_p != NULL)
      {
	locale = strdup (loc_p);
      }

    res = jsp_create_java_vm (&env_p, &vm_arguments);
  // *INDENT-OFF*
  delete[] options;
  // *INDENT-ON*

#if !defined(WINDOWS)
    if (er_has_error ())
      {
	if (locale != NULL)
	  {
	    free (locale);
	  }
	return er_errid ();
      }
#endif

    setlocale (LC_TIME, locale);
    if (locale != NULL)
      {
	free (locale);
      }

    if (res < 0)
      {
	jvm = NULL;
	return er_errid ();
      }

    cls = JVM_FindClass (env_p, "com/cubrid/jsp/Server");
    if (cls == NULL)
      {
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_CANNOT_START_JVM, 1, "FindClass: " "com/cubrid/jsp/Server");
	goto error;
      }

    mid = JVM_GetStaticMethodID (env_p, cls, "main", "([Ljava/lang/String;)V");
    if (mid == NULL)
      {
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_CANNOT_START_JVM, 1,
		"GetStaticMethodID: " "com/cubrid/jsp/Server.main([Ljava/lang/String;)V");
	goto error;
      }

    jstr_dbname = JVM_NewStringUTF (env_p, db_name);
    if (jstr_dbname == NULL)
      {
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_CANNOT_START_JVM, 1,
		"Failed to construct a new 'java.lang.String object' by NewStringUTF()");
	goto error;
      }

    jstr_path = JVM_NewStringUTF (env_p, path);
    if (jstr_path == NULL)
      {
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_CANNOT_START_JVM, 1,
		"Failed to construct a new 'java.lang.String object' by NewStringUTF()");
	goto error;
      }

    jstr_version = JVM_NewStringUTF (env_p, rel_build_number ());
    if (jstr_version == NULL)
      {
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_CANNOT_START_JVM, 1,
		"Failed to construct a new 'java.lang.String object' by NewStringUTF()");
	goto error;
      }

    jstr_envroot = JVM_NewStringUTF (env_p, envroot);
    if (jstr_envroot == NULL)
      {
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_CANNOT_START_JVM, 1,
		"Failed to construct a new 'java.lang.String object' by NewStringUTF()");
	goto error;
      }

    jstr_uds_path = JVM_NewStringUTF (env_p, uds_path);
    if (jstr_uds_path == NULL)
      {
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_CANNOT_START_JVM, 1,
		"Failed to construct a new 'java.lang.String object' by NewStringUTF()");
	goto error;
      }

    sprintf (port_str, "%d", port);
    jstr_port = JVM_NewStringUTF (env_p, port_str);
    if (jstr_port == NULL)
      {
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_CANNOT_START_JVM, 1,
		"Failed to construct a new 'java.lang.String object' by NewStringUTF()");
	goto error;
      }

    string_cls = JVM_FindClass (env_p, "java/lang/String");
    if (string_cls == NULL)
      {
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_CANNOT_START_JVM, 1, "FindClass: " "java/lang/String");
	goto error;
      }

    args = JVM_NewObjectArray (env_p, 6, string_cls, NULL);
    if (args == NULL)
      {
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_CANNOT_START_JVM, 1,
		"Failed to construct a new java array by NewObjectArray()");
	goto error;
      }

    JVM_SetObjectArrayElement (env_p, args, 0, jstr_dbname);
    JVM_SetObjectArrayElement (env_p, args, 1, jstr_path);
    JVM_SetObjectArrayElement (env_p, args, 2, jstr_version);
    JVM_SetObjectArrayElement (env_p, args, 3, jstr_envroot);
    JVM_SetObjectArrayElement (env_p, args, 4, jstr_uds_path);
    JVM_SetObjectArrayElement (env_p, args, 5, jstr_port);

    sp_port = JVM_CallStaticIntMethod (env_p, cls, mid, args);
    if (JVM_ExceptionOccurred (env_p))
      {
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_CANNOT_START_JVM, 1,
		"Error occured while starting Java SP Server by CallStaticIntMethod()");
	goto error;
      }

    return NO_ERROR;
  }
error:
  assert (er_errid () != NO_ERROR);
  return er_errid ();
}

/*
 * jsp_server_port
 *   return: if jsp is disabled return -2 (JAVASP_PORT_DISABLED)
 *           else if jsp is UDS mode return -1
 *           else return a port (TCP mode)
 *
 * Note:
 */

int
jsp_server_port (void)
{
  return sp_port;
}

/*
 * jsp_server_port_from_info
 *   return: if jsp is disabled return -2 (JAVASP_PORT_DISABLED)
 *           else if jsp is UDS mode return -1
 *           else return a port (TCP mode)
 * 
 *
 * Note:
 */

int
jsp_server_port_from_info (void)
{
#if defined (SA_MODE)
  return sp_port;
#else
  // check $CUBRID/var/javasp_<db_name>.info
// *INDENT-OFF*
  JAVASP_SERVER_INFO jsp_info {-1, -1};
// *INDENT-ON*
  javasp_read_info (boot_db_name (), jsp_info);
  return sp_port = jsp_info.port;
#endif
}

/*
 * jsp_jvm_is_loaded
 *   return: if disable jsp function and return false
 *              enable jsp function and return not false
 *
 * Note:
 */

int
jsp_jvm_is_loaded (void)
{
  return jvm != NULL;
}
