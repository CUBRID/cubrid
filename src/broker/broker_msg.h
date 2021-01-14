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
 * broker_msg.h - Dispatcher Interface Header File
 *               This file contains exported stuffs from Dispatcher
 */

#ifndef	_BROKER_MSG_H_
#define	_BROKER_MSG_H_

#ident "$Id$"

/* We use the following length to store any identifiers in the
 * implementation. The identifiers include application_name, path_name,
 * a line in the UniWeb application registry and so forth.
 * Note that, however, we DO NOT ensure that the length is enough for each
 * given identifier in the implementation. If too long identifier is given,
 * the behavior is undefined. We just recommend to increase the following
 * value in that case.
 */
#define	UW_MAX_LENGTH		1024

/* the magic string for UniWeb socket */
#ifdef _EDU_
#define	UW_SOCKET_MAGIC		"UW4.0EDU"
#else
#define	UW_SOCKET_MAGIC		"V3RQ4.0"
#endif

#define V3_HEADER_OK_COMPRESS           "V3_OK_COMP"
#define V3_HEADER_OK			"V3_OK"
#define V3_HEADER_ERR                   "V3_ERR"
#define V3_RESPONSE_HEADER_SIZE     16

/* PRE_SEND_DATA_SIZE = PRE_SEND_SCRIPT_SIZE + PRE_SEND_PRG_NAME_SIZE */
#ifdef _EDU_
#define	PRE_SEND_DATA_SIZE		114
#else
#define	PRE_SEND_DATA_SIZE		66
#endif
#define PRE_SEND_SCRIPT_SIZE		30
#define	PRE_SEND_PRG_NAME_SIZE		20
#define PRE_SEND_SESSION_ID_SIZE	16

#ifdef _EDU_
#define PRE_SEND_KEY_SIZE		48
#define PRE_SEND_KEY_OFFSET	\
      (PRE_SEND_SCRIPT_SIZE + PRE_SEND_PRG_NAME_SIZE + PRE_SEND_SESSION_ID_SIZE)
#endif

#endif /* _BROKER_MSG_H_ */
