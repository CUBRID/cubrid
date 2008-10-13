/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * rvcl.c - 
 * 									       
 * 	Overview: RECOVERY FUNCTIONS (AT CLIENT)			       
 * 									       
 * This module list all undo and recovery functions available to the log and   
 * recovery manager in the client. The actual implementation of the functions  
 * is located somewhere else. The name of a recovery function follows the      
 * module prefix conventions where the function is located and it should also  
 * include the "rv" prefix to indicate that this is a recovery function.       
 * Example: foo_rv_hello ...> function hello in module foo		       
 * The log manager provides interfaces using constants instead of the recovery 
 * function. The recovery functions are registered into an array of recovery   
 * functions. The index of the array is used to interact with the log and      
 * recovery manager. Every element of the array contains the following 	       
 * structure.								       
 * 									       
 * rvfun_array:								       
 *   recv_index   -->> Used for debugging purposes			       
 *   undo_fun     -->> Undo function associated with the action		       
 *   redo_fun     -->> Redo function associated with the action		       
 *   dump_undofun -->> Function to dump undo information (for debugging)       
 *   dump_redofun -->> Function to dump redo information (for debugging)       
 * 									       
 * A similar array of recovery functions available for server actions such as  
 * multimedia resides in the server (see rv.c)				       
 * 									       
 */

#ident "$Id$"

#include "config.h"

#include "recover_cl.h"
#include "error_manager.h"
#include "elo_recovery.h"

/*
 * THE ARRAY OF CLIENT RECOVERY FUNCTIONS                    
 */
struct rvcl_fun RVCL_fun[] = {
  {RVMM_INTERFACE,
   esm_undo,
   esm_redo,
   esm_dump,
   esm_dump},
};

/*
 * rv_rcvcl_index_string - RETURN STRING ASSOCIATED WITH THE CLIENT LOG_RVINDEX
 *
 * return: 
 *
 *   rcvindex(in): Numeric recovery index
 *
 * NOTE:Return the string index associated with the given recovery
 *              index.
 */
const char *
rv_rcvcl_index_string (LOG_RCVCLIENT_INDEX rcvindex)
{
  switch (rcvindex)
    {
    case RVMM_INTERFACE:
      return "RVMM_INTERFACE";
    default:
      break;
    }

  return "UNKNOWN";

}
