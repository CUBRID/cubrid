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
#include <time.h>

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

#include "cas_common.h"
#include "cas_log.h"
#include "cas_ssl.h"

#define CERT_FILENAME_LEN	512
#define ER_SSL_GENERAL		-1
#define ER_CERT_EXPIRED		-2
#define ER_CERT_CORRUPTED	-3
#define SOCKET_NONBLOCK		1
#define SOCKET_BLOCK		0

/* TLS 1.2: ephemeral key exchange only. !kRSA excludes static RSA, which gives no forward secrecy */
#define CAS_SSL_CIPHER_LIST	"ECDHE+AESGCM:ECDHE+CHACHA20:DHE+AESGCM:!aNULL:!kRSA"
#define CAS_SSL_CIPHERSUITES	"TLS_AES_256_GCM_SHA384:TLS_CHACHA20_POLY1305_SHA256:TLS_AES_128_GCM_SHA256"
#define CAS_SSL_TIME_STR_LEN	32

static SSL *ssl = NULL;
bool ssl_client = false;

static int cas_ssl_load_cert_and_key (SSL_CTX * ctx, const char *cert, const char *key);
static int cas_ssl_passwd_cb (char *buf, int size, int rwflag, void *userdata);
static int cas_ssl_validity_check (SSL_CTX * ctx, const char *cert);
static void cas_ssl_time_to_str (const ASN1_TIME * t, char *buf, size_t size);

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
      /* a failure below returns before SSL_new (), which would leave ssl dangling for the next connection */
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
  snprintf (cert, CERT_FILENAME_LEN, "%s/conf/%s", getenv ("CUBRID"), CAS_SSL_CERT_FILE);
  snprintf (key, CERT_FILENAME_LEN, "%s/conf/%s", getenv ("CUBRID"), CAS_SSL_KEY_FILE);

  cert_not_found = (stat (cert, &sbuf) < 0) ? true : false;
  pk_not_found = (stat (key, &sbuf) < 0) ? true : false;

  if (cert_not_found && pk_not_found)
    {
      cas_log_write_and_end (0, false, "SSL: Both the certificate & Private key could not be found: %s, %s", cert, key);
      return ER_CERT_CORRUPTED;
    }

  if (cert_not_found)
    {
      cas_log_write_and_end (0, false, "SSL: Certificate not found: %s", cert);
      return ER_CERT_CORRUPTED;
    }

  if (pk_not_found)
    {
      cas_log_write_and_end (0, false, "SSL: Private key not found: %s", key);
      return ER_CERT_CORRUPTED;
    }

#if OPENSSL_VERSION_NUMBER < 0x10100000L
  SSL_load_error_strings ();
  SSLeay_add_ssl_algorithms ();
  ERR_load_crypto_strings ();
#endif

  /* errors left from an earlier connection would be logged as the cause of a failure below */
  ERR_clear_error ();

  if ((ctx = SSL_CTX_new (TLS_server_method ())) == NULL)
    {
      cas_log_write_and_end (0, true, "SSL: Initialize failed.");
      return ER_SSL_GENERAL;
    }

  if (SSL_CTX_set_min_proto_version (ctx, TLS1_2_VERSION) == 0
      || SSL_CTX_set_cipher_list (ctx, CAS_SSL_CIPHER_LIST) == 0
      || SSL_CTX_set_ciphersuites (ctx, CAS_SSL_CIPHERSUITES) == 0)
    {
      cas_log_write_and_end (0, true, "SSL: Setting the protocol version or cipher suites failed - '%s'",
			     ERR_error_string (ERR_get_error (), NULL));
      SSL_CTX_free (ctx);
      return ER_SSL_GENERAL;
    }

  if ((err_code = cas_ssl_load_cert_and_key (ctx, cert, key)) < 0)
    {
      SSL_CTX_free (ctx);
      return err_code;
    }

  if ((err_code = cas_ssl_validity_check (ctx, cert)) < 0)
    {
      SSL_CTX_free (ctx);
      return err_code;
    }


  if ((ssl = SSL_new (ctx)) == NULL)
    {
      cas_log_write_and_end (0, true, "SSL: Creating SSL context failed.");
      SSL_CTX_free (ctx);
      return ER_SSL_GENERAL;
    }

  /* ssl holds its own reference to ctx, so ctx is freed by SSL_free () on every path from here */
  SSL_CTX_free (ctx);

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

/*
 * cas_ssl_load_cert_and_key () - load the certificate chain and the private key into ctx
 *   return: 0 on success, ER_CERT_CORRUPTED otherwise
 *   ctx(in/out):
 *   cert(in): PEM file with the server certificate, optionally followed by intermediate CA certificates
 *   key(in): PEM file with the private key
 *
 * Each cause of failure is logged separately because the operator has to fix each one differently.
 */
