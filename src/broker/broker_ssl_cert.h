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
 * broker_ssl_cert.h - SSL certificate that a broker start creates or checks (see broker_ssl_cert.c)
 */

#ifndef _BROKER_SSL_CERT_H_
#define _BROKER_SSL_CERT_H_

extern int broker_create_ssl_cert (const char *cert_file, const char *key_file, char *err_msg, int err_msg_size);
extern bool broker_has_bundled_ssl_key (const char *cert_file);

#endif /* _BROKER_SSL_CERT_H_ */
