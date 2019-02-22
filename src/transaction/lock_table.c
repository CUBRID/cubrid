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

/*
 * lock_table.c - lock managment module. (client + server)
 *          Definition of lock matrix tables
 */

#ident "$Id$"

#include "config.h"

#include "storage_common.h"

/*
 *
 *                       LOCK COMPATIBILITY TABLE
 *
 * column : current lock mode (granted lock mode)
 * row    : request lock mode
 * --------------------------------------------------------------------------------------------------------------
 *         |   N/A  NON2PL  NULL  SCH-S     IS       S      IX     SIX       U       X   SCH-M
 * --------------------------------------------------------------------------------------------------------------
 *    N/A  |   N/A    N/A    N/A    N/A    N/A     N/A     N/A     N/A     N/A     N/A     N/A
 *
 * NON2PL  |   N/A    N/A    N/A    N/A    N/A     N/A     N/A     N/A     N/A     N/A     N/A
 *
 *   NULL  |   N/A    N/A   True   True   True    True    True    True    True    True    True
 *
 *  SCH-S  |   N/A    N/A   True   True   True    True    True    True     N/A    True   False
 *
 *     IS  |   N/A    N/A   True   True   True    True    True    True     N/A   False   False
 *
 *      S  |   N/A    N/A   True   True   True    True   False   False   False   False   False
 *
 *     IX  |   N/A    N/A   True   True   True   False    True   False     N/A   False   False
 *
 *    SIX  |   N/A    N/A   True   True   True   False   False   False     N/A   False   False
 *
 *      U  |   N/A    N/A   True    N/A    N/A    True     N/A     N/A   False   False     N/A
 *
 *      X  |   N/A    N/A   True   True  False   False   False   False   False   False   False
 *
 *  SCH-M  |   N/A    N/A   True  False  False   False   False   False     N/A   False   False
 * --------------------------------------------------------------------------------------------------------------
 * N/A : not applicable
 */

