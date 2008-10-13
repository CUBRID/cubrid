/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * uw_acl.h - 
 */

#ifndef	_UW_ACL_H_
#define	_UW_ACL_H_

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

#endif /* _UW_ACL_H_ */
