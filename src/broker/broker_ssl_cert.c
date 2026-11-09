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
 * broker_ssl_cert.c - SSL certificate that a broker start creates or checks
 *
 * CUBRID ships no certificate, so a broker with SSL=ON creates a private key and a self-signed certificate for its
 * host when it starts without one. A certificate for a private key that earlier releases shipped, which is public,
 * is told apart. Only cubrid_broker and cubrid_gateway build this file, so that the others do not link OpenSSL;
 * CUBRID Manager starts brokers with cubrid_broker on Linux. Not for Windows.
 */

#ident "$Id$"

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <ifaddrs.h>
#include <openssl/bn.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <openssl/pem.h>

#include "porting.h"
#include "broker_ssl_cert.h"

static X509 *make_ssl_cert (EVP_PKEY ** pkey);
static bool add_ssl_cert_ext (X509 * x509, int nid, const char *value);
static GENERAL_NAMES *make_ssl_cert_san (const char *host);
static bool add_ssl_cert_san (GENERAL_NAMES * san, int type, const void *data, int len);
static bool write_ssl_file (const char *file, int mode, EVP_PKEY * pkey, X509 * x509);

/*
 * SHA-256 of the public keys (DER SubjectPublicKeyInfo) of the private keys that earlier releases shipped as
 * conf/cas_ssl_cert.key, which are public. An installation upgraded from such a release may still have one.
 * A certificate reissued with one of these keys still matches. Same value as
 *   openssl x509 -in cas_ssl_cert.crt -noout -pubkey | openssl pkey -pubin -outform DER | openssl dgst -sha256
 */
static const unsigned char bundled_ssl_key_spki_sha256[][32] = {
  /* CBRD-23687 (2020) */
  {0x59, 0x34, 0xB7, 0x5F, 0x89, 0xA7, 0xA8, 0xF7, 0xBA, 0x8A, 0x3A, 0xC8, 0x76, 0x58, 0x62, 0x1A,
   0x6E, 0x42, 0xC8, 0xB5, 0xC6, 0x4F, 0x01, 0x6A, 0xF7, 0xDA, 0xE8, 0x46, 0x8F, 0xB1, 0x28, 0x8F},
  /* CBRD-27091 (2026) */
  {0xF9, 0x2B, 0x39, 0x42, 0x73, 0x47, 0x67, 0xCD, 0x8C, 0x9F, 0x8F, 0xF0, 0x7D, 0x9F, 0x5E, 0xAD,
   0xF1, 0x1E, 0xD6, 0x5B, 0x41, 0xDC, 0xE0, 0xF8, 0xF2, 0x23, 0x09, 0xEC, 0xE2, 0xF6, 0x35, 0x17}
};

/*
 * broker_create_ssl_cert () - create a private key and a self-signed certificate for this host
 *   return: 0, or -1 with err_msg set
 *   cert_file(in): certificate file to create
 *   key_file(in): private key file to create
 *   err_msg(out): why the files were not created
 *   err_msg_size(in): size of err_msg
 *
 * Neither file may exist: an existing file is not replaced, and the private key written first is removed if the
 * certificate cannot be written.
 */
int
broker_create_ssl_cert (const char *cert_file, const char *key_file, char *err_msg, int err_msg_size)
{
  EVP_PKEY *pkey = NULL;
  X509 *x509;
  int error;

  x509 = make_ssl_cert (&pkey);
  if (x509 == NULL)
    {
      snprintf (err_msg, err_msg_size,
		"Failed to create a self-signed SSL certificate: %s. "
		"Provide a private key and certificate, and restart the service.",
		ERR_error_string (ERR_get_error (), NULL));
      EVP_PKEY_free (pkey);
      return -1;
    }

  if (!write_ssl_file (key_file, 0600, pkey, NULL))
    {
      error = errno;
      snprintf (err_msg, err_msg_size,
		"Failed to create a self-signed SSL certificate: %s: %s. "
		"Provide a private key and certificate, and restart the service.", key_file, strerror (error));
    }
  else if (!write_ssl_file (cert_file, 0644, NULL, x509))
    {
      error = errno;
      unlink (key_file);
      snprintf (err_msg, err_msg_size,
		"Failed to create a self-signed SSL certificate: %s: %s. "
		"Provide a private key and certificate, and restart the service.", cert_file, strerror (error));
    }
  else
    {
      error = 0;
    }

  X509_free (x509);
  EVP_PKEY_free (pkey);

  return error == 0 ? 0 : -1;
}

