/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * cci_t_set.h - 
 */

#ifndef	_CCI_T_SET_H_
#define	_CCI_T_SET_H_

#ident "$Id$"

/************************************************************************
 * IMPORTED SYSTEM HEADER FILES						*
 ************************************************************************/

/************************************************************************
 * IMPORTED OTHER HEADER FILES						*
 ************************************************************************/

#include "cci_handle_mng.h"

/************************************************************************
 * EXPORTED DEFINITIONS							*
 ************************************************************************/

/************************************************************************
 * EXPORTED TYPE DEFINITIONS						*
 ************************************************************************/

typedef struct
{
  char type;
  int num_element;
  void **element;
  void *data_buf;
  int data_size;
  T_VALUE_BUF conv_value_buffer;
} T_SET;

/************************************************************************
 * EXPORTED FUNCTION PROTOTYPES						*
 ************************************************************************/

extern void t_set_free (T_SET *);
extern int t_set_size (T_SET *);
extern int t_set_element_type (T_SET *);
extern int t_set_get (T_SET * set,
		      int index, T_CCI_A_TYPE a_type, void *value, int *ind);
extern int t_set_alloc (T_SET ** out_set);
extern int t_set_make (T_SET * set,
		       char ele_type, int size, void *value, int *ind);
extern int t_set_decode (T_SET * set);
extern int t_set_to_str (T_SET * set, T_VALUE_BUF * conv_val);

/************************************************************************
 * EXPORTED VARIABLES							*
 ************************************************************************/

#endif /* _CCI_T_SET_H_ */
