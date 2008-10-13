/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * fbo_class.h -
 * TODO: rename this file to fbo_class.h
 */

#ifndef _FBO_CLASS_H_
#define _FBO_CLASS_H_

#ident "$Id$"

#if defined(WINDOWS)
#include <io.h>
#endif

extern int esm_create (char *pathname);
extern void esm_destroy (char *pathname);
extern int esm_get_size (char *pathname);
extern int esm_read (char *pathname, const off_t offset, const int size,
		     char *buffer);
extern int esm_write (char *pathname, const off_t offset, const int size,
		      char *buffer);
extern int esm_insert (char *pathname, const off_t offset, const int size,
		       char *buffer);
extern int esm_delete (char *pathname, off_t offset, const int size);
extern int esm_truncate (char *pathname, const int offset);
extern int esm_append (char *pathname, const int size, char *buffer);

#endif /* _FBO_CLASS_H_ */
