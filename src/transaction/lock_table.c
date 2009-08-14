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

#ifndef DB_NA
#define DB_NA           2
#endif

/*
 *
 *                       LOCK COMPATIBILITY TABLE
 *
 * column : current lock mode (granted lock mode)
 * row    : request lock mode
 * ----------------------------------------------------------------------------------------
 *         |   N/A   NON2PL   NULL     IS     NS      S     IX    SIX      U     NX      X
 * ----------------------------------------------------------------------------------------
 *    N/A  |   N/A     N/A    N/A     N/A    N/A    N/A    N/A    N/A    N/A    N/A    N/A
 *
 * NON2PL  |   N/A     N/A    N/A     N/A    N/A    N/A    N/A    N/A    N/A    N/A    N/A
 *
 *   NULL  |   N/A     N/A   True    True   True   True   True   True   True   True   True
 *
 *     IS  |   N/A     N/A   True    True    N/A   True   True   True    N/A    N/A  False
 *
 *     NS  |   N/A     N/A   True     N/A   True   True    N/A    N/A  False   True  False
 *
 *      S  |   N/A     N/A   True    True   True   True  False  False  False  False  False
 *
 *     IX  |   N/A     N/A   True    True    N/A  False   True  False    N/A    N/A  False
 *
 *    SIX  |   N/A     N/A   True    True    N/A  False  False  False    N/A    N/A  False
 *
 *      U  |   N/A     N/A   True     N/A   True   True    N/A    N/A  False  False  False
 *
 *     NX  |   N/A     N/A   True     N/A   True  False    N/A    N/A  False  False  False
 *
 *      X  |   N/A     N/A   True   False  False  False  False  False  False  False  False
 * ----------------------------------------------------------------------------------------
 * N/A : not appplicable
 */

int lock_Comp[11][11] = {
  {DB_NA, DB_NA, DB_NA, DB_NA, DB_NA, DB_NA, DB_NA, DB_NA, DB_NA, DB_NA, DB_NA}
  ,
  {DB_NA, DB_NA, DB_NA, DB_NA, DB_NA, DB_NA, DB_NA, DB_NA, DB_NA, DB_NA, DB_NA}
  ,
  {DB_NA, DB_NA, true, true, true, true, true, true, true, true, true}
  ,
  {DB_NA, DB_NA, true, true, DB_NA, true, true, true, DB_NA, DB_NA, false}
  ,
  {DB_NA, DB_NA, true, DB_NA, true, true, DB_NA, DB_NA, false, true, false}
  ,
  {DB_NA, DB_NA, true, true, true, true, false, false, false, false, false}
  ,
  {DB_NA, DB_NA, true, true, DB_NA, false, true, false, DB_NA, DB_NA, false}
  ,
  {DB_NA, DB_NA, true, true, DB_NA, false, false, false, DB_NA, DB_NA, false}
  ,
  {DB_NA, DB_NA, true, DB_NA, true, true, DB_NA, DB_NA, false, false, false}
  ,
  {DB_NA, DB_NA, true, DB_NA, true, false, DB_NA, DB_NA, false, false, false}
  ,
  {DB_NA, DB_NA, true, false, false, false, false, false, false, false, false}
  ,
};

/*
 *
 *                         LOCK CONVERSION TABLE
 *
 * column : current lock mode (granted lock mode)
 * row    : request lock mode
 * -------------------------------------------------------------------------------------
 *         |  N/A   NON2PL  NULL    IS     NS      S     IX    SIX      U     NX      X
 * -------------------------------------------------------------------------------------
 *    N/A  |  N/A    N/A    N/A    N/A    N/A    N/A    N/A    N/A    N/A    N/A    N/A
 *
 * NON2PL  |  N/A    N/A    N/A    N/A    N/A    N/A    N/A    N/A    N/A    N/A    N/A
 *
 *   NULL  |  N/A    N/A   NULL     IS     NS      S     IX    SIX      U     NX      X
 *
 *     IS  |  N/A    N/A     IS     IS    N/A      S     IX    SIX    N/A    N/A      X
 *
 *     NS  |  N/A    N/A     NS    N/A     NS      S    N/A    N/A      U     NX      X
 *
 *      S  |  N/A    N/A      S      S      S      S    SIX    SIX      U     NX      X
 *
 *     IX  |  N/A    N/A     IX     IX    N/A    SIX     IX    SIX    N/A    N/A      X
 *
 *    SIX  |  N/A    N/A    SIX    SIX    N/A    SIX    SIX    SIX    N/A    N/A      X
 *
 *      U  |  N/A    N/A      U    N/A      U      U    N/A    N/A      U      X      X
 *
 *     NX  |  N/A    N/A     NX    N/A     NX     NX    N/A    N/A      X     NX      X
 *
 *      X  |  N/A    N/A      X      X      X      X      X      X      X      X      X
 * -------------------------------------------------------------------------------------
 * N/A : not appplicable
 */

LOCK lock_Conv[11][11] = {
  {NA_LOCK, NA_LOCK, NA_LOCK, NA_LOCK, NA_LOCK, NA_LOCK, NA_LOCK, NA_LOCK,
   NA_LOCK, NA_LOCK, NA_LOCK}
  ,
  {NA_LOCK, NA_LOCK, NA_LOCK, NA_LOCK, NA_LOCK, NA_LOCK, NA_LOCK, NA_LOCK,
   NA_LOCK, NA_LOCK, NA_LOCK}
    ,
  {NA_LOCK, NA_LOCK, NULL_LOCK, IS_LOCK, NS_LOCK, S_LOCK, IX_LOCK, SIX_LOCK,
   U_LOCK, NX_LOCK, X_LOCK}
  ,
  {NA_LOCK, NA_LOCK, IS_LOCK, IS_LOCK, NA_LOCK, S_LOCK, IX_LOCK, SIX_LOCK,
   NA_LOCK, NA_LOCK, X_LOCK}
  ,
  {NA_LOCK, NA_LOCK, NS_LOCK, NA_LOCK, NS_LOCK, S_LOCK, NA_LOCK, NA_LOCK,
   U_LOCK, NX_LOCK, X_LOCK}
  ,
  {NA_LOCK, NA_LOCK, S_LOCK, S_LOCK, S_LOCK, S_LOCK, SIX_LOCK, SIX_LOCK,
   U_LOCK, NX_LOCK, X_LOCK}
  ,
  {NA_LOCK, NA_LOCK, IX_LOCK, IX_LOCK, NA_LOCK, SIX_LOCK, IX_LOCK, SIX_LOCK,
   NA_LOCK, NA_LOCK, X_LOCK}
  ,
  {NA_LOCK, NA_LOCK, SIX_LOCK, SIX_LOCK, NA_LOCK, SIX_LOCK, SIX_LOCK, SIX_LOCK,
   NA_LOCK, NA_LOCK, X_LOCK}
  ,
  {NA_LOCK, NA_LOCK, U_LOCK, NA_LOCK, U_LOCK, U_LOCK, NA_LOCK, NA_LOCK,
   U_LOCK, X_LOCK, X_LOCK}
  ,
  {NA_LOCK, NA_LOCK, NX_LOCK, NA_LOCK, NX_LOCK, NX_LOCK, NA_LOCK, NA_LOCK,
   X_LOCK, NX_LOCK, X_LOCK}
  ,
  {NA_LOCK, NA_LOCK, X_LOCK, X_LOCK, X_LOCK, X_LOCK, X_LOCK, X_LOCK,
   X_LOCK, X_LOCK, X_LOCK}
};
