/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * lkbt.c - 
 *
 * 	Overview: LOCK MANAGMENT MODULE. (CLIENT + SERVER)		       
 *                ** Definition of lock matrix tables **		       
 * 									       
 * The definition of the lock compatibilty and conversion tables is included   
 * in this file. For an explanation of these table see the lk.c		       
 * 									       
 */

#ident "$Id$"

#include "config.h"

#include "common.h"

#ifndef DB_NA
#define DB_NA           2
#endif

/*
 *                                                                             
 *                       LOCK COMPATIBILITY TABLE                              
 *                                                                             
 * column : current lock mode (granted lock mode)                              
 * row    : request lock mode                                                  
 * --------------------------------------------------------------------------  
 *         |   NULL     IS     NS      S     IX    SIX      U     NX      X    
 * --------------------------------------------------------------------------  
 *   NULL  |   True   True   True   True   True   True   True   True   True    
 *                                                                             
 *     IS  |   True   True    N/A   True   True   True    N/A    N/A  False    
 *                                                                             
 *     NS  |   True    N/A   True   True    N/A    N/A  False   True  False    
 *                                                                             
 *      S  |   True   True   True   True  False  False  False  False  False    
 *                                                                             
 *     IX  |   True   True    N/A  False   True  False    N/A    N/A  False    
 *                                                                             
 *    SIX  |   True   True    N/A  False  False  False    N/A    N/A  False    
 *                                                                             
 *      U  |   True    N/A   True   True    N/A    N/A  False  False  False    
 *                                                                             
 *     NX  |   True    N/A   True  False    N/A    N/A  False  False  False    
 *                                                                             
 *      X  |   True  False  False  False  False  False  False  False  False    
 * --------------------------------------------------------------------------  
 * N/A : not appplicable                                                       
 */

bool lock_Comp[9][9] = {
  {true, true, true, true, true, true, true, true, true}
  ,
  {true, true, DB_NA, true, true, true, DB_NA, DB_NA, false}
  ,
  {true, DB_NA, true, true, DB_NA, DB_NA, false, true, false}
  ,
  {true, true, true, true, false, false, false, false, false}
  ,
  {true, true, DB_NA, false, true, false, DB_NA, DB_NA, false}
  ,
  {true, true, DB_NA, false, false, false, DB_NA, DB_NA, false}
  ,
  {true, DB_NA, true, true, DB_NA, DB_NA, false, false, false}
  ,
  {true, DB_NA, true, false, DB_NA, DB_NA, false, false, false}
  ,
  {true, false, false, false, false, false, false, false, false}
  ,
};

/*
 *                                                                             
 *                         LOCK CONVERSION TABLE                               
 *                                                                             
 * column : current lock mode (granted lock mode)                              
 * row    : request lock mode                                                  
 * --------------------------------------------------------------------------  
 *         |   NULL     IS     NS      S     IX    SIX      U     NX      X    
 * --------------------------------------------------------------------------  
 *   NULL  |   NULL     IS     NS      S     IX    SIX      U     NX      X    
 *                                                                             
 *     IS  |     IS     IS    N/A      S     IX    SIX    N/A    N/A      X    
 *                                                                             
 *     NS  |     NS    N/A     NS      S    N/A    N/A      U     NX      X    
 *                                                                             
 *      S  |      S      S      S      S    SIX    SIX      U     NX      X    
 *                                                                             
 *     IX  |     IX     IX    N/A    SIX     IX    SIX    N/A    N/A      X    
 *                                                                             
 *    SIX  |    SIX    SIX    N/A    SIX    SIX    SIX    N/A    N/A      X    
 *                                                                             
 *      U  |      U    N/A      U      U    N/A    N/A      U      X      X    
 *                                                                             
 *     NX  |     NX    N/A     NX     NX    N/A    N/A      X     NX      X    
 *                                                                             
 *      X  |      X      X      X      X      X      X      X      X      X    
 * --------------------------------------------------------------------------  
 * N/A : not appplicable                                                       
 */

LOCK lock_Conv[9][9] = {
  {NULL_LOCK, IS_LOCK, NS_LOCK, S_LOCK, IX_LOCK, SIX_LOCK, U_LOCK, NX_LOCK,
   X_LOCK}
  ,
  {IS_LOCK, IS_LOCK, NA_LOCK, S_LOCK, IX_LOCK, SIX_LOCK, NA_LOCK, NA_LOCK,
   X_LOCK}
  ,
  {NS_LOCK, NA_LOCK, NS_LOCK, S_LOCK, NA_LOCK, NA_LOCK, U_LOCK, NX_LOCK,
   X_LOCK}
  ,
  {S_LOCK, S_LOCK, S_LOCK, S_LOCK, SIX_LOCK, SIX_LOCK, U_LOCK, NX_LOCK,
   X_LOCK}
  ,
  {IX_LOCK, IX_LOCK, NA_LOCK, SIX_LOCK, IX_LOCK, SIX_LOCK, NA_LOCK, NA_LOCK,
   X_LOCK}
  ,
  {SIX_LOCK, SIX_LOCK, NA_LOCK, SIX_LOCK, SIX_LOCK, SIX_LOCK, NA_LOCK,
   NA_LOCK, X_LOCK}
  ,
  {U_LOCK, NA_LOCK, U_LOCK, U_LOCK, NA_LOCK, NA_LOCK, U_LOCK, X_LOCK, X_LOCK}
  ,
  {NX_LOCK, NA_LOCK, NX_LOCK, NX_LOCK, NA_LOCK, NA_LOCK, X_LOCK, NX_LOCK,
   X_LOCK}
  ,
  {X_LOCK, X_LOCK, X_LOCK, X_LOCK, X_LOCK, X_LOCK, X_LOCK, X_LOCK, X_LOCK}
  ,
};
