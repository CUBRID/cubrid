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
 * lock_table.c - lock managment module. (client + server)
 *          Definition of lock matrix tables
 */

#ident "$Id$"

#include "config.h"

#include "storage_common.h"
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

/*
 *
 *                       LOCK COMPATIBILITY TABLE
 *
 * column : current lock mode (granted lock mode)
 * row    : request lock mode
 * ----------------------------------------------------------------------------------------------------------
 *         |   N/A  NON2PL  NULL  SCH-S     IS       S      IX      BU    SIX       U       X   SCH-M
 * ----------------------------------------------------------------------------------------------------------
 *    N/A  |   N/A    N/A    N/A    N/A    N/A     N/A     N/A     N/A    N/A     N/A     N/A     N/A
 *
 * NON2PL  |   N/A    N/A    N/A    N/A    N/A     N/A     N/A     N/A    N/A     N/A     N/A     N/A
 *
 *   NULL  |   N/A    N/A   True   True   True    True    True    True   True    True    True    True
 *
 *  SCH-S  |   N/A    N/A   True   True   True    True    True    True   True     N/A    True   False
 *
 *     IS  |   N/A    N/A   True   True   True    True    True   False   True     N/A   False   False
 *
 *      S  |   N/A    N/A   True   True   True    True   False   False  False   False   False   False
 *
 *     IX  |   N/A    N/A   True   True   True   False    True   False  False     N/A   False   False
 *
 *     BU  |   N/A    N/A   True   True  False   False   False    True  False     N/A   False   False
 *
 *    SIX  |   N/A    N/A   True   True   True   False   False   False  False     N/A   False   False
 *
 *      U  |   N/A    N/A   True    N/A    N/A    True     N/A     N/A    N/A   False   False     N/A
 *
 *      X  |   N/A    N/A   True   True  False   False   False   False  False   False   False   False
 *
 *  SCH-M  |   N/A    N/A   True  False  False   False   False   False  False     N/A   False   False
 * ----------------------------------------------------------------------------------------------------------
 * N/A : not applicable
 */

