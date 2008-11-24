/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution. 
 *
 *   This program is free software; you can redistribute it and/or modify 
 *   it under the terms of the GNU General Public License as published by 
 *   the Free Software Foundation; version 2 of the License. 
 *
 *  This program is distributed in the hope that it will be useful, 
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of 
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the 
 *  GNU General Public License for more details. 
 *
 *  You should have received a copy of the GNU General Public License 
 *  along with this program; if not, write to the Free Software 
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA 
 *
 */


/*
 * recovery_cl.h
 * 									       
 * 	Overview: RECOVERY FUNCTIONS (AT CLIENT) -- Interface --	       
 * 									       
 */

#ifndef _RECOVERY_CL_H_
#define _RECOVERY_CL_H_

#ident "$Id$"

#include "config.h"

#include "log_comm.h"
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

#endif /* _RECOVERY_CL_H_ */
