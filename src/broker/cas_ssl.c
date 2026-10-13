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
#if defined (SERVER_MODE)
#include <pthread.h>
#endif

#include "cas_common.h"
#include "cas_common_vars.h"	/* CAS_TLS: session-local SSL state in the merged server (B2-D9) */
#include "cas_log.h"
#include "cas_ssl.h"
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

#define CERTF "cas_ssl_cert.crt"
#define KEYF "cas_ssl_cert.key"
#define CERT_FILENAME_LEN	512
#define ER_SSL_GENERAL		-1
#define ER_CERT_EXPIRED		-2
#define ER_CERT_COPPUPTED	-3
#define SOCKET_NONBLOCK		1
#define SOCKET_BLOCK		0

/* one CAS process serves one connection; in the merged server a session
 * thread does, so the SSL state is session(=thread)-local (B2-D9) */
static CAS_TLS SSL *ssl = NULL;
CAS_TLS bool ssl_client = false;

static int cas_ssl_validity_check (SSL_CTX * ctx);
static SSL_CTX *cas_ssl_load_ctx (int *err_code, time_t * cert_mtime, time_t * key_mtime);
#if defined (SERVER_MODE)
static SSL_CTX *cas_ssl_shared_ctx (int *err_code);
#endif

/* Build a server SSL_CTX from $CUBRID/conf/{cas_ssl_cert.crt,cas_ssl_cert.key}.
 * Returns NULL with *err_code set (and nothing leaked) when the files are
 * missing or unusable. The file mtimes are reported so a caching caller can
 * notice a rotated certificate. */
static SSL_CTX *
cas_ssl_load_ctx (int *err_code, time_t * cert_mtime, time_t * key_mtime)
{
  SSL_CTX *ctx;
  char cert[CERT_FILENAME_LEN];
  char key[CERT_FILENAME_LEN];
  struct stat cert_sbuf, key_sbuf;
  bool cert_not_found, pk_not_found;

  snprintf (cert, CERT_FILENAME_LEN, "%s/conf/%s", getenv ("CUBRID"), CERTF);
  snprintf (key, CERT_FILENAME_LEN, "%s/conf/%s", getenv ("CUBRID"), KEYF);

  cert_not_found = (stat (cert, &cert_sbuf) < 0) ? true : false;
  pk_not_found = (stat (key, &key_sbuf) < 0) ? true : false;

  if (cert_not_found && pk_not_found)
    {
      cas_log_write_and_end (0, false, "SSL: Both the certificate & Private key could not be found: %s, %s", cert, key);
      *err_code = ER_CERT_COPPUPTED;
      return NULL;
    }

  if (cert_not_found)
    {
      cas_log_write_and_end (0, false, "SSL: Certificate not found: %s", cert);
      *err_code = ER_CERT_COPPUPTED;
      return NULL;
    }

  if (pk_not_found)
    {
      cas_log_write_and_end (0, false, "SSL: Private key not found: %s", key);
      *err_code = ER_CERT_COPPUPTED;
      return NULL;
    }

#if OPENSSL_VERSION_NUMBER < 0x10100000L
  SSL_load_error_strings ();
  SSLeay_add_ssl_algorithms ();
  ERR_load_crypto_strings ();
#endif

  if ((ctx = SSL_CTX_new (TLS_server_method ())) == NULL)
    {
      cas_log_write_and_end (0, true, "SSL: Initialize failed.");
      *err_code = ER_SSL_GENERAL;
      return NULL;
    }

  if (SSL_CTX_use_certificate_file (ctx, cert, SSL_FILETYPE_PEM) <= 0
      || SSL_CTX_use_PrivateKey_file (ctx, key, SSL_FILETYPE_PEM) <= 0)
    {
      cas_log_write_and_end (0, true, "SSL: Certificate or Key is coppupted.");
      SSL_CTX_free (ctx);
      *err_code = ER_CERT_COPPUPTED;
      return NULL;
    }

  *cert_mtime = cert_sbuf.st_mtime;
  *key_mtime = key_sbuf.st_mtime;
  return ctx;
}