/* *INDENT-OFF* */
LOCK_COMPATIBILITY lock_Comp[12][12] = {
  /* N/A */
  { /* N/A */ LOCK_COMPAT_UNKNOWN, /* NON2PL */ LOCK_COMPAT_UNKNOWN, /* NULL */ LOCK_COMPAT_UNKNOWN,
    /* SCH-S */ LOCK_COMPAT_UNKNOWN, /* IS */ LOCK_COMPAT_UNKNOWN, /* S */ LOCK_COMPAT_UNKNOWN,
    /* IX */ LOCK_COMPAT_UNKNOWN, /* BU */ LOCK_COMPAT_UNKNOWN, /* SIX */ LOCK_COMPAT_UNKNOWN,
    /* U */ LOCK_COMPAT_UNKNOWN, /* X */ LOCK_COMPAT_UNKNOWN, /* SCH-M */ LOCK_COMPAT_UNKNOWN},

  /* NON2PL */
  { /* N/A */ LOCK_COMPAT_UNKNOWN, /* NON2PL */ LOCK_COMPAT_UNKNOWN, /* NULL */ LOCK_COMPAT_UNKNOWN,
    /* SCH-S */ LOCK_COMPAT_UNKNOWN, /* IS */ LOCK_COMPAT_UNKNOWN, /* S */ LOCK_COMPAT_UNKNOWN,
    /* IX */ LOCK_COMPAT_UNKNOWN, /* BU */ LOCK_COMPAT_UNKNOWN, /* SIX */ LOCK_COMPAT_UNKNOWN,
    /* U */ LOCK_COMPAT_UNKNOWN, /* X */ LOCK_COMPAT_UNKNOWN, /* SCH-M */ LOCK_COMPAT_UNKNOWN},

  /* NULL */
  { /* N/A */ LOCK_COMPAT_UNKNOWN, /* NON2PL */ LOCK_COMPAT_UNKNOWN, /* NULL */ LOCK_COMPAT_YES,
    /* SCH-S */ LOCK_COMPAT_YES, /* IS */ LOCK_COMPAT_YES, /* S */ LOCK_COMPAT_YES,
    /* IX */ LOCK_COMPAT_YES, /* BU */ LOCK_COMPAT_YES, /* SIX */ LOCK_COMPAT_YES,
    /* U */ LOCK_COMPAT_YES, /* X */ LOCK_COMPAT_YES, /* SCH-M */ LOCK_COMPAT_YES},

  /* SCH-S */
  { /* N/A */ LOCK_COMPAT_UNKNOWN, /* NON2PL */ LOCK_COMPAT_UNKNOWN, /* NULL */ LOCK_COMPAT_YES,
    /* SCH-S */ LOCK_COMPAT_YES, /* IS */ LOCK_COMPAT_YES, /* S */ LOCK_COMPAT_YES,
    /* IX */ LOCK_COMPAT_YES, /* BU */ LOCK_COMPAT_YES, /* SIX */ LOCK_COMPAT_YES,
    /* U */ LOCK_COMPAT_UNKNOWN, /* X */ LOCK_COMPAT_YES, /* SCH-M */ LOCK_COMPAT_NO},

  /* IS */
  { /* N/A */ LOCK_COMPAT_UNKNOWN, /* NON2PL */ LOCK_COMPAT_UNKNOWN, /* NULL */ LOCK_COMPAT_YES,
    /* SCH-S */ LOCK_COMPAT_YES, /* IS */ LOCK_COMPAT_YES, /* S */ LOCK_COMPAT_YES,
    /* IX */ LOCK_COMPAT_YES, /* BU */ LOCK_COMPAT_NO, /* SIX */ LOCK_COMPAT_YES,
    /* U */ LOCK_COMPAT_UNKNOWN, /* X */ LOCK_COMPAT_NO, /* SCH-M */ LOCK_COMPAT_NO},

  /* S */
  { /* N/A */ LOCK_COMPAT_UNKNOWN, /* NON2PL */ LOCK_COMPAT_UNKNOWN, /* NULL */ LOCK_COMPAT_YES,
    /* SCH-S */ LOCK_COMPAT_YES, /* IS */ LOCK_COMPAT_YES, /* S */ LOCK_COMPAT_YES,
    /* IX */ LOCK_COMPAT_NO, /* BU */ LOCK_COMPAT_NO, /* SIX */ LOCK_COMPAT_NO,
    /* U */ LOCK_COMPAT_NO, /* X */ LOCK_COMPAT_NO, /* SCH-M */ LOCK_COMPAT_NO},

  /* IX */
  { /* N/A */ LOCK_COMPAT_UNKNOWN, /* NON2PL */ LOCK_COMPAT_UNKNOWN, /* NULL */ LOCK_COMPAT_YES,
    /* SCH-S */ LOCK_COMPAT_YES, /* IS */ LOCK_COMPAT_YES, /* S */ LOCK_COMPAT_NO,
    /* IX */ LOCK_COMPAT_YES, /* BU */ LOCK_COMPAT_NO, /* SIX */ LOCK_COMPAT_NO,
    /* U */ LOCK_COMPAT_UNKNOWN, /* X */ LOCK_COMPAT_NO, /* SCH-M */ LOCK_COMPAT_NO},

  /* BU_LOCK */
  { /* N/A */ LOCK_COMPAT_UNKNOWN, /* NON2PL */ LOCK_COMPAT_UNKNOWN, /* NULL */ LOCK_COMPAT_YES,
    /* SCH-S */ LOCK_COMPAT_YES, /* IS */ LOCK_COMPAT_NO, /* S */ LOCK_COMPAT_NO,
    /* IX */ LOCK_COMPAT_NO, /* BU */ LOCK_COMPAT_YES, /* SIX */ LOCK_COMPAT_NO,
    /* U */ LOCK_COMPAT_UNKNOWN, /* X */ LOCK_COMPAT_NO, /* SCH-M */ LOCK_COMPAT_NO},

  /* SIX */
  { /* N/A */ LOCK_COMPAT_UNKNOWN, /* NON2PL */ LOCK_COMPAT_UNKNOWN, /* NULL */ LOCK_COMPAT_YES,
    /* SCH-S */ LOCK_COMPAT_YES, /* IS */ LOCK_COMPAT_YES, /* S */ LOCK_COMPAT_NO,
    /* IX */ LOCK_COMPAT_NO, /* BU */ LOCK_COMPAT_NO, /* SIX */ LOCK_COMPAT_NO,
    /* U */ LOCK_COMPAT_UNKNOWN, /* X */ LOCK_COMPAT_NO, /* SCH-M */ LOCK_COMPAT_NO},

  /* U */
  { /* N/A */ LOCK_COMPAT_UNKNOWN, /* NON2PL */ LOCK_COMPAT_UNKNOWN, /* NULL */ LOCK_COMPAT_YES,
    /* SCH-S */ LOCK_COMPAT_UNKNOWN, /* IS */ LOCK_COMPAT_UNKNOWN, /* S */ LOCK_COMPAT_YES,
    /* IX */ LOCK_COMPAT_UNKNOWN, /* BU */ LOCK_COMPAT_UNKNOWN, /* SIX */ LOCK_COMPAT_UNKNOWN,
    /* U */ LOCK_COMPAT_NO, /* X */ LOCK_COMPAT_NO, /* SCH-M */ LOCK_COMPAT_UNKNOWN},

  /* X */
  { /* N/A */ LOCK_COMPAT_UNKNOWN, /* NON2PL */ LOCK_COMPAT_UNKNOWN, /* NULL */ LOCK_COMPAT_YES,
    /* SCH-S */ LOCK_COMPAT_YES, /* IS */ LOCK_COMPAT_NO, /* S */ LOCK_COMPAT_NO,
    /* IX */ LOCK_COMPAT_NO, /* BU */ LOCK_COMPAT_NO, /* SIX */ LOCK_COMPAT_NO,
    /* U */ LOCK_COMPAT_NO, /* X */ LOCK_COMPAT_NO, /* SCH-M */ LOCK_COMPAT_NO},

  /* SCH-M */
  { /* N/A */ LOCK_COMPAT_UNKNOWN, /* NON2PL */ LOCK_COMPAT_UNKNOWN, /* NULL */ LOCK_COMPAT_YES,
    /* SCH-S */ LOCK_COMPAT_NO, /* IS */ LOCK_COMPAT_NO, /* S */ LOCK_COMPAT_NO,
    /* IX */ LOCK_COMPAT_NO, /* BU */ LOCK_COMPAT_NO, /* SIX */ LOCK_COMPAT_NO,
    /* U */ LOCK_COMPAT_UNKNOWN, /* X */ LOCK_COMPAT_NO, /* SCH-M */ LOCK_COMPAT_NO}
};
/* *INDENT-ON* */

