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
 * environment_variable.h : Functions for manipulating the environment variable
 *
 */

#ifndef _ENVIRONMENT_VARIABLE_H_
#define _ENVIRONMENT_VARIABLE_H_

#ident "$Id$"

#ifdef __cplusplus
extern "C"
{
#endif

  extern const char *envvar_prefix (void);
  extern const char *envvar_root (void);
  extern const char *envvar_name (char *, size_t, const char *);
  extern const char *envvar_get (const char *);
  extern int envvar_set (const char *, const char *);
  extern int envvar_expand (const char *, char *, size_t);

  extern char *envvar_bindir_file (char *path, size_t size, const char *filename);
  extern char *envvar_libdir_file (char *path, size_t size, const char *filename);
  extern char *envvar_javadir_file (char *path, size_t size, const char *filename);
  extern char *envvar_localedir_file (char *path, size_t size, const char *langpath, const char *filename);
  extern char *envvar_confdir_file (char *path, size_t size, const char *filename);
  extern char *envvar_vardir_file (char *path, size_t size, const char *filename);
  extern char *envvar_tmpdir_file (char *path, size_t size, const char *filename);
  extern char *envvar_logdir_file (char *path, size_t size, const char *filename);
  extern void envvar_trim_char (char *var, const int c);
  extern char *envvar_ldmldir_file (char *path, size_t size, const char *filename);
  extern char *envvar_codepagedir_file (char *path, size_t size, const char *filename);
  extern char *envvar_localedatadir_file (char *path, size_t size, const char *filename);
  extern char *envvar_loclib_dir_file (char *path, size_t size, const char *filename);
  extern char *envvar_cubrid_dir (char *path, size_t size);
  extern char *envvar_tzdata_dir_file (char *path, size_t size, const char *filename);

#ifdef __cplusplus
}
#endif

#endif				/* _ENVIRONMENT_VARIABLE_H_ */
