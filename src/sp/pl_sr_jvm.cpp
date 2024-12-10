/*
 *
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
 * pl_sr_jvm.cpp - PL Server Module Source related to setup JVM
 */

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

#include "pl_sr.h"
#include "pl_file.h"
#include "pl_comm.h"

#include "boot_sr.h"
#include "environment_variable.h"
#include "system_parameter.h"
#include "release_string.h"
#include "memory_alloc.h"
#include "error_manager.h"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

#if defined(WINDOWS)
#if __WORDSIZE == 32
#define JVM_LIB_PATH_JDK "jre\\bin\\client"
#define JVM_LIB_PATH_JRE "bin\\client"
#define JVM_LIB_PATH_JDK11 ""	/* JDK 11 does not support for Windows x64 */
#else
#define JVM_LIB_PATH_JDK "jre\\bin\\server"
#define JVM_LIB_PATH_JRE "bin\\server"
#define JVM_LIB_PATH_JDK11 "bin\\server"
#endif
#else
#define JVM_LIB_PATH "jre/lib/amd64/server"
#define JVM_LIB_PATH_JDK11 "lib/server"
#endif

#if defined(WINDOWS)
#define JVM_LIB_FILE "libjvm.dll"
#else
#define JVM_LIB_FILE "libjvm.so"
#endif

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
static std::string err_msgs;

#if defined(WINDOWS)
int get_java_root_path (char *path);
FARPROC WINAPI delay_load_hook (unsigned dliNotify, PDelayLoadInfo pdli);
LONG WINAPI delay_load_dll_exception_filter (PEXCEPTION_POINTERS pep);

extern PfnDliHook __pfnDliNotifyHook2 = delay_load_hook;
extern PfnDliHook __pfnDliFailureHook2 = delay_load_hook;

#else /* WINDOWS */
static void *pl_get_create_java_vm_function_ptr (void);
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
 * pl_get_create_java_vm_func_ptr
 *   return: return java vm function pointer
 *
 * Note:
 */

static void *
pl_get_create_java_vm_function_ptr ()
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
 * pl_create_java_vm
 *   return: create java vm
 *
 * Note:
 */
static int
pl_create_java_vm (JNIEnv **env_p, JavaVMInitArgs *vm_arguments)
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
  CREATE_VM_FUNC create_vm_func = (CREATE_VM_FUNC) pl_get_create_java_vm_function_ptr ();
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
 * pl_tokenize_jvm_options
 *  return: tokenized array of string
 *
 */
static std::vector <std::string>
pl_tokenize_jvm_options (char *opt_str)
{
  std::string str (opt_str);
  std::istringstream iss (str);
  std::vector <std::string> options;
  std::copy (std::istream_iterator <std::string> (iss),
	     std::istream_iterator <std::string> (), std::back_inserter (options));
  return options;
}

/*
 * pl_jvm_options
 *  return: jvm options
 *
 */
static std::vector <std::string>
pl_jvm_options ()
{
  char buffer[PATH_MAX];

  envvar_javadir_file (buffer, PATH_MAX, "");
  std::string pl_file_path (buffer);

  std::vector <std::string> options;

#ifndef NDEBUG
  // enable assertions in PL Server
  options.push_back ("-ea"); // must be the first option in order not to override ones specified by the user
#endif // !NDEBUG

  // defaults
  options.push_back ("-Djava.awt.headless=true");
  options.push_back ("-Dfile.encoding=UTF-8");

  // CBRD-25364: Prevent JVM crash caused by libzip
  // Added the following option as a default until the minimum JDK version is upgraded
  options.push_back ("-Dsun.zip.disableMemoryMapping=true");

  //
  options.push_back ("-Djava.class.path=" + pl_file_path + "pl_server.jar");
  options.push_back ("-Djava.util.logging.config.file=" + pl_file_path + "logging.properties");

  int debug_port = prm_get_integer_value (PRM_ID_JAVA_STORED_PROCEDURE_DEBUG);
  if (debug_port != -1)
    {
      options.push_back ("-Xdebug");
      options.push_back ("-agentlib:jdwp=transport=dt_socket,server=y,suspend=n,address=" + std::to_string (debug_port));
    }

  char *jvm_opt_sysprm = (char *) prm_get_string_value (PRM_ID_JAVA_STORED_PROCEDURE_JVM_OPTIONS);
  if (jvm_opt_sysprm != NULL)
    {
      std::vector <std::string> ext_opts = pl_tokenize_jvm_options (jvm_opt_sysprm);
      options.insert (options.end(), ext_opts.begin(), ext_opts.end());
    }

  return options;
}

/*
 * pl_start_jvm_server -
 *   return: Error Code
 *   db_name(in): db name
 *   path(in): path
 *
 * Note:
 */

