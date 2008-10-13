/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * schema_manager_1.c - "Schema" (in the SQL standard sense) implementation
 *
 * Note: 
 *  This is a _very_ primitive initial implementation to achieve
 *  just one purpose: to have a formal way of grabbing the current
 *  schema for use as a class/vclass/proxy qualifier.
 */

#ident "$Id$"

#include "config.h"

#include "dbtype.h"
#include "authenticate.h"
#include "qp_str.h"
#include "schema_manager_1.h"


/*
 * SCHEMA_DEFINITION							      
 *									      
 * description: 							      
 *    Maintains information about an SQL schema.			      
 */

/*
   NOTE: This is simple-minded implementation for now since we don't yet
         support CREATE SCHEMA, SET SCHEMA, and associated statements.
 */

typedef struct schema_def
{

  /* This is the default qualifier for class/vclass/proxy names */
  char name[DB_MAX_SCHEMA_LENGTH + 4];

  /* The only user who can delete this schema. */
  /* But, note that entry level doesn't support DROP SCHEMA anyway */
  MOP owner;

  /* The next three items are currently not used at all.
     They are simply a reminder of future todo's.
     Although entry level SQL leaves out many schema management functions,
     entry level SQL does include specification of tables, views, and grants
     as part of CREATE SCHEMA statements. */

  void *tables;			/* unused dummy                             */
  void *views;			/* unused dummy                             */
  void *grants;			/* unused dummy                             */

} SCHEMA_DEF;

/*
 * Current_schema							      
 *									      
 * description: 							      
 *    This is the current schema.  The schema name in this structure is the   
 *    default qualifier for any class/vclass/proxy names which are not        
 *    explicitly qualified.                                                   
 *    This structure should only be changed with sc_set_current_schema which  
 *    currently is called only from AU_SET_USER                               
 */

static SCHEMA_DEF Current_Schema = { {'\0'}, NULL, NULL, NULL, NULL };

/*
 * sc_set_current_schema() 
 *      return: NO_ERROR if successful                                                
 *              ER_FAILED if any problem extracting name from authorization              
 * 
 *  user(in) : MOP for authorization (user)
 * 
 * Note :
 *    This function is temporary kludge to allow initial implementation       
 *    of schema names.  It is to be called from just one place: AU_SET_USER.  
 *    Entry level SQL specifies that a schema name is equal to the            
 *    <authorization user name>, so this function simply extracts the user    
 *    name from the input argument, makes it lower case, and uses that name   
 *    as the schema name.                                                     
 * 
 * 
 */

int
sc_set_current_schema (MOP user)
{
  int error = ER_FAILED;
  char *wsp_user_name;

  Current_Schema.name[0] = '\0';
  Current_Schema.owner = user;
  wsp_user_name = au_get_user_name (user);

  if (wsp_user_name == NULL)
    {
      return error;
    }

  /* As near as I can tell, this is the most generalized  */
  /* case conversion function on our system.  If it's not */
  /* the most general, change this code accordingly.      */
  if (intl_mbs_lower (wsp_user_name, Current_Schema.name) == 0)
    {
      /* Last time I looked, intl_mbs_lower always returns 0.      */
      /* However, it does malloc without checking result, so  */
      /* perhaps someday it might return an error.            */
      error = NO_ERROR;
    }
  ws_free_string (wsp_user_name);

  /* If there's any error, it's not obvious what can be done about it here. */
  /* Probably some code needs to be fixed in the caller: AU_SET_USER        */
  return error;
}

/*
 * sc_current_schema_name() - Returns current schema name which is 
 *                            the default qualifier for otherwise 
 *                            unqualified class/vclass/proxy names 
 *      return: pointer to current schema name
 * 
 */

const char *
sc_current_schema_name (void)
{
  return (const char *) &(Current_Schema.name);
}
