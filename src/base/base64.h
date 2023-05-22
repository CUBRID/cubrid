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
*  base64.h -
*/

#ifndef __BASE64_H_
#define __BASE64_H_

#ident "$Id$"

/* internal error code only for base64 */
enum
{
  BASE64_EMPTY_INPUT = 1,
  BASE64_INVALID_INPUT = 2
};


extern int base64_encode (const unsigned char *src, int src_len, unsigned char **dest, int *dest_len);
extern int base64_decode (const unsigned char *src, int src_len, unsigned char **dest, int *dest_len);

#endif
