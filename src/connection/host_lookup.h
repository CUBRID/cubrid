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
extern hostent *gethostbyname_uhost (char *hostname);
extern int gethostbyname_r_uhost (const char *hostname, struct hostent *out_hp);
extern int getnameinfo_uhost (struct sockaddr *saddr, socklen_t saddr_len, char *hostname, size_t host_len,
			      char *servname, size_t serv_len, int flags);
extern int getaddrinfo_uhost (char *hostname, char *servname, struct addrinfo *hints, struct addrinfo **results);

#endif /* _HOST_LOOKUP_H_ */
