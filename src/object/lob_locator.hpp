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

//
// Lob locator interface
//

#ifndef _LOB_LOCATOR_HPP_
#define _LOB_LOCATOR_HPP_

/* there can be following transitions in transient lobs

   -------------------------------------------------------------------------
   | 	       locator  | created               | deleted		   |
   |--------------------|-----------------------|--------------------------|
   | in     | transient | LOB_TRANSIENT_CREATED i LOB_UNKNOWN		   |
   | tran   |-----------|-----------------------|--------------------------|
   |        | permanent | LOB_PERMANENT_CREATED | LOB_PERMANENT_DELETED    |
   |--------------------|-----------------------|--------------------------|
   | out of | transient | LOB_UNKNOWN		| LOB_UNKNOWN		   |
   | tran   |-----------|-----------------------|--------------------------|
   |        | permanent | LOB_UNKNOWN 		| LOB_TRANSIENT_DELETED    |
   -------------------------------------------------------------------------

   s1: create a transient locator and delete it
       LOB_TRANSIENT_CREATED -> LOB_UNKNOWN

   s2: create a transient locator and bind it to a row in table
       LOB_TRANSIENT_CREATED -> LOB_PERMANENT_CREATED

   s3: bind a transient locator to a row and delete the locator
       LOB_PERMANENT_CREATED -> LOB_PERMANENT_DELETED

   s4: delete a locator to be create out of transaction
       LOB_UNKNOWN -> LOB_TRANSIENT_DELETED

 */
enum lob_locator_state
{
  LOB_UNKNOWN,
  LOB_TRANSIENT_CREATED,
  LOB_TRANSIENT_DELETED,
  LOB_PERMANENT_CREATED,
  LOB_PERMANENT_DELETED,
  LOB_NOT_FOUND
};
typedef enum lob_locator_state LOB_LOCATOR_STATE;

LOB_LOCATOR_STATE lob_locator_find (const char *locator, char *real_locator);
int lob_locator_add (const char *locator, LOB_LOCATOR_STATE state);
int lob_locator_change_state (const char *locator, const char *new_locator, LOB_LOCATOR_STATE state);
int lob_locator_drop (const char *locator);

bool lob_locator_is_valid (const char *locator);
const char *lob_locator_key (const char *locator);      // pointer in locator to key
const char *lob_locator_meta (const char *locator);     // pointer in locator to meta

#endif // _LOB_LOCATOR_HPP_
