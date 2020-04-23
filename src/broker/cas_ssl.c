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
 * cas_ssl.c -
 */

#ident "$Id$"

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <assert.h>

#if defined(WINDOWS)
#include <winsock2.h>
#include <windows.h>
#include <process.h>
#include <sys/timeb.h>
#include <dbgHelp.h>
#else /* WINDOWS */
#include <unistd.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/time.h>
#endif /* WINDOWS */

#include "cas_common.h"
#include "cas.h"
#include "cas_network.h"
#include "cas_function.h"
#include "cas_net_buf.h"
#include "cas_log.h"
#include "cas_util.h"
#include "broker_filename.h"
#include "cas_execute.h"

#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/bio.h>

#if defined(WINDOWS)
#else
#include <unistd.h>
#include <fcntl.h>
#endif

#define CERTF "cas_ssl_cert.crt"
#define KEYF "cas_ssl_cert.key"
#define CERT_FILENAME_LEN 512
#define ER_SSL_GENERAL -1

static SSL *ssl = NULL;
bool ssl_client = false;

int
initSSL (int sd)
{
  SSL_CTX *ctx;
  char cert[CERT_FILENAME_LEN];
  char key[CERT_FILENAME_LEN];
  int err_code;
  unsigned long err;

  if (ssl)
    {
      SSL_free (ssl);
    }

#if defined(WINDOWS)
  u_long argp;
  argp = 0;
  ioctlsocket (sd, FIONBIO, &argp);
#else
  int flags, oflags;
  flags = fcntl (sd, F_GETFL, 0);
  oflags = flags;
  flags = flags & ~O_NONBLOCK;

  fcntl (sd, F_SETFL, flags);
#endif
  snprintf (cert, CERT_FILENAME_LEN, "%s/conf/%s", getenv ("CUBRID"), CERTF);
  snprintf (key, CERT_FILENAME_LEN, "%s/conf/%s", getenv ("CUBRID"), KEYF);

  SSL_load_error_strings ();
  SSLeay_add_ssl_algorithms ();
  ERR_load_crypto_strings ();

  if ((ctx = SSL_CTX_new (TLSv1_2_server_method ())) == NULL)
    {
      return ER_SSL_GENERAL;
    }

  if (SSL_CTX_use_certificate_file (ctx, cert, SSL_FILETYPE_PEM) <= 0
      || SSL_CTX_use_PrivateKey_file (ctx, key, SSL_FILETYPE_PEM) <= 0)
    {
      return ER_SSL_GENERAL;
    }

  if ((ssl = SSL_new (ctx)) == NULL)
    {
      return ER_SSL_GENERAL;
    }

  if (SSL_set_fd (ssl, sd) == 0)
    {
      return ER_SSL_GENERAL;
    }

  err_code = SSL_accept (ssl);
  if (err_code < 0)
    {
      err_code = SSL_get_error (ssl, err_code);
      err = ERR_get_error ();
      return ER_SSL_GENERAL;
    }

#if defined (WINDOWS)
  argp = 1;
  ioctlsocket (sd, FIONBIO, &argp);
#else
  fcntl (sd, F_SETFL, oflags);
#endif

  ssl_client = true;

  return 0;
}

int
cas_ssl_read (int sd, char *buf, int size)
{
  int nread;

  if (IS_INVALID_SOCKET (sd) || ssl == NULL)
    {
      return ER_SSL_GENERAL;
    }

#if defined(WINDOWS)
  u_long argp;
  argp = 0;
  ioctlsocket (sd, FIONBIO, &argp);
#else
  int flags, oflags;
  flags = fcntl (sd, F_GETFL, 0);
  oflags = flags;
  flags = flags & ~O_NONBLOCK;
  fcntl (sd, F_SETFL, flags);
#endif

  nread = SSL_read (ssl, buf, size);

#if defined(WINDOWS)
  argp = 1;
  ioctlsocket (sd, FIONBIO, &argp);
#else
  fcntl (sd, F_SETFL, oflags);
#endif
  return nread;
}

int
cas_ssl_write (int sd, const char *buf, int size)
{
  int nwrite;

  if (IS_INVALID_SOCKET (sd) || ssl == NULL)
    {
      return ER_SSL_GENERAL;
    }
#if defined(WINDOWS)
  u_long argp;
  argp = 0;
  ioctlsocket (sd, FIONBIO, &argp);
#else
  int flags, oflags;
  flags = fcntl (sd, F_GETFL, 0);
  oflags = flags;
  flags = flags & ~O_NONBLOCK;
  fcntl (sd, F_SETFL, flags);
#endif

  nwrite = SSL_write (ssl, buf, size);

#if defined(WINDOWS)
  argp = 1;
  ioctlsocket (sd, FIONBIO, &argp);
#else
  fcntl (sd, F_SETFL, oflags);
#endif
  return nwrite;
}

void
cas_ssl_close (int client_sock_fd)
{
  if (ssl)
    {
      SSL_free (ssl);
      ssl = NULL;
    }
}
