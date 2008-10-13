/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * rvcl.h
 * 									       
 * 	Overview: RECOVERY FUNCTIONS (AT CLIENT) -- Interface --	       
 * See .c file for overview and description of the interface functions.	       
 * 									       
 */

#ifndef RVCL_HEADER_
#define RVCL_HEADER_

#ident "$Id$"

#include "config.h"

#include "logcp.h"
#include "error_manager.h"

#define RVMM_INTERFACE 0

struct rvcl_fun
{
  LOG_RCVCLIENT_INDEX rcvclient_index;	/* For verification   */
  int (*undofun) (int length, char *data);	/* Undo function      */
  int (*redofun) (int length, char *data);	/* Undo function      */
  void (*dump_undofun) (int length, void *data);	/* Dump undo function */
  void (*dump_redofun) (int length, void *data);	/* Dump redo function */
};

extern struct rvcl_fun RVCL_fun[];

extern const char *rv_rcvcl_index_string (LOG_RCVCLIENT_INDEX rcvindex);

#endif