#if defined (SERVER_MODE)
/* The merged server terminates TLS for every driver connection. Loading the
 * certificate and building an SSL_CTX per connection is pre-authentication
 * work an unauthenticated peer can trigger at will, so the context is shared
 * by the process and only rebuilt when the certificate or key file changes
 * (the legacy per-connection reload picked up a rotated certificate without a
 * restart; the mtime check keeps that behaviour). Live SSL objects hold their
 * own reference, so replacing the shared context never invalidates them. The
 * returned pointer is borrowed: SSL_new takes its own reference. */
static SSL_CTX *
cas_ssl_shared_ctx (int *err_code)
{
  static pthread_mutex_t ctx_lock = PTHREAD_MUTEX_INITIALIZER;
  static SSL_CTX *shared_ctx = NULL;
  static time_t shared_cert_mtime = 0;
  static time_t shared_key_mtime = 0;
  char cert[CERT_FILENAME_LEN];
  char key[CERT_FILENAME_LEN];
  struct stat cert_sbuf, key_sbuf;
  SSL_CTX *ctx = NULL;

  snprintf (cert, CERT_FILENAME_LEN, "%s/conf/%s", getenv ("CUBRID"), CERTF);
  snprintf (key, CERT_FILENAME_LEN, "%s/conf/%s", getenv ("CUBRID"), KEYF);

  pthread_mutex_lock (&ctx_lock);
  if (shared_ctx != NULL && stat (cert, &cert_sbuf) == 0 && stat (key, &key_sbuf) == 0
      && cert_sbuf.st_mtime == shared_cert_mtime && key_sbuf.st_mtime == shared_key_mtime)
    {
      ctx = shared_ctx;
    }
  else
    {
      time_t cert_mtime = 0, key_mtime = 0;
      SSL_CTX *fresh = cas_ssl_load_ctx (err_code, &cert_mtime, &key_mtime);
      if (fresh != NULL)
	{
	  if (shared_ctx != NULL)
	    {
	      SSL_CTX_free (shared_ctx);
	    }
	  shared_ctx = fresh;
	  shared_cert_mtime = cert_mtime;
	  shared_key_mtime = key_mtime;
	  ctx = shared_ctx;
	}
    }
  pthread_mutex_unlock (&ctx_lock);
  return ctx;
}
#endif /* SERVER_MODE */

int
cas_init_ssl (int sd)
{
  SSL_CTX *ctx;
  int err_code = 0;
  unsigned long err;
#if !defined (SERVER_MODE)
  time_t cert_mtime = 0, key_mtime = 0;
#endif

  if (ssl)
    {
      SSL_free (ssl);
      ssl = NULL;
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
#if defined (SERVER_MODE)
  ctx = cas_ssl_shared_ctx (&err_code);
#else
  ctx = cas_ssl_load_ctx (&err_code, &cert_mtime, &key_mtime);
#endif
  if (ctx == NULL)
    {
      return err_code;
    }

  /* the certificate's validity window is checked per connection even with a
   * shared context, matching the legacy per-connection check */
  if ((err_code = cas_ssl_validity_check (ctx)) < 0)
    {
      cas_log_write (0, true, "SSL: Certificate validity error (%s)",
		     err_code == ER_CERT_EXPIRED ? "Expired" : "Unknow");
#if !defined (SERVER_MODE)
      SSL_CTX_free (ctx);
#endif
      return err_code;
    }


  if ((ssl = SSL_new (ctx)) == NULL)
    {
      cas_log_write_and_end (0, true, "SSL: Creating SSL context failed.");
#if !defined (SERVER_MODE)
      SSL_CTX_free (ctx);
#endif
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

  /* SERVER_MODE: the context is the process-shared one above (the SSL holds
   * its own reference). A CAS process keeps its private context until exit,
   * as before. */

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