LOCK_COMPATIBILITY lock_Comp[11][11] = {
  /* N/A */
  {LOCK_COMPAT_UNKNOWN, LOCK_COMPAT_UNKNOWN, LOCK_COMPAT_UNKNOWN, LOCK_COMPAT_UNKNOWN, LOCK_COMPAT_UNKNOWN,
   LOCK_COMPAT_UNKNOWN, LOCK_COMPAT_UNKNOWN, LOCK_COMPAT_UNKNOWN, LOCK_COMPAT_UNKNOWN, LOCK_COMPAT_UNKNOWN,
   LOCK_COMPAT_UNKNOWN}
  ,
  /* NON2PL */
  {LOCK_COMPAT_UNKNOWN, LOCK_COMPAT_UNKNOWN, LOCK_COMPAT_UNKNOWN, LOCK_COMPAT_UNKNOWN, LOCK_COMPAT_UNKNOWN,
   LOCK_COMPAT_UNKNOWN, LOCK_COMPAT_UNKNOWN, LOCK_COMPAT_UNKNOWN, LOCK_COMPAT_UNKNOWN, LOCK_COMPAT_UNKNOWN,
   LOCK_COMPAT_UNKNOWN}
  ,
  /* NULL */
  {LOCK_COMPAT_UNKNOWN, LOCK_COMPAT_UNKNOWN, LOCK_COMPAT_YES, LOCK_COMPAT_YES, LOCK_COMPAT_YES, LOCK_COMPAT_YES,
   LOCK_COMPAT_YES, LOCK_COMPAT_YES, LOCK_COMPAT_YES, LOCK_COMPAT_YES, LOCK_COMPAT_YES}
  ,
  /* SCH-S */
  {LOCK_COMPAT_UNKNOWN, LOCK_COMPAT_UNKNOWN, LOCK_COMPAT_YES, LOCK_COMPAT_YES, LOCK_COMPAT_YES, LOCK_COMPAT_YES,
   LOCK_COMPAT_YES, LOCK_COMPAT_YES, LOCK_COMPAT_UNKNOWN, LOCK_COMPAT_YES, LOCK_COMPAT_NO}
  ,
  /* IS */
  {LOCK_COMPAT_UNKNOWN, LOCK_COMPAT_UNKNOWN, LOCK_COMPAT_YES, LOCK_COMPAT_YES, LOCK_COMPAT_YES, LOCK_COMPAT_YES,
   LOCK_COMPAT_YES, LOCK_COMPAT_YES, LOCK_COMPAT_UNKNOWN, LOCK_COMPAT_NO, LOCK_COMPAT_NO}
  ,
  /* S */
  {LOCK_COMPAT_UNKNOWN, LOCK_COMPAT_UNKNOWN, LOCK_COMPAT_YES, LOCK_COMPAT_YES, LOCK_COMPAT_YES, LOCK_COMPAT_YES,
   LOCK_COMPAT_NO, LOCK_COMPAT_NO, LOCK_COMPAT_NO, LOCK_COMPAT_NO, LOCK_COMPAT_NO}
  ,
  /* IX */
  {LOCK_COMPAT_UNKNOWN, LOCK_COMPAT_UNKNOWN, LOCK_COMPAT_YES, LOCK_COMPAT_YES, LOCK_COMPAT_YES, LOCK_COMPAT_NO,
   LOCK_COMPAT_YES, LOCK_COMPAT_NO, LOCK_COMPAT_UNKNOWN, LOCK_COMPAT_NO, LOCK_COMPAT_NO}
  ,
  /* SIX */
  {LOCK_COMPAT_UNKNOWN, LOCK_COMPAT_UNKNOWN, LOCK_COMPAT_YES, LOCK_COMPAT_YES, LOCK_COMPAT_YES, LOCK_COMPAT_NO,
   LOCK_COMPAT_NO, LOCK_COMPAT_NO, LOCK_COMPAT_UNKNOWN, LOCK_COMPAT_NO, LOCK_COMPAT_NO}
  ,
  /* U */
  {LOCK_COMPAT_UNKNOWN, LOCK_COMPAT_UNKNOWN, LOCK_COMPAT_YES, LOCK_COMPAT_UNKNOWN, LOCK_COMPAT_UNKNOWN, LOCK_COMPAT_YES,
   LOCK_COMPAT_UNKNOWN, LOCK_COMPAT_UNKNOWN, LOCK_COMPAT_NO, LOCK_COMPAT_NO, LOCK_COMPAT_UNKNOWN}
  ,
  /* X */
  {LOCK_COMPAT_UNKNOWN, LOCK_COMPAT_UNKNOWN, LOCK_COMPAT_YES, LOCK_COMPAT_YES, LOCK_COMPAT_NO, LOCK_COMPAT_NO,
   LOCK_COMPAT_NO, LOCK_COMPAT_NO, LOCK_COMPAT_NO, LOCK_COMPAT_NO, LOCK_COMPAT_NO}
  ,
  /* SCH-M */
  {LOCK_COMPAT_UNKNOWN, LOCK_COMPAT_UNKNOWN, LOCK_COMPAT_YES, LOCK_COMPAT_NO, LOCK_COMPAT_NO, LOCK_COMPAT_NO,
   LOCK_COMPAT_NO, LOCK_COMPAT_NO, LOCK_COMPAT_UNKNOWN, LOCK_COMPAT_NO, LOCK_COMPAT_NO}
};

