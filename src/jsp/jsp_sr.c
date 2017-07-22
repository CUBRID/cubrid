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

#include "jsp_sr.h"

#include "environment_variable.h"
#include "system_parameter.h"
#include "release_string.h"
#include "memory_alloc.h"
#include "error_manager.h"

#if defined(sparc)
#define JVM_LIB_PATH "jre/lib/sparc/client"
#elif defined(WINDOWS)
#if __WORDSIZE == 32
#define JVM_LIB_PATH_JDK "jre\\bin\\client"
#define JVM_LIB_PATH_JRE "bin\\client"
#else
#define JVM_LIB_PATH_JDK "jre\\bin\\server"
#define JVM_LIB_PATH_JRE "bin\\server"
#endif
#elif defined(HPUX) && defined(IA64)
#define JVM_LIB_PATH "jre/lib/IA64N/hotspot"
#elif defined(HPUX) && !defined(IA64)
#define JVM_LIB_PATH "jre/lib/PA_RISC2.0/hotspot"
#elif defined(AIX)
#if __WORDSIZE == 32
#define JVM_LIB_PATH "jre/bin/classic"
#elif __WORDSIZE == 64
#define JVM_LIB_PATH "jre/lib/ppc64/classic"
#endif
#elif defined(__i386) || defined(__x86_64)
#if __WORDSIZE == 32
#define JVM_LIB_PATH "jre/lib/i386/client"
#else
#define JVM_LIB_PATH "jre/lib/amd64/server"
#endif
#else /* ETC */
#define JVM_LIB_PATH ""
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

#ifdef __cplusplus
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
#define JVM_CallStaticIntMethod(ENV, CLAZZ, METHODID, ARGS)	\
	(ENV)->CallStaticIntMethod(CLAZZ, METHODID, ARGS)
#else
#define JVM_FindClass(ENV, NAME)	\
	(*ENV)->FindClass(ENV, NAME)
#define JVM_GetStaticMethodID(ENV, CLAZZ, NAME, SIG)	\
	(*ENV)->GetStaticMethodID(ENV, CLAZZ, NAME, SIG)
#define JVM_NewStringUTF(ENV, BYTES)	\
	(*ENV)->NewStringUTF(ENV, BYTES);
#define JVM_NewObjectArray(ENV, LENGTH, ELEMENTCLASS, INITIALCLASS)	\
	(*ENV)->NewObjectArray(ENV, LENGTH, ELEMENTCLASS, INITIALCLASS)
#define JVM_SetObjectArrayElement(ENV, ARRAY, INDEX, VALUE)	\
	(*ENV)->SetObjectArrayElement(ENV, ARRAY, INDEX, VALUE)
#define JVM_CallStaticIntMethod(ENV, CLAZZ, METHODID, ARGS)	\
	(*ENV)->CallStaticIntMethod(ENV, CLAZZ, METHODID, ARGS)
#endif

JavaVM *jvm = NULL;
jint sp_port = -1;

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
	char *java_home = NULL, *tmp = NULL, *tail;
	char jvm_lib_path[BUF_SIZE];
	void *libVM;

	java_home = getenv ("JAVA_HOME");
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
	    sprintf (jvm_lib_path, "%s\\%s\\jvm.dll", java_home, tail);
	    libVM = LoadLibrary (jvm_lib_path);

	    if (libVM)
	      {
		fp = (FARPROC) (HMODULE) libVM;
	      }
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
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_JVM_LIB_NOT_FOUND, 1, "jvm.dll");
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
jsp_get_create_java_vm_function_ptr (void)
{
  char *java_home = NULL;
  char jvm_library_path[PATH_MAX];
  void *libVM_p;

  libVM_p = dlopen (JVM_LIB_FILE, RTLD_LAZY | RTLD_GLOBAL);
  if (libVM_p == NULL)
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_SP_JVM_LIB_NOT_FOUND, 1, dlerror ());

      java_home = getenv ("JAVA_HOME");
      if (java_home != NULL)
	{
	  snprintf (jvm_library_path, PATH_MAX - 1, "%s/%s/%s", java_home, JVM_LIB_PATH, JVM_LIB_FILE);
	  libVM_p = dlopen (jvm_library_path, RTLD_LAZY | RTLD_GLOBAL);

	  if (libVM_p == NULL)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_JVM_LIB_NOT_FOUND, 1, dlerror ());
	      return NULL;
	    }
	}
      else
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_JVM_LIB_NOT_FOUND, 1, dlerror ());
	  return NULL;
	}
    }

  return dlsym (libVM_p, "JNI_CreateJavaVM");
}

#endif /* !WINDOWS */

/*
 * jsp_start_server -
 *   return: Error Code
 *   db_name(in): db name
 *   path(in): path
 *
 * Note:
 */