int
pl_start_jvm_server (const char *db_name, const char *path, int port)
{
  jint res;
  jclass cls, string_cls;
  JNIEnv *env_p = NULL;
  jmethodID mid;
  jstring jstr_dbname, jstr_path, jstr_version, jstr_envroot, jstr_port, jstr_uds_path;
  jobjectArray args;
  JavaVMInitArgs vm_arguments;
  JavaVMOption *options;
  int vm_n_options = 0;
  const char *envroot;
  const char *uds_path;
  char *loc_p, *locale;
  const bool is_uds_mode = (port == PL_PORT_UDS_MODE);
  {
    if (jvm != NULL)
      {
	return ER_SP_ALREADY_EXIST;	/* already created */
      }

    envroot = envvar_root ();
    uds_path = (is_uds_mode) ? pl_get_socket_file_path (db_name) : "";

    std::vector <std::string> opts = pl_jvm_options ();

    vm_n_options = (int) opts.size ();
    options = new JavaVMOption[vm_n_options];
    if (options == NULL)
      {
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 0);
	goto exit;
      }

    int idx = 0;
    for (auto it = opts.begin (); it != opts.end (); ++it)
      {
	options[idx++].optionString = const_cast <char *> (it->c_str ());
      }

    vm_arguments.version = JNI_VERSION_1_6;
    vm_arguments.nOptions = vm_n_options;
    vm_arguments.options = options;
    vm_arguments.ignoreUnrecognized = JNI_TRUE;

    locale = NULL;
    loc_p = setlocale (LC_TIME, NULL);
    if (loc_p != NULL)
      {
	locale = strdup (loc_p);
      }

    res = pl_create_java_vm (&env_p, &vm_arguments);
    delete[] options;

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
	goto exit;
      }

    mid = JVM_GetStaticMethodID (env_p, cls, "main", "([Ljava/lang/String;)V");
    if (mid == NULL)
      {
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_CANNOT_START_JVM, 1,
		"GetStaticMethodID: " "com/cubrid/jsp/Server.main([Ljava/lang/String;)V");
	goto exit;
      }

    jstr_dbname = JVM_NewStringUTF (env_p, db_name);
    if (jstr_dbname == NULL)
      {
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_CANNOT_START_JVM, 1,
		"Failed to construct a new 'java.lang.String object' by NewStringUTF()");
	goto exit;
      }

    jstr_path = JVM_NewStringUTF (env_p, path);
    if (jstr_path == NULL)
      {
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_CANNOT_START_JVM, 1,
		"Failed to construct a new 'java.lang.String object' by NewStringUTF()");
	goto exit;
      }

    jstr_version = JVM_NewStringUTF (env_p, rel_build_number ());
    if (jstr_version == NULL)
      {
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_CANNOT_START_JVM, 1,
		"Failed to construct a new 'java.lang.String object' by NewStringUTF()");
	goto exit;
      }

    jstr_envroot = JVM_NewStringUTF (env_p, envroot);
    if (jstr_envroot == NULL)
      {
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_CANNOT_START_JVM, 1,
		"Failed to construct a new 'java.lang.String object' by NewStringUTF()");
	goto exit;
      }

    jstr_uds_path = JVM_NewStringUTF (env_p, uds_path);
    if (jstr_uds_path == NULL)
      {
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_CANNOT_START_JVM, 1,
		"Failed to construct a new 'java.lang.String object' by NewStringUTF()");
	goto exit;
      }

    char port_str[6] = { 0 };
    sprintf (port_str, "%d", port);
    jstr_port = JVM_NewStringUTF (env_p, port_str);
    if (jstr_port == NULL)
      {
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_CANNOT_START_JVM, 1,
		"Failed to construct a new 'java.lang.String object' by NewStringUTF()");
	goto exit;
      }

    string_cls = JVM_FindClass (env_p, "java/lang/String");
    if (string_cls == NULL)
      {
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_CANNOT_START_JVM, 1, "FindClass: " "java/lang/String");
	goto exit;
      }

    args = JVM_NewObjectArray (env_p, 6, string_cls, NULL);
    if (args == NULL)
      {
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_CANNOT_START_JVM, 1,
		"Failed to construct a new java array by NewObjectArray()");
	goto exit;
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
	goto exit;
      }
  }

exit:
#if defined (SA_MODE)
  if (jvm != NULL)
    {
      JVM_DetachCurrentThread (jvm);
    }
#endif

  return er_errid ();
}

/*
 * pl_server_port
 *   return: if jsp is disabled return -2 (PL_PORT_DISABLED)
 *           else if jsp is UDS mode return -1
 *           else return a port (TCP mode)
 *
 * Note:
 */

int
pl_server_port (void)
{
  return sp_port;
}

/*
 * pl_server_port_from_info
 *   return: if jsp is disabled return -2 (PL_PORT_DISABLED)
 *           else if jsp is UDS mode return -1
 *           else return a port (TCP mode)
 *
 *
 * Note:
 */

int
pl_server_port_from_info (void)
{
#if defined (SERVER_MODE)
  // check $CUBRID/var/pl_<db_name>.info
  if (sp_port != PL_PORT_DISABLED)
    {
      PL_SERVER_INFO pl_info {-1, -1};
      pl_read_info (boot_db_name (), pl_info);
      sp_port = pl_info.port;
    }
#endif
  return sp_port;
}

/*
 * pl_jvm_is_loaded
 *   return: if disable jsp function and return false
 *              enable jsp function and return not false
 *
 * Note:
 */

int
pl_jvm_is_loaded (void)
{
  return jvm != NULL;
}
