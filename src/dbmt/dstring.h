/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * dstring.h - 
 */

#ifndef _DSTRING_H_
#define _DSTRING_H_

#ident "$Id$"

typedef struct dstring_t
{
  int dsize;			/* allocated dbuf size */
  int dlen;			/* string length stored in dbuf */
  char *dbuf;
} dstring;

dstring *dst_create (void);
void dst_destroy (dstring * dstr);
void dst_reset (dstring * dstr);
int dst_append (dstring * dstr, const char *str, int slen);
int dst_length (dstring * dstr);
int dst_size (dstring * dstr);
char *dst_buffer (dstring * dstr);

#endif /* _DSTRING_H_ */