/*
 *
 *                         LOCK CONVERSION TABLE
 *
 * column : current lock mode (granted lock mode)
 * row    : request lock mode
 * -----------------------------------------------------------------------------------------------
 *         | N/A  NON2PL   NULL  SCH-S     IS      S     IX     BU   SIX      U      X  SCH-M
 * -----------------------------------------------------------------------------------------------
 *     N/A | N/A     N/A    N/A    N/A    N/A    N/A    N/A    N/A   N/A    N/A    N/A    N/A
 *
 *  NON2PL | N/A     N/A    N/A    N/A    N/A    N/A    N/A    N/A   N/A    N/A    N/A    N/A
 *
 *    NULL | N/A     N/A   NULL  SCH-S     IS      S     IX     BU   SIX      U      X  SCH-M
 *
 *   SCH-S | N/A     N/A  SCH-S  SCH-S     IS      S     IX     BU   SIX    N/A      X  SCH-M
 *
 *      IS | N/A     N/A     IS     IS     IS      S     IX      X   SIX    N/A      X  SCH-M
 *
 *       S | N/A     N/A      S      S      S      S    SIX      X   SIX      U      X  SCH-M
 *
 *      IX | N/A     N/A     IX     IX     IX    SIX     IX      X   SIX    N/A      X  SCH-M
 *
 *      BU | N/A     N/A     BU     BU     BU      X     BU     BU    BU    N/A      X  SCH-M
 *
 *     SIX | N/A     N/A    SIX    SIX    SIX    SIX    SIX      X   SIX    N/A      X  SCH-M
 *
 *       U | N/A     N/A      U    N/A    N/A      U    N/A    N/A   N/A      U      X    N/A
 *
 *       X | N/A     N/A      X      X      X      X      X      X     X      X      X  SCH-M
 *
 *   SCH-M | N/A     N/A  SCH-M  SCH-M  SCH-M  SCH-M  SCH-M  SCH-M SCH-M    N/A  SCH-M  SCH-M
 * -----------------------------------------------------------------------------------------------
 * N/A : not applicable
 */

