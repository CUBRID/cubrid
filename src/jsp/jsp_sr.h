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
 * jsp_sr.h - Java Stored Procedure Server Module Header
 *
 * Note:
 */

#ifndef _JSP_SR_H_
#define _JSP_SR_H_

#ident "$Id$"

#if defined(__cplusplus)
extern "C"
{
#endif

  extern int jsp_start_server (const char *server_name, const char *path, int port_number);
  extern int jsp_server_port (void);
  extern int jsp_server_port_from_info (void);
  extern int jsp_jvm_is_loaded (void);

#if defined(__cplusplus)
}
#endif

#endif				/* _JSP_SR_H_ */
