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
 * cas_protocol.h -
 */

#ifndef _CAS_PROTOCOL_H_
#define _CAS_PROTOCOL_H_

#ifdef __cplusplus
extern "C"
{
#endif

#ident "$Id$"

#define SRV_CON_CLIENT_INFO_SIZE	10
#define SRV_CON_CLIENT_MAGIC_LEN	5
#define SRV_CON_CLIENT_MAGIC_STR	"CUBRK"
#define SRV_CON_MSG_IDX_CLIENT_TYPE	5

/* 8th and 9th-byte (index 7 and 8) are reserved for backward compatibility.
 * 8.4.0 patch 1 or earlier versions hold minor and patch version on them.
 */
#define SRV_CON_MSG_IDX_PROTO_VERSION   6
#define SRV_CON_MSG_IDX_FUNCTION_FLAG   7
#define SRV_CON_MSG_IDX_RESERVED2       8
/* For backward compatibility */
#define SRV_CON_MSG_IDX_MAJOR_VER	(SRV_CON_MSG_IDX_PROTO_VERSION)
#define SRV_CON_MSG_IDX_MINOR_VER	(SRV_CON_MSG_IDX_FUNCTION_FLAG)
#define SRV_CON_MSG_IDX_PATCH_VER	(SRV_CON_MSG_IDX_RESERVED2)

#define SRV_CON_DBNAME_SIZE		32
#define SRV_CON_DBUSER_SIZE		32
#define SRV_CON_DBPASSWD_SIZE		32
#define SRV_CON_URL_SIZE                512
#define SRV_CON_DBSESS_ID_SIZE		20
#define SRV_CON_VER_STR_MAX_SIZE        20

#define SRV_CON_DB_INFO_SIZE \
        (SRV_CON_DBNAME_SIZE + SRV_CON_DBUSER_SIZE + SRV_CON_DBPASSWD_SIZE + \
         SRV_CON_URL_SIZE + SRV_CON_DBSESS_ID_SIZE)
#define SRV_CON_DB_INFO_SIZE_PRIOR_8_4_0 \
        (SRV_CON_DBNAME_SIZE + SRV_CON_DBUSER_SIZE + SRV_CON_DBPASSWD_SIZE + \
         SRV_CON_URL_SIZE)
#define SRV_CON_DB_INFO_SIZE_PRIOR_8_2_0 \
        (SRV_CON_DBNAME_SIZE + SRV_CON_DBUSER_SIZE + SRV_CON_DBPASSWD_SIZE)

  typedef enum
  {
    CAS_CLIENT_TYPE_MIN = 0,
    CAS_CLIENT_NONE = 0,
    CAS_CLIENT_CCI = 1,
    CAS_CLIENT_ODBC = 2,
    CAS_CLIENT_JDBC = 3,
    CAS_CLIENT_PHP = 4,
    CAS_CLIENT_OLEDB = 5,
    CAS_CLIENT_TYPE_MAX = 5
  } CAS_CLIENT_TYPE;

  typedef enum
  {
    CAS_INFO_STATUS_INACTIVE = 0,
    CAS_INFO_STATUS_ACTIVE = 1
  } CAS_INFO_STATUS_TYPE;

  typedef enum
  {
    CAS_INFO_STATUS = 0,
    CAS_INFO_RESERVED_1 = 1,
    CAS_INFO_RESERVED_2 = 2,
    CAS_INFO_ADDITIONAL_FLAG = 3
  } CAS_INFO_TYPE;

  typedef enum
  {
    CAS_CHANGE_MODE_UNKNOWN = 0,
    CAS_CHANGE_MODE_AUTO = 1,
    CAS_CHANGE_MODE_KEEP = 2,
    CAS_CHANGE_MODE_DEFAULT = CAS_CHANGE_MODE_AUTO
  } CAS_CHANGE_MODE;

#define CAS_INFO_FLAG_MASK_AUTOCOMMIT		0x01
#define CAS_INFO_FLAG_MASK_FORCE_OUT_TRAN       0x02
#define CAS_INFO_FLAG_MASK_NEW_SESSION_ID       0x04

#define CAS_INFO_SIZE			(4)
#define CAS_INFO_RESERVED_DEFAULT	(-1)

#define MSG_HEADER_INFO_SIZE        CAS_INFO_SIZE
#define MSG_HEADER_MSG_SIZE         ((int) sizeof(int))
#define MSG_HEADER_SIZE             (MSG_HEADER_INFO_SIZE +  MSG_HEADER_MSG_SIZE)

#define BROKER_INFO_SIZE			8
#define BROKER_RENEWED_ERROR_CODE		0x80
#define BROKER_SUPPORT_HOLDABLE_RESULT          0x40
/* Do not remove or rename BROKER_RECONNECT_WHEN_SERVER_DOWN */
#define BROKER_RECONNECT_WHEN_SERVER_DOWN       0x20

/* For backward compatibility */
#define BROKER_INFO_MAJOR_VERSION               (BROKER_INFO_PROTO_VERSION)
#define BROKER_INFO_MINOR_VERSION               (BROKER_INFO_FUNCTION_FLAG)
#define BROKER_INFO_PATCH_VERSION               (BROKER_INFO_RESERVED2)
#define BROKER_INFO_RESERVED                    (BROKER_INFO_RESERVED3)

#define CAS_PID_SIZE                            4
#define SESSION_ID_SIZE                         4
#define DRIVER_SESSION_SIZE			SRV_CON_DBSESS_ID_SIZE
#define CAS_CONNECTION_REPLY_SIZE_PRIOR_PROTOCOL_V3               (CAS_PID_SIZE + BROKER_INFO_SIZE + SESSION_ID_SIZE)
#define CAS_CONNECTION_REPLY_SIZE_V3               (CAS_PID_SIZE + BROKER_INFO_SIZE + DRIVER_SESSION_SIZE)
#define CAS_CONNECTION_REPLY_SIZE_V4               (CAS_PID_SIZE + CAS_PID_SIZE + BROKER_INFO_SIZE + DRIVER_SESSION_SIZE)
#define CAS_CONNECTION_REPLY_SIZE               CAS_CONNECTION_REPLY_SIZE_V4

#define CAS_KEEP_CONNECTION_ON                  1

#define CAS_GET_QUERY_INFO_PLAN			1

#define CAS_STATEMENT_POOLING_OFF		0
#define CAS_STATEMENT_POOLING_ON		1

#define CCI_PCONNECT_OFF                        0
#define CCI_PCONNECT_ON                         1

#define CAS_REQ_HEADER_JDBC	"JDBC"
#define CAS_REQ_HEADER_ODBC	"ODBC"
#define CAS_REQ_HEADER_PHP	"PHP"
#define CAS_REQ_HEADER_OLEDB	"OLEDB"
#define CAS_REQ_HEADER_CCI	"CCI"

#define SHARD_ID_INVALID 		(-1)
#define SHARD_ID_UNSUPPORTED	(-2)

/* db_name used by client's broker health checker */
#define HEALTH_CHECK_DUMMY_DB "___health_check_dummy_db___"

  enum t_cas_func_code
  {
    CAS_FC_END_TRAN = 1,
    CAS_FC_PREPARE = 2,
    CAS_FC_EXECUTE = 3,
    CAS_FC_GET_DB_PARAMETER = 4,
    CAS_FC_SET_DB_PARAMETER = 5,
    CAS_FC_CLOSE_REQ_HANDLE = 6,
    CAS_FC_CURSOR = 7,
    CAS_FC_FETCH = 8,
    CAS_FC_SCHEMA_INFO = 9,
    CAS_FC_OID_GET = 10,
    CAS_FC_OID_PUT = 11,
    CAS_FC_DEPRECATED1 = 12,
    CAS_FC_DEPRECATED2 = 13,
    CAS_FC_DEPRECATED3 = 14,
    CAS_FC_GET_DB_VERSION = 15,
    CAS_FC_GET_CLASS_NUM_OBJS = 16,
    CAS_FC_OID_CMD = 17,
    CAS_FC_COLLECTION = 18,
    CAS_FC_NEXT_RESULT = 19,
    CAS_FC_EXECUTE_BATCH = 20,
    CAS_FC_EXECUTE_ARRAY = 21,
    CAS_FC_CURSOR_UPDATE = 22,
    CAS_FC_GET_ATTR_TYPE_STR = 23,
    CAS_FC_GET_QUERY_INFO = 24,
    CAS_FC_DEPRECATED4 = 25,
    CAS_FC_SAVEPOINT = 26,
    CAS_FC_PARAMETER_INFO = 27,
    CAS_FC_XA_PREPARE = 28,
    CAS_FC_XA_RECOVER = 29,
    CAS_FC_XA_END_TRAN = 30,
    CAS_FC_CON_CLOSE = 31,
    CAS_FC_CHECK_CAS = 32,
    CAS_FC_MAKE_OUT_RS = 33,
    CAS_FC_GET_GENERATED_KEYS = 34,
    CAS_FC_LOB_NEW = 35,
    CAS_FC_LOB_WRITE = 36,
    CAS_FC_LOB_READ = 37,
    CAS_FC_END_SESSION = 38,
    CAS_FC_GET_ROW_COUNT = 39,
    CAS_FC_GET_LAST_INSERT_ID = 40,
    CAS_FC_PREPARE_AND_EXECUTE = 41,
    CAS_FC_CURSOR_CLOSE = 42,
    CAS_FC_GET_SHARD_INFO = 43,
    CAS_FC_CAS_CHANGE_MODE = 44,

    /* Whenever you want to introduce a new function code, you must add a corresponding function entry to
     * server_fn_table of both CUBRID and (MySQL, Oracle). */
    CAS_FC_MAX,

    /* function code list of protocol version V2 - 9.0.0.xxxx */
    CAS_FC_CURSOR_CLOSE_FOR_PROTO_V2 = 41,
    CAS_FC_PREPARE_AND_EXECUTE_FOR_PROTO_V2 = 42
  };
  typedef enum t_cas_func_code T_CAS_FUNC_CODE;

  enum t_cas_protocol
  {
    PROTOCOL_V0 = 0,		/* old protocol */
    PROTOCOL_V1 = 1,		/* query_timeout and query_cancel */
    PROTOCOL_V2 = 2,		/* send columns meta-data with the result for executing */
    PROTOCOL_V3 = 3,		/* session information extend with server session key */
    PROTOCOL_V4 = 4,		/* send as_index to driver */
    PROTOCOL_V5 = 5,		/* shard feature, fetch end flag */
    PROTOCOL_V6 = 6,		/* cci/cas4m support unsigned integer type */
    PROTOCOL_V7 = 7,		/* timezone types, to pin xasl entry for retry */
    PROTOCOL_V8 = 8,		/* JSON type */
    CURRENT_PROTOCOL = PROTOCOL_V8
  };
  typedef enum t_cas_protocol T_CAS_PROTOCOL;

  enum t_broker_info_pos
  {
    BROKER_INFO_DBMS_TYPE = 0,
    BROKER_INFO_KEEP_CONNECTION,
    BROKER_INFO_STATEMENT_POOLING,
    BROKER_INFO_CCI_PCONNECT,
    BROKER_INFO_PROTO_VERSION,
    BROKER_INFO_FUNCTION_FLAG,
    BROKER_INFO_RESERVED2,
    BROKER_INFO_RESERVED3
  };
  typedef enum t_broker_info_pos T_BROKER_INFO_POS;

  enum t_driver_info_pos
  {
    DRIVER_INFO_MAGIC1 = 0,
    DRIVER_INFO_MAGIC2,
    DRIVER_INFO_MAGIC3,
    DRIVER_INFO_MAGIC4,
    DRIVER_INFO_MAGIC5,
    DRIVER_INFO_CLIENT_TYPE,
    DRIVER_INFO_PROTOCOL_VERSION,
    DRIVER_INFO_FUNCTION_FLAG,
    DRIVER_INFO_RESERVED,
  };
  typedef enum t_driver_info_pos T_DRIVER_INFO_POS;

  enum t_dbms_type
  {
    CAS_DBMS_CUBRID = 1,
    CAS_DBMS_MYSQL = 2,
    CAS_DBMS_ORACLE = 3,
    CAS_PROXY_DBMS_CUBRID = 4,
    CAS_PROXY_DBMS_MYSQL = 5,
    CAS_PROXY_DBMS_ORACLE = 6
  };
  typedef enum t_dbms_type T_DBMS_TYPE;
#define IS_CONNECTED_TO_PROXY(type) \
	((type) == CAS_PROXY_DBMS_CUBRID \
	|| (type) == CAS_PROXY_DBMS_MYSQL \
	|| (type) == CAS_PROXY_DBMS_ORACLE)

#define IS_VALID_CAS_FC(fc) \
	(fc >= CAS_FC_END_TRAN && fc < CAS_FC_MAX)

/* Current protocol version */
#define CAS_PROTOCOL_VERSION    ((unsigned char)(CURRENT_PROTOCOL))

/* Indicates version variable holds CAS protocol version. */
#define CAS_PROTO_INDICATOR     (0x40)

/* Make a version to be used in CAS. */
#define CAS_PROTO_MAKE_VER(VER)         \
        ((T_BROKER_VERSION) (CAS_PROTO_INDICATOR << 24 | (VER)))
#define CAS_PROTO_CURRENT_VER           \
        ((T_BROKER_VERSION) CAS_PROTO_MAKE_VER(CURRENT_PROTOCOL))

#define DOES_CLIENT_MATCH_THE_PROTOCOL(CLIENT, MATCH) ((CLIENT) == CAS_PROTO_MAKE_VER((MATCH)))
#define DOES_CLIENT_UNDERSTAND_THE_PROTOCOL(CLIENT, REQUIRE) ((CLIENT) >= CAS_PROTO_MAKE_VER((REQUIRE)))

/* Pack/unpack CAS protocol version to/from network. */
#define CAS_PROTO_VER_MASK      (0x3F)
#define CAS_PROTO_PACK_NET_VER(VER)         \
        (char)((char)CAS_PROTO_INDICATOR | (char)(VER))
#define CAS_PROTO_UNPACK_NET_VER(VER)       \
        (CAS_PROTO_MAKE_VER(CAS_PROTO_VER_MASK & (char)(VER)))
#define CAS_PROTO_PACK_CURRENT_NET_VER      \
        CAS_PROTO_PACK_NET_VER(CURRENT_PROTOCOL)

#define CAS_CONV_ERROR_TO_OLD(V) (V + 9000)
#define CAS_CONV_ERROR_TO_NEW(V) (V - 9000)

#define CAS_MAKE_VER(MAJOR, MINOR, PATCH)       \
	((T_BROKER_VERSION) (((MAJOR) << 16) | ((MINOR) << 8) | (PATCH)))

#define CAS_MAKE_PROTO_VER(DRIVER_INFO) \
    (((DRIVER_INFO)[SRV_CON_MSG_IDX_PROTO_VERSION]) & CAS_PROTO_INDICATOR) ? \
        CAS_PROTO_UNPACK_NET_VER ((DRIVER_INFO)[SRV_CON_MSG_IDX_PROTO_VERSION]) : \
        CAS_MAKE_VER ((DRIVER_INFO)[SRV_CON_MSG_IDX_MAJOR_VER], \
                      (DRIVER_INFO)[SRV_CON_MSG_IDX_MINOR_VER], \
                      (DRIVER_INFO)[SRV_CON_MSG_IDX_PATCH_VER])

#define CAS_TYPE_FIRST_BYTE_PROTOCOL_MASK 0x80

/* For backward compatibility */
#define CAS_VER_TO_MAJOR(VER)    ((int) (((VER) >> 16) & 0xFF))
#define CAS_VER_TO_MINOR(VER)    ((int) (((VER) >> 8) & 0xFF))
#define CAS_VER_TO_PATCH(VER)    ((int) ((VER) & 0xFF))
#define CAS_PROTO_TO_VER_STR(MSG_P, VER)			\
	do {							\
            switch (VER)					\
              {							\
            case PROTOCOL_V1:					\
                *((char **) (MSG_P)) = (char *) "8.4.1";	\
                break;						\
            case PROTOCOL_V2:					\
                *((char **) (MSG_P)) = (char *) "9.0.0";	\
                break;						\
            case PROTOCOL_V3:					\
                *((char **) (MSG_P)) = (char *) "8.4.3";	\
                break;						\
            case PROTOCOL_V4:					\
                *((char **) (MSG_P)) = (char *) "9.1.0";	\
                break;						\
            default:						\
                *((char **) (MSG_P)) = (char *) "";		\
                break;						\
              }							\
	} while (0)

  typedef int T_BROKER_VERSION;

  extern const char *cas_bi_get_broker_info (void);
  extern char cas_bi_get_dbms_type (void);
  extern void cas_bi_set_dbms_type (const char dbms_type);
  extern void cas_bi_set_keep_connection (const char keep_connection);
  extern char cas_bi_get_keep_connection (void);
  extern void cas_bi_set_statement_pooling (const char statement_pooling);
  extern char cas_bi_get_statement_pooling (void);
  extern void cas_bi_set_cci_pconnect (const char cci_pconnect);
  extern char cas_bi_get_cci_pconnect (void);
  extern void cas_bi_set_protocol_version (const char protocol_version);
  extern char cas_bi_get_protocol_version (void);
  extern void cas_bi_set_renewed_error_code (const bool renewed_error_code);
  extern bool cas_bi_get_renewed_error_code (void);
  extern bool cas_di_understand_renewed_error_code (const char *driver_info);
  extern void cas_bi_make_broker_info (char *broker_info, char dbms_type, char statement_pooling, char cci_pconnect);
#ifdef __cplusplus
}
#endif

#endif				/* _CAS_PROTOCOL_H_ */
