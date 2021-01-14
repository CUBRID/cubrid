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
#ifndef UC_ADMIN_HEADER_INCLUDED
#define UC_ADMIN_HEADER_INCLUDED

#define DLL_EXPORT	__declspec(dllexport)

typedef enum {
  AS_STATUS_IDLE,
  AS_STATUS_BUSY,
  AS_STATUS_RESTART
} T_AS_STATUS;

typedef struct {
  int		pid;
  int		num_request;
  int		as_port;
  T_AS_STATUS	status;
  int		last_access_time;
  char		clt_ip_addr[32];
  char		clt_appl_name[256];
  char		request_file[256];
  char		log_msg[1024];
} T_AS_INFO;

extern int DLL_EXPORT uc_start();
extern int DLL_EXPORT uc_stop(char *err_msg);
extern int DLL_EXPORT uc_add(char *br_name, char *err_msg);
extern int DLL_EXPORT uc_restart(char *br_name, int as_index, char *err_msg);
extern int DLL_EXPORT uc_drop(char *br_name, char *err_msg);
extern int DLL_EXPORT uc_on(char *br_name, char *err_msg);
extern int DLL_EXPORT uc_off(char *br_name, char *err_msg);
extern int DLL_EXPORT uc_as_info(char *br_name, T_AS_INFO *as_info, int num_as_info, char *err_msg);

#endif /* UC_ADMIN_HEADER_INCLUDED */