int
jsp_start_server (const char *db_name, const char *path)
{
  JNIEnv *env_p = NULL;
  jint res;
  jclass cls, string_cls;
  jmethodID mid;
  jstring jstr_dbname, jstr_path, jstr_version, jstr_envroot;
  jobjectArray args;
  JavaVMInitArgs vm_arguments;
  JavaVMOption options[3];
  char classpath[PATH_MAX + 32], logging_prop[PATH_MAX + 32];
  char *loc_p, *locale;
  const char *envroot;
  char jsp_file_path[PATH_MAX];
  char optionString2[] = "-Xrs";
  CREATE_VM_FUNC create_vm_func = NULL;

  if (prm_get_bool_value (PRM_ID_JAVA_STORED_PROCEDURE) == false)
    {
      return NO_ERROR;
    }

  if (jvm != NULL)
    {
      return NO_ERROR;		/* already created */
    }

  envroot = envvar_root ();
  if (envroot == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_CANNOT_START_JVM, 1, "envvar_root");
      return ER_SP_CANNOT_START_JVM;
    }

  snprintf (classpath, sizeof (classpath) - 1, "-Djava.class.path=%s",
	    envvar_javadir_file (jsp_file_path, PATH_MAX, "jspserver.jar"));

  snprintf (logging_prop, sizeof (logging_prop) - 1, "-Djava.util.logging.config.file=%s",
	    envvar_javadir_file (jsp_file_path, PATH_MAX, "logging.properties"));

  options[0].optionString = classpath;
  options[1].optionString = logging_prop;
  options[2].optionString = optionString2;
  vm_arguments.version = JNI_VERSION_1_4;
  vm_arguments.options = options;
  vm_arguments.nOptions = 3;
  vm_arguments.ignoreUnrecognized = JNI_TRUE;

  locale = NULL;
  loc_p = setlocale (LC_TIME, NULL);
  if (loc_p != NULL)
    {
      locale = strdup (loc_p);
    }

#if defined(WINDOWS)

  __try
  {
    res = JNI_CreateJavaVM (&jvm, (void **) &env_p, &vm_arguments);
  }
  __except (delay_load_dll_exception_filter (GetExceptionInformation ()))
  {
    res = -1;
  }

#else /* WINDOWS */

  create_vm_func = (CREATE_VM_FUNC) jsp_get_create_java_vm_function_ptr ();
  if (create_vm_func)
    {
      res = (*create_vm_func) (&jvm, (void **) &env_p, &vm_arguments);
    }
  else
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_JVM_LIB_NOT_FOUND, 1, dlerror ());
      if (locale != NULL)
	{
	  free (locale);
	}
      return er_errid ();
    }

#endif /* !WINDOWS */

  setlocale (LC_TIME, locale);
  if (locale != NULL)
    {
      free (locale);
    }

  if (res < 0)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_CANNOT_START_JVM, 1, "JNI_CreateJavaVM");
      jvm = NULL;
      return er_errid ();
    }

  cls = JVM_FindClass (env_p, "com/cubrid/jsp/Server");
  if (cls == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_CANNOT_START_JVM, 1, "FindClass: " "com/cubrid/jsp/Server");
      goto error;
    }

  mid = JVM_GetStaticMethodID (env_p, cls, "start", "([Ljava/lang/String;)I");
  if (mid == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_CANNOT_START_JVM, 1, "GetStaticMethodID");
      goto error;
    }

  jstr_dbname = JVM_NewStringUTF (env_p, db_name);
  if (jstr_dbname == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_CANNOT_START_JVM, 1, "NewStringUTF");
      goto error;
    }

  jstr_path = JVM_NewStringUTF (env_p, path);
  if (jstr_path == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_CANNOT_START_JVM, 1, "NewStringUTF");
      goto error;
    }

  jstr_version = JVM_NewStringUTF (env_p, rel_build_number ());
  if (jstr_version == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_CANNOT_START_JVM, 1, "NewStringUTF");
      goto error;
    }

  jstr_envroot = JVM_NewStringUTF (env_p, envvar_root ());
  if (jstr_envroot == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_CANNOT_START_JVM, 1, "NewStringUTF");
      goto error;
    }

  string_cls = JVM_FindClass (env_p, "java/lang/String");
  if (string_cls == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_CANNOT_START_JVM, 1, "FindClass: " "java/lang/String");
      goto error;
    }

  args = JVM_NewObjectArray (env_p, 4, string_cls, NULL);
  if (args == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_CANNOT_START_JVM, 1, "NewObjectArray");
      goto error;
    }

  JVM_SetObjectArrayElement (env_p, args, 0, jstr_dbname);
  JVM_SetObjectArrayElement (env_p, args, 1, jstr_path);
  JVM_SetObjectArrayElement (env_p, args, 2, jstr_version);
  JVM_SetObjectArrayElement (env_p, args, 3, jstr_envroot);

  sp_port = JVM_CallStaticIntMethod (env_p, cls, mid, args);

  return 0;

error:
  jsp_stop_server ();
  jvm = NULL;

  assert (er_errid () != NO_ERROR);
  return er_errid ();
}

/*
 * jsp_stop_server
 *   return: 0
 *
 * Note:
 */

int
jsp_stop_server (void)
{
  return NO_ERROR;
}

/*
 * jsp_server_port
 *   return: if disable jsp function and return -1
 *              enable jsp function and return jsp server port
 *
 * Note:
 */

int
jsp_server_port (void)
{
  return sp_port;
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