/*
 *
 *                         LOCK CONVERSION TABLE
 *
 * column : current lock mode (granted lock mode)
 * row    : request lock mode
 * -----------------------------------------------------------------------------------------------
 *         | N/A  NON2PL   NULL  SCH-S     IS      S     IX    SIX      U      X  SCH-M
 * -----------------------------------------------------------------------------------------------
 *     N/A | N/A     N/A    N/A    N/A    N/A    N/A    N/A    N/A    N/A    N/A    N/A
 *
 *  NON2PL | N/A     N/A    N/A    N/A    N/A    N/A    N/A    N/A    N/A    N/A    N/A
 *
 *    NULL | N/A     N/A   NULL  SCH-S     IS      S     IX    SIX      U      X  SCH-M
 *
 *   SCH-S | N/A     N/A  SCH-S  SCH-S     IS      S     IX    SIX    N/A      X  SCH-M
 *
 *      IS | N/A     N/A     IS     IS     IS      S     IX    SIX    N/A      X  SCH-M
 *
 *       S | N/A     N/A      S      S      S      S    SIX    SIX      U      X  SCH-M
 *
 *      IX | N/A     N/A     IX     IX     IX    SIX     IX    SIX    N/A      X  SCH-M
 *
 *     SIX | N/A     N/A    SIX    SIX    SIX    SIX    SIX    SIX    N/A      X  SCH-M
 *
 *       U | N/A     N/A      U    N/A    N/A      U    N/A    N/A      U      X    N/A
 *
 *       X | N/A     N/A      X      X      X      X      X      X      X      X  SCH-M
 *
 *   SCH-M | N/A     N/A  SCH-M  SCH-M  SCH-M  SCH-M  SCH-M  SCH-M    N/A  SCH-M  SCH-M
 * ------------------------------------------------------------------------------------------------
 * N/A : not applicable
 */

LOCK lock_Conv[11][11] = {
  /* N/A */
  {NA_LOCK, NA_LOCK, NA_LOCK, NA_LOCK, NA_LOCK, NA_LOCK, NA_LOCK, NA_LOCK,
   NA_LOCK, NA_LOCK, NA_LOCK}
  ,
  /* NON2PL */
  {NA_LOCK, NA_LOCK, NA_LOCK, NA_LOCK, NA_LOCK, NA_LOCK, NA_LOCK, NA_LOCK,
   NA_LOCK, NA_LOCK, NA_LOCK}
  ,
  /* NULL */
  {NA_LOCK, NA_LOCK, NULL_LOCK, SCH_S_LOCK, IS_LOCK, S_LOCK, IX_LOCK,
   SIX_LOCK, U_LOCK, X_LOCK, SCH_M_LOCK}
  ,
  /* SCH-S */
  {NA_LOCK, NA_LOCK, SCH_S_LOCK, SCH_S_LOCK, IS_LOCK, S_LOCK, IX_LOCK,
   SIX_LOCK, NA_LOCK, X_LOCK, SCH_M_LOCK}
  ,
  /* IS */
  {NA_LOCK, NA_LOCK, IS_LOCK, IS_LOCK, IS_LOCK, S_LOCK, IX_LOCK, SIX_LOCK,
   NA_LOCK, X_LOCK, SCH_M_LOCK}
  ,
  /* S */
  {NA_LOCK, NA_LOCK, S_LOCK, S_LOCK, S_LOCK, S_LOCK, SIX_LOCK, SIX_LOCK,
   U_LOCK, X_LOCK, SCH_M_LOCK}
  ,
  /* IX */
  {NA_LOCK, NA_LOCK, IX_LOCK, IX_LOCK, IX_LOCK, SIX_LOCK, IX_LOCK, SIX_LOCK,
   NA_LOCK, X_LOCK, SCH_M_LOCK}
  ,
  /* SIX */
  {NA_LOCK, NA_LOCK, SIX_LOCK, SIX_LOCK, SIX_LOCK, SIX_LOCK, SIX_LOCK,
   SIX_LOCK, NA_LOCK, X_LOCK, SCH_M_LOCK}
  ,
  /* U */
  {NA_LOCK, NA_LOCK, U_LOCK, NA_LOCK, NA_LOCK, U_LOCK, NA_LOCK, NA_LOCK,
   U_LOCK, X_LOCK, NA_LOCK}
  ,
  /* X */
  {NA_LOCK, NA_LOCK, X_LOCK, X_LOCK, X_LOCK, X_LOCK, X_LOCK, X_LOCK, X_LOCK,
   X_LOCK, SCH_M_LOCK}
  ,
  /* SCH-M */
  {NA_LOCK, NA_LOCK, SCH_M_LOCK, SCH_M_LOCK, SCH_M_LOCK, SCH_M_LOCK,
   SCH_M_LOCK, SCH_M_LOCK, NA_LOCK, SCH_M_LOCK, SCH_M_LOCK}
};
