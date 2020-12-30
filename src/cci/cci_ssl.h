/*
 * 
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
 * cci_ssl.h -
 */

#ifndef	_CCI_SSL_H_
#define	_CCI_SSL_H_

#ident "$Id$"

#include <stdio.h>
#include <stdlib.h>

#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/opensslv.h>

#if defined(WINDOWS)
#include <winsock2.h>
#else
#include <sys/socket.h>
#include "netinet/in.h"
#include "porting.h"
#endif

#define NON_USESSL      0
#define USESSL          1

extern SSL_CTX *create_ssl_ctx ();
extern SSL *create_ssl (SOCKET srv_sock_fd, SSL_CTX * ctx);
extern int connect_ssl (SSL * ssl);
extern void cleanup_ssl (SSL * ssl);
extern void cleanup_ssl_ctx (SSL_CTX * ctx);

#endif /* _CCI_SSL_H_ */
