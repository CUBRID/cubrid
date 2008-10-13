/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * setup.h - Setup Module Interface Header
 *           This file contains exported stuffs from the Library - Setup Module.
 */

#ifndef	_SETUP_H_
#define	_SETUP_H_

#ident "$Id$"

#ifdef WIN32
typedef struct t_setup_env T_SETUP_ENV;
struct t_setup_env
{
  int env_buf_size;
  char *env_buf;
  char *remote_addr_str;
  char *out_filename_str;
  char *doc_root_str;
  char *v3_doc_root_str;
  char *appl_root_str;
  char *upload_temp_dir;
  char *upload_delimiter;
  char *set_delimiter_str;
};
#endif

extern int uw_init_env (const char *appl_name);
extern void uw_final_env (void);
extern int uw_connect_client (void);
extern void uw_disconnect_client (void);

#ifndef UNITCLSH
extern int uw_sock_buf_init (void);
extern void uw_sock_buf_flush (void);
extern int uw_write_to_client (char *buf, int size);
extern int uw_read_from_client (char *buf, int size);
#endif

#ifdef WIN32
extern T_SETUP_ENV setup_env;
#endif

#endif /* _SETUP_H_ */