/* *INDENT-OFF* */
LOCK lock_Conv[12][12] = {
  /* N/A */
  { /* N/A */ NA_LOCK, /* NON2PL */ NA_LOCK, /* NULL */ NA_LOCK, /* SCH-S */ NA_LOCK,
    /* IS */ NA_LOCK, /* S */ NA_LOCK, /* IX */ NA_LOCK, /* BU */ NA_LOCK, /* SIX */ NA_LOCK,
    /* U */ NA_LOCK, /* X */ NA_LOCK, /* SCH-M */ NA_LOCK},

  /* NON2PL */
  { /* N/A */ NA_LOCK, /* NON2PL */ NA_LOCK, /* NULL */ NA_LOCK, /* SCH-S */ NA_LOCK,
    /* IS */ NA_LOCK, /* S */ NA_LOCK, /* IX */ NA_LOCK, /* BU */ NA_LOCK, /* SIX */ NA_LOCK,
    /* U */ NA_LOCK, /* X */ NA_LOCK, /* SCH-M */ NA_LOCK},

  /* NULL */
  { /* N/A */ NA_LOCK, /* NON2PL */ NA_LOCK, /* NULL */ NULL_LOCK, /* SCH-S */ SCH_S_LOCK,
    /* IS */ IS_LOCK, /* S */ S_LOCK, /* IX */ IX_LOCK, /* BU */ BU_LOCK, /* SIX */ SIX_LOCK,
    /* U */ U_LOCK, /* X */ X_LOCK, /* SCH-M */ SCH_M_LOCK},

  /* SCH-S */
  { /* N/A */ NA_LOCK, /* NON2PL */ NA_LOCK, /* NULL */ SCH_S_LOCK, /* SCH-S */ SCH_S_LOCK,
    /* IS */ IS_LOCK, /* S */ S_LOCK, /* IX */ IX_LOCK, /* BU */ BU_LOCK, /* SIX */ SIX_LOCK,
    /* U */ NA_LOCK, /* X */ X_LOCK, /* SCH-M */ SCH_M_LOCK},

  /* IS */
  { /* N/A */ NA_LOCK, /* NON2PL */ NA_LOCK, /* NULL */ IS_LOCK, /* SCH-S */ IS_LOCK,
    /* IS */ IS_LOCK, /* S */ S_LOCK, /* IX */ IX_LOCK, /* BU */ X_LOCK, /* SIX */ SIX_LOCK,
    /* U */ NA_LOCK, /* X */ X_LOCK, /* SCH-M */ SCH_M_LOCK},

  /* S */
  { /* N/A */ NA_LOCK, /* NON2PL */ NA_LOCK, /* NULL */ S_LOCK, /* SCH-S */ S_LOCK,
    /* IS */ S_LOCK, /* S */ S_LOCK, /* IX */ SIX_LOCK, /* BU */ X_LOCK, /* SIX */ SIX_LOCK,
    /* U */ U_LOCK, /* X */ X_LOCK, /* SCH-M */ SCH_M_LOCK},

  /* IX */
  { /* N/A */ NA_LOCK, /* NON2PL */ NA_LOCK, /* NULL */ IX_LOCK, /* SCH-S */ IX_LOCK,
    /* IS */ IX_LOCK, /* S */ SIX_LOCK, /* IX */ IX_LOCK, /* BU */ X_LOCK, /* SIX */ SIX_LOCK,
    /* U */ NA_LOCK, /* X */ X_LOCK, /* SCH-M */ SCH_M_LOCK},

  /* BU */
  { /* N/A */ NA_LOCK, /* NON2PL */ NA_LOCK, /* NULL */ BU_LOCK, /* SCH-S */ BU_LOCK,
    /* IS */ BU_LOCK, /* S */ X_LOCK, /* IX */ BU_LOCK, /* BU */ BU_LOCK, /* SIX */ BU_LOCK,
    /* U */ NA_LOCK, /* X */ X_LOCK, /* SCH-M */ SCH_M_LOCK},

  /* SIX */
  { /* N/A */ NA_LOCK, /* NON2PL */ NA_LOCK, /* NULL */ SIX_LOCK, /* SCH-S */ SIX_LOCK,
    /* IS */ SIX_LOCK, /* S */ SIX_LOCK, /* IX */ SIX_LOCK, /* BU */ X_LOCK, /* SIX */ SIX_LOCK,
    /* U */ NA_LOCK, /* X */ X_LOCK, /* SCH-M */ SCH_M_LOCK},

  /* U */
  { /* N/A */ NA_LOCK, /* NON2PL */ NA_LOCK, /* NULL */ U_LOCK, /* SCH-S */ NA_LOCK,
    /* IS */ NA_LOCK, /* S */ U_LOCK, /* IX */ NA_LOCK, /* BU */ NA_LOCK, /* SIX */ NA_LOCK,
    /* U */ U_LOCK, /* X */ X_LOCK, /* SCH-M */ NA_LOCK},

  /* X */
  { /* N/A */ NA_LOCK, /* NON2PL */ NA_LOCK, /* NULL */ X_LOCK, /* SCH-S */ X_LOCK,
    /* IS */ X_LOCK, /* S */ X_LOCK, /* IX */ X_LOCK, /* BU */ X_LOCK, /* SIX */ X_LOCK,
    /* U */ X_LOCK, /* X */ X_LOCK, /* SCH-M */ SCH_M_LOCK},

  /* SCH-M */
  { /* N/A */ NA_LOCK, /* NON2PL */ NA_LOCK, /* NULL */ SCH_M_LOCK, /* SCH-S */ SCH_M_LOCK,
    /* IS */ SCH_M_LOCK, /* S */ SCH_M_LOCK, /* IX */ SCH_M_LOCK, /* BU */ SCH_M_LOCK, /* SIX */ SCH_M_LOCK,
    /* U */ NA_LOCK, /* X */ SCH_M_LOCK, /* SCH-M */ SCH_M_LOCK}
};
/* *INDENT-ON* */
