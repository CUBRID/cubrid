/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * t_string.h - 
 */

#ifndef	_T_STRING_H_
#define	_T_STRING_H_

#ident "$Id$"

typedef struct t_string T_STRING;
struct t_string
{
  char *data;
  int data_len;
  int alloc_size;
};


T_STRING *t_string_make (int init_size);
void t_string_clear (T_STRING * t_str);
int t_string_add (T_STRING * t_str, char *str, int str_len);
void t_string_free (T_STRING * t_str);
char *t_string_str (T_STRING * t_str);
int t_string_len (T_STRING * t_str);

#endif /* _T_STRING_H_ */
