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

/*
 * authenticate_constants.h -
 */

#ifndef _AUTHENTICATE_CONSTATNS_H_
#define _AUTHENTICATE_CONSTATNS_H_

/*
 * Authorization Class Names
 */
#define AU_ROOT_CLASS_NAME      CT_ROOT_NAME
#define AU_OLD_ROOT_CLASS_NAME  CT_AUTHORIZATIONS_NAME
#define AU_USER_CLASS_NAME      CT_USER_NAME
#define AU_PASSWORD_CLASS_NAME  CT_PASSWORD_NAME
#define AU_AUTH_CLASS_NAME      CT_AUTHORIZATION_NAME

#define AU_PUBLIC_USER_NAME     "PUBLIC"
#define AU_DBA_USER_NAME        "DBA"

/*
 * Authorization Types
 */
/* obsolete, should be using the definition from dbdef.h */

#define AU_TYPE         DB_AUTH
#define AU_NONE         DB_AUTH_NONE
#define AU_SELECT       DB_AUTH_SELECT
#define AU_INSERT       DB_AUTH_INSERT
#define AU_UPDATE       DB_AUTH_UPDATE
#define AU_DELETE       DB_AUTH_DELETE
#define AU_ALTER        DB_AUTH_ALTER
#define AU_INDEX        DB_AUTH_INDEX
#define AU_EXECUTE      DB_AUTH_EXECUTE

/*
 * Message id in the set MSGCAT_SET_AUTHORIZATION
 * in the message catalog MSGCAT_CATALOG_CUBRID (file cubrid.msg).
 */
#define MSGCAT_AUTH_INVALID_CACHE       1
#define MSGCAT_AUTH_CLASS_NAME          2
#define MSGCAT_AUTH_FROM_USER           3
#define MSGCAT_AUTH_USER_TITLE          4
#define MSGCAT_AUTH_UNDEFINED_USER      5
#define MSGCAT_AUTH_USER_NAME           6
#define MSGCAT_AUTH_USER_ID             7
#define MSGCAT_AUTH_USER_MEMBERS        8
#define MSGCAT_AUTH_USER_GROUPS         9
#define MSGCAT_AUTH_USER_NAME2          10
#define MSGCAT_AUTH_CURRENT_USER        11
#define MSGCAT_AUTH_ROOT_TITLE          12
#define MSGCAT_AUTH_ROOT_USERS          13
#define MSGCAT_AUTH_GRANT_DUMP_ERROR    14
#define MSGCAT_AUTH_AUTH_TITLE          15
#define MSGCAT_AUTH_USER_DIRECT_GROUPS  16

enum AU_OBJECT
{
  AU_OBJECT_CLASS,		/* TABLE, VIEW (_db_class) */
  AU_OBJECT_TRIGGER,		/* TRIGGER (_db_trigger) */
  AU_OBJECT_SERIAL,		/* SERIAL (db_serial) */
  AU_OBJECT_SERVER,		/* SERVER (db_server) */
  AU_OBJECT_SYNONYM,		/* SYNONYM (_db_synonym) */
  AU_OBJECT_PROCEDURE		/* PROCEDURE, FUNCTION  (_db_stored_procedure) */
};


/*
 * Mask to extract only the authorization bits from a cache.  This can also
 * be used as an absolute value to see if all possible authorizations have
 * been given
 * TODO : LP64
 */

#define AU_TYPE_MASK            0x7F
#define AU_GRANT_MASK           0x7F00
#define AU_FULL_AUTHORIZATION   0x7F7F
#define AU_NO_AUTHORIZATION     0

/*
 * the grant option for any particular authorization type is cached in the
 * same integer, shifted up eight bits.
 */

#define AU_GRANT_SHIFT          8

/* Invalid cache is identified when the high bit is on. */
#define AU_CACHE_INVALID        0x80000000

#define AU_MAX_PASSWORD_CHARS   31
#define AU_MAX_PASSWORD_BUF     2048
#define AU_MAX_COMMENT_CHARS    SM_MAX_COMMENT_LENGTH

#endif
