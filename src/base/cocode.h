/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * cocode.h : condition code definitions
 *
 */

#ifndef _COCODE_H_
#define _COCODE_H_

#ident "$Id$"

/* Constants for encoding and decoding CO_CODE values. */
#define CO_MAX_CODE         1024	/* Max codes per module */
#define CO_MAX_MODULE       INT_MAX/CO_MAX_CODE	/* Max module identifier */

#define CO_CODE(MODULE, CODE)           \
  -((int)MODULE * (int)CO_MAX_CODE + (int)CODE - (int)1)

/* co module names */
typedef enum
{
  CO_MODULE_CO = 1,
  CO_MODULE_MTS = 2,
  CO_MODULE_SYS = 13,
  CO_MODULE_CNV = 27,
  CO_MODULE_ARGS = 1000
} CO_MODULE;

#endif /* _COCODE_H_ */