/*
 * broker_has_bundled_ssl_key () - whether the first certificate in the file is for a private key shipped with CUBRID
 *   return: true if the SHA-256 of its public key is in bundled_ssl_key_spki_sha256
 *   cert_file(in): PEM certificate file
 *
 * The public key is taken from the certificate, not from the private key file: the CAS refuses a key that does not
 * match the certificate, and the private key is not opened here.
 */
bool
broker_has_bundled_ssl_key (const char *cert_file)
{
  FILE *fp;
  X509 *cert;
  EVP_PKEY *pkey;
  unsigned char *spki = NULL;
  int spki_len = -1;
  unsigned char md[EVP_MAX_MD_SIZE];
  unsigned int md_len = 0;
  bool bundled = false;
  size_t i;

  fp = fopen (cert_file, "r");
  if (fp == NULL)
    {
      return false;
    }

  cert = PEM_read_X509 (fp, NULL, NULL, NULL);
  fclose (fp);
  if (cert == NULL)
    {
      return false;
    }

  pkey = X509_get0_pubkey (cert);
  if (pkey != NULL)
    {
      spki_len = i2d_PUBKEY (pkey, &spki);
    }

  if (spki_len > 0 && EVP_Digest (spki, spki_len, md, &md_len, EVP_sha256 (), NULL) == 1)
    {
      for (i = 0; i < sizeof (bundled_ssl_key_spki_sha256) / sizeof (bundled_ssl_key_spki_sha256[0]); i++)
	{
	  if (md_len == sizeof (bundled_ssl_key_spki_sha256[i])
	      && memcmp (md, bundled_ssl_key_spki_sha256[i], md_len) == 0)
	    {
	      bundled = true;
	      break;
	    }
	}
    }

  OPENSSL_free (spki);
  X509_free (cert);

  return bundled;
}

/*
 * make_ssl_cert () - make an RSA private key and a self-signed certificate for this host
 *   return: certificate, or NULL with the OpenSSL error queued
 *   pkey(out): private key of the certificate, which the caller frees, also on failure
 *
 * The subject alternative names let a client that trusts the certificate also verify the host name (verify-full).
 */
