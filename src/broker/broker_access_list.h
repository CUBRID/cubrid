/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution. 
 *
 *   This program is free software; you can redistribute it and/or modify 
 *   it under the terms of the GNU General Public License as published by 
 *   the Free Software Foundation; either version 2 of the License, or 
 *   (at your option) any later version. 
 *
 *  This program is distributed in the hope that it will be useful, 
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of 
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the 
 *  GNU General Public License for more details. 
 *
 *  You should have received a copy of the GNU General Public License 
 *  along with this program; if not, write to the Free Software 
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA 
 *
 */


/*
 * broker_access_list.h - 
 */

#ifndef	_BROKER_ACCESS_LIST_H_
#define	_BROKER_ACCESS_LIST_H_

#ident "$Id$"

typedef struct t_ip T_IP;
struct t_ip
{
  unsigned char ip[4];
  unsigned char ip_length;
};

typedef struct t_acl T_ACL;
struct t_acl
{
  int num_acl;
  T_IP *acl;
};

int uw_acl_make (char *acl_file);
int uw_acl_check (unsigned char *ip_addr);

extern T_ACL *v3_acl;

#endif /* _BROKER_ACCESS_LIST_H_ */
