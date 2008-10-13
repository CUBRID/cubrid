/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * environment_variable.h : Functions for manipulating the environment variable
 *
 */

#ifndef _ENVIRONMENT_VARIABLE_H_
#define _ENVIRONMENT_VARIABLE_H_

#ident "$Id$"

extern const char *envvar_prefix (void);
extern const char *envvar_root (void);
extern const char *envvar_name (char *, size_t, const char *);
extern const char *envvar_get (const char *);
extern int envvar_set (const char *, const char *);
extern int envvar_expand (const char *, char *, size_t);

#endif /* _ENVIRONMENT_VARIABLE_H_ */