static X509 *
make_ssl_cert (EVP_PKEY ** pkey)
{
  /* clients trust this certificate itself, so a short validity buys little, and an expired one would make every SSL
   * connection fail with no notice beforehand */
  const long valid_secs = 365L * 24 * 60 * 60;
  char host[CUB_MAXHOSTNAMELEN];
  EVP_PKEY_CTX *pctx = NULL;
  X509 *x509 = NULL;
  X509_NAME *name;
  GENERAL_NAMES *san = NULL;
  BIGNUM *serial = NULL;
  bool done = false;

  *pkey = NULL;
  ERR_clear_error ();

  if (gethostname (host, sizeof (host)) != 0)
    {
      goto end;
    }

  pctx = EVP_PKEY_CTX_new_id (EVP_PKEY_RSA, NULL);
  if (pctx == NULL || EVP_PKEY_keygen_init (pctx) <= 0 || EVP_PKEY_CTX_set_rsa_keygen_bits (pctx, 2048) <= 0
      || EVP_PKEY_keygen (pctx, pkey) <= 0)
    {
      goto end;
    }

  x509 = X509_new ();
  serial = BN_new ();
  san = make_ssl_cert_san (host);
  /* version 2 is X.509 v3, which has extensions */
  if (x509 == NULL || serial == NULL || san == NULL || !X509_set_version (x509, 2)
      || !BN_rand (serial, 127, BN_RAND_TOP_ANY, BN_RAND_BOTTOM_ANY)
      || BN_to_ASN1_INTEGER (serial, X509_get_serialNumber (x509)) == NULL
      || X509_gmtime_adj (X509_getm_notBefore (x509), 0) == NULL
      || X509_gmtime_adj (X509_getm_notAfter (x509), valid_secs) == NULL || !X509_set_pubkey (x509, *pkey))
    {
      goto end;
    }

  name = X509_get_subject_name (x509);
  if (!X509_NAME_add_entry_by_txt (name, "CN", MBSTRING_ASC, (const unsigned char *) host, -1, -1, 0)
      || !X509_set_issuer_name (x509, name))
    {
      goto end;
    }

  if (!add_ssl_cert_ext (x509, NID_basic_constraints, "CA:FALSE")
      || !add_ssl_cert_ext (x509, NID_key_usage, "critical,digitalSignature,keyEncipherment")
      || !add_ssl_cert_ext (x509, NID_ext_key_usage, "serverAuth")
      || !add_ssl_cert_ext (x509, NID_subject_key_identifier, "hash")
      || !X509_add1_ext_i2d (x509, NID_subject_alt_name, san, 0, X509V3_ADD_DEFAULT)
      || !X509_sign (x509, *pkey, EVP_sha256 ()))
    {
      goto end;
    }

  done = true;

end:
  GENERAL_NAMES_free (san);
  BN_free (serial);
  EVP_PKEY_CTX_free (pctx);
  if (!done)
    {
      X509_free (x509);
      x509 = NULL;
    }

  return x509;
}

/*
 * add_ssl_cert_ext () - add an X.509 v3 extension written as in an OpenSSL configuration file
 *   return: false if the extension could not be made or added
 *   x509(in/out): self-signed certificate with its public key set
 *   nid(in): extension
 *   value(in): extension value, e.g. "CA:FALSE"
 */
static bool
add_ssl_cert_ext (X509 * x509, int nid, const char *value)
{
  X509V3_CTX v3ctx;
  X509_EXTENSION *ext;
  bool added;

  /* the certificate is its own issuer */
  X509V3_set_ctx (&v3ctx, x509, x509, NULL, NULL, 0);
  ext = X509V3_EXT_conf_nid (NULL, &v3ctx, nid, value);
  added = ext != NULL && X509_add_ext (x509, ext, -1);
  X509_EXTENSION_free (ext);

  return added;
}

/*
 * make_ssl_cert_san () - the names a client may put in the connection URL to reach this host
 *   return: subject alternative names, or NULL
 *   host(in): host name of this host
 *
 * The host name, its short and canonical (FQDN) forms, localhost, and the IP addresses of all the interfaces of this
 * host, loopback included: only IPv6 link-local addresses are left out.
 */
