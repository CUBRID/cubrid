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

//
// query_hash_scan_constants - interface for hash list scan during queries
//

#ifndef _QUERY_HASH_SCAN_CONSTANTS_H_
#define _QUERY_HASH_SCAN_CONSTANTS_H_

/* kind of hash list scan method */
enum hash_method
{
  HASH_METH_NOT_USE = 0,
  HASH_METH_IN_MEM = 1,
  HASH_METH_HYBRID = 2,
  HASH_METH_HASH_FILE = 3
};
typedef enum hash_method HASH_METHOD;

#endif /* _QUERY_HASH_SCAN_CONSTANTS_H_ */