static int
cas_ssl_load_cert_and_key (SSL_CTX * ctx, const char *cert, const char *key)
{
  bool passwd_requested = false;
  unsigned long err;

  /* SSL_CTX_use_certificate_file () would load only the first certificate and drop the intermediate CAs */
  if (SSL_CTX_use_certificate_chain_file (ctx, cert) <= 0)
    {
      cas_log_write_and_end (0, true, "SSL: Failed to load the certificate: %s - '%s'", cert,
			     ERR_error_string (ERR_get_error (), NULL));
      return ER_CERT_CORRUPTED;
    }

  /* without a callback, OpenSSL tries to read the passphrase from the terminal */
  SSL_CTX_set_default_passwd_cb (ctx, cas_ssl_passwd_cb);
  SSL_CTX_set_default_passwd_cb_userdata (ctx, &passwd_requested);

  if (SSL_CTX_use_PrivateKey_file (ctx, key, SSL_FILETYPE_PEM) <= 0)
    {
      err = ERR_peek_last_error ();
      SSL_CTX_set_default_passwd_cb_userdata (ctx, NULL);

      if (passwd_requested)
	{
	  cas_log_write_and_end (0, true,
				 "SSL: Private key with a passphrase is not supported. Use an unencrypted key: %s",
				 key);
	}
      else if (ERR_GET_LIB (err) == ERR_LIB_X509 && ERR_GET_REASON (err) == X509_R_KEY_VALUES_MISMATCH)
	{
	  cas_log_write_and_end (0, true, "SSL: Certificate and private key do not match: %s, %s", cert, key);
	}
      else
	{
	  cas_log_write_and_end (0, true, "SSL: Failed to load the private key: %s - '%s'", key,
				 ERR_error_string (ERR_get_error (), NULL));
	}
      return ER_CERT_CORRUPTED;
    }

  SSL_CTX_set_default_passwd_cb_userdata (ctx, NULL);

  /* SSL_CTX_use_PrivateKey_file () detects a mismatch only when the key type equals the certificate's */
  if (SSL_CTX_check_private_key (ctx) <= 0)
    {
      cas_log_write_and_end (0, true, "SSL: Certificate and private key do not match: %s, %s", cert, key);
      return ER_CERT_CORRUPTED;
    }

  return 0;
}

/*
 * cas_ssl_passwd_cb () - passphrase callback that supplies no passphrase
 *   return: 0 (length of the passphrase)
 *   userdata(out): set to true to tell the caller that the key is encrypted
 */
static int
cas_ssl_passwd_cb (char *buf, int size, int rwflag, void *userdata)
{
  if (userdata != NULL)
    {
      *(bool *) userdata = true;
    }

  return 0;
}

/*
 * cas_ssl_validity_check () - check the validity period of the server certificate
 *   return: 0 if valid, error code otherwise
 *   ctx(in):
 *   cert(in): certificate file name for the log
 *
 * The log carries notBefore, notAfter and the current time, so that a wrong system clock can be told from
 * a certificate that has really expired.
 */
static int
cas_ssl_validity_check (SSL_CTX * ctx, const char *cert)
{
  ASN1_TIME *not_before, *not_after;
  X509 *crt;
  int err_code;
  const char *reason;
  char not_before_str[CAS_SSL_TIME_STR_LEN];
  char not_after_str[CAS_SSL_TIME_STR_LEN];
  char now_str[CAS_SSL_TIME_STR_LEN];

  crt = SSL_CTX_get0_certificate (ctx);

  if (crt == NULL)
    {
      cas_log_write (0, true, "SSL: No certificate is loaded: %s", cert);
      return ER_SSL_GENERAL;
    }

  not_before = X509_getm_notBefore (crt);
  not_after = X509_getm_notAfter (crt);

  if (X509_cmp_time (not_after, NULL) != 1)
    {
      err_code = ER_CERT_EXPIRED;
      reason = "Certificate has expired";
    }
  else if (X509_cmp_time (not_before, NULL) != -1)
    {
      err_code = ER_SSL_GENERAL;
      reason = "Certificate is not yet valid (check the system clock)";
    }
  else
    {
      return 0;
    }

  cas_ssl_time_to_str (not_before, not_before_str, sizeof (not_before_str));
  cas_ssl_time_to_str (not_after, not_after_str, sizeof (not_after_str));
  cas_ssl_time_to_str (NULL, now_str, sizeof (now_str));

  cas_log_write (0, true, "SSL: %s: %s (notBefore=%s, notAfter=%s, now=%s)", reason, cert, not_before_str,
		 not_after_str, now_str);

  return err_code;
}

/*
 * cas_ssl_time_to_str () - format a time as ISO 8601 UTC
 *   t(in): time to format, or NULL for the current time
 *   buf(out):
 *   size(in):
 */
static void
cas_ssl_time_to_str (const ASN1_TIME * t, char *buf, size_t size)
{
  struct tm tm;

  if (ASN1_TIME_to_tm (t, &tm) == 0 || strftime (buf, size, "%Y-%m-%dT%H:%M:%SZ", &tm) == 0)
    {
      snprintf (buf, size, "unknown");
    }
}

bool
is_ssl_data_ready (int sock_fd)
{
  return (SSL_has_pending (ssl) == 1 ? true : false);
}
