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
 * condition_handler_code.h : condition code definitions
 *
 */

#ifndef _CONDITION_HANDLER_CODE_H_
#define _CONDITION_HANDLER_CODE_H_

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

#endif /* _CONDITION_HANDLER_CODE_H_ */
