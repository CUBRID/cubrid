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
 * host_lookup.h
 */

#ifndef _HOST_LOOKUP_H_
#define _HOST_LOOKUP_H_

#ident "$Id$"
extern struct hostent *gethostbyname_uhost (char *name);
extern int getnameinfo_uhost (struct sockaddr *addr, socklen_t addrlen, char *host, size_t hostlen,
			      char *serv, size_t servlen, int flags);
extern int getaddrinfo_uhost (char *node, char *service, struct addrinfo *hints, struct addrinfo **res);

//extern int gethostbyname_r_uhost (const char *hostname, struct hostent *out_hp);
#ifdef HAVE_GETHOSTBYNAME_R
#if defined (HAVE_GETHOSTBYNAME_R_GLIBC)
extern int
gethostbyname_r_uhost (const char *name,
		       struct hostent *ret, char *buf, size_t buflen, struct hostent **result, int *h_errnop);
#elif defined (HAVE_GETHOSTBYNAME_R_SOLARIS)
extern struct hostent *gethostbyname_r_uhost (const char *name,
					      struct hostent *ret, char *buf, size_t buflen, int *h_errnop);
#elif defined (HAVE_GETHOSTBYNAME_R_HOSTENT_DATA)
extern int gethostbyname_r_uhost (const char *name, struct hostent *ret, struct hostent_data *ht_data);
#else
#error "HAVE_GETHOSTBYNAME_R"
#endif /* HAVE_GETHOSTBYNAME_R_GLIBC */
#endif /* HAVE_GETHOSTBYNAME_R */

#endif /* _HOST_LOOKUP_H_ */
