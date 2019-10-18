/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
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