static GENERAL_NAMES *
make_ssl_cert_san (const char *host)
{
  GENERAL_NAMES *san;
  struct addrinfo hints;
  struct addrinfo *ai;
  struct ifaddrs *ifs;
  struct ifaddrs *ifa;
  struct in6_addr *addr6;
  const char *dot;
  bool ok;

  san = sk_GENERAL_NAME_new_null ();
  if (san == NULL)
    {
      return NULL;
    }

  dot = strchr (host, '.');
  ok = add_ssl_cert_san (san, GEN_DNS, host, (int) strlen (host));
  if (ok && dot != NULL)
    {
      ok = add_ssl_cert_san (san, GEN_DNS, host, (int) (dot - host));
    }

  memset (&hints, 0, sizeof (hints));
  hints.ai_flags = AI_CANONNAME;
  if (ok && getaddrinfo (host, NULL, &hints, &ai) == 0)
    {
      if (ai->ai_canonname != NULL)
	{
	  ok = add_ssl_cert_san (san, GEN_DNS, ai->ai_canonname, (int) strlen (ai->ai_canonname));
	}
      freeaddrinfo (ai);
    }

  if (ok)
    {
      ok = add_ssl_cert_san (san, GEN_DNS, "localhost", (int) strlen ("localhost"));
    }

  if (ok && getifaddrs (&ifs) == 0)
    {
      for (ifa = ifs; ifa != NULL && ok; ifa = ifa->ifa_next)
	{
	  if (ifa->ifa_addr == NULL)
	    {
	      continue;
	    }

	  if (ifa->ifa_addr->sa_family == AF_INET)
	    {
	      ok = add_ssl_cert_san (san, GEN_IPADD, &((struct sockaddr_in *) ifa->ifa_addr)->sin_addr,
				     (int) sizeof (struct in_addr));
	    }
	  else if (ifa->ifa_addr->sa_family == AF_INET6)
	    {
	      addr6 = &((struct sockaddr_in6 *) ifa->ifa_addr)->sin6_addr;
	      if (!IN6_IS_ADDR_LINKLOCAL (addr6))
		{
		  ok = add_ssl_cert_san (san, GEN_IPADD, addr6, (int) sizeof (struct in6_addr));
		}
	    }
	}
      freeifaddrs (ifs);
    }

  if (!ok)
    {
      GENERAL_NAMES_free (san);
      return NULL;
    }

  return san;
}

/*
 * add_ssl_cert_san () - add a DNS name or an IP address to the subject alternative names unless it is there
 *   return: false if out of memory
 *   san(in/out): subject alternative names
 *   type(in): GEN_DNS or GEN_IPADD
 *   data(in): name, or address in network byte order
 *   len(in): length of data
 */
static bool
add_ssl_cert_san (GENERAL_NAMES * san, int type, const void *data, int len)
{
  GENERAL_NAME *gen;
  ASN1_STRING *value;
  int i;

  gen = GENERAL_NAME_new ();
  value = ASN1_STRING_type_new (type == GEN_DNS ? V_ASN1_IA5STRING : V_ASN1_OCTET_STRING);
  if (gen == NULL || value == NULL || !ASN1_STRING_set (value, data, len))
    {
      GENERAL_NAME_free (gen);
      ASN1_STRING_free (value);
      return false;
    }
  GENERAL_NAME_set0_value (gen, type, value);

  for (i = 0; i < sk_GENERAL_NAME_num (san); i++)
    {
      if (GENERAL_NAME_cmp (sk_GENERAL_NAME_value (san, i), gen) == 0)
	{
	  GENERAL_NAME_free (gen);
	  return true;
	}
    }

  if (sk_GENERAL_NAME_push (san, gen) == 0)
    {
      GENERAL_NAME_free (gen);
      return false;
    }

  return true;
}

/*
 * write_ssl_file () - create a file and write a private key or a certificate in it as PEM
 *   return: false with errno set if the file could not be created or written; EEXIST if it exists
 *   file(in): file to create
 *   mode(in): permission of the new file, before the umask
 *   pkey(in): private key to write, or NULL
 *   x509(in): certificate to write if pkey is NULL
 */
static bool
write_ssl_file (const char *file, int mode, EVP_PKEY * pkey, X509 * x509)
{
  int fd;
  FILE *fp;
  int written;
  int error;

  fd = open (file, O_WRONLY | O_CREAT | O_EXCL, mode);
  if (fd < 0)
    {
      return false;
    }

  fp = fdopen (fd, "w");
  if (fp == NULL)
    {
      error = errno;
      close (fd);
      unlink (file);
      errno = error;
      return false;
    }

  errno = 0;
  if (pkey != NULL)
    {
      written = PEM_write_PrivateKey (fp, pkey, NULL, NULL, 0, NULL, NULL);
    }
  else
    {
      written = PEM_write_X509 (fp, x509);
    }
  if (fclose (fp) != 0 || !written)
    {
      error = errno != 0 ? errno : EIO;
      unlink (file);
      errno = error;
      return false;
    }

  return true;
}
