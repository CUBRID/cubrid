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
#include <io.h>
#include <direct.h>
#else /* WINDOWS */
#include <unistd.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
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

#include <openssl/crypto.h>
#include <openssl/x509.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

#if defined(WINDOWS)
#else
#include <unistd.h>
#include <fcntl.h>
#endif

#define CERTF "cas_ssl_cert.crt"
#define KEYF "cas_ssl_cert.key"
#define CERT_FILENAME_LEN	512
#define ER_SSL_GENERAL		-1
#define ER_CERT_EXPIRED		-2
#define ER_CERT_COPPUPTED	-3
#define SOCKET_NONBLOCK		1
#define SOCKET_BLOCK		0

static SSL *ssl = NULL;
bool ssl_client = false;

static int cas_ssl_validity_check (SSL_CTX * ctx);

int
cas_init_ssl (int sd)
{
  SSL_CTX *ctx;
  char cert[CERT_FILENAME_LEN];
  char key[CERT_FILENAME_LEN];
  int err_code;
  unsigned long err;
  struct stat sbuf;
  bool cert_not_found, pk_not_found;

  if (ssl)
    {
      SSL_free (ssl);
    }

#if defined(WINDOWS)
  u_long argp = SOCKET_BLOCK;
  ioctlsocket (sd, FIONBIO, &argp);
#else
  int oflags, flags = fcntl (sd, F_GETFL, 0);
  oflags = flags;
  flags = flags & ~O_NONBLOCK;

  fcntl (sd, F_SETFL, flags);

#endif
  snprintf (cert, CERT_FILENAME_LEN, "%s/conf/%s", getenv ("CUBRID"), CERTF);
  snprintf (key, CERT_FILENAME_LEN, "%s/conf/%s", getenv ("CUBRID"), KEYF);

  cert_not_found = (stat (cert, &sbuf) < 0) ? true : false;
  pk_not_found = (stat (key, &sbuf) < 0) ? true : false;

  if (cert_not_found && pk_not_found)
    {
      cas_log_write_and_end (0, false, "SSL: Both the certificate & Private key could not be found: %s, %s", cert, key);
      return ER_CERT_COPPUPTED;
    }

  if (cert_not_found)
    {
      cas_log_write_and_end (0, false, "SSL: Certificate not found: %s", cert);
      return ER_CERT_COPPUPTED;
    }

  if (pk_not_found)
    {
      cas_log_write_and_end (0, false, "SSL: Private key not found: %s", key);
      return ER_CERT_COPPUPTED;
    }

#if OPENSSL_VERSION_NUMBER < 0x10100000L
  SSL_load_error_strings ();
  SSLeay_add_ssl_algorithms ();
  ERR_load_crypto_strings ();
#endif

  if ((ctx = SSL_CTX_new (TLS_server_method ())) == NULL)
    {
      cas_log_write_and_end (0, true, "SSL: Initialize failed.");
      return ER_SSL_GENERAL;
    }

  if (SSL_CTX_use_certificate_file (ctx, cert, SSL_FILETYPE_PEM) <= 0
      || SSL_CTX_use_PrivateKey_file (ctx, key, SSL_FILETYPE_PEM) <= 0)
    {
      cas_log_write_and_end (0, true, "SSL: Certificate or Key is coppupted.");
      return ER_CERT_COPPUPTED;
    }

  if ((err_code = cas_ssl_validity_check (ctx)) < 0)
    {
      cas_log_write (0, true, "SSL: Certificate validity error (%s)",
		     err_code == ER_CERT_EXPIRED ? "Expired" : "Unknow");
      return err_code;
    }


  if ((ssl = SSL_new (ctx)) == NULL)
    {
      cas_log_write_and_end (0, true, "SSL: Creating SSL context failed.");
      SSL_CTX_free (ctx);
      return ER_SSL_GENERAL;
    }

  if (SSL_set_fd (ssl, sd) == 0)
    {
      cas_log_write_and_end (0, true, "SSL: Cannot associate with socket.");
      SSL_free (ssl);
      ssl = NULL;
      return ER_SSL_GENERAL;
    }

  err_code = SSL_accept (ssl);
  if (err_code < 0)
    {
      err_code = SSL_get_error (ssl, err_code);
      err = ERR_get_error ();
      cas_log_write_and_end (0, true, "SSL: Accept failed - '%s'", ERR_error_string (err, NULL));
      SSL_free (ssl);
      ssl = NULL;
      return ER_SSL_GENERAL;
    }

#if defined (WINDOWS)
  argp = SOCKET_NONBLOCK;
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
      cas_log_write_and_end (0, true, "SSL: READ attempt for brokern connection");
      return ER_SSL_GENERAL;
    }

#if defined(WINDOWS)
  u_long argp = SOCKET_BLOCK;
  ioctlsocket (sd, FIONBIO, &argp);
#else
  int oflags, flags = fcntl (sd, F_GETFL, 0);
  oflags = flags;
  flags = flags & ~O_NONBLOCK;
  fcntl (sd, F_SETFL, flags);
#endif

  nread = SSL_read (ssl, buf, size);

#if defined(WINDOWS)
  argp = SOCKET_NONBLOCK;
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
      cas_log_write_and_end (0, true, "SSL: WRITE attempt for brokern connection");
      return ER_SSL_GENERAL;
    }
#if defined(WINDOWS)
  u_long argp = SOCKET_BLOCK;
  ioctlsocket (sd, FIONBIO, &argp);
#else
  int oflags, flags = fcntl (sd, F_GETFL, 0);
  oflags = flags;
  flags = flags & ~O_NONBLOCK;
  fcntl (sd, F_SETFL, flags);
#endif

  nwrite = SSL_write (ssl, buf, size);

#if defined(WINDOWS)
  argp = SOCKET_NONBLOCK;
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

static int
cas_ssl_validity_check (SSL_CTX * ctx)
{
  ASN1_TIME *not_before, *not_after;
  X509 *crt;

  crt = SSL_CTX_get0_certificate (ctx);

  if (crt == NULL)
    return ER_SSL_GENERAL;

  not_after = X509_getm_notAfter (crt);
  if (X509_cmp_time (not_after, NULL) != 1)
    {
      return ER_CERT_EXPIRED;
    }

  not_before = X509_getm_notBefore (crt);
  if (X509_cmp_time (not_before, NULL) != -1)
    {
      return ER_SSL_GENERAL;
    }

  return 0;
}

bool
is_ssl_data_ready (int sock_fd)
{
  return (SSL_has_pending (ssl) == 1 ? true : false);
}
