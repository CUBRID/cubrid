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
 * cas_ssl.h -
 */

#ifndef _CAS_SSL_H_
#define _CAS_SSL_H_

extern bool ssl_client;

extern int cas_init_ssl (int sd);
extern int cas_ssl_read (int sd, char *buf, int size);
extern int cas_ssl_write (int sd, const char *buf, int size);
extern void cas_ssl_close (int client_sock_fd);
extern bool is_ssl_data_ready (int sock_fd);

#endif /* _CAS_SSL_H_ */
