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
// Lob locator implementation
//

#include "lob_locator.hpp"

#if defined (CS_MODE)
#include "network_interface_cl.h"
#endif
#include "porting.h"
#if !defined (CS_MODE)
#include "transaction_transient.hpp"
#endif

#include <cstring>
#include <string>
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

bool
lob_locator_is_valid (const char *locator)
{
  if (locator == NULL)
    {
      return false;
    }
  const char *key = lob_locator_key (locator);
  const char *meta = lob_locator_meta (locator);
  if (key == NULL || meta == NULL || key - 1 <= meta)
    {
      return false;
    }
  // is valid
  return true;
}

const char *
lob_locator_key (const char *locator)
{
  return std::strrchr (locator, '.') + 1;
}

const char *
lob_locator_meta (const char *locator)
{
  return std::strrchr (locator, PATH_SEPARATOR);
}

/*
 * lob_locator_find () - wrapper function
 * return: LOB_LOCATOR_STATE
 * locator(in):
 * real_locator(out):
 */
LOB_LOCATOR_STATE
lob_locator_find (const char *locator, char *real_locator)
{
#if defined(CS_MODE)
  return log_find_lob_locator (locator, real_locator);
#else /* CS_MODE */
  return xtx_find_lob_locator (NULL, locator, real_locator);
#endif /* CS_MODE */
}

/*
 * lob_locator_add () - wrapper function
 * return: error status
 * locator(in):
 * state(in):
 */
int
lob_locator_add (const char *locator, LOB_LOCATOR_STATE state)
{
#if defined(CS_MODE)
  return log_add_lob_locator (locator, state);
#else /* CS_MODE */
  return xtx_add_lob_locator (NULL, locator, state);
#endif /* CS_MODE */
}

/*
 * lob_locator_change_state () - wrapper function
 * return: error status
 * locator(in):
 * new_locator(in):
 * state(in):
 */
int
lob_locator_change_state (const char *locator, const char *new_locator, LOB_LOCATOR_STATE state)
{
#if defined(CS_MODE)
  return log_change_state_of_locator (locator, new_locator, state);
#else /* CS_MODE */
  return xtx_change_state_of_locator (NULL, locator, new_locator, state);
#endif /* CS_MODE */
}

/*
 * lob_locator_drop () - wrapper function
 * return: error status
 * locator(in):
 */
int
lob_locator_drop (const char *locator)
{
#if defined(CS_MODE)
  return log_drop_lob_locator (locator);
#else /* CS_MODE */
  return xtx_drop_lob_locator (NULL, locator);
#endif /* CS_MODE */
}
