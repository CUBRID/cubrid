/*
 * Copyright 2008 Search Solution Corporation
 * Copyright 2016 CUBRID Corporation
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 */


/*
 * broker_access_list.h -
 */

#ifndef	_BROKER_ACCESS_LIST_H_
#define	_BROKER_ACCESS_LIST_H_

#ident "$Id$"

#define IPV4_LENGTH_MAX         4

typedef struct t_ip T_IP;
struct t_ip
{
  unsigned char ip[IPV4_LENGTH_MAX];
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
