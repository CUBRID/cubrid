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

#ident "$Id$"
#include "cci_ssl.h"


SSL_CTX *
create_ssl_ctx ()
{
  const SSL_METHOD *method;
  SSL_CTX *ctx = NULL;

  method = TLS_client_method ();

  ctx = SSL_CTX_new (method);
  if (ctx == NULL)
    {
      /*The creation of a new SSL_CTX object failed. */
    }

  return ctx;
}


SSL *
create_ssl (SOCKET srv_sock_fd, SSL_CTX * ctx)
{
  SSL *ssl;
  int server = (int) srv_sock_fd;

  ssl = SSL_new (ctx);
  if (ssl == NULL)
    {
      /*The creation of a new SSL structure failed. */
      return NULL;
    }

  if (SSL_set_fd (ssl, server) == 0)
    {
      /*The operation failed.Check the error stack to find out why. */
      return NULL;
    }

  return ssl;
}

int
connect_ssl (SSL * ssl)
{
  return SSL_connect (ssl);
}


void
cleanup_ssl (SSL * ssl)
{
  if (ssl != NULL)
    {
      SSL_shutdown (ssl);
      SSL_free (ssl);
    }
}

void
cleanup_ssl_ctx (SSL_CTX * ctx)
{
  if (ctx != NULL)
    {
      SSL_CTX_free (ctx);
    }
}
