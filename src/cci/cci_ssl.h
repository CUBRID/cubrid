/*
 * Copyright (C) 2016 CUBRID Corporation
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
