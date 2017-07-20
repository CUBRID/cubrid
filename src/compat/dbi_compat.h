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
 * dbi.h - Definitions and function prototypes for the CUBRID Application Program Interface (API).
 */

#include <stdio.h>
#include <time.h>
#include <sys/types.h>

#ifndef _DBI_COMPAT_H_
#define _DBI_COMPAT_H_

#define bool char

#if defined(WINDOWS) && !defined(__GNUC__)
#define int32_t __int32
#define int64_t __int64
#define u_int32_t unsigned __int32
#define u_int64_t unsigned __int64
#endif /* WINDOWS && !__GNUC__ */


#ifdef NO_ERROR
#undef NO_ERROR
#endif

#define NO_ERROR                                       0
#define ER_FAILED                                     -1

#define ER_GENERIC_ERROR                              -2
#define ER_OUT_OF_VIRTUAL_MEMORY                      -3
#define ER_INTERRUPTED                                -4

#define ER_MHT_NOTFOUND                               -5
#define ER_MHT_NULL_HASHTABLE                         -6

#define ER_IO_FORMAT_BAD_NPAGES                       -7
#define ER_IO_FORMAT_FAIL                             -8
#define ER_IO_FORMAT_OUT_OF_SPACE                     -9
#define ER_IO_MOUNT_FAIL                             -10
#define ER_IO_MOUNT_LOCKED                           -11
#define ER_IO_DISMOUNT_FAIL                          -12
#define ER_IO_READ                                   -13
#define ER_IO_WRITE                                  -14
#define ER_IO_WRITE_OUT_OF_SPACE                     -15
#define ER_IO_RENAME_FAIL                            -16

#define ER_PB_BAD_PAGEID                             -17
#define ER_PB_ALL_BUFFERS_FIXED                      -18
#define ER_PB_UNFIXED_PAGEPTR                        -19
#define ER_PB_UNKNOWN_PAGEPTR                        -20

#define ER_DISK_UNKNOWN_SECTOR                       -21
#define ER_DISK_UNKNOWN_PAGE                         -22
#define ER_DISK_TRY_DEALLOC_DISK_SYSPAGE             -23
#define ER_DISK_ALMOST_OUT_OF_SPACE                  -24
#define ER_DISK_DATA_ALMOST_OUT_OF_SPACE             -25
#define ER_DISK_INDEX_ALMOST_OUT_OF_SPACE            -26
#define ER_DISK_GENERIC_ALMOST_OUT_OF_SPACE          -27
#define ER_DISK_TEMP_ALMOST_OUT_OF_SPACE             -28
#define ER_DISK_LAST_ALMOST_OUT_OF_SPACE             -29
#define ER_DISK_DATA_LAST_ALMOST_OUT_OF_SPACE        -30
#define ER_DISK_INDEX_LAST_ALMOST_OUT_OF_SPACE       -31
#define ER_DISK_GENERIC_LAST_ALMOST_OUT_OF_SPACE     -32
#define ER_DISK_TEMP_LAST_ALMOST_OUT_OF_SPACE        -33

#define ER_FILE_NTH_FPAGE_OUT_OF_RANGE               -34
#define ER_FILE_UNKNOWN_VOLID                        -35
#define ER_FILE_NOT_ENOUGH_PAGES_IN_DATABASE         -36
#define ER_FILE_NOT_ENOUGH_PAGES_IN_VOLUME           -37
#define ER_FILE_UNKNOWN_FILE                         -38
#define ER_FILE_INCONSISTENT_ALLOCATION              -39
#define ER_FILE_INCONSISTENT_EXPECTED_PAGES          -40
#define ER_FILE_PAGE_ISNOT_PARTOF                    -41

#define ER_SP_CANNOT_REORDER_ANCHORED                -42
#define ER_SP_WRONG_NUM_SLOTS                        -43
#define ER_SP_NOSPACE_IN_PAGE                        -44
#define ER_SP_BAD_INSERTION_SLOT                     -45
#define ER_SP_UNKNOWN_SLOTID                         -46

#define ER_HEAP_UNABLE_TO_CREATE_HEAP                -47
#define ER_HEAP_UNKNOWN_OBJECT                       -48
#define ER_HEAP_UNKNOWN_CLASS_OF_INSTANCE            -49
#define ER_HEAP_BAD_RELOCATION_RECORD                -50
#define ER_HEAP_BAD_OBJECT_TYPE                      -51
#define ER_HEAP_OVFADDRESS_CORRUPTED                 -52
#define ER_HEAP_NODATA_NEWADDRESS                    -53
#define ER_HEAP_OVERPASS_MAXOBJ_SIZE                 -54
#define ER_HEAP_CYCLE                                -55

#define ER_EH_UNKNOWN_EXT_HASH                       -56
#define ER_EH_UNKNOWN_KEY                            -57
#define ER_EH_STR_TOO_LONG                           -58
#define ER_EH_INVALID_KEY_TYPE                       -59
#define ER_EH_CORRUPTED                              -60
#define ER_EH_ROOT_CORRUPTED                         -61

#define ER_SORT_REC_TOO_BIG                          -62
#define ER_SORT_TEMP_PAGE_CORRUPTED                  -63

#define ER_LC_UNKNOWN_CLASSNAME                      -64
#define ER_LC_CLASSNAME_EXIST                        -65
#define ER_LC_BADFORCE_OPERATION                     -66
#define ER_LC_NOHEAP                                 -67
#define ER_LC_INCONSISTENT_CLASSNAME_TYPE1           -68
#define ER_LC_INCONSISTENT_CLASSNAME_TYPE2           -69
#define ER_LC_INCONSISTENT_CLASSNAME_TYPE3           -70
#define ER_LC_INCONSISTENT_CLASSNAME_TYPE4           -71

#define ER_LK_UNILATERALLY_ABORTED                   -72
#define ER_LK_OBJECT_TIMEOUT_SIMPLE_MSG              -73
#define ER_LK_OBJECT_TIMEOUT_CLASS_MSG               -74
#define ER_LK_OBJECT_TIMEOUT_CLASSOF_MSG             -75
#define ER_LK_PAGE_TIMEOUT                           -76

#define ER_LOG_ALL_BUFFERS_FIXED                     -77
#define ER_LOG_READ                                  -78
#define ER_LOG_WRITE                                 -79
#define ER_LOG_WRITE_OUT_OF_SPACE                    -80
#define ER_LOG_PAGE_CORRUPTED                        -81
#define ER_LOG_MOUNT_FAIL                            -82
#define ER_LOG_NAME_IS_TOO_LONG                      -83
#define ER_LOG_PREFIX_NAME_IS_TOO_LONG               -84
#define ER_LOG_INCOMPATIBLE_PREFIX_NAME              -85
#define ER_LOG_INCOMPATIBLE_DATABASE                 -86
#define ER_LOG_RECOVER_ON_OLD_RELEASE                -87
#define ER_LOG_COMPILATION_RELEASE                   -88
#define ER_LOG_DOESNT_CORRESPOND_TO_DATABASE         -89
#define ER_LOG_REDO_INTERFACE                        -90
#define ER_LOG_POSTPONE_INTERFACE                    -91
#define ER_LOG_COMPENSATE_INTERFACE                  -92
#define ER_LOG_REDO_LOGGING_DURING_RECOVERY          -93
#define ER_LOG_POSTPONE_LOGGING_DURING_RECOVERY      -94	/* Obsolete */
#define ER_LOG_UNDO_LOGGING_DURING_RECOVERY          -95	/* Obsolete. */
#define ER_LOG_MAYNEED_MEDIA_RECOVERY                -96
#define ER_LOG_NOTIN_ARCHIVE                         -97
#define ER_LOG_CREATE_LOGARCHIVE_FAIL                -98
#define ER_LOG_CREATE_DBBACKUP_DIRINFO               -99
#define ER_LOG_DBBACKUP_FAIL                        -100
#define ER_LOG_USER_FILE_UNKNOWN                    -101
#define ER_LOG_USER_FILE_WITHOUT_ENOUGH_ENTRIES     -102
#define ER_LOG_USER_FILE_UNORDERED_ENTRIES          -103
#define ER_LOG_USER_FILE_INCORRECT_PRIMARY_VOLNAME  -104
#define ER_LOG_CANNOT_ACCESS_BACKUP                 -105
#define ER_LOG_2PC_NON_UNIQUE_GTID                  -106
#define ER_LOG_2PC_UNKNOWN_GTID                     -107
#define ER_LOG_2PC_CANNOT_ATTACH                    -108
#define ER_LOG_2PC_PARTICIPANT_UNILATERALLY_ABORTED -109
#define ER_LOG_INVALID_ISOLATION_LEVEL              -110

#define ER_TM_SERVER_DOWN_UNILATERALLY_ABORTED      -111

#define ER_BO_UNAUTHORIZED_HOST                     -112
#define ER_BO_UNABLE_TO_RESTART_SERVER              -113
#define ER_BO_NOT_A_VOLUME                          -114
#define ER_BO_DATABASE_EXISTS                       -115
#define ER_BO_UNKNOWN_DATABASE                      -116
#define ER_BO_FULL_DATABASE_NAME_IS_TOO_LONG        -117
#define ER_BO_CWD_FAIL                              -118
#define ER_BO_UNABLE_TO_FIND_HOSTNAME               -119
#define ER_BO_MAXNUM_VOLS_HAS_BEEN_EXCEEDED         -120
#define ER_BO_TRYING_TO_REMOVE_PERMANENT_VOLUME     -121
#define ER_BO_CANNOT_ACCESS_MESSAGE_CATALOG         -122
#define ER_BO_CANNOT_CREATE_VOL                     -123
#define ER_BO_VOLUME_EXISTS                         -124
#define ER_BO_NOTIFY_AUTO_VOLEXT                    -125
#define ER_BO_PARSE_ADDVOLS_UNKNOWN_PURPOSE         -126
#define ER_BO_PARSE_ADDVOLS_BAD_NPAGES              -127
#define ER_BO_PARSE_ADDVOLS_NOGIVEN_NPAGES          -128
#define ER_BO_PARSE_ADDVOLS_UNKNOWN_TOKEN           -129

#define ER_REGU_NO_SPACE                            -130
#define ER_REGU_SYSTEM                              -131
#define ER_REGU_NOT_IMPLEMENTED                     -132
#define ER_REGU_NESTED_SET                          -133
#define ER_REGU_MIX_CLASS_NONCLASS_UPDATE           -134
#define ER_REGU_INVALID_QUERY_FOR_OID_COLUMN        -135

#define ER_DB_UNIMPLEMENTED                         -136

#define ER_AU_CORRUPTED                             -137
#define ER_AU_MISSING_CLASS                         -138
#define ER_AU_ACCESS_ERROR                          -139
#define ER_AU_DBA_ONLY                              -140
#define ER_AU_CANT_ADD_MEMBER                       -141
#define ER_AU_MEMBER_CAUSES_CYCLES                  -142
#define ER_AU_CLASS_WITH_NO_OWNER                   -143
#define ER_AU_USER_ACCESS_FAILURE                   -144
#define ER_AU_CANT_GRANT_SELF                       -145
#define ER_AU_CANT_GRANT_OWNER                      -146
#define ER_AU_NO_GRANT_OPTION                       -147
#define ER_AU_CANT_UPDATE                           -148
#define ER_AU_CANT_CREATE_INSTANCE                  -149
#define ER_AU_CANT_REVOKE_SELF                      -150
#define ER_AU_CANT_REVOKE_OWNER                     -151
#define ER_AU_GRANT_NOT_FOUND                       -152
#define ER_AU_NO_AUTHORIZATION                      -153
#define ER_AU_INCOMPLETE_AUTH                       -154
#define ER_AU_MULTIPLE_ROOTS                        -155
#define ER_AU_AUTHORIZATION_FAILURE                 -156
#define ER_AU_SELECT_FAILURE                        -157
#define ER_AU_ALTER_FAILURE                         -158
#define ER_AU_UPDATE_FAILURE                        -159
#define ER_AU_INSERT_FAILURE                        -160
#define ER_AU_DELETE_FAILURE                        -161
#define ER_AU_INDEX_FAILURE                         -162
#define ER_AU_EXECUTE_FAILURE                       -163
#define ER_AU_USER_EXISTS                           -164
#define ER_AU_INVALID_USER                          -165
#define ER_AU_MISSING_OR_INVALID_USER               -166
#define ER_AU_NOT_OWNER                             -167
#define ER_AU_MEMBER_NOT_FOUND                      -168
#define ER_AU_CANT_DROP_USER                        -169
#define ER_AU_NO_USER_LOGGED_IN                     -170
#define ER_AU_INVALID_PASSWORD                      -171
#define ER_AU_PASSWORD_OVERFLOW                     -172

#define ER_CFG_NO_FILE                              -173
#define ER_CFG_NO_WRITE_ACCESS                      -174
#define ER_CFG_BAD_FORMAT                           -175

#define ER_DATE_CONVERSION                          -176

#define ER_ELO_CANT_CREATE_LARGE_OBJECT             -177

#define ER_MR_TEMP_OID_WITHOUT_MOP                  -178

#define ER_TP_INCOMPATIBLE_DOMAINS                  -179	/* not used */
#define ER_TP_INCOMPATIBLE_VALUE                    -180
#define ER_TP_CANT_COERCE                           -181
#define ER_TP_CANT_COERCE_OVERFLOW                  -182

#define ER_NET_DATASIZE_MISMATCH                    -183
#define ER_NET_CANT_ALLOC_BUFFER                    -184
#define ER_NET_CLIENT_DATA_RECEIVE                  -185
#define ER_NET_SERVER_DATA_RECEIVE                  -186
#define ER_NET_UNUSED_BUFFER                        -187
#define ER_NET_INVALID_SERVER_NAME                  -188
#define ER_NET_INVALID_HOST_NAME                    -189
#define ER_NET_NO_SERVER_HOST                       -190
#define ER_NET_CANT_CONNECT_SERVER                  -191
#define ER_NET_NO_CONFIG_FILE                       -192
#define ER_NET_SERVER_SHUTDOWN                      -193
#define ER_NET_UNKNOWN_SERVER_REQ                   -194
#define ER_NET_SERVER_COMM_ERROR                    -195
#define ER_NET_NO_SERVER                            -196
#define ER_NET_NO_MASTER                            -197
#define ER_NET_DATA_TRUNCATED                       -198
#define ER_NET_SERVER_CRASHED                       -199

#define ER_OBJ_SET_DISCONNECT                       -200
#define ER_OBJ_BAD_UNIQUE_REMOVAL                   -201
#define ER_OBJ_INVALID_ATTRIBUTE                    -202
#define ER_OBJ_ATTRIBUTE_TYPE_CONFLICT              -203
#define ER_OBJ_INVALID_ARGUMENTS                    -204
#define ER_OBJ_ATTRIBUTE_CANT_BE_NULL               -205
#define ER_OBJ_NO_UNIQUE_CONSTRAINT                 -206
#define ER_OBJ_OBJECT_SIZE_ZERO                     -207
#define ER_OBJ_INVALID_METHOD                       -208
#define ER_OBJ_INVALID_ATTMETH                      -209
#define ER_OBJ_TEMPLATE_INTERNAL                    -210
#define ER_OBJ_TEMPLATE_ATT_DELETED                 -211
#define ER_OBJ_ATTRIBUTE_NOT_UNIQUE                 -212
#define ER_OBJ_INVALID_UNIQUE_ENTRY                 -213
#define ER_OBJ_DOMAIN_CONFLICT                      -214
#define ER_OBJ_INVALID_ATTRIBUTE_ID                 -215
#define ER_OBJ_BAD_OWNER_TAG                        -216
#define ER_OBJ_NOT_A_CLASS                          -217
#define ER_OBJ_INVALID_OBJECT_IN_PATH               -218
#define ER_OBJ_INVALID_PATH_EXPRESSION              -219
#define ER_OBJ_INVALID_SET_IN_PATH                  -220
#define ER_OBJ_INVALID_INDEX_IN_PATH                -221
#define ER_OBJ_STRING_OVERFLOW                      -222
#define ER_OBJ_SHORT_OVERFLOW                       -223
#define ER_OBJ_NO_CONNECT                           -224
#define ER_OBJ_MISSING_NON_NULL_ASSIGN              -225
#define ER_OBJ_NO_COMPONENTS                        -226
#define ER_OBJ_DUPLICATE_ASSIGNMENT                 -227
#define ER_OBJ_TOO_MANY_ARGUMENTS                   -228
#define ER_OBJ_ARGUMENT_DOMAIN_CONFLICT             -229
#define ER_OBJ_MAX_STRING                           -230
#define ER_OBJ_INVALID_TEMP_OBJECT                  -231
#define ER_OBJ_INVALID_TEMPLATE                     -232

#define ER_SM_CLASS_WITH_PRIM_NAME                  -233
#define ER_SM_METHOD_FILE_NOT_FOUND                 -234
#define ER_SM_UNRESOLVED_METHODS                    -235
#define ER_SM_UNRESOLVED_METHOD                     -236
#define ER_SM_DYNAMIC_LINK_PROBLEMS                 -237
#define ER_SM_METHOD_FILE_ACCESS                    -238
#define ER_SM_ATTRIBUTE_NOT_FOUND                   -239
#define ER_SM_METHOD_NOT_FOUND                      -240
#define ER_SM_ATTMETH_NOT_FOUND                     -241
#define ER_SM_SIGNATURE_NOT_FOUND                   -242
#define ER_SM_METHOD_ARG_NOT_FOUND                  -243
#define ER_SM_DOMAIN_NOT_A_CLASS                    -244
#define ER_SM_NAME_RESERVED_BY_ATT                  -245
#define ER_SM_NAME_RESERVED_BY_METHOD               -246
#define ER_SM_INVALID_ARGUMENTS                     -247
#define ER_SM_INVALID_UNIQUE_TYPE                   -248
#define ER_SM_INSTANCES_EXIST                       -249
#define ER_SM_INDEX_ON_SHARED                       -250
#define ER_SM_SIGNATURE_EXISTS                      -251
#define ER_SM_DOMAIN_NOT_A_SET                      -252
#define ER_SM_NO_NESTED_SETS                        -253
#define ER_SM_DOMAIN_NOT_FOUND                      -254
#define ER_SM_ATTRIBUTE_NOT_VARIABLE                -255
#define ER_SM_SUPER_CLASS_EXISTS                    -256
#define ER_SM_SUPER_CAUSES_CYCLES                   -257
#define ER_SM_SUPER_NOT_FOUND                       -258
#define ER_SM_MULTIPLE_SIGNATURES                   -259
#define ER_SM_ARG_DOMAIN_NOT_A_SET                  -260
#define ER_SM_RESOLUTION_NOT_FOUND                  -261
#define ER_SM_DOMAIN_MISMATCH                       -262
#define ER_SM_CORRUPTED                             -263
#define ER_SM_ALIAS_NOT_UNIQUE                      -264
#define ER_SM_SHADOW_TYPE_CONFLICT                  -265
#define ER_SM_ATTRIBUTE_NAME_CONFLICT               -266
#define ER_SM_SHADOW_METHOD_CONFLICT                -267
#define ER_SM_METHOD_NAME_CONFLICT                  -268
#define ER_SM_INVALID_INDEX_TYPE                    -269
#define ER_SM_INVALID_UNIQUE_DOMAIN                 -270
#define ER_SM_CYCLE_DETECTED                        -271
#define ER_SM_INDEX_EXISTS                          -272
#define ER_SM_NO_INDEX                              -273
#define ER_SM_INVALID_NAME                          -274
#define ER_SM_INHERITED_ATTRIBUTE                   -275
#define ER_SM_INHERITED_METHOD                      -276
#define ER_SM_INHERITED                             -277
#define ER_SM_INCOMPATIBLE_DOMAINS                  -278
#define ER_SM_RESOLUTION_OVERRIDE                   -279
#define ER_SM_INCOMPATIBLE_SHADOW                   -280
#define ER_SM_MISSING_ALIAS_SUBSTITUTE              -281
#define ER_SM_INCOMPATIBLE_ALIAS_SUBSTITUTE         -282
#define ER_SM_LESS_SPECIFIC_ALIAS_SUBSTITUTE        -283
#define ER_SM_RESOLUTION_COMPONENT_EXISTS           -284
#define ER_SM_ALIAS_COMPONENT_EXISTS                -285
#define ER_SM_ALIAS_COMPONENT_INHERITED             -286
#define ER_SM_CANT_SHADOW_METHOD                    -287
#define ER_SM_CANT_SHADOW_ATTRIBUTE                 -288
#define ER_SM_CANT_INHERIT_METHOD                   -289
#define ER_SM_CANT_INHERIT_ATTRIBUTE                -290
#define ER_SM_INCOMPATIBLE_COMPONENTS               -291
#define ER_SM_POPULATE_NOT_FOUND                    -292
#define ER_SM_INVALID_CLASS                         -293
#define ER_SM_INVALID_METHOD_ENV                    -294
#define ER_SM_CATALOG_SPACE                         -295	/* Unused */
#define ER_SM_INVALID_PROPERTY                      -296
#define ER_SM_MULTIPLE_ALIAS                        -297
#define ER_SM_INVALID_RESOLUTION                    -298
#define ER_SM_NAME_IS_RESERVED                      -299
#define ER_SM_DEFAULT_UNIQUE                        -300
#define ER_SM_MAX_LENGTH_CONSTRAINT                 -301

#define ER_SET_ADD                                  -302
#define ER_SET_VALUE_EXISTS                         -303
#define ER_SET_OUT_OF_BOUNDS                        -304
#define ER_SEQ_OUT_OF_BOUNDS                        -305
#define ER_SET_NOT_A_SEQUENCE                       -306
#define ER_SET_NOT_A_SET                            -307
#define ER_SET_DOMAIN_CONFLICT                      -308
#define ER_SET_INVALID_INDEX                        -309
#define ER_SET_ELEMENT_NOT_FOUND                    -310
#define ER_SEQ_ELEMENT_NOT_FOUND                    -311
#define ER_SET_INVALID_DOMAIN                       -312

#define ER_TF_BUFFER_UNDERFLOW                      -313
#define ER_TF_BUFFER_OVERFLOW                       -314
#define ER_TF_INVALID_METACLASS                     -315
#define ER_TF_SIZE_MISMATCH                         -316
#define ER_TF_INVALID_REPRESENTATION                -317
#define ER_TF_OUT_OF_SYNC                           -318
#define ER_TF_UNKNOWN_ATT_EXTENSION                 -319
#define ER_TF_CORRUPTED                             -320

#define ER_WS_CORRUPTED                             -321
#define ER_WS_MOP_NOT_FOUND                         -322
#define ER_WS_MOP_NOT_TEMPORARY                     -323
#define ER_WS_CLASS_NOT_CACHED                      -324
#define ER_WS_GC_DIRTY_MOP                          -325
#define ER_WS_CHANGING_OBJECT_CLASS                 -326
#define ER_WS_CANT_INSTALL_NULL_OID                 -327
#define ER_WS_NO_CLASS_FOR_INSTANCE                 -328
#define ER_WS_OBJLIST_NOT_ALLOCATED                 -329
#define ER_WS_PIN_VIOLATION                         -330

#define ER_AREA_NOSPACE                             -331
#define ER_AREA_EXTENDING                           -332
#define ER_AREA_ABORT                               -333
#define ER_AREA_OUTRAGEOUS                          -334
#define ER_AREA_ILLEGAL_POINTER                     -335
#define ER_AREA_FREE_TWICE                          -336
#define ER_AREA_NEGATIVE_SIZE                       -337

#define ERR_CSS_ENTRY_OVERRUN                       -338
#define ERR_CS_WRONG_OWNER                          -339
#define ERR_CSS_CANNOT_FORK                         -340
#define ERR_CSS_CANNOT_EXEC                         -341
#define ERR_CSS_CANNOT_CHANGE_GROUP                 -342
#define ERR_CSS_REQUEST_ID_FAILURE                  -343
#define ERR_CSS_MINFO_MESSAGE                       -344
#define ERR_CSS_SHUTDOWN_ERROR                      -345
#define ERR_CSS_STOP_SHUTDOWN_ERROR                 -346
#define ERR_CSS_MASTER_PIPE_ERROR                   -347
#define ERR_CSS_TCP_PORT_ERROR                      -348
#define ERR_CSS_TCP_HOST_NAME_ERROR                 -350
#define ERR_CSS_TCP_CANNOT_CREATE_SOCKET            -351
#define ERR_CSS_TCP_CANNOT_RESERVE_PORT             -352
#define ERR_CSS_TCP_CANNOT_CONNECT_TO_MASTER        -353
#define ERR_CSS_TCP_CANNOT_SET_OWNER                -354
#define ERR_CSS_TCP_CANNOT_CREATE_STREAM            -355
#define ERR_CSS_UNIX_DOMAIN_SOCKET_FILE_EXIST       -356
#define ERR_CSS_TCP_BIND_ABORT                      -357
#define ERR_CSS_TCP_ACCEPT_ERROR                    -358
#define ERR_CSS_TCP_DATAGRAM_BIND                   -359
#define ERR_CSS_TCP_DATAGRAM_ACCEPT                 -360
#define ERR_CSS_TCP_DATAGRAM_CONNECT                -361
#define ERR_CSS_TCP_DATAGRAM_SOCKET                 -362
#define ERR_CSS_TCP_RECVMSG                         -363
#define ERR_CSS_TCP_PASSING_FD                      -364
#define ERR_CSS_TCP_BROADCAST_TO_CLIENT             -365
#define ERR_CSS_ERROR_FROM_SERVER                   -366
#define ERR_CSS_SERVER_ALREADY_EXISTS               -367
#define ERR_CSS_ERROR_DURING_SERVER_CONNECT         -368

#define ERR_MM_EARLY_EOF                            -373
#define ERR_MM_EARLY_EOF_TWO                        -374
#define ERR_MM_CONVERSION_ERROR                     -375
#define ERR_MM_FINDING_PUBLIC                       -376
#define ERR_MM_ADDING_ATTRIBUTE                     -377
#define ERR_MM_ADDING_METHOD                        -378
#define ERR_MM_ADDING_SUPER                         -379

#define ER_DL_EXISTS                                -380
#define ER_DL_ABSENT                                -381
#define ER_DL_INVALID                               -382
#define ER_DL_BADHDR                                -383
#define ER_DL_PATH                                  -384
#define ER_DL_LDEXIT                                -385
#define ER_DL_LDTERM                                -386
#define ER_DL_LDWAIT                                -387
#define ER_DL_IMAGE                                 -388
#define ER_DL_ESYS                                  -389
#define ER_DL_EFILE                                 -390
#define ER_DL_PIPEHNDLR                             -391
#define ER_DL_DAEMON_MISSING                        -392
#define ER_DL_DAEMON_DISAPPEARED                    -393

#define ER_TX_ENDPOINT_TOO_LARGE                    -394
#define ER_TX_BAD_NUMBER                            -395
#define ER_TX_ESCAPED_CHARACTER_OUT_OF_RANGE        -396
#define ER_TX_ILLEGAL_OR_MISSING_DELIMITER          -397
#define ER_TX_NO_REMEMBERED_STRING                  -398
#define ER_TX_UNBALANCED_PARENS                     -399
#define ER_TX_TOO_MANY_PARENS                       -400
#define ER_TX_TOO_MANY_NUMBERS                      -401
#define ER_TX_CURLY_BRACE_EXPECTED                  -402
#define ER_TX_FIRST_TOO_BIG                         -403
#define ER_TX_UNBALANCED_SQUARE_BRACKETS            -404
#define ER_TX_TOO_LONG                              -405

#define ER_BTREE_INVALID_INDEX_ID                   -406
#define ER_BTREE_UNKNOWN_KEY                        -407
#define ER_BTREE_UNKNOWN_OID                        -408
#define ER_BTREE_DUPLICATE_OID                      -409
#define ER_BTREE_NULL_KEY                           -410
#define ER_BTREE_INVALID_KEYTYPE                    -411
#define ER_BTREE_INVALID_RANGE                      -412

#define ER_CT_UNKNOWN_ATTRID                        -413
#define ER_CT_UNKNOWN_CLASSID                       -414	/* Unused */
#define ER_CT_INVALID_CLASSID                       -415
#define ER_CT_UNKNOWN_REPRID                        -416
#define ER_CT_INVALID_REPRID                        -417
#define ER_CT_NOSPACE_FOR_ATTRDIR                   -418
#define ER_CT_REPRCNT_OVERFLOW                      -419	/* Unused */
#define ER_CT_CLASS_HAS_REPRESENTATIONS             -420
#define ER_CT_MISSING_REPR_DIR                      -421
#define ER_CT_MISSING_REPR_INFO                     -422

#define ER_IT_INVALID_SESSION                       -423
#define ER_IT_EMPTY_STATEMENT                       -424
#define ER_IT_INCOMPATIBLE_DATATYPE                 -425
#define ER_IT_INCOMPATIBLE_DATATYPE1                -426
#define ER_IT_DATA_OVERFLOW                         -427
#define ER_IT_NOT_UPDATABLE_STMT                    -428
#define ER_IT_ILLEGAL_COMMAND                       -429
#define ER_IT_UNKNOWN_VARIABLE                      -430
#define ER_IT_PARSER                                -431
#define ER_IT_UNKNOWN_CALL_OBJECT                   -432
#define ER_IT_UNKNOWN_ATTRIBUTE                     -433
#define ER_IT_FAIL_FIND_COLNAME                     -434
#define ER_IT_MULTIPLE_STATEMENT                    -435
#define ER_IT_NOT_QUERY                             -436

#define ER_LO_INVALID_LOID                          -437
#define ER_LO_DESCRIPTOR_CONFLICT                   -438
#define ER_LO_OVER_OFFSET                           -439

#define ER_QPROC_INVALID_CRSPOS                     -440
#define ER_QPROC_INVALID_CRSOPR                     -441
#define ER_QPROC_UNKNOWN_CRSPOS                     -442
#define ER_QPROC_INVALID_TPLVAL_INDEX               -443
#define ER_QPROC_INVALID_COLNAME                    -444
#define ER_QPROC_INVALID_VALLIST_INDEX              -445
#define ER_QPROC_CLOSED_QRES_EXISTS                 -446
#define ER_QPROC_OPR_ON_CLOSED_QRES                 -447
#define ER_QPROC_BIG_TPLSIZE                        -448
#define ER_QPROC_UNKNOWN_QUERYID                    -449
#define ER_QPROC_INVALID_SET_OPR                    -450
#define ER_QPROC_EMPTY_HEAPFILE                     -451
#define ER_QPROC_INVALID_XASLNODE                   -452
#define ER_QPROC_NOMORE_SPECS                       -453
#define ER_QPROC_INVALID_DATATYPE                   -454
#define ER_QPROC_NOMORE_QFILE_PAGES                 -455
#define ER_QPROC_INCOMPATIBLE_TYPES                 -456
#define ER_QPROC_INVALID_RESTYPE                    -457
#define ER_QPROC_OVERFLOW_ADDITION                  -458
#define ER_QPROC_INVALID_QRY_SINGLE_TUPLE           -459

#define ER_UCI_TOO_FEW_HOST_VARS                    -460
#define ER_UCI_TOO_MANY_HOST_VARS                   -461
#define ER_UCI_NULL_IND_NEEDED                      -462
#define ER_UCI_NOT_PREPARED_STMT                    -463
#define ER_UCI_NOT_SELECT_STMT                      -464
#define ER_UCI_CURSOR_NOT_OPENED                    -465
#define ER_UCI_CURSOR_STILL_OPEN                    -466
#define ER_UCI_MULTIPLE_OBJECTS                     -467
#define ER_UCI_NO_MARK_ALLOWED                      -468
#define ER_UCI_INVALID_DATA_TYPE                    -469

#define ER_QO_SET_SIZE_EXCEEDED                     -470
#define ER_QO_OUT_OF_MEMORY                         -471

#define ER_SQLM_DRIVER_CONNECTION_ERROR             -472
#define ER_ERROR_FROM_SQLM_DRIVER                   -473
#define ER_ERROR_FROM_FOREIGN_DRIVER                -474

#define ER_SM_QUERY_SPEC_NOT_FOUND                  -475

#define ER_LDB_EXISTS                               -476
#define ER_LDB_CONNECT_ERROR                        -477

#define ER_SM_LDB_NOT_REGISTERED                    -478
#define ER_SM_LDB_CONNECTED                         -479
#define ER_SM_LDB_NOT_EMPTY                         -480
#define ER_SM_NOT_A_PROXY_VCLASS                    -481
#define ER_SM_NOT_A_VIRTUAL_CLASS                   -482
#define ER_SM_UNKNOWN_ATTRIBUTE                     -483
#define ER_SM_OBJECT_ID_ALREADY_SET                 -484
#define ER_SM_OBJECT_ID_NOT_SET                     -485
#define ER_SM_OBJECT_NOT_UPDATABLE                  -486
#define ER_SM_NOT_INSTANCE                          -487
#define ER_SM_INCOMPATIBLE_CLASS_DOMAIN             -488
#define ER_SM_INCOMPATIBLE_PROXY_DOMAIN             -489
#define ER_SM_INCOMPATIBLE_SUPER_CLASS              -490

#define ER_PT_NO_ENTITY_IN_QRYSPEC                  -491
#define ER_PT_ERROR                                 -492
#define ER_PT_SYNTAX                                -493
#define ER_PT_SEMANTIC                              -494
#define ER_PT_EXECUTE                               -495

#define ER_RT_UNKNOWN_KEY                           -496
#define ER_RT_UNKNOWN_OID                           -497
#define ER_RT_DUPLICATE_OID                         -498
#define ER_RT_NULL_KEY                              -499
#define ER_RT_DUPLICATE_KEY                         -500

#define ER_TR_INVALID_PRIORITY                      -501
#define ER_TR_MISSING_TARGET_CLASS                  -502
#define ER_TR_TRIGGER_NOT_FOUND                     -503
#define ER_TR_TRIGGER_INTERNAL                      -504
#define ER_TR_TRIGGER_EXISTS                        -505
#define ER_TR_NO_VCLASSES                           -506
#define ER_TR_BAD_TARGET_CLASS                      -507
#define ER_TR_BAD_TARGET_ATTR                       -508
#define ER_TR_INVALID_CONDITION                     -509
#define ER_TR_INVALID_ACTION                        -510
#define ER_TR_TRIGGER_SELECT_FAILURE                -511
#define ER_TR_TRIGGER_DELETE_FAILURE                -512
#define ER_TR_TRIGGER_UPDATE_FAILURE                -513
#define ER_TR_TRIGGER_ALTER_FAILURE                 -514
#define ER_TR_INVALID_ACTION_TIME                   -515
#define ER_TR_EXCEEDS_MAX_REC_LEVEL                 -516
#define ER_TR_REJECTED                              -517
#define ER_TR_INTERNAL_ERROR                        -518
#define ER_TR_INVALID_CONDITION_TYPE                -519
#define ER_TR_REJECT_AFTER_EVENT                    -520
#define ER_TR_REJECT_NOT_POSSIBLE                   -521
#define ER_TR_MISSING_CONDITION_STRING              -522
#define ER_TR_MISSING_ACTION_STRING                 -523
#define ER_TR_ACTIVITY_NOT_OWNED                    -524
#define ER_TR_CONDITION_COMPILE                     -525
#define ER_TR_ACTION_COMPILE                        -526
#define ER_TR_CONDITION_EVAL                        -527
#define ER_TR_ACTION_EVAL                           -528
#define ER_TR_TRANSACTION_INVALIDATED               -529

#define ER_REG_MISSING_EXPRESSION                   -530
#define ER_REG_EXPRESSION_TOO_LONG                  -531
#define ER_REG_UNBALANCED_PARENS                    -532
#define ER_REG_OUT_OF_RANGE                         -533
#define ER_REG_SYNTAX_ERROR                         -534
#define ER_REG_MISSSING_TEXT                        -535
#define ER_REG_ILLEGAL_OPCODE                       -536
#define ER_REG_ILLEGAL_COMMAND                      -537
#define ER_REG_BUFFER_NOT_INITIALIZED               -538

#define ER_QPROC_ZERO_DIVIDE                        -539

#define ER_EMERGENCY_ERROR                          -540

#define ER_DISK_INCONSISTENT_NFREE_PAGES            -541
#define ER_DISK_INCONSISTENT_NFREE_SECTS            -542
#define ER_DISK_INCONSISTENT_VOL_HEADER             -543

#define ER_LC_INCONSISTENT_BTREE_ENTRY_TYPE1        -544
#define ER_LC_INCONSISTENT_BTREE_ENTRY_TYPE2        -545
#define ER_LC_INCONSISTENT_BTREE_ENTRY_TYPE3        -546

#define ER_NET_DIFFERENT_RELEASE                    -547

#define ER_SM_NO_OBJECT_ID_IN_PROXY                 -548

#define ER_VID_PROXY_INSTANCE_NOT_FOUND             -549

#define ER_LOG_UNKNOWN_SAVEPOINT                    -550

#define ER_BO_CANNOT_FINE_VOLINFO                   -551

#define ER_QPROC_DATE_UNDERFLOW                     -552
#define ER_QPROC_TIME_UNDERFLOW                     -553

#define ER_LDR_INVALID_STATE			    -554
#define ER_LDR_MEMORY_ERROR			    -555
#define ER_LDR_VALUE_OVERFLOW			    -556
#define ER_LDR_SET_DOMAIN_MISMATCH		    -557
#define ER_LDR_UNEXPECTED_SET			    -558
#define ER_LDR_DOMAIN_MISMATCH 			    -559
#define ER_LDR_AMBIGUOUS_DOMAIN			    -560
#define ER_LDR_NESTED_SET			    -561
#define ER_LDR_SYSTEM_CLASS 			    -562
#define ER_LDR_INTERNAL_REFERENCE  		    -563
#define ER_LDR_UNIQUE_VIOLATION			    -564
#define ER_LDR_INVALID_CONSTRUCTOR 		    -565
#define ER_LDB_NO_CLASS_OR_NO_ATTRIBUTE 	    -566
#define ER_LDR_UNEXPECTED_ARGUMENT		    -567
#define ER_LDR_MISSING_ARGUMENT			    -568
#define ER_LDR_MISSING_ATTRIBUTES		    -569
#define ER_LDR_ELO_INPUT_FILE      		    -570
#define ER_LDR_FORWARD_CONSTRUCTOR 	   	    -571
#define ER_LDR_CANT_TRANSFORM      		    -572
#define ER_LDR_CANT_INSERT         		    -573
#define ER_LDR_CANT_UPDATE			    -574
#define ER_LDR_ARGUMENT_DOMAIN_MISMATCH             -575
#define ER_LDR_OBJECT_DOMAIN_MISMATCH               -576
#define ER_LDR_ARGUMENT_AMBIGUOUS_DOMAIN            -577
#define ER_LDR_ARGUMENT_OBJECT_DOMAIN_MISMATCH      -578
#define ER_LDR_CLASS_OBJECT_REFERENCE		    -579
#define ER_LDR_INVALID_ATTRIBUTE		    -580

#define ER_DB_NO_MODIFICATIONS                      -581

#define ER_DISK_UNKNOWN_PURPOSE                     -582

#define ER_FILE_ALLOC_NOPAGES                       -583
#define ER_FILE_FTB_LOOP                            -584

#define ER_HEAP_UNKNOWN_HEAP                        -585
#define ER_HEAP_CANNOT_UPDATE_CHAIN_HDR             -586

#define ER_BO_UNSORTED_VOLINFO                      -587

#define ER_FAILED_ASSERTION			    -588

#define ER_AU_INVALID_USER_NAME			    -589

#define ER_SM_INCOMPATIBLE_DOMAIN_CLASS_TYPE        -590
#define ER_SM_INCOMPATIBLE_PROXY_DOMAIN_NAME        -591
#define ER_SM_INCOMPATIBLE_PROXY_DIFF_LDBS    	    -592

#define ER_VID_LOST_NON_UPDATABLE_OBJECT    	    -593
#define ER_VID_INVALID_OBJECT_ID_TYPE    	    -594

#define ER_LOG_NOFULL_DATABASE_NAME_IS_TOO_LONG     -595

#define ER_BO_MAXTEMP_SPACE_HAS_BEEN_EXCEEDED       -596

#define ER_HEAP_MISMATCH_NPAGES                     -597

#define ER_FILE_MISMATCH_NFILES                     -598

#define ER_IO_SYNC                                  -599

#define ER_PC_UNIMPLEMENTED                         -600

#define ER_DL_LOAD_ERR                              -601
#define ER_DL_MULTIPLY_DEFINED                      -602

#define ER_FILE_TABLE_CORRUPTED                     -603
#define ER_FILE_ALLOCSET_INCON_EXPECTED_NHOLES      -604
#define ER_FILE_INCONSISTENT_EXPECTED_MARKED_DEL    -605

#define ER_PC_NO_ODBC_INI_FILE			    -606
#define ER_PC_DATABASE_NOT_FOUND		    -607
#define ER_PC_DATABASE_NOT_CUBRID		    -608

#define ER_LOG_CORRUPTED_DB_DUE_NOLOGGING           -609
#define ER_LOG_CORRUPTED_DB_DUE_CRASH_NOLOGGING     -610
#define ER_ONLY_IN_STANDALONE                       -611
#define ER_LOG_THEREARE_PENDING_ACTIONS_MUST_LOG    -612

#define ER_SM_INCOMPATIBLE_ALIAS_LOCAL_SUB          -613

#define ER_LOG_MAX_ARCHIVES_HAS_BEEN_EXCEEDED       -614

#define ER_CSS_WINSOCK_STARTUP			    -615
#define ER_CSS_WINSOCK_HOSTNAME			    -616
#define ER_CSS_WINSOCK_HOSTID			    -617

#define ER_IT_ATTR_NOT_UPDATABLE                    -618

#define ER_QSTR_BAD_SRC_CODESET			    -619
#define ER_QSTR_BAD_DEST_CODESET		    -620
#define ER_QSTR_INVALID_DATA_TYPE		    -621
#define ER_QSTR_INCOMPATIBLE_CODE_SETS		    -622
#define ER_QSTR_INVALID_ESCAPE_SEQUENCE             -623
#define ER_QSTR_INVALID_ESCAPE_CHARACTER            -624

#define ER_HEAP_WRONG_ATTRINFO                      -625
#define ER_HEAP_UNKNOWN_ATTRS                       -626

#define ER_QSTR_INVALID_TRIM_OPERAND                -627

#define ER_INVALID_CURRENCY_TYPE                    -628

#define ER_DTSR_BAD_PAGESIZE                        -629

#define ER_DB_UNSUPPORTED_CONVERSION		    -630

#define ER_NULL_CONSTRAINT_VIOLATION                -631

#define ER_IO_NOT_A_BACKUP                          -632
#define ER_IO_NOT_A_BACKUP_OF_GIVEN_DATABASE        -633
#define ER_IO_BKUP_DATABASE_VOLUME_OR_FILE_EXPECTED -634

#define ER_LOG_BUFFER_POOL_TOO_SMALL                -635
#define ER_LOG_NBUFFERS_TOO_SMALL                   -636
#define ER_LOG_FREEING_TOO_MUCH                     -637
#define ER_LOG_FLUSHING_UNUPDATABLE                 -638
#define ER_LOG_WRONG_FORCE_DELAYED                  -639
#define ER_LOG_CANNOT_ADD_SAVEPOINT                 -640
#define ER_LOG_NONAME_SAVEPOINT                     -641
#define ER_LOG_NOTACTIVE_TOPOPS                     -642
#define ER_LOG_HAS_TOPOPS_DURING_COMMIT_ABORT       -643
#define ER_LOG_FATAL_ERROR                          -644
#define ER_LOG_BADSTATE_FOR_CLIENT_UNDO_OR_POSTPONE -645	/* Obsolete */
#define ER_LOG_MISSING_COMPENSATING_RECORD          -646
#define ER_LOG_BKUP_DOESNOT_CORRESPOND              -647
#define ER_LOG_BKUP_INCOMPATIBLE                    -648

#define ER_INVALID_PRECISION                        -649

#define ER_THREAD_STACK	                            -650

#define ER_SORT_MEMORY	                            -651

#define ER_SP_SPLIT_WRONG_OFFSET                    -652
#define ER_SP_TAKEOUT_WRONG_OFFSET                  -653
#define ER_SP_OVERWRITE_WRONG_OFFSET                -654

#define ER_EV_CONF_FILE                             -655
#define ER_EV_NULL_ID_CONF                          -656
#define ER_EV_INV_ID                                -657
#define ER_EV_OUT_OF_RANGE_CONF                     -658
#define ER_EV_ACCESS_HANDLER                        -659
#define ER_EV_CONNECT_HANDLER                       -660
#define ER_EV_WRITE_HANDLER                         -661
#define ER_EV_INIT                                  -662
#define ER_EV_TRUNC                                 -663
#define ER_EV_STARTED                               -664
#define ER_EV_BROKEN_PIPE                           -665
#define ER_EV_STOPPED                               -666

#define ER_CPLUS_NO_CLASS_MATCH                     -667
#define ER_CPLUS_UNKNOWN_DOMAIN                     -668

#define ER_CSS_CLIENTS_EXCEEDED                     -669

#define ER_BTREE_UNIQUE_FAILED                      -670

#define ER_CSS_RECV_OR_SEND                         -671
#define ER_CSS_SOCKET_CLOSE                         -672
#define ER_CSS_TIMEOUT_DUE_SHUTDOWN                 -673

#define ER_LK_NOTENOUGH_ACTIVE_THREADS              -674

#define ER_CFG_READ_DATABASES                       -675
#define ER_CFG_FIND_DATABASE                        -676
#define ER_BO_CONNECT_FAILED                        -677
#define ER_BO_CLIENT_INIT_INTERNAL                  -678

#define ER_CPLUS_TRANSACTION_BEGUN_TWICE            -679
#define ER_CPLUS_MAX_TRANS_NEST_EXCEEDED            -680
#define ER_CPLUS_TRANS_NOT_BEGUN                    -681
#define ER_CPLUS_DEP_TRANS_IN_PROGRESS              -682

#define ER_QSTR_BAD_LENGTH                          -683

#define ER_OBJ_METHOD_USER_ERROR                    -684

#define ER_AU_INVALID_CLASS                         -685

#define ER_EV_HANDLER_RESTARTED                     -686

#define ER_CPLUS_BAD_DYN_CAST                       -687
#define ER_CPLUS_BAD_SUBSCRIPT                      -688
#define ER_CPLUS_CORRUPT_ITER                       -689
#define ER_CPLUS_NULL_REF                           -690
#define ER_CPLUS_INVALID_ITER                       -691
#define ER_CPLUS_WRONG_ITER                         -692

#define ER_NUM_OVERFLOW                             -693	/* not used */

#define ER_BTREE_LOAD_FAILED			    -694

#define ER_CSS_KILL_BAD_INTERFACE                   -695
#define ER_CSS_KILL_UNKNOWN_TRANSACTION             -696
#define ER_CSS_KILL_DOES_NOTMATCH                   -697

#define ER_LC_INCONSISTENT_BTREE_ENTRY_TYPE4        -698
#define ER_LC_INCONSISTENT_BTREE_ENTRY_TYPE5        -699
#define ER_LC_INCONSISTENT_BTREE_ENTRY_TYPE6        -700

#define ER_SM_NO_PROXY_ON_LDB_ENTITY                -701

#define ER_LC_INCONSISTENT_BTREE_ENTRY_TYPE7        -702
#define ER_LC_INCONSISTENT_BTREE_ENTRY_TYPE8        -703

#define ER_QPROC_GENERIC_FUNCTION_FAILURE           -704

#define ER_BO_INCONSISTENT_NPERM_VOLUMES            -705

#define ER_DISK_CANNOT_EXPAND_PERMVOLS              -706
#define ER_DISK_UNABLE_TO_EXPAND                    -707

#define ER_IO_EXPAND_OUT_OF_SPACE                   -708

#define ER_SM_CONSTRAINT_NOT_FOUND		    -709
#define ER_SM_INVALID_CONSTRAINT		    -710

#define ER_OBJ_OBJECT_NOT_FOUND			    -711

#define ER_SM_CONSTRAINT_EXISTS                     -712

#define ER_OBJ_INDEX_NOT_FOUND			    -713

#define ER_QPROC_OUT_OF_TEMP_SPACE                  -714

#define ER_CPLUS_VERIFY_SCHEMA_FAILED		    -715

#define ER_SM_NOT_NULL_ON_VCLASS		    -716
#define ER_SM_UNIQUE_ON_VCLASS			    -717

#define ER_GADGET_INVALID                           -718
#define ER_GADGET_ATTRS_VALS_NE                     -719
#define ER_GADGET_NO_VCLASSES                       -720

#define ER_DO_INSERT_TOO_MANY			    -721

#define ER_SM_NOT_NULL_WRONG_NUM_ATTS		    -722

#define ER_DB_NO_DOMAIN_CHANGE     		    -723

#define ER_SM_INVALID_DEF_CONSTRAINT_NAME_PARAMS    -724

#define ER_LC_UNEXPECTED_PERM_OID                   -725

#define ER_DO_ALTER_ADD_WITH_UNIQUE                 -726

#define ER_TR_INVALID_EVENT                         -727

#define ER_QPROC_INVALID_TEMP_FILE                  -728
#define ER_QPROC_OVERFLOW_SUBTRACTION               -729
#define ER_QPROC_OVERFLOW_MULTIPLICATION            -730
#define ER_QPROC_OVERFLOW_DIVISION                  -731
#define ER_QPROC_OVERFLOW_UMINUS                    -732
#define ER_QPROC_OVERFLOW_COERCION                  -733	/* not used */

#define ER_FILE_INCONSISTENT_HEADER                 -734

#define ER_LOG_UNKNOWN_TRANINDEX                    -735

#define ERR_CSS_WINTCP_PORT_ERROR                   -736
#define ERR_CSS_WINTCP_CANNOT_CREATE_STREAM         -738
#define ERR_CSS_WINTCP_BIND_RETRY                   -739
#define ERR_CSS_WINTCP_BIND_ABORT                   -740
#define ERR_CSS_WINTCP_ACCEPT_ERROR                 -741
#define ERR_CSS_WINTCP_BROADCAST_TO_CLIENT          -742

#define ER_NET_SERVER_HAND_SHAKE                    -743

#define ER_WS_REHASH_VMOP_ERROR                     -744

#define ER_DB_GC_INVALID_PHASE                      -745
#define ER_DB_GC_INVALID_CALLBACK                   -746

#define ER_GC_BAD_TICKET                            -747
#define ER_GC_BAD_POINTER                           -748

#define ER_VID_PROXY_INSTANCES_DONT_MATCH           -749
#define ER_VID_PROXY_NCOLS_DONT_MATCH               -750

#define ER_TR_CORRELATION_ERROR                     -751

#define ER_IO_RESTORE_READ_ERROR                    -752
#define ER_IO_RESTORE_PAGEID_OUTOF_BOUNDS           -753

#define ER_VID_OO_PROXY_MOP_HAS_BAD_KEY             -754

#define ER_NOT_IN_STANDALONE                        -755

#define ER_OBJ_BUFFER_TOO_SMALL			    -756
#define ER_OBJ_CANT_ASSIGN_OID			    -757
#define ER_OBJ_INVALID_ARGUMENT			    -758
#define ER_OBJ_DELETED				    -759
#define ER_OBJ_NULL_VID				    -760
#define ER_OBJ_VOBJ_MAPS_INVALID_OBJ		    -761
#define ER_OBJ_CANT_RESOLVE_VOBJ_TO_OBJ		    -762
#define ER_OBJ_CANT_ENCODE_NONUPD_OBJ		    -763
#define ER_OBJ_CANT_ENCODE_VOBJ		    	    -764
#define ER_OBJ_NULL_ADDR_OUTPUT_OBJ		    -765
#define ER_OBJ_INTERNAL_ERROR_IN_DECODING	    -766

#define ER_LOG_BACKUP_LEVEL_NOGAPS                  -767

#define ER_SM_LDB_ACCESSED                  	    -768

#define ER_LDR_INVALID_CLASS_ATTR                   -769

#define ER_QPROC_NO_TABLE_FUNCTIONS                 -770
#define ER_QPROC_INVALID_PARAMETER		    -771
#define ER_QPROC_DB_SERIAL_NOT_FOUND	            -772
#define ER_QPROC_SERIAL_NOT_FOUND	            -773
#define ER_QPROC_SERIAL_ALREADY_EXIST		    -774
#define ER_QPROC_SERIAL_RANGE_OVERFLOW		    -775
#define ER_QPROC_CANNOT_FETCH_SERIAL		    -776
#define ER_QPROC_CANNOT_UPDATE_SERIAL		    -777

#define ER_BO_CANNOT_CREATE_LINK                    -778

#define ER_DATE_EXCEED_LIMIT                        -779
#define ER_SYSTEM_DATE                              -780
#define ER_QSTR_FORMAT_TOO_LONG                     -781
#define ER_QSTR_EMPTY_STRING                        -782
#define ER_QSTR_INVALID_FORMAT                      -783
#define ER_QSTR_MISMATCHING_ARGUMENTS               -784
#define ER_QSTR_SRC_TOO_LONG                        -785
#define ER_QSTR_FORMAT_DUPLICATION                  -786
#define ER_TIME_CONVERSION                          -787
#define ER_TIMESTAMP_CONVERSION                     -788
#define ER_WRONG_NUMBER				    -789

#define ER_QM_EXECUTION_INTERRUPTED		    -790

#define ER_INVALID_SERIAL_VALUE		 	    -791

#define ER_CSS_ALLOC				    -792
#define ER_CSS_PTHREAD_ATTR_INIT		    -793
#define ER_CSS_PTHREAD_ATTR_DESTROY		    -794
#define ER_CSS_PTHREAD_ATTR_SETDETACHSTATE	    -795
#define ER_CSS_PTHREAD_ATTR_SETSCOPE		    -796
#define ER_CSS_PTHREAD_ATTR_SETSTACKSIZE	    -797
#define ER_CSS_PTHREAD_CREATE			    -798
#define ER_CSS_PTHREAD_JOIN			    -799
#define ER_CSS_PTHREAD_MUTEX_INIT		    -800
#define ER_CSS_PTHREAD_MUTEX_DESTROY		    -801
#define ER_CSS_PTHREAD_MUTEX_LOCK		    -802
#define ER_CSS_PTHREAD_MUTEX_TRYLOCK		    -803
#define ER_CSS_PTHREAD_MUTEX_UNLOCK		    -804
#define ER_CSS_PTHREAD_MUTEXATTR_INIT		    -805
#define ER_CSS_PTHREAD_MUTEXATTR_DESTROY	    -806
#define ER_CSS_PTHREAD_MUTEXATTR_SETTYPE	    -807
#define ER_CSS_PTHREAD_MUTEXATTR_GETTYPE	    -808
#define ER_CSS_PTHREAD_COND_INIT		    -809
#define ER_CSS_PTHREAD_COND_DESTROY		    -810
#define ER_CSS_PTHREAD_COND_WAIT		    -811
#define ER_CSS_PTHREAD_COND_TIMEDWAIT		    -812
#define ER_CSS_PTHREAD_COND_SIGNAL		    -813
#define ER_CSS_PTHREAD_COND_BROADCAST		    -814
#define ER_CSS_PTHREAD_KEY_CREATE		    -815
#define ER_CSS_PTHREAD_KEY_DELETE		    -816
#define ER_CSS_PTHREAD_SETSPECIFIC		    -817
#define ER_CSS_PTHREAD_GETSPECIFIC		    -818
#define ER_CSS_PTHREAD_ONCE			    -819
#define ER_CSS_CONN_INIT			    -820
#define ER_CSS_CONN_SHUTDOWN			    -821
#define ER_CSS_CONN_GET_NEXT_CLIENT_ID		    -822
#define ER_CSS_LIST_INIT                            -823
#define ER_CSS_LIST_FINAL                           -824

#define ER_IO_PREAD				    -825
#define ER_IO_PWRITE				    -826

#define ER_CSS_INVALID_RETURN_VALUE		    -827
#define ER_CS_INVALID_INDEX			    -828
#define ER_CS_UNLOCKED_BEFORE			    -829

#define ER_QM_QENTRY_RUNOUT			    -830

#define ER_TM_TOO_MANY_CLIENTS			    -831

#define ER_LOG_BKUP_DUPLICATE_REQUESTS		    -832

#define ER_SM_INDEX_AMBIGUOUS                       -833

#define ER_QSTR_TONUM_FORMAT_MISMATCH               -834

#define ER_DO_UNDEFINED_CST_ITEM                    -835

#define ER_PAGE_LATCH_TIMEDOUT                      -836

#define ER_AU_USER_HAS_DATABASE_OBJECTS		    -837

#define ER_NOT_ENOUGH_SCANID_BIT                    -838	/* Obsolete */

#define ER_PRM_BAD_VALUE                            -839
#define ER_PRM_CANNOT_CHANGE                        -840

#define ER_NOT_SOLE_TRAN                            -841

#define ER_LC_LOCK_CACHE_ERROR                      -842

#define ER_LK_BAD_ARGUMENT                          -843
#define ER_LK_UNKNOWN_ISOLATION                     -844
#define ER_LK_INVALID_OBJECT_TYPE                   -845
#define ER_LK_NOTFOUND_IN_LOCK_HOLDER_LIST          -846
#define ER_LK_NOTFOUND_IN_TRAN_HOLD_LIST            -847
#define ER_LK_NOTFOUND_IN_TRAN_NON2PL_LIST          -848
#define ER_LK_ABORT_TRAN_TWICE                      -849
#define ER_LK_LOST_TRANSACTION                      -850
#define ER_LK_ALLOC_RESOURCE                        -851
#define ER_LK_TOTAL_HOLDERS_MODE                    -852
#define ER_LK_FAULT_GRANTED_MODE                    -853
#define ER_LK_LOCK_WAITER_ONLY                      -854
#define ER_LK_MANY_LOCK_WAIT_TRAN                   -855
#define ER_LK_STRANGE_LOCK_WAIT                     -856

#define ER_IO_TRUNCATE                              -857

#define ER_BO_UNKNOWN_VOLUME                        -858

#define ER_PAGE_LATCH_ABORTED                       -859

#define ER_LOG_2PC_CANNOT_START                     -860
#define ER_LOG_2PC_NOT_STARTED                      -861
#define ER_LOG_CANNOT_SET_GTRINFO                   -862
#define ER_LOG_CANNOT_GET_GTRINFO                   -863

#define ER_IO_CANNOT_OPEN_VERBOSE_FILE              -864

#define ER_DO_UNKNOWN_HOSTVAR_TYPE                  -865

#define ER_AU_FAIL_TO_PKI_AUTHENTICATION            -873

#define ER_QPROC_POWER_ERROR                        -874
#define ER_QPROC_OVERFLOW_POWER                     -875


#define ER_NODATA_TOBE_UNLOADED                     -877
#define ER_CFG_INVALID_DATABASES                    -878

#define ER_IO_CANNOT_GET_PERMISSION                 -879
#define ER_IO_CANNOT_CHANGE_PERMISSION              -880
#define ER_IO_GET_LOCK_FAIL                         -881
#define ER_IO_RELEASE_LOCK_FAIL                     -882

#define ER_TM_GET_STAT_FAIL                         -883
#define ER_TM_IS_NOT_WRITEABLE                      -884
#define ER_TM_CROSS_DEVICE_LINK                     -885

#define ER_UNIQUE_VIOLATION_WITHKEY                 -886

#define ER_SP_ALREADY_EXIST                         -887
#define ER_SP_INVALID_PARAM_COUNT                   -888
#define ER_SP_EXECUTE_ERROR                         -889

#define ER_PARTITION_WORK_FAILED                    -890
#define ER_PARTITION_NOT_EXIST                      -891

#define ER_SM_PRIMARY_KEY_EXISTS		    -892
#define ER_SM_ATTRIBUTE_PRIMARY_KEY_MEMBER	    -893

#define ER_SP_NOT_EXIST                             -894
#define ER_SP_INVALID_TYPE                          -895

#define ER_IO_LZO_COMPRESS_FAIL                     -896
#define ER_IO_LZO_DECOMPRESS_FAIL                   -897

#define ER_REPL_ERROR                               -898

#define ER_INVALID_PARTITION_REQUEST                -899

#define ER_SP_JVM_LIB_NOT_FOUND                     -900
#define ER_SP_CANNOT_START_JVM                      -901
#define ER_SP_NOT_RUNNING_JVM                       -902
#define ER_SP_CANNOT_CONNECT_JVM                    -903
#define ER_SP_INVALID_NAME                          -904
#define ER_SP_NETWORK_ERROR                         -905
#define ER_SP_INVAILD_JAVA_METHOD                   -906
#define ER_SP_DROP_NOT_ALLOWED                      -907
#define ER_SP_TOO_MANY_ARG_COUNT                    -908

#define ER_BO_MISSING_OR_INVALID_CATALOG            -909

#define ER_NOT_ALLOWED_ACCESS_TO_PARTITION          -910

#define ER_SP_CANNOT_RETURN_RESULTSET               -911
#define ER_SP_CANNOT_INPUT_RESULTSET                -912
#define ER_SP_TOO_MANY_NESTED_CALL                  -913

#define ER_AUTO_INCREMENT_SERIAL_ALREADY_EXIST      -914
#define ER_INCREMENT_VALUE_CANNOT_BE_ZERO           -915

#define ER_IO_RESTORE_BKVOL_NOT_INC_ACTIVE_LOG      -916
#define ER_IO_RESTORE_ACTIVE_LOG_EXIST              -917

#define ER_FK_UNKNOWN_REF_CLASSNAME                 -918
#define ER_FK_REF_CLASS_HAS_NOT_PK                  -919
#define ER_FK_NOT_HAVE_PK_MEMBER                    -920
#define ER_FK_HAS_DEFFERENT_TYPE_WITH_PK            -921
#define ER_FK_INVALID                               -922
#define ER_FK_CANT_DROP_PK_REFERRED                 -923
#define ER_FK_RESTRICT                              -924
#define ER_FK_NOT_GRANTED_LOCK                      -925
#define ER_FK_CANT_DELETE_INSTANCE                  -926
#define ER_FK_NOT_MATCH_KEY_COUNT                   -927
#define ER_FK_CANT_ASSIGN_CACHE_ATTR                -928	/* not used */
#define ER_FK_CANT_ON_VCLASS                        -929
#define ER_FK_CANT_DROP_CACHE_ATTR                  -930	/* not used */

#define ER_AUTO_INCREMENT_STARTVAL_MUST_LT_MAXVAL   -931

#define ER_DISK_CANNOT_REPAIR_INCONSISTENT_NFREE_PAGES -932
#define ER_DISK_CANNOT_REPAIR_INCONSISTENT_NFREE_SECTS -933

#define ER_MR_NULL_DOMAIN                           -934

#define ER_QPROC_FUNCTION_ARG_ERROR                 -935
#define ER_QPROC_OVERFLOW_EXP                       -936

#define ER_INTERFACE_DBMS                           -937
#define ER_INTERFACE_INVALID_ARGUMENT               -938
#define ER_INTERFACE_TOO_MANY_CONNECTION            -939
#define ER_INTERFACE_INVALID_HANDLE                 -940
#define ER_INTERFACE_NOT_SUPPORTED_OPERATION        -941
#define ER_INTERFACE_HANDLE_TIMEOUT                 -942
#define ER_INTERFACE_GENERIC                        -943
#define ER_INTERFACE_NOT_PREPARED                   -944
#define ER_INTERFACE_HAS_NO_RESULT_SET              -945
#define ER_INTERFACE_NOT_EXECUTED                   -946
#define ER_INTERFACE_NO_MORE_RESULT                 -947
#define ER_INTERFACE_NOT_ENOUGH_DATA_SIZE           -948
#define ER_INTERFACE_NO_AVAILABLE_INFORMATION       -949
#define ER_INTERFACE_INVALID_NAME                   -950
#define ER_INTERFACE_RESULTSET_NOT_UPDATABLE        -951
#define ER_INTERFACE_ROW_IS_DELETED                 -952
#define ER_INTERFACE_PARAM_IS_NOT_SET               -953
#define ER_INTERFACE_IS_NOT_BATCH_STATEMENT         -954
#define ER_INTERFACE_CANNOT_CLEAR_BATCH             -955
#define ER_INTERFACE_CANNOT_BATCH_EXECUTE           -956
#define ER_INTERFACE_IS_PREPARED_STATEMENT          -957
#define ER_INTERFACE_IS_NOT_PREPARED_STATEMENT      -958
#define ER_INTERFACE_IS_BATCH_STATEMENT             -959
#define ER_INTERFACE_NO_MORE_ERROR                  -960
#define ER_INTERFACE_END_OF_CURSOR                  -961
#define ER_INTERFACE_NO_MORE_MEMORY                 -962
#define ER_INTERFACE_BROKER                         -963
#define ER_INTERFACE_RESULTSET_CLOSED               -964
#define ER_SM_INDEX_ATTR_DUPLICATED                 -965

#define ER_LK_OBJECT_DL_TIMEOUT_SIMPLE_MSG          -966
#define ER_LK_OBJECT_DL_TIMEOUT_CLASS_MSG           -967
#define ER_LK_OBJECT_DL_TIMEOUT_CLASSOF_MSG         -968

#define ER_NET_DIFFERENT_BIT_PLATFORM               -969

#define ER_CSS_SERVER_HA_MODE_CHANGE                -970

#define ER_BO_CONNECTED_TO                          -971
#define ER_BO_CLIENT_CONNECTED                      -972
#define ER_BO_SERVER_STATUS                         -973

#define ER_LOG_ARCHIVE_CREATED                      -974

#define ER_REPL_MULTI_UPDATE_UNIQUE_VIOLATION       -975

#define ER_FILE_TABLE_OVERFLOW                      -976

#define ER_LOG_CHECKPOINT_STARTED                   -977
#define ER_LOG_CHECKPOINT_FINISHED                  -978

#define	ER_QPROC_CYCLE_DETECTED                     -979

#define ER_MNT_WAITING_THREAD                       -980
#define ER_MNT_STATS_THRESHOLD                      -981

#define ER_INTERRUPTING                             -982

#define ER_REFERENCE_TO_NON_REFERABLE_NOT_ALLOWED   -983

#define ER_PRM_CONFLICT_EXISTS_ON_MULTIPLE_SECTIONS -984
#define ER_NET_NO_EXPLICIT_SERVER_HOST              -985

#define ER_HB_STARTED                               -986
#define ER_HB_STOPPED                               -987
#define ER_HB_NODE_EVENT                            -988
#define ER_HB_PROCESS_EVENT                         -989
#define ER_HB_COMMAND_EXECUTION                     -990

#define ER_LOG_FLUSH_VICTIM_STARTED                 -991
#define ER_LOG_FLUSH_VICTIM_FINISHED                -992
#define	ER_QPROC_OVERFLOW_BITOP			    -993
#define ER_IT_IS_DISALLOWED_AS_PREPARED             -994
#define ER_IT_PREPARED_NAME_NOT_FOUND               -995
#define ER_IT_INCORRECT_HOSTVAR_COUNT               -996
#define ER_FK_CANT_ON_PARTITION                     -997
#define ER_FK_MUST_NOT_BE_NOT_NULL                  -998

#define ER_SM_CANT_COPY_WITH_FEATURE                -999
#define ER_SM_ONLY_NORMAL_ATTRIBUTES                -1000

#define ER_UNEXPECTED                               -1001
#define ER_CANNOT_GET_LOCK                          -1002
#define ER_SM_CONSTRAINT_HAS_DIFFERENT_TYPE         -1003
#define ER_SM_FK_MYSQL_DIFFERENT                    -1004
#define ER_SM_INDEX_PREFIX_LENGTH_ON_UNIQUE_FOREIGN -1007
#define ER_SM_INDEX_PREFIX_LENGTH_ON_PARTITIONED_CLASS -1008

#define ER_COMPACTDB_ALREADY_STARTED		    -1009
#define ER_SM_INVALID_INDEX_WITH_PREFIX_TYPE        -1010
#define ER_FK_CANT_ON_SHARED_ATTRIBUTE		    -1011
#define ER_CREATE_AS_SELECT_NULL_TYPE		    -1012

#define ER_SM_DEFAULT_NOT_ALLOWED                   -1013
#define ER_SM_NOT_NULL_NOT_ALLOWED                  -1014

#define ER_BO_DIRECTORY_DOESNOT_EXIST               -1015

#define ER_ES_GENERAL                               -1016
#define ER_ES_INVALID_PATH                          -1017
#define ER_ES_COPY_TO_DIFFERENT_TYPE                -1018
#define ER_ES_NO_LOB_PATH			    -1019
#define ER_ES_FILE_NOT_FOUND                        -1020
#define ER_LK_DEADLOCK_CYCLE_DETECTED		    -1021

#define ER_SM_INVALID_PREFIX_LENGTH                 -1022

#define ER_HA_LA_REPL_FILTER_GENERIC                -1023
#define ER_HA_LW_FAILED_GET_LOG_PAGE                -1024
#define ER_HA_REPL_DELAY_DETECTED                   -1025
#define ER_HA_REPL_DELAY_RESOLVED                   -1026
#define ER_HA_LA_FAILED_TO_CHANGE_STATE             -1027
#define ER_HA_LA_UNEXPECTED_EOF_IN_ARCHIVE_LOG      -1028
#define ER_HA_LA_INVALID_REPL_LOG_PAGEID_OFFSET     -1029
#define ER_HA_LA_INVALID_REPL_LOG_RECORD            -1030
#define ER_HA_LA_FAILED_TO_APPLY_STATEMENT          -1031
#define ER_HA_LA_FAILED_TO_APPLY_INSERT             -1032
#define ER_HA_LA_FAILED_TO_APPLY_UPDATE             -1033
#define ER_HA_LA_FAILED_TO_APPLY_DELETE             -1034
#define ER_HA_LA_EXCEED_MAX_MEM_SIZE                -1035
#define ER_HA_LA_STOPPED_BY_SIGNAL                  -1036
#define ER_HA_LW_STOPPED_BY_SIGNAL                  -1037
#define ER_HA_LA_STARTED		            -1038
#define ER_HA_LW_STARTED			    -1039
#define ER_HA_GENERIC_ERROR			    -1040

#define ER_QPROC_SIZE_STRING_TRUNCATED		    -1041
#define ER_QPROC_STRING_SIZE_TOO_BIG		    -1042

#define ER_ALTER_CHANGE_CLASS_HIERARCHY		    -1043
#define ER_ALTER_CHANGE_PARTITIONS		    -1044
#define ER_ALTER_CHANGE_FK			    -1045
#define ER_ALTER_CHANGE_TYPE_NOT_SUPP		    -1046
#define ER_ALTER_CHANGE_TYPE_NEED_ROW_CHECK	    -1047
#define ER_ALTER_CHANGE_TYPE_WITH_AUTO_INCR	    -1048
#define ER_ALTER_CHANGE_TYPE_UPGRADE_CFG	    -1049
#define ER_ALTER_CHANGE_TYPE_WITH_INDEX		    -1050
#define ER_ALTER_CHANGE_GAIN_CONSTRAINT		    -1051
#define ER_ALTER_CHANGE_WARN_NO_CHANGE		    -1052

#define ER_REPLACE_ODKU_NOT_ALLOWED		    -1053

#define ER_AUTO_INCREMENT_NEWVAL_MUST_GT_OLDVAL	    -1054
#define ER_AUTO_INCREMENT_NEWVAL_MUST_LT_MAXVAL	    -1055
#define ER_AUTO_INCREMENT_SINGLE_COL_AMBIGUITY	    -1056

#define ER_NOTNULL_ON_TYPE_WITHOUT_DEFAULT_VALUE    -1057

#define ER_ALTER_CHANGE_TRUNC_OVERFLOW_NOT_ALLOWED  -1058
#define ER_ALTER_CHANGE_CAST_FAILED_SET_DEFAULT	    -1059
#define ER_ALTER_CHANGE_CAST_FAILED_SET_MIN	    -1060
#define ER_ALTER_CHANGE_CAST_FAILED_SET_MAX	    -1061
#define ER_ALTER_CHANGE_MULTIPLE_PK		    -1062

#define ER_SM_ATTR_NOT_NULL			    -1063
#define ER_ALTER_CHANGE_ADD_NOT_NULL_SET_HARD_DEFAULT   -1064
#define ER_ALTER_CHANGE_HARD_DEFAULT_NOT_EXIST	    -1065

#define ER_SES_SESSION_EXPIRED			    -1066
#define ER_CANNOT_HAVE_NOTNULL_DEFAULT_NULL	    -1067

#define ER_TR_MAX_DEPTH_TOO_BIG			    -1068

#define ER_SES_TOO_MANY_STATEMENTS		    -1069

#define ER_SES_VARIABLE_NOT_FOUND		    -1070
#define ER_SES_TOO_MANY_VARIABLES		    -1071
#define ER_INACCESSIBLE_IP			    -1072
#define ER_INVALID_ACCESS_IP_CONTROL_FILE_FORMAT    -1073
#define ER_OPEN_ACCESS_LIST_FILE		    -1074

#define ER_DESC_ISCAN_ABORTED			    -1075

#define ER_BO_CANT_LOAD_SYSPRM                      -1076

#define ER_PRM_UNKNOWN_SYSPRM                       -1077

#define ER_INVALID_CHAR				    -1078
#define ER_INVALID_SERVER_CHARSET		    -1079

#define ER_SM_INVALID_FILTER_PREDICATE_LENGTH       -1080
#define ER_SM_ALTER_COLUMN_WITH_FILTER_PRED	    -1081

#define ER_PB_ALL_BUFFERS_DIRTY                     -1082

#define ER_LK_DEADLOCK_SPECIFIC_INFO                -1083

#define ER_LOG_CHECKPOINT_SKIP_INVALID_PAGE         -1084

#define ER_REGEX_COMPILE_ERROR			    -1085
#define ER_REGEX_EXEC_ERROR			    -1086

#define ER_LOG_BACKUP_CS_ENTER 			    -1087
#define ER_LOG_BACKUP_CS_EXIT			    -1088

#define ER_HF_MAX_BESTSPACE_ENTRIES		    -1089

#define ER_LOC_INIT				    -1090
#define ER_LOC_GEN				    -1091
#define ER_CANNOT_PREPARE_WITH_HOST_VAR		    -1092

#define ER_OPFUNC_INET_ATON_ARG			    -1093
#define ER_OPFUNC_INET_NTOA_ARG			    -1094

#define ER_USER_NAME_TOO_LONG			    -1095

#define ER_ALTER_PARTITIONS_FK_NOT_ALLOWED	    -1096
#define ER_CANNOT_HAVE_PK_DEFAULT_NULL		    -1097

#define ER_LANG_CODESET_NOT_AVAILABLE		    -1098

#define ER_BLOCK_DDL_STMT                           -1099
#define ER_BLOCK_NOWHERE_STMT                       -1100

#define ER_CANNOT_GET_KEY_LOCK                      -1101

#define ER_PRM_BAD_VALUE_NO_DATA                    -1102
#define ER_PRM_CANNOT_CHANGE_NO_DATA                -1103

#define ER_SLOW_QUERY				    -1104

#define ER_LC_PARTIALLY_FAILED_TO_FLUSH             -1105

#define ER_MERGE_TOO_MANY_SOURCE_ROWS		    -1106

#define ER_NTILE_INVALID_BUCKET_NUMBER              -1107

#define ER_PROC_WIDTH_BUCKET_COUNT                  -1108

#define ER_INVALID_DATA_FOR_PARTITION		    -1109

#define ER_BTREE_NO_SPACE			    -1110

#define ER_DATA_IS_TRUNCATED_TO_PRECISION           -1111

#define ER_CHAR_CONV_NO_MATCH			    -1112

#define ER_BINARY_HEAP_OUT_OF_RANGE		    -1113

#define ER_ARG_OUT_OF_RANGE                         -1114

#define ER_LOG_STARTED_TO_UPDATE_STATISTICS         -1115
#define ER_LOG_FINISHED_TO_UPDATE_STATISTICS        -1116

#define ER_ALTER_CHANGE_ATTR_TO_FROM_SHARED_NOT_ALLOWED  -1117

#define ER_ARG_CAN_NOT_BE_CASTED_TO_DESIRED_DOMAIN  -1118

#define ER_FILE_INCONSISTENT_PAGE_NOT_ALLOCED       -1119
#define ER_FILE_INCONSISTENT_PAGE_ALLOCED           -1120

#define ER_NOT_NULL_DOES_NOT_ALLOW_NULL_VALUE	    -1121

#define ER_NET_DATA_RECEIVE_TIMEDOUT                -1122

#define ER_CSS_PTHREAD_COND_TIMEDOUT                -1123

#define ER_QUERY_EXECUTION_ERROR                    -1124

#define ER_BTREE_CREATED_OVERFLOW_KEY                 -1125

#define ER_BTREE_CREATED_OVERFLOW_PAGE                -1126
#define ER_BTREE_DELETED_OVERFLOW_PAGE                -1127

#define ER_LOG_RECOVERY_STARTED                     -1128

#define ER_LOG_RECOVERY_FINISHED                    -1129

#define ER_LK_ROLLBACK_ON_LOCK_ESCALATION	    -1130

#define ER_SP_INVALID_HEADER                        -1131

#define ER_ENCRYPTION_LIB_FAILED                    -1132

#define ERR_CSS_COPYLOG_ALREADY_EXISTS              -1133
#define ERR_CSS_APPLYLOG_ALREADY_EXISTS             -1134

#define ER_BTREE_CORRUPT_PREV_LINK                  -1135
#define ER_BTREE_REPAIR_PREV_LINK                   -1136

#define ER_MAX_RECURSION_SQL_DEPTH                  -1137

#define ER_NET_HS_INCOMPAT_INTERRUPTIBILITY         -1138
#define ER_NET_HS_INCOMPAT_RW_MODE                  -1139
#define ER_NET_HS_HA_REPL_DELAY                     -1140
#define ER_NET_HS_HA_REPLICA_ONLY                   -1141
#define ER_NET_HS_REMOTE_DISABLED                   -1142
#define ER_NET_HS_UNKNOWN_SERVER_REL                -1143

#define ERR_CSS_TCP_CONNECT_TIMEDOUT                -1144

#define ER_AU_CANT_ALTER_OWNER_OF_SYSTEM_CLASS      -1145
#define ER_DIAG_VOLID_NOT_EXIST                     -1146

#define ER_ALL_PLAN_CACHE_ENTRIES_ARE_FIXED         -1147
#define ER_ALL_FILTER_PRED_CACHE_ENTRIES_ARE_FIXED  -1148

#define ER_DIAG_PAGE_NOT_FOUND                      -1149

#define ER_QSTR_INCOMPATIBLE_COLLATIONS		    -1150

#define ER_DIAG_NOT_SPAGE                           -1151

#define ER_KILL_TR_NOT_ALLOWED                      -1152

#define ER_ATTEMPT_TO_USE_ZERODATE                  -1153

#define ER_MVCC_NOT_SATISFIED_REEVALUATION	    -1154
#define ER_MVCC_ROW_INVALID_FOR_DELETE		    -1155
#define ER_MVCC_CANT_GET_SNAPSHOT		    -1156
#define ER_MVCC_LOG_INVALID_ISOLATION_LEVEL         -1157
#define ER_MVCC_SERIALIZABLE_CONFLICT		    -1158

#define ER_TZ_COMPILE_ERROR                         -1159
#define ER_TZ_LOAD_ERROR                            -1160
#define ER_TZ_INTERNAL_ERROR			    -1161
#define ER_TZ_INVALID_TIMEZONE			    -1162
#define ER_TZ_INVALID_DST			    -1163
#define ER_TZ_DST_NOT_SUPPORTED			    -1164
#define ER_TZ_INVALID_COMBINATION		    -1165
#define ER_TZ_DURING_DS_LEAP                        -1166

#define ER_AU_COMMENT_OVERFLOW                      -1167

#define ER_UPDATE_STAT_CANNOT_GET_LOCK              -1168

#define ER_SM_INVALID_UNIQUE_IDX_PARTITION	    -1169

#define ER_LC_FAILED_TO_FLUSH_REPL_ITEMS            -1170

#define ER_NOT_A_EMPTY_VOLUME                       -1171

#define ER_PERCENTILE_FUNC_INVALID_PERCENTILE_RANGE     -1172
#define ER_PERCENTILE_FUNC_PERCENTILE_CHANGED_IN_GROUP  -1173

#define ER_INHERIT_FROM_PARTITION_TABLE		    -1174

#define ER_TZ_GEOGRAPHIC_ZONE			    -1175

#define ER_PAGE_LATCH_PROMOTE_FAIL		    -1176

#define ER_TZ_INCOMPATIBLE_TIMEZONE_LIBRARIES       -1177

#define ER_LF_BITMAP_INVALID_FREE                   -1178

#define ER_HEAP_FOUND_NOT_VACUUMED		    -1179
#define ER_INDEX_FOUND_NOT_VACUUMED		    -1180
#define ER_VACUUM_CS_NOT_AVAILABLE		    -1181

#define ER_PB_ORDERED_REFIX_FAILED                  -1182
#define ER_PB_ORDERED_INCONSISTENCY		    -1183
#define ER_PB_ORDERED_TOO_MANY_RETRIES		    -1184
#define ER_PB_UNEXPECTED_PAGE_REFIX		    -1185

#define ER_CHKSUM_GENERIC_ERR         		    -1186

#define ER_PB_ORDERED_NO_HEAP         		    -1187

#define ER_AU_NOT_ALLOW_TO_DROP_ACTIVE_USER 	    -1188

#define ER_TZ_DURING_OFFSET_RULE_LEAP		    -1189

#define ER_STAND_ALONE_VACUUM_START		    -1190
#define ER_STAND_ALONE_VACUUM_END		    -1191

#define ER_PRECISION_OVERFLOW			    -1192
#define ER_PARTITION_EXPRESSION_TOO_LONG	    -1193

#define ER_CANNOT_CHECK_FILE                        -1194

#define ER_BUILDVALUE_IN_REC_CTE		    -1195
#define ER_CTE_MAX_RECURSION_REACHED		    -1196

#define ER_LAST_ERROR                               -1197

#define DB_TRUE 1
#define DB_FALSE 0

#define TRAN_ASYNC_WS_BIT                        0x10	/* 1 0000 */
#define TRAN_ISO_LVL_BITS                        0x0F	/* 0 1111 */

#define DB_AUTH_ALL \
  ((DB_AUTH) (DB_AUTH_SELECT | DB_AUTH_INSERT | DB_AUTH_UPDATE | DB_AUTH_DELETE | \
   DB_AUTH_ALTER  | DB_AUTH_INDEX  | DB_AUTH_EXECUTE))

/* It is strongly advised that applications use these macros for access
   to the fields of the DB_QUERY_ERROR structure */

#define DB_QUERY_ERROR_LINE(error) ((error)->err_lineno)
#define DB_QUERY_ERROR_CHAR(error) ((error)->err_posno)

/*  These are the status codes that can be returned by
    the functions that iterate over statement results. */
#define DB_CURSOR_SUCCESS      0
#define DB_CURSOR_END          1
#define DB_CURSOR_ERROR       -1

#define DB_IS_CONSTRAINT_UNIQUE_FAMILY(c) \
                                    ( ((c) == DB_CONSTRAINT_UNIQUE          || \
                                       (c) == DB_CONSTRAINT_REVERSE_UNIQUE  || \
                                       (c) == DB_CONSTRAINT_PRIMARY_KEY)       \
                                      ? true : false )

#define DB_IS_CONSTRAINT_INDEX_FAMILY(c) \
                                    ( (DB_IS_CONSTRAINT_UNIQUE_FAMILY(c)    || \
                                       (c) == DB_CONSTRAINT_INDEX           || \
                                       (c) == DB_CONSTRAINT_REVERSE_INDEX   || \
                                       (c) == DB_CONSTRAINT_FOREIGN_KEY)       \
                                      ? true : false )

#define DB_IS_CONSTRAINT_REVERSE_INDEX_FAMILY(c) \
                                    ( ((c) == DB_CONSTRAINT_REVERSE_UNIQUE  || \
                                       (c) == DB_CONSTRAINT_REVERSE_INDEX)     \
                                      ? true : false )

#define DB_IS_CONSTRAINT_FAMILY(c) \
                                    ( (DB_IS_CONSTRAINT_UNIQUE_FAMILY(c)    || \
                                       (c) == DB_CONSTRAINT_NOT_NULL        || \
                                       (c) == DB_CONSTRAINT_FOREIGN_KEY)       \
                                      ? true : false )

/* Volume purposes constants.  These are intended for use by the
   db_add_volext API function. */
typedef enum
{
  DB_PERMANENT_DATA_PURPOSE = 0,
  DB_TEMPORARY_DATA_PURPOSE = 1,	/* internal use only */
  DISK_UNKNOWN_PURPOSE = 2,	/* internal use only: Does not mean anything */
} DB_VOLPURPOSE;

typedef enum
{
  DB_PERMANENT_VOLTYPE,
  DB_TEMPORARY_VOLTYPE
} DB_VOLTYPE;

/* These are the status codes that can be returned by db_value_compare. */
typedef enum
{

  DB_SUBSET = -3,		/* strict subset for set types.  */
  DB_UNK = -2,			/* unknown */
  DB_LT = -1,			/* cannonical less than */
  DB_EQ = 0,			/* equal */
  DB_GT = 1,			/* cannonical greater than, */
  DB_NE = 2,			/* not equal because types incomparable */
  DB_SUPERSET = 3		/* strict superset for set types.  */
} DB_VALUE_COMPARE_RESULT;

/* Object fetch and locking constants.  These are used to specify
   a lock mode when fetching objects using of the explicit fetch and
   lock functions. */
typedef enum
{
  DB_FETCH_READ = 0,		/* Read an object (class or instance) */
  DB_FETCH_WRITE = 1,		/* Update an object (class or instance) */
  DB_FETCH_DIRTY = 2,		/* Does not care about the state of the object (class or instance). Get it even if it
				 * is obsolete or if it becomes obsolete. INTERNAL USE ONLY */
  DB_FETCH_CLREAD_INSTREAD = 3,	/* Read the class and read an instance of class. This is to access an instance in
				 * shared mode Note class must be given INTERNAL USE ONLY */
  DB_FETCH_CLREAD_INSTWRITE = 4,	/* Read the class and update an instance of the class. Note class must be given
					 * This is for creation of instances INTERNAL USE ONLY */
  DB_FETCH_QUERY_READ = 5,	/* Read the class and query (read) all instances of the class. Note class must be given
				 * This is for SQL select INTERNAL USE ONLY */
  DB_FETCH_QUERY_WRITE = 6,	/* Read the class and query (read) all instances of the class and update some of those
				 * instances. Note class must be given This is for Query update (SQL update) or Query
				 * delete (SQL delete) INTERNAL USE ONLY */
  DB_FETCH_SCAN = 7,		/* Read the class for scan purpose The lock of the lock should be kept since the actual 
				 * access happens later. This is for loading an index. INTERNAL USE ONLY */
  DB_FETCH_EXCLUSIVE_SCAN = 8	/* Read the class for exclusive scan purpose The lock of the lock should be kept since
				 * the actual access happens later. This is for loading an index. INTERNAL USE ONLY */
} DB_FETCH_MODE;

/* Authorization type identifier constants.  The numeric values of these
   are defined such that they can be used with the bitwise or operator
    "|" in order to specify more than one authorization type. */
typedef enum
{
  DB_AUTH_NONE = 0,
  DB_AUTH_SELECT = 1,
  DB_AUTH_INSERT = 2,
  DB_AUTH_UPDATE = 4,
  DB_AUTH_DELETE = 8,
  DB_AUTH_ALTER = 16,
  DB_AUTH_INDEX = 32,
  DB_AUTH_EXECUTE = 64
} DB_AUTH;

/* object_id type constants used in a db_register_ldb api call to specify
   whether a local database supports intrinsic object identity or user-
   defined object identity. */
typedef enum
{
  DB_OID_INTRINSIC = 1,
  DB_OID_USER_DEFINED
} DB_OBJECT_ID_TYPE;

/* These are abstract data type pointers used by the functions
   that issue SQL statements and return their results. */
typedef struct db_query_result DB_QUERY_RESULT;
typedef struct db_query_type DB_QUERY_TYPE;

/* Type of the column in SELECT list within DB_QUERY_TYPE structure */
typedef enum
{
  DB_COL_EXPR,
  DB_COL_VALUE,
  DB_COL_NAME,
  DB_COL_OID,
  DB_COL_PATH,
  DB_COL_FUNC,
  DB_COL_OTHER
} DB_COL_TYPE;

typedef enum db_class_modification_status
{
  DB_CLASS_NOT_MODIFIED,
  DB_CLASS_MODIFIED,
  DB_CLASS_ERROR
} DB_CLASS_MODIFICATION_STATUS;

typedef enum
{
  CUBRID_STMT_ALTER_CLASS,
  CUBRID_STMT_ALTER_SERIAL,
  CUBRID_STMT_COMMIT_WORK,
  CUBRID_STMT_REGISTER_DATABASE,
  CUBRID_STMT_CREATE_CLASS,
  CUBRID_STMT_CREATE_INDEX,
  CUBRID_STMT_CREATE_TRIGGER,
  CUBRID_STMT_CREATE_SERIAL,
  CUBRID_STMT_DROP_DATABASE,
  CUBRID_STMT_DROP_CLASS,
  CUBRID_STMT_DROP_INDEX,
  CUBRID_STMT_DROP_LABEL,
  CUBRID_STMT_DROP_TRIGGER,
  CUBRID_STMT_DROP_SERIAL,
  CUBRID_STMT_EVALUATE,
  CUBRID_STMT_RENAME_CLASS,
  CUBRID_STMT_ROLLBACK_WORK,
  CUBRID_STMT_GRANT,
  CUBRID_STMT_REVOKE,
  CUBRID_STMT_UPDATE_STATS,
  CUBRID_STMT_INSERT,
  CUBRID_STMT_SELECT,
  CUBRID_STMT_UPDATE,
  CUBRID_STMT_DELETE,
  CUBRID_STMT_CALL,
  CUBRID_STMT_GET_ISO_LVL,
  CUBRID_STMT_GET_TIMEOUT,
  CUBRID_STMT_GET_OPT_LVL,
  CUBRID_STMT_SET_OPT_LVL,
  CUBRID_STMT_SCOPE,
  CUBRID_STMT_GET_TRIGGER,
  CUBRID_STMT_SET_TRIGGER,
  CUBRID_STMT_SAVEPOINT,
  CUBRID_STMT_PREPARE,
  CUBRID_STMT_ATTACH,
  CUBRID_STMT_USE,
  CUBRID_STMT_REMOVE_TRIGGER,
  CUBRID_STMT_RENAME_TRIGGER,
  CUBRID_STMT_ON_LDB,
  CUBRID_STMT_GET_LDB,
  CUBRID_STMT_SET_LDB,
  CUBRID_STMT_GET_STATS,
  CUBRID_STMT_CREATE_USER,
  CUBRID_STMT_DROP_USER,
  CUBRID_STMT_ALTER_USER,
  CUBRID_STMT_SET_SYS_PARAMS,
  CUBRID_STMT_ALTER_INDEX,

  CUBRID_STMT_CREATE_STORED_PROCEDURE,
  CUBRID_STMT_DROP_STORED_PROCEDURE,
  CUBRID_STMT_PREPARE_STATEMENT,
  CUBRID_STMT_EXECUTE_PREPARE,
  CUBRID_STMT_DEALLOCATE_PREPARE,
  CUBRID_STMT_TRUNCATE,
  CUBRID_STMT_DO,
  CUBRID_STMT_SELECT_UPDATE,
  CUBRID_STMT_SET_SESSION_VARIABLES,
  CUBRID_STMT_DROP_SESSION_VARIABLES,
  CUBRID_STMT_MERGE,
  CUBRID_STMT_SET_NAMES,
  CUBRID_STMT_ALTER_STORED_PROCEDURE,
  CUBRID_STMT_ALTER_STORED_PROCEDURE_OWNER = CUBRID_STMT_ALTER_STORED_PROCEDURE,
  CUBRID_STMT_KILL,

  CUBRID_MAX_STMT_TYPE
} CUBRID_STMT_TYPE;

#define SQLX_CMD_TYPE CUBRID_STMT_TYPE

#define SQLX_CMD_ALTER_CLASS   CUBRID_STMT_ALTER_CLASS
#define SQLX_CMD_ALTER_SERIAL   CUBRID_STMT_ALTER_SERIAL
#define SQLX_CMD_COMMIT_WORK   CUBRID_STMT_COMMIT_WORK
#define SQLX_CMD_REGISTER_DATABASE   CUBRID_STMT_REGISTER_DATABASE
#define SQLX_CMD_CREATE_CLASS   CUBRID_STMT_CREATE_CLASS
#define SQLX_CMD_CREATE_INDEX   CUBRID_STMT_CREATE_INDEX
#define SQLX_CMD_CREATE_TRIGGER   CUBRID_STMT_CREATE_TRIGGER
#define SQLX_CMD_CREATE_SERIAL   CUBRID_STMT_CREATE_SERIAL
#define SQLX_CMD_DROP_DATABASE   CUBRID_STMT_DROP_DATABASE
#define SQLX_CMD_DROP_CLASS   CUBRID_STMT_DROP_CLASS
#define SQLX_CMD_DROP_INDEX   CUBRID_STMT_DROP_INDEX
#define SQLX_CMD_DROP_LABEL   CUBRID_STMT_DROP_LABEL
#define SQLX_CMD_DROP_TRIGGER   CUBRID_STMT_DROP_TRIGGER
#define SQLX_CMD_DROP_SERIAL   CUBRID_STMT_DROP_SERIAL
#define SQLX_CMD_EVALUATE   CUBRID_STMT_EVALUATE
#define SQLX_CMD_RENAME_CLASS   CUBRID_STMT_RENAME_CLASS
#define SQLX_CMD_ROLLBACK_WORK   CUBRID_STMT_ROLLBACK_WORK
#define SQLX_CMD_GRANT   CUBRID_STMT_GRANT
#define SQLX_CMD_REVOKE   CUBRID_STMT_REVOKE
#define SQLX_CMD_UPDATE_STATS   CUBRID_STMT_UPDATE_STATS
#define SQLX_CMD_INSERT   CUBRID_STMT_INSERT
#define SQLX_CMD_SELECT   CUBRID_STMT_SELECT
#define SQLX_CMD_UPDATE   CUBRID_STMT_UPDATE
#define SQLX_CMD_DELETE   CUBRID_STMT_DELETE
#define SQLX_CMD_CALL   CUBRID_STMT_CALL
#define SQLX_CMD_GET_ISO_LVL   CUBRID_STMT_GET_ISO_LVL
#define SQLX_CMD_GET_TIMEOUT   CUBRID_STMT_GET_TIMEOUT
#define SQLX_CMD_GET_OPT_LVL   CUBRID_STMT_GET_OPT_LVL
#define SQLX_CMD_SET_OPT_LVL   CUBRID_STMT_SET_OPT_LVL
#define SQLX_CMD_SCOPE   CUBRID_STMT_SCOPE
#define SQLX_CMD_GET_TRIGGER   CUBRID_STMT_GET_TRIGGER
#define SQLX_CMD_SET_TRIGGER   CUBRID_STMT_SET_TRIGGER
#define SQLX_CMD_SAVEPOINT   CUBRID_STMT_SAVEPOINT
#define SQLX_CMD_PREPARE   CUBRID_STMT_PREPARE
#define SQLX_CMD_ATTACH   CUBRID_STMT_ATTACH
#define SQLX_CMD_USE   CUBRID_STMT_USE
#define SQLX_CMD_REMOVE_TRIGGER   CUBRID_STMT_REMOVE_TRIGGER
#define SQLX_CMD_RENMAE_TRIGGER   CUBRID_STMT_RENAME_TRIGGER
#define SQLX_CMD_ON_LDB   CUBRID_STMT_ON_LDB
#define SQLX_CMD_GET_LDB   CUBRID_STMT_GET_LDB
#define SQLX_CMD_SET_LDB   CUBRID_STMT_SET_LDB
#define SQLX_CMD_GET_STATS   CUBRID_STMT_GET_STATS
#define SQLX_CMD_CREATE_USER   CUBRID_STMT_CREATE_USER
#define SQLX_CMD_DROP_USER   CUBRID_STMT_DROP_USER
#define SQLX_CMD_ALTER_USER   CUBRID_STMT_ALTER_USER
#define SQLX_CMD_SET_SYS_PARAMS   CUBRID_STMT_SET_SYS_PARAMS
#define SQLX_CMD_ALTER_INDEX   CUBRID_STMT_ALTER_INDEX

#define SQLX_CMD_CREATE_STORED_PROCEDURE   CUBRID_STMT_CREATE_STORED_PROCEDURE
#define SQLX_CMD_DROP_STORED_PROCEDURE   CUBRID_STMT_DROP_STORED_PROCEDURE
#define SQLX_CMD_PREPARE_STATEMENT  CUBRID_STMT_PREPARE_STATEMENT
#define SQLX_CMD_EXECUTE_PREPARE  CUBRID_STMT_EXECUTE_PREPARE
#define SQLX_CMD_DEALLOCATE_PREPARE  CUBRID_STMT_DEALLOCATE_PREPARE
#define SQLX_CMD_TRUNCATE  CUBRID_STMT_TRUNCATE
#define SQLX_CMD_DO  CUBRID_STMT_DO
#define SQLX_CMD_SELECT_UPDATE   CUBRID_STMT_SELECT_UPDATE
#define SQLX_CMD_SET_SESSION_VARIABLES  CUBRID_STMT_SET_SESSION_VARIABLES
#define SQLX_CMD_DROP_SESSION_VARIABLES  CUBRID_STMT_DROP_SESSION_VARIABLES
#define SQLX_CMD_STMT_MERGE  CUBRID_STMT_MERGE
#define SQLX_CMD_SET_NAMES   CUBRID_STMT_SET_NAMES
#define SQLX_CMD_ALTER_STORED_PROCEDURE   CUBRID_STMT_ALTER_STORED_PROCEDURE
#define SQLX_CMD_ALTER_STORED_PROCEDURE_OWNER   CUBRID_STMT_ALTER_STORED_PROCEDURE
#define SQLX_MAX_CMD_TYPE   CUBRID_MAX_STMT_TYPE

/* Structure used to contain information about the position of
   an error detected while compiling a statement. */
typedef struct db_query_error DB_QUERY_ERROR;
struct db_query_error
{

  int err_lineno;		/* Line number where error occurred */
  int err_posno;		/* Position number where error occurred */
};

/* ESQL/CSQL/API INTERFACE */
typedef struct db_session DB_SESSION;
typedef struct parser_node DB_NODE;
typedef DB_NODE DB_SESSION_ERROR;
typedef DB_NODE DB_SESSION_WARNING;
typedef DB_NODE DB_PARAMETER;
typedef DB_NODE DB_MARKER;
typedef int STATEMENT_ID;

/* These are abstract data type pointers used by the "browsing" functions.
 * Currently they map directly onto internal unpublished data
 * structures but that are subject to change. API programs are
 * allowed to use them only for those API functions that
 * return them or accept them as arguments. API functions cannot
 * make direct structure references or make any assumptions about
 * the actual definition of these structures.
 */
typedef struct sm_attribute DB_ATTRIBUTE;
typedef struct sm_method DB_METHOD;
typedef struct sm_method_argument DB_METHARG;
typedef struct sm_method_file DB_METHFILE;
typedef struct sm_resolution DB_RESOLUTION;
typedef struct sm_query_spec DB_QUERY_SPEC;
typedef struct tp_domain DB_DOMAIN;
typedef struct tp_domain SM_DOMAIN;
typedef struct tp_domain TP_DOMAIN;

/* These are handles to attribute and method descriptors that can
   be used for optimized lookup during repeated operations.
   They are NOT the same as the DB_ATTRIBUTE and DB_METHOD handles. */
typedef struct sm_descriptor DB_ATTDESC;
typedef struct sm_descriptor DB_METHDESC;

/* These structures are used for building editing templates on classes
 * and objects.  Templates allow the specification of multiple
 * operations to the object that are treated as an atomic unit.  If any
 * of the operations in the template fail, none of the operations
 * will be applied to the object.
 * They are defined as abstract data types on top of internal
 * data structures, API programs are not allowed to make assumptions
 * about the contents of these structures.
 */

typedef struct sm_template DB_CTMPL;
typedef struct obj_template DB_OTMPL;

/* Structure used to define statically linked methods. */
typedef void (*METHOD_LINK_FUNCTION) ();
typedef struct db_method_link DB_METHOD_LINK;
struct db_method_link
{

  const char *method;
  METHOD_LINK_FUNCTION function;

};

/* Used to indicate the status of a trigger.
 * If a trigger is ACTIVE, it will be raised when its event is
 * detected.  If it is INACTIVE, it will not be raised.  If it is
 * INVALID, it indicates that the class associated with the trigger
 * has been deleted.
 */
typedef enum
{

  TR_STATUS_INVALID = 0,
  TR_STATUS_INACTIVE = 1,
  TR_STATUS_ACTIVE = 2
} DB_TRIGGER_STATUS;


/* These define the possible trigger event types.
 * The system depends on the numeric order of these constants, do not
 * modify this definition without understanding the trigger manager
 * source.
 */
typedef enum
{

  /* common to both class cache & attribute cache */
  TR_EVENT_UPDATE = 0,
  TR_EVENT_STATEMENT_UPDATE = 1,

  /* class cache events */
  TR_EVENT_DELETE = 2,
  TR_EVENT_STATEMENT_DELETE = 3,
  TR_EVENT_INSERT = 4,
  TR_EVENT_STATEMENT_INSERT = 5,
  TR_EVENT_ALTER = 6,		/* currently unsupported */
  TR_EVENT_DROP = 7,		/* currently unsupported */

  /* user cache events */
  TR_EVENT_COMMIT = 8,
  TR_EVENT_ROLLBACK = 9,
  TR_EVENT_ABORT = 10,		/* currently unsupported */
  TR_EVENT_TIMEOUT = 11,	/* currently unsupported */

  /* default */
  TR_EVENT_NULL = 12,

  /* not really event, but used for processing */
  TR_EVENT_ALL = 13
} DB_TRIGGER_EVENT;

/* These define the possible trigger activity times. Numeric order is
 * important here, don't change without understanding
 * the trigger manager source.
 */
typedef enum
{
  TR_TIME_NULL = 0,
  TR_TIME_BEFORE = 1,
  TR_TIME_AFTER = 2,
  TR_TIME_DEFERRED = 3
} DB_TRIGGER_TIME;

/* These define the possible trigger action types. */
typedef enum
{
  TR_ACT_NULL = 0,		/* no action */
  TR_ACT_EXPRESSION = 1,	/* complex expression */
  TR_ACT_REJECT = 2,		/* REJECT action */
  TR_ACT_INVALIDATE = 3,	/* INVALIDATE TRANSACTION action */
  TR_ACT_PRINT = 4		/* PRINT action */
} DB_TRIGGER_ACTION;

/* This is the generic pointer to database objects.  An object may be
 * either an instance or a class.  The actual structure is defined
 * elsewhere and it is not necessary for database applications to
 * understand its contents.
 */
typedef struct db_object DB_OBJECT, *MOP;

/* Structure defining the common list link header used by the general
 * list routines.  Any structure in the db_ layer that are linked in
 * lists will follow this convention.
 */
typedef struct db_list DB_LIST;
struct db_list
{

  struct db_list *next;

};

/* List structure with an additional name field.
 * Used by: obsolete browsing functions
 *  pt_find_labels
 *  db_get_savepoints
 *  "object id" functions in SQL/M
 */
typedef struct db_namelist DB_NAMELIST;

struct db_namelist
{
  struct db_namelist *next;
  const char *name;

};

/* List structure with additional object pointer field.
   Might belong in dbtype.h but we rarely use object lists on the server. */
typedef struct db_objlist DB_OBJLIST;
typedef struct db_objlist *MOPLIST;

struct db_objlist
{
  struct db_objlist *next;
  struct db_object *op;

};

typedef struct sm_class_constraint DB_CONSTRAINT;


/* Types of constraints that may be applied to applibutes.  This type
   is used by the db_add_constraint()/db_drop_constraint() API functions. */
typedef enum
{
  DB_CONSTRAINT_UNIQUE = 0,
  DB_CONSTRAINT_INDEX = 1,
  DB_CONSTRAINT_NOT_NULL = 2,
  DB_CONSTRAINT_REVERSE_UNIQUE = 3,
  DB_CONSTRAINT_REVERSE_INDEX = 4,
  DB_CONSTRAINT_PRIMARY_KEY = 5,
  DB_CONSTRAINT_FOREIGN_KEY = 6
} DB_CONSTRAINT_TYPE;

typedef enum
{
  DB_FK_DELETE = 0,
  DB_FK_UPDATE = 1
} DB_FK_ACTION_TYPE;

typedef enum
{
  DB_INSTANCE_OF_A_CLASS = 'a',
  DB_INSTANCE_OF_A_PROXY = 'b',
  DB_INSTANCE_OF_A_VCLASS_OF_A_CLASS = 'c',
  DB_INSTANCE_OF_A_VCLASS_OF_A_PROXY = 'd',
  DB_INSTANCE_OF_NONUPDATABLE_OBJECT = 'e'
} DB_OBJECT_TYPE;

/* session state id */
typedef unsigned int SESSION_ID;
/* uninitialized value for session id */
#define DB_EMPTY_SESSION 0
/* uninitialized value for row count */
#define DB_ROW_COUNT_NOT_SET -2

/*
 * DB_MAX_IDENTIFIER_LENGTH -
 * This constant defines the maximum length of an identifier
 * in the database.  An identifier is anything that is passed as a string
 * to the db_ functions (other than user attribute values).  This
 * includes such things as class names, attribute names etc.  This
 * isn't strictly enforced right now but applications must be aware that
 * this will be a requirement.
 */
#define DB_MAX_IDENTIFIER_LENGTH 255

/* Maximum allowable user name.*/
#define DB_MAX_USER_LENGTH 32

#define DB_MAX_PASSWORD_LENGTH 8

/* Maximum allowable schema name. */
#define DB_MAX_SCHEMA_LENGTH DB_MAX_USER_LENGTH

/* Maximum allowable class name. */
#define DB_MAX_CLASS_LENGTH (DB_MAX_IDENTIFIER_LENGTH-DB_MAX_SCHEMA_LENGTH-4)

#define DB_MAX_SPEC_LENGTH       4096

/* Maximum allowable class comment length */
#define DB_MAX_CLASS_COMMENT_LENGTH     2048
/* Maximum allowable comment length */
#define DB_MAX_COMMENT_LENGTH    1024

/* This constant defines the maximum length of a character
   string that can be used as the value of an attribute. */
#define DB_MAX_STRING_LENGTH	0x3fffffff

/* This constant defines the maximum length of a bit string
   that can be used as the value of an attribute. */
#define DB_MAX_BIT_LENGTH 0x3fffffff

/* The maximum precision that can be specified for a numeric domain. */
#define DB_MAX_NUMERIC_PRECISION 38

/* The upper limit for a numeber that can be represented by a numeric type */
#define DB_NUMERIC_OVERFLOW_LIMIT 1e38

/* The lower limit for a number that can be represented by a numeric type */
#define DB_NUMERIC_UNDERFLOW_LIMIT 1e-38

/* The maximum precision that can be specified for a CHAR(n) domain. */
#define DB_MAX_CHAR_PRECISION DB_MAX_STRING_LENGTH

/* The maximum precision that can be specified
   for a CHARACTER VARYING domain.*/
#define DB_MAX_VARCHAR_PRECISION DB_MAX_STRING_LENGTH

/* The maximum precision that can be specified for a NATIONAL CHAR(n)
   domain.
   This probably isn't restrictive enough.  We may need to define
   this functionally as the maximum precision will depend on the size
   multiplier of the codeset.*/
#define DB_MAX_NCHAR_PRECISION (DB_MAX_STRING_LENGTH/2)

/* The maximum precision that can be specified for a NATIONAL CHARACTER
   VARYING domain.
   This probably isn't restrictive enough.  We may need to define
   this functionally as the maximum precision will depend on the size
   multiplier of the codeset. */
#define DB_MAX_VARNCHAR_PRECISION DB_MAX_NCHAR_PRECISION

/*  The maximum precision that can be specified for a BIT domain. */
#define DB_MAX_BIT_PRECISION DB_MAX_BIT_LENGTH

/* The maximum precision that can be specified for a BIT VARYING domain. */
#define DB_MAX_VARBIT_PRECISION DB_MAX_BIT_PRECISION

/* This constant indicates that the system defined default for
   determining the length of a string is to be used for a DB_VALUE. */
#define DB_DEFAULT_STRING_LENGTH -1

/* This constant indicates that the system defined default for
   precision is to be used for a DB_VALUE. */
#define DB_DEFAULT_PRECISION -1

/* This constant indicates that the system defined default for
   scale is to be used for a DB_VALUE. */
#define DB_DEFAULT_SCALE -1

/* This constant defines the default precision of DB_TYPE_NUMERIC. */
#define DB_DEFAULT_NUMERIC_PRECISION 15

/* This constant defines the default scale of DB_TYPE_NUMERIC. */
#define DB_DEFAULT_NUMERIC_SCALE 0

/* This constant defines the default scale of result
   of numeric division operation */
#define DB_DEFAULT_NUMERIC_DIVISION_SCALE 9

/* These constants define the size of buffers within a DB_VALUE. */
#define DB_NUMERIC_BUF_SIZE	(2*sizeof(double))
#define DB_SMALL_CHAR_BUF_SIZE	(2*sizeof(double) - 3*sizeof(unsigned char))

/* This constant defines the default precision of DB_TYPE_BIGINT. */
#define DB_BIGINT_PRECISION      19

/* This constant defines the default precision of DB_TYPE_INTEGER. */
#define DB_INTEGER_PRECISION      10

/* This constant defines the default precision of DB_TYPE_SMALLINT. */
#define DB_SMALLINT_PRECISION      5

/* This constant defines the default precision of DB_TYPE_SHORT.*/
#define DB_SHORT_PRECISION      DB_SMALLINT_PRECISION

/* This constant defines the default decimal precision of DB_TYPE_FLOAT. */
#define DB_FLOAT_DECIMAL_PRECISION      7

/* This constant defines the default decimal precision of DB_TYPE_DOUBLE. */
#define DB_DOUBLE_DECIMAL_PRECISION      15

/* This constant defines the default decimal precision of DB_TYPE_MONETARY. */
#define DB_MONETARY_DECIMAL_PRECISION DB_DOUBLE_DECIMAL_PRECISION

/* This constant defines the default precision of DB_TYPE_TIME. */
#define DB_TIME_PRECISION      8

/* This constant defines the default precision of ECISIONTZ_PRECISION. */
#define DB_TIMETZ_PRECISION   DB_TIME_PRECISION

/* This constant defines the default precision of DB_TYPE_DATE. */
#define DB_DATE_PRECISION      10

/* This constant defines the default precision of DB_TYPE_TIMESTAMP. */
#define DB_TIMESTAMP_PRECISION      19

/* This constant defines the default precision of DB_TYPE_TIMESTAMPTZ. */
#define DB_TIMESTAMPTZ_PRECISION   DB_TIMESTAMP_PRECISION

/* This constant defines the default precision of DB_TYPE_DATETIME. */
#define DB_DATETIME_PRECISION      23

/* This constant defines the default precision of DB_TYPE_DATETIMETZ. */
#define DB_DATETIMETZ_PRECISION    DB_DATETIME_PRECISION

/* This constant defines the default scale of DB_TYPE_DATETIME. */
#define DB_DATETIME_DECIMAL_SCALE      3

#define DB_CURRENCY_DEFAULT db_get_currency_default()

#define db_set db_collection

#define db_make_utime db_make_timestamp

#define DB_MAKE_NULL(value) db_make_null(value)

#define DB_VALUE_CLONE_AS_NULL(src_value, dest_value)                   \
  do {                                                                  \
    if ((db_value_domain_init(dest_value,                               \
                              db_value_domain_type(src_value),          \
                              db_value_precision(src_value),            \
                              db_value_scale(src_value)))               \
        == NO_ERROR)                                                    \
      (void)db_value_put_null(dest_value);                              \
  } while (0)

#define DB_MAKE_INTEGER(value, num) db_make_int(value, num)

#define DB_MAKE_INT DB_MAKE_INTEGER

#define DB_MAKE_BIGINT(value, num) db_make_bigint(value, num)

#define DB_MAKE_BIGINTEGER DB_MAKE_BIGINT

#define DB_MAKE_FLOAT(value, num) db_make_float(value, num)

#define DB_MAKE_DOUBLE(value, num) db_make_double(value, num)

#define DB_MAKE_OBJECT(value, obj) db_make_object(value, obj)

#define DB_MAKE_OBJ DB_MAKE_OBJECT

#define DB_MAKE_SET(value, set) db_make_set(value, set)

#define DB_MAKE_MULTISET(value, set) db_make_multiset(value, set)

/* obsolete */
#define DB_MAKE_MULTI_SET DB_MAKE_MULTISET

#define DB_MAKE_SEQUENCE(value, set) db_make_sequence(value, set)

#define DB_MAKE_LIST DB_MAKE_SEQUENCE

/* obsolete */
#define DB_MAKE_SEQ DB_MAKE_SEQUENCE

/* new preferred interface */
#define DB_MAKE_COLLECTION(value, col) db_make_collection(value, col)

#define DB_MAKE_MIDXKEY(value, midxkey) db_make_midxkey(value, midxkey)

#define DB_MAKE_ELO(value, type, elo) db_make_elo(value, type, elo)

#define DB_MAKE_TIME(value, hour, minute, second) \
    db_make_time(value, hour, minute, second)

#define DB_MAKE_TIMETZ(value, timetz_value) \
    db_make_timetz(value, timetz_value)

#define DB_MAKE_TIMELTZ(value, time_value) \
    db_make_timeltz(value, time_value)

#define DB_MAKE_ENCODED_TIME(value, time_value) \
    db_value_put_encoded_time(value, time_value)

#define DB_MAKE_DATE(value, month, day, year) \
    db_make_date(value, month, day, year)

#define DB_MAKE_ENCODED_DATE(value, date_value) \
    db_value_put_encoded_date(value, date_value)

#define DB_MAKE_TIMESTAMP(value, timeval) \
    db_make_timestamp(value, timeval)

#define DB_MAKE_UTIME DB_MAKE_TIMESTAMP

#define DB_MAKE_TIMESTAMPTZ(value, ts_tz) \
    db_make_timestamptz(value, ts_tz)

#define DB_MAKE_TIMESTAMPLTZ(value, timeval) \
    db_make_timestampltz(value, timeval)

#define DB_MAKE_MONETARY_AMOUNT(value, amount) \
    db_make_monetary(value, DB_CURRENCY_DEFAULT, amount)

#define DB_MAKE_DATETIME(value, datetime_value) \
    db_make_datetime(value, datetime_value)

#define DB_MAKE_DATETIMETZ(value, datetimetz_value) \
    db_make_datetimetz(value, datetimetz_value)

#define DB_MAKE_DATETIMELTZ(value, datetime_value) \
    db_make_datetimeltz(value, datetime_value)

#define DB_MAKE_MONETARY DB_MAKE_MONETARY_AMOUNT

#define DB_MAKE_MONETARY_TYPE_AMOUNT(value, type, amount) \
    db_make_monetary(value, type, amount)

#define DB_MAKE_POINTER(value, ptr) db_make_pointer(value, ptr)

#define DB_MAKE_ERROR(value, errcode) db_make_error(value, errcode)

#define DB_MAKE_METHOD_ERROR(value, errcode, errmsg) \
           db_make_method_error(value, errcode, errmsg)

#define DB_MAKE_SMALLINT(value, num) db_make_short(value, num)

#define DB_MAKE_SHORT DB_MAKE_SMALLINT

#define DB_MAKE_NUMERIC(value, num, precision, scale) \
        db_make_numeric(value, num, precision, scale)

#define DB_MAKE_BIT(value, bit_length, bit_str, bit_str_bit_size) \
        db_make_bit(value, bit_length, bit_str, bit_str_bit_size)

#define DB_MAKE_VARBIT(value, max_bit_length, bit_str, bit_str_bit_size)\
        db_make_varbit(value, max_bit_length, bit_str, bit_str_bit_size)

#define DB_MAKE_CHAR(value, char_length, str, char_str_byte_size, \
		     codeset, collation) \
        db_make_char(value, char_length, str, char_str_byte_size, \
		     codeset, collation)

#define DB_MAKE_VARCHAR(value, max_char_length, str, char_str_byte_size, \
		        codeset, collation) \
        db_make_varchar(value, max_char_length, str, char_str_byte_size, \
			codeset, collation)

#define DB_MAKE_STRING(value, str) db_make_string(value, str)

#define DB_MAKE_NCHAR(value, nchar_length, str, nchar_str_byte_size, \
		      codeset, collation) \
        db_make_nchar(value, nchar_length, str, nchar_str_byte_size, \
		      codeset, collation)

#define DB_MAKE_VARNCHAR(value, max_nchar_length, str, nchar_str_byte_size, \
			 codeset, collation)\
        db_make_varnchar(value, max_nchar_length, str, nchar_str_byte_size, \
			 codeset, collation)

#define DB_MAKE_ENUMERATION(value, index, str, size, codeset, collation) \
	db_make_enumeration(value, index, str, size, codeset, collation)

#define DB_MAKE_RESULTSET(value, handle) db_make_resultset(value, handle)

#define db_get_collection db_get_set
#define db_get_utime db_get_timestamp

#define DB_IS_NULL(value)               db_value_is_null(value)

#define DB_VALUE_DOMAIN_TYPE(value)     db_value_domain_type(value)

#define DB_VALUE_TYPE(value)            db_value_type(value)

#define DB_VALUE_PRECISION(value)       db_value_precision(value)

#define DB_VALUE_SCALE(value)           db_value_scale(value)

#define DB_GET_INTEGER(value)           db_get_int(value)

#define DB_GET_INT                      DB_GET_INTEGER

#define DB_GET_BIGINT(value)            db_get_bigint(value)

#define DB_GET_BIGINTEGER               DB_GET_BIGINT

#define DB_GET_FLOAT(value)             db_get_float(value)

#define DB_GET_DOUBLE(value)            db_get_double(value)

#define DB_GET_STRING(value)            db_get_string(value)

#define DB_GET_OBJECT(value)            db_get_object(value)

#define DB_GET_OBJ DB_GET_OBJECT

#define DB_GET_SET(value)               db_get_set(value)

#define DB_GET_MULTISET(value)          db_get_set(value)

/* obsolete */
#define DB_GET_MULTI_SET DB_GET_MULTISET

#define DB_GET_LIST(value)              db_get_set(value)

#define DB_GET_SEQUENCE DB_GET_LIST

/* obsolete */
#define DB_GET_SEQ DB_GET_SEQUENCE

/* new preferred interface */
#define DB_GET_COLLECTION(value)        db_get_set(value)

#define DB_GET_MIDXKEY(value)           db_get_midxkey(value)

#define DB_GET_ELO(value)               db_get_elo(value)

#define DB_GET_TIME(value)              db_get_time(value)

#define DB_GET_TIMETZ(value)		db_get_timetz(value)

#define DB_GET_DATE(value)              db_get_date(value)

#define DB_GET_TIMESTAMP(value)         db_get_timestamp(value)
#define DB_GET_UTIME DB_GET_TIMESTAMP

#define DB_GET_TIMESTAMPTZ(value)	db_get_timestamptz(value)

#define DB_GET_DATETIME(value)          db_get_datetime(value)

#define DB_GET_DATETIMETZ(value)	db_get_datetimetz(value)

#define DB_GET_MONETARY(value)          db_get_monetary(value)

#define DB_GET_POINTER(value)           db_get_pointer(value)

#define DB_GET_ERROR(value)             db_get_error(value)

#define DB_GET_SHORT(value)             db_get_short(value)

#define DB_GET_SMALLINT(value)          db_get_short(value)

#define DB_GET_NUMERIC(value)           db_get_numeric(value)

#define DB_GET_BIT(value, length)       db_get_bit(value, length)

#define DB_GET_CHAR(value, length)      db_get_char(value, length)

#define DB_GET_NCHAR(value, length)     db_get_nchar(value, length)

#define DB_GET_STRING_SIZE(value)       db_get_string_size(value)

#define DB_GET_METHOD_ERROR_MSG()       db_get_method_error_msg()

#define DB_GET_RESULTSET(value)         db_get_resultset(value)

#define DB_GET_STRING_LENGTH(value) db_get_string_length(value)

#define DB_GET_STRING_CODESET(value) db_get_string_codeset(value)

#define DB_GET_STRING_COLLATION(value) db_get_string_collation(value)

#define DB_GET_ENUM_CODESET(value) db_get_enum_codeset(value)

#define DB_GET_ENUM_COLLATION(value) db_get_enum_collation(value)

#define DB_INT16_MIN   (-(DB_INT16_MAX)-1)
#define DB_INT16_MAX   0x7FFF
#define DB_UINT16_MAX  0xFFFFU
#define DB_INT32_MIN   (-(DB_INT32_MAX)-1)
#define DB_INT32_MAX   0x7FFFFFFF
#define DB_UINT32_MIN  0
#define DB_UINT32_MAX  0xFFFFFFFFU
#if (__WORDSIZE == 64) || defined(_WIN64)
#define DB_BIGINT_MAX  9223372036854775807L
#define DB_BIGINT_MIN  (-DB_BIGINT_MAX - 1L)
#else /* (__WORDSIZE == 64) || defined(_WIN64) */
#define DB_BIGINT_MAX  9223372036854775807LL
#define DB_BIGINT_MIN  (-DB_BIGINT_MAX - 1LL)
#endif /* (__WORDSIZE == 64) || defined(_WIN64) */
#define DB_ENUM_ELEMENTS_MAX  512
/* special ENUM index for PT_TO_ENUMERATION_VALUE function */
#define DB_ENUM_OVERFLOW_VAL  0xFFFF

/* DB_DATE_MIN and DB_DATE_MAX are calculated by julian_encode function
   with arguments (1,1,1) and (12,31,9999) respectively. */
#define DB_DATE_ZERO       DB_UINT32_MIN	/* 0 means zero date */
#define DB_DATE_MIN        1721424
#define DB_DATE_MAX        5373484

#define DB_TIME_MIN        DB_UINT32_MIN
#define DB_TIME_MAX        DB_UINT32_MAX

#define DB_UTIME_ZERO      DB_DATE_ZERO	/* 0 means zero date */
#define DB_UTIME_MIN       (DB_UTIME_ZERO + 1)
#define DB_UTIME_MAX       DB_UINT32_MAX

#define DB_IS_DATETIME_DEFAULT_EXPR(v) ((v) == DB_DEFAULT_SYSDATE || \
    (v) == DB_DEFAULT_CURRENTTIME || (v) == DB_DEFAULT_CURRENTDATE || \
    (v) == DB_DEFAULT_SYSDATETIME || (v) == DB_DEFAULT_SYSTIMESTAMP || \
    (v) == DB_DEFAULT_UNIX_TIMESTAMP || (v) == DB_DEFAULT_CURRENTDATETIME || \
    (v) == DB_DEFAULT_CURRENTTIMESTAMP || (v) == DB_DEFAULT_SYSTIME)

/* This defines the basic type identifier constants.  These are used in
   the domain specifications of attributes and method arguments and
   as value type tags in the DB_VALUE structures. */
typedef enum
{
  DB_TYPE_FIRST = 0,		/* first for iteration */
  DB_TYPE_UNKNOWN = 0,
  DB_TYPE_NULL = 0,
  DB_TYPE_INTEGER = 1,
  DB_TYPE_FLOAT = 2,
  DB_TYPE_DOUBLE = 3,
  DB_TYPE_STRING = 4,
  DB_TYPE_OBJECT = 5,
  DB_TYPE_SET = 6,
  DB_TYPE_MULTISET = 7,
  DB_TYPE_SEQUENCE = 8,
  DB_TYPE_ELO = 9,
  DB_TYPE_TIME = 10,
  DB_TYPE_TIMESTAMP = 11,
  DB_TYPE_DATE = 12,
  DB_TYPE_MONETARY = 13,
  DB_TYPE_VARIABLE = 14,	/* internal use only */
  DB_TYPE_SUB = 15,		/* internal use only */
  DB_TYPE_POINTER = 16,		/* method arguments only */
  DB_TYPE_ERROR = 17,		/* method arguments only */
  DB_TYPE_SHORT = 18,
  DB_TYPE_VOBJ = 19,		/* internal use only */
  DB_TYPE_OID = 20,		/* internal use only */
  DB_TYPE_DB_VALUE = 21,	/* special for esql */
  DB_TYPE_NUMERIC = 22,		/* SQL NUMERIC(p,s) values */
  DB_TYPE_BIT = 23,		/* SQL BIT(n) values */
  DB_TYPE_VARBIT = 24,		/* SQL BIT(n) VARYING values */
  DB_TYPE_CHAR = 25,		/* SQL CHAR(n) values */
  DB_TYPE_NCHAR = 26,		/* SQL NATIONAL CHAR(n) values */
  DB_TYPE_VARNCHAR = 27,	/* SQL NATIONAL CHAR(n) VARYING values */
  DB_TYPE_RESULTSET = 28,	/* internal use only */
  DB_TYPE_MIDXKEY = 29,		/* internal use only */
  DB_TYPE_TABLE = 30,		/* internal use only */
  DB_TYPE_BIGINT = 31,
  DB_TYPE_DATETIME = 32,
  DB_TYPE_BLOB = 33,
  DB_TYPE_CLOB = 34,
  DB_TYPE_ENUMERATION = 35,
  DB_TYPE_TIMESTAMPTZ = 36,
  DB_TYPE_TIMESTAMPLTZ = 37,
  DB_TYPE_DATETIMETZ = 38,
  DB_TYPE_DATETIMELTZ = 39,
  /* Disabled types */
  DB_TYPE_TIMETZ = 40,		/* internal use only - RESERVED */
  DB_TYPE_TIMELTZ = 41,		/* internal use only - RESERVED */
  /* end of disabled types */
  DB_TYPE_LIST = DB_TYPE_SEQUENCE,
  DB_TYPE_SMALLINT = DB_TYPE_SHORT,	/* SQL SMALLINT */
  DB_TYPE_VARCHAR = DB_TYPE_STRING,	/* SQL CHAR(n) VARYING values */
  DB_TYPE_UTIME = DB_TYPE_TIMESTAMP,	/* SQL TIMESTAMP */

  DB_TYPE_LAST = DB_TYPE_DATETIMELTZ
} DB_TYPE;

/* Domain information stored in DB_VALUE structures. */
typedef union db_domain_info DB_DOMAIN_INFO;
union db_domain_info
{
  struct general_info
  {
    unsigned char is_null;
    unsigned char type;
  } general_info;
  struct numeric_info
  {
    unsigned char is_null;
    unsigned char type;
    unsigned char precision;
    unsigned char scale;
  } numeric_info;
  struct char_info
  {
    unsigned char is_null;
    unsigned char type;
    int length;
    int collation_id;
  } char_info;
};

/* types used for the representation of bigint values. */
typedef int64_t DB_BIGINT;

/* Structure used for the representation of time values. */
typedef unsigned int DB_TIME;

typedef unsigned int TZ_ID;
typedef struct db_timetz DB_TIMETZ;
struct db_timetz
{
  DB_TIME time;
  TZ_ID tz_id;			/* zone id */
};

/* Structure used for the representation of universal times.
   These are compatible with the Unix time_t definition. */
typedef unsigned int DB_TIMESTAMP;

typedef DB_TIMESTAMP DB_UTIME;

typedef struct db_timestamptz DB_TIMESTAMPTZ;
struct db_timestamptz
{
  DB_TIMESTAMP timestamp;	/* Unix timestamp */
  TZ_ID tz_id;			/* zone id */
};

/* Structure used for the representation of date values. */
typedef unsigned int DB_DATE;

typedef struct db_datetime DB_DATETIME;
struct db_datetime
{
  unsigned int date;		/* date */
  unsigned int time;		/* time */
};

typedef struct db_datetimetz DB_DATETIMETZ;
struct db_datetimetz
{
  DB_DATETIME datetime;
  TZ_ID tz_id;			/* zone id */
};

typedef enum tz_region_type TZ_REGION_TYPE;
enum tz_region_type
{
  TZ_REGION_OFFSET = 0,
  TZ_REGION_ZONE = 1
};

typedef struct tz_region TZ_REGION;
struct tz_region
{
  TZ_REGION_TYPE type;		/* 0 : offset ; 1 : zone */
  union
  {
    int offset;			/* in seconds */
    unsigned int zone_id;	/* geographical zone id */
  };
};

/* Structure used for the representation of numeric values. */
typedef struct db_numeric DB_NUMERIC;
struct db_numeric
{
  union
  {
    unsigned char *digits;
    unsigned char buf[DB_NUMERIC_BUF_SIZE];
  } d;
};

/* Structure used for the representation of monetary amounts. */
typedef enum
{
  DB_CURRENCY_DOLLAR,
  DB_CURRENCY_YEN,
  DB_CURRENCY_BRITISH_POUND,
  DB_CURRENCY_WON,
  DB_CURRENCY_TL,
  DB_CURRENCY_CAMBODIAN_RIEL,
  DB_CURRENCY_CHINESE_RENMINBI,
  DB_CURRENCY_INDIAN_RUPEE,
  DB_CURRENCY_RUSSIAN_RUBLE,
  DB_CURRENCY_AUSTRALIAN_DOLLAR,
  DB_CURRENCY_CANADIAN_DOLLAR,
  DB_CURRENCY_BRASILIAN_REAL,
  DB_CURRENCY_ROMANIAN_LEU,
  DB_CURRENCY_EURO,
  DB_CURRENCY_SWISS_FRANC,
  DB_CURRENCY_DANISH_KRONE,
  DB_CURRENCY_NORWEGIAN_KRONE,
  DB_CURRENCY_BULGARIAN_LEV,
  DB_CURRENCY_VIETNAMESE_DONG,
  DB_CURRENCY_CZECH_KORUNA,
  DB_CURRENCY_POLISH_ZLOTY,
  DB_CURRENCY_SWEDISH_KRONA,
  DB_CURRENCY_CROATIAN_KUNA,
  DB_CURRENCY_SERBIAN_DINAR,
  DB_CURRENCY_NULL
} DB_CURRENCY;

typedef struct db_monetary DB_MONETARY;
struct db_monetary
{
  double amount;
  DB_CURRENCY type;
};

/* Definition for the collection descriptor structure. The structures for
 * the collection descriptors and the sequence descriptors are identical
 * internally but not all db_collection functions can be used with sequences
 * and no db_seq functions can be used with sets. It is advisable to
 * recognize the type of set being used, type it appropriately and only
 * call those db_ functions defined for that type.
 */
typedef struct db_collection DB_COLLECTION;
typedef DB_COLLECTION DB_MULTISET;
typedef DB_COLLECTION DB_SEQ;
typedef DB_COLLECTION DB_SET;


typedef struct db_midxkey DB_MIDXKEY;
struct db_midxkey
{
  int size;			/* size of buf */
  int ncolumns;			/* # of elements */
  DB_DOMAIN *domain;		/* MIDXKEY domain */
  char *buf;			/* key structure */
};


/*
 * DB_ELO
 * This is the run-time state structure for an ELO. The ELO is part of
 * the implementation of large object type and not intended to be used
 * directly by the API.
 */

typedef struct vpid VPID;	/* REAL PAGE IDENTIFIER */
struct vpid
{
  int pageid;			/* Page identifier */
  short volid;			/* Volume identifier where the page reside */
};

typedef struct vfid VFID;	/* REAL FILE IDENTIFIER */
struct vfid
{
  int fileid;			/* File identifier */
  short volid;			/* Volume identifier where the file reside */
};

typedef enum db_elo_type DB_ELO_TYPE;
typedef struct db_elo DB_ELO;

enum db_elo_type
{
  ELO_NULL,
  ELO_FBO
};

struct db_elo
{
  int64_t size;
  char *locator;
  char *meta_data;
  DB_ELO_TYPE type;
  int es_type;
};

/* This is the memory representation of an internal object
 * identifier.  It is in the API only for a few functions that
 * are not intended for general use.
 * An object identifier is NOT a fixed identifier; it cannot be used
 * reliably as an object identifier across database sessions or even
 * across transaction boundaries.  API programs are not allowed
 * to make assumptions about the contents of this structure.
 */
typedef struct db_identifier DB_IDENTIFIER;
struct db_identifier
{
  int pageid;
  short slotid;
  short volid;
};

typedef DB_IDENTIFIER OID;

/* Structure used for the representation of char, nchar and bit values. */
typedef struct db_large_string DB_LARGE_STRING;

/* db_char.sm was formerly db_char.small.  small is an (undocumented)
 * reserved word on NT. */

typedef union db_char DB_CHAR;
union db_char
{
  struct
  {
    unsigned char style;
    unsigned char codeset;
    unsigned char is_max_string;
    unsigned char compressed_need_clear;
  } info;
  struct
  {
    unsigned char style;
    unsigned char codeset;
    unsigned char is_max_string;
    unsigned char compressed_need_clear;
    unsigned char size;
    char buf[DB_SMALL_CHAR_BUF_SIZE];
  } sm;
  struct
  {
    unsigned char style;
    unsigned char codeset;
    unsigned char is_max_string;
    unsigned char compressed_need_clear;
    int size;
    char *buf;
    int compressed_size;
    char *compressed_buf;
  } medium;
  struct
  {
    unsigned char style;
    unsigned char codeset;
    unsigned char is_max_string;
    unsigned char compressed_need_clear;
    DB_LARGE_STRING *str;
  } large;
};

typedef DB_CHAR DB_NCHAR;
typedef DB_CHAR DB_BIT;

typedef int DB_RESULTSET;

/* Structure for an ENUMERATION element */
typedef struct db_enum_element DB_ENUM_ELEMENT;
struct db_enum_element
{
  unsigned short short_val;	/* element index */
  DB_CHAR str_val;		/* element string */
};

/* Structure for an ENUMERATION */
typedef struct db_enumeration DB_ENUMERATION;
struct db_enumeration
{
  DB_ENUM_ELEMENT *elements;	/* array of enumeration elements */
  int collation_id;		/* collation */
  unsigned short count;		/* count of enumeration elements */
};

/* A union of all of the possible basic type values.  This is used in the
 * definition of the DB_VALUE which is the fundamental structure used
 * in passing data in and out of the db_ function layer.
 */

typedef union db_data DB_DATA;
union db_data
{
  int i;
  short sh;
  DB_BIGINT bigint;
  float f;
  double d;
  void *p;
  DB_OBJECT *op;
  DB_TIME time;
  DB_TIMETZ timetz;
  DB_DATE date;
  DB_TIMESTAMP utime;
  DB_TIMESTAMPTZ timestamptz;
  DB_DATETIME datetime;
  DB_DATETIMETZ datetimetz;
  DB_MONETARY money;
  DB_COLLECTION *set;
  DB_COLLECTION *collect;
  DB_MIDXKEY midxkey;
  DB_ELO elo;
  int error;
  DB_IDENTIFIER oid;
  DB_NUMERIC num;
  DB_CHAR ch;
  DB_RESULTSET rset;
  DB_ENUM_ELEMENT enumeration;
};

/* This is the primary structure used for passing values in and out of
 * the db_ function layer. Values are always tagged with a datatype
 * so that they can be identified and type checking can be performed.
 */

typedef struct db_value DB_VALUE;
struct db_value
{
  DB_DOMAIN_INFO domain;
  DB_DATA data;
  bool need_clear;
};

/* This is used to chain DB_VALUEs into a list. */
typedef struct db_value_list DB_VALUE_LIST;
struct db_value_list
{
  struct db_value_list *next;
  DB_VALUE val;
};

/* This is used to chain DB_VALUEs into a list.  It is used as an argument
   to db_send_arglist. */
typedef struct db_value_array DB_VALUE_ARRAY;
struct db_value_array
{
  int size;
  DB_VALUE *vals;
};

/* This is used to gather stats about the workspace.
 * It contains the number of object descriptors used and
 * total number of object descriptors allocated
 */
typedef struct db_workspace_stats DB_WORKSPACE_STATS;
struct db_workspace_stats
{
  int obj_desc_used;		/* number of object descriptors used */
  int obj_desc_total;		/* total # of object descriptors allocated */
};

/* This defines the C language type identifier constants.
 * These are used to describe the types of values used for setting
 * DB_VALUE contents or used to get DB_VALUE contents into.
 */
typedef enum
{
  DB_TYPE_C_DEFAULT = 0,
  DB_TYPE_C_FIRST = 100,	/* first for iteration */
  DB_TYPE_C_INT,
  DB_TYPE_C_SHORT,
  DB_TYPE_C_LONG,
  DB_TYPE_C_FLOAT,
  DB_TYPE_C_DOUBLE,
  DB_TYPE_C_CHAR,
  DB_TYPE_C_VARCHAR,
  DB_TYPE_C_NCHAR,
  DB_TYPE_C_VARNCHAR,
  DB_TYPE_C_BIT,
  DB_TYPE_C_VARBIT,
  DB_TYPE_C_OBJECT,
  DB_TYPE_C_SET,
  DB_TYPE_C_ELO,
  DB_TYPE_C_TIME,
  DB_TYPE_C_DATE,
  DB_TYPE_C_TIMESTAMP,
  DB_TYPE_C_MONETARY,
  DB_TYPE_C_NUMERIC,
  DB_TYPE_C_POINTER,
  DB_TYPE_C_ERROR,
  DB_TYPE_C_IDENTIFIER,
  DB_TYPE_C_DATETIME,
  DB_TYPE_C_BIGINT,
  DB_TYPE_C_LAST,		/* last for iteration */
  DB_TYPE_C_UTIME = DB_TYPE_C_TIMESTAMP
} DB_TYPE_C;

typedef DB_BIGINT DB_C_BIGINT;
typedef int DB_C_INT;
typedef short DB_C_SHORT;
typedef long DB_C_LONG;
typedef float DB_C_FLOAT;
typedef double DB_C_DOUBLE;
typedef char *DB_C_CHAR;
typedef char *DB_C_NCHAR;
typedef char *DB_C_BIT;
typedef DB_OBJECT DB_C_OBJECT;
typedef DB_COLLECTION DB_C_SET;
typedef DB_COLLECTION DB_C_COLLECTION;
typedef DB_ELO DB_C_ELO;
typedef struct db_c_time DB_C_TIME;
struct db_c_time
{
  int hour;
  int minute;
  int second;
};

typedef struct db_c_date DB_C_DATE;
struct db_c_date
{
  int year;
  int month;
  int day;
};

/* identifiers for the default expression */
typedef enum
{
  DB_DEFAULT_NONE = 0,
  DB_DEFAULT_SYSDATE = 1,
  DB_DEFAULT_SYSDATETIME = 2,
  DB_DEFAULT_SYSTIMESTAMP = 3,
  DB_DEFAULT_UNIX_TIMESTAMP = 4,
  DB_DEFAULT_USER = 5,
  DB_DEFAULT_CURR_USER = 6,
  DB_DEFAULT_CURRENTDATETIME = 7,
  DB_DEFAULT_CURRENTTIMESTAMP = 8,
  DB_DEFAULT_CURRENTTIME = 9,
  DB_DEFAULT_CURRENTDATE = 10,
  DB_DEFAULT_SYSTIME = 11,
  DB_DEFAULT_FORMATTED_SYSDATE = 12,
} DB_DEFAULT_EXPR_TYPE;

typedef DB_DATETIME DB_C_DATETIME;
typedef DB_DATETIMETZ DB_C_DATETIMETZ;
typedef DB_TIMESTAMP DB_C_TIMESTAMP;
typedef DB_TIMESTAMPTZ DB_C_TIMESTAMPTZ;
typedef DB_MONETARY DB_C_MONETARY;
typedef unsigned char *DB_C_NUMERIC;
typedef void *DB_C_POINTER;
typedef DB_IDENTIFIER DB_C_IDENTIFIER;

extern DB_VALUE *db_value_create (void);
extern DB_VALUE *db_value_copy (DB_VALUE * value);
extern int db_value_clone (DB_VALUE * src, DB_VALUE * dest);
extern int db_value_clear (DB_VALUE * value);
extern int db_value_free (DB_VALUE * value);
extern int db_value_clear_array (DB_VALUE_ARRAY * value_array);
extern void db_value_print (const DB_VALUE * value);
extern int db_value_coerce (const DB_VALUE * src, DB_VALUE * dest, const DB_DOMAIN * desired_domain);

extern int db_value_equal (const DB_VALUE * value1, const DB_VALUE * value2);
extern int db_value_compare (const DB_VALUE * value1, const DB_VALUE * value2);
extern int db_value_domain_init (DB_VALUE * value, DB_TYPE type, const int precision, const int scale);
extern int db_value_domain_min (DB_VALUE * value, DB_TYPE type, const int precision, const int scale, const int codeset,
				const int collation_id, const DB_ENUMERATION * enumeration);
extern int db_value_domain_max (DB_VALUE * value, DB_TYPE type, const int precision, const int scale, const int codeset,
				const int collation_id, const DB_ENUMERATION * enumeration);
extern int db_value_domain_default (DB_VALUE * value, const DB_TYPE type, const int precision, const int scale,
				    const int codeset, const int collation_id, DB_ENUMERATION * enumeration);
extern int db_value_domain_zero (DB_VALUE * value, const DB_TYPE type, const int precision, const int scale);
extern int db_string_truncate (DB_VALUE * value, const int max_precision);
extern DB_TYPE db_value_domain_type (const DB_VALUE * value);
extern DB_TYPE db_value_type (const DB_VALUE * value);
extern int db_value_precision (const DB_VALUE * value);
extern int db_value_scale (const DB_VALUE * value);
extern int db_value_put_null (DB_VALUE * value);
extern int db_value_put (DB_VALUE * value, const DB_TYPE_C c_type, void *input, const int input_length);
extern bool db_value_type_is_collection (const DB_VALUE * value);
extern bool db_value_type_is_numeric (const DB_VALUE * value);
extern bool db_value_type_is_bit (const DB_VALUE * value);
extern bool db_value_type_is_char (const DB_VALUE * value);
extern bool db_value_type_is_internal (const DB_VALUE * value);
extern bool db_value_is_null (const DB_VALUE * value);
extern int db_value_get (DB_VALUE * value, const DB_TYPE_C type, void *buf, const int buflen, int *transferlen,
			 int *outputlen);
extern int db_value_size (const DB_VALUE * value, DB_TYPE_C type, int *size);
extern int db_value_char_size (const DB_VALUE * value, int *size);
extern DB_CURRENCY db_value_get_monetary_currency (const DB_VALUE * value);
extern double db_value_get_monetary_amount_as_double (const DB_VALUE * value);
extern int db_value_put_monetary_currency (DB_VALUE * value, const DB_CURRENCY type);
extern int db_value_put_monetary_amount_as_double (DB_VALUE * value, const double amount);

/*
 * DB_MAKE_ value constructors.
 * These macros are provided to make the construction of DB_VALUE
 * structures easier.  They will fill in the fields from the supplied
 * arguments. It is not necessary to use these macros but is usually more
 * convenient.
 */
extern int db_make_null (DB_VALUE * value);
extern int db_make_int (DB_VALUE * value, const int num);
extern int db_make_float (DB_VALUE * value, const DB_C_FLOAT num);
extern int db_make_double (DB_VALUE * value, const DB_C_DOUBLE num);
extern int db_make_object (DB_VALUE * value, DB_C_OBJECT * obj);
extern int db_make_set (DB_VALUE * value, DB_C_SET * set);
extern int db_make_multiset (DB_VALUE * value, DB_C_SET * set);
extern int db_make_sequence (DB_VALUE * value, DB_C_SET * set);
extern int db_make_collection (DB_VALUE * value, DB_C_SET * set);
extern int db_make_midxkey (DB_VALUE * value, DB_MIDXKEY * midxkey);
extern int db_make_elo (DB_VALUE * value, DB_TYPE type, const DB_ELO * elo);
extern int db_make_time (DB_VALUE * value, const int hour, const int minute, const int second);
extern int db_make_timetz (DB_VALUE * value, const DB_TIMETZ * timetz_value);
extern int db_make_timeltz (DB_VALUE * value, const DB_TIME * time_value);
extern int db_value_put_encoded_time (DB_VALUE * value, const DB_TIME * time_value);
extern int db_make_date (DB_VALUE * value, const int month, const int day, const int year);
extern int db_value_put_encoded_date (DB_VALUE * value, const DB_DATE * date_value);
extern int db_make_timestamp (DB_VALUE * value, const DB_C_TIMESTAMP timeval);
extern int db_make_timestampltz (DB_VALUE * value, const DB_C_TIMESTAMP ts_val);
extern int db_make_timestamptz (DB_VALUE * value, const DB_C_TIMESTAMPTZ * ts_tz_val);
extern int db_make_datetime (DB_VALUE * value, const DB_DATETIME * datetime);
extern int db_make_datetimeltz (DB_VALUE * value, const DB_DATETIME * datetime);
extern int db_make_datetimetz (DB_VALUE * value, const DB_DATETIMETZ * datetimetz);
extern int db_make_monetary (DB_VALUE * value, const DB_CURRENCY type, const double amount);
extern int db_make_pointer (DB_VALUE * value, DB_C_POINTER ptr);
extern int db_make_error (DB_VALUE * value, const int errcode);
extern int db_make_method_error (DB_VALUE * value, const int errcode, const char *errmsg);
extern int db_make_short (DB_VALUE * value, const DB_C_SHORT num);
extern int db_make_bigint (DB_VALUE * value, const DB_BIGINT num);
extern int db_make_string (DB_VALUE * value, const char *str);
extern int db_make_string_copy (DB_VALUE * value, const char *str);
extern int db_make_numeric (DB_VALUE * value, const DB_C_NUMERIC num, const int precision, const int scale);
extern int db_value_put_numeric (DB_VALUE * value, DB_C_NUMERIC num);
extern int db_make_bit (DB_VALUE * value, const int bit_length, const DB_C_BIT bit_str, const int bit_str_bit_size);
extern int db_value_put_bit (DB_VALUE * value, DB_C_BIT str, int size);
extern int db_make_varbit (DB_VALUE * value, const int max_bit_length, const DB_C_BIT bit_str,
			   const int bit_str_bit_size);
extern int db_value_put_varbit (DB_VALUE * value, DB_C_BIT str, int size);
extern int db_make_char (DB_VALUE * value, const int char_length, const DB_C_CHAR str, const int char_str_byte_size,
			 const int codeset, const int collation_id);
extern int db_value_put_char (DB_VALUE * value, DB_C_CHAR str, int size);
extern int db_make_varchar (DB_VALUE * value, const int max_char_length, const DB_C_CHAR str,
			    const int char_str_byte_size, const int codeset, const int collation_id);
extern int db_value_put_varchar (DB_VALUE * value, DB_C_CHAR str, int size);
extern int db_make_nchar (DB_VALUE * value, const int nchar_length, const DB_C_NCHAR str, const int nchar_str_byte_size,
			  const int codeset, const int collation_id);
extern int db_value_put_nchar (DB_VALUE * value, DB_C_NCHAR str, int size);
extern int db_make_varnchar (DB_VALUE * value, const int max_nchar_length, const DB_C_NCHAR str,
			     const int nchar_str_byte_size, const int codeset, const int collation_id);
extern int db_value_put_varnchar (DB_VALUE * value, DB_C_NCHAR str, int size);

extern int db_make_enumeration (DB_VALUE * value, unsigned short index, DB_C_CHAR str, int size, unsigned char codeset,
				const int collation_id);

extern DB_CURRENCY db_get_currency_default (void);

extern int db_make_resultset (DB_VALUE * value, const DB_RESULTSET handle);

/*
 * DB_GET_ accessor macros.
 * These macros can be used to extract a particular value from a
 * DB_VALUE structure. No type checking is done so you need to make sure
 * that the type is correct.
 */
extern int db_get_int (const DB_VALUE * value);
extern DB_C_SHORT db_get_short (const DB_VALUE * value);
extern DB_BIGINT db_get_bigint (const DB_VALUE * value);
extern DB_C_CHAR db_get_string (const DB_VALUE * value);
extern DB_C_FLOAT db_get_float (const DB_VALUE * value);
extern DB_C_DOUBLE db_get_double (const DB_VALUE * value);
extern DB_OBJECT *db_get_object (const DB_VALUE * value);
extern DB_COLLECTION *db_get_set (const DB_VALUE * value);
extern DB_MIDXKEY *db_get_midxkey (const DB_VALUE * value);
extern DB_C_POINTER db_get_pointer (const DB_VALUE * value);
extern DB_TIME *db_get_time (const DB_VALUE * value);
extern DB_TIMETZ *db_get_timetz (const DB_VALUE * value);
extern DB_TIMESTAMP *db_get_timestamp (const DB_VALUE * value);
extern DB_TIMESTAMPTZ *db_get_timestamptz (const DB_VALUE * value);
extern DB_DATETIME *db_get_datetime (const DB_VALUE * value);
extern DB_DATETIMETZ *db_get_datetimetz (const DB_VALUE * value);
extern DB_DATE *db_get_date (const DB_VALUE * value);
extern DB_MONETARY *db_get_monetary (const DB_VALUE * value);
extern int db_get_error (const DB_VALUE * value);
extern DB_ELO *db_get_elo (const DB_VALUE * value);
extern DB_C_NUMERIC db_get_numeric (const DB_VALUE * value);
extern DB_C_BIT db_get_bit (const DB_VALUE * value, int *length);
extern DB_C_CHAR db_get_char (const DB_VALUE * value, int *length);
extern DB_C_NCHAR db_get_nchar (const DB_VALUE * value, int *length);
extern int db_get_string_size (const DB_VALUE * value);
extern DB_C_SHORT db_get_enum_short (const DB_VALUE * value);
extern DB_C_CHAR db_get_enum_string (const DB_VALUE * value);
extern int db_get_enum_string_size (const DB_VALUE * value);
extern DB_C_CHAR db_get_method_error_msg (void);

extern DB_RESULTSET db_get_resultset (const DB_VALUE * value);

extern int db_string_put_cs_and_collation (DB_VALUE * value, const int codeset, const int collation_id);
extern int db_enum_put_cs_and_collation (DB_VALUE * value, const int codeset, const int collation_id);
extern int db_get_string_codeset (const DB_VALUE * value);
extern int db_get_string_collation (const DB_VALUE * value);
extern int valcnv_convert_value_to_string (DB_VALUE * value);

extern int db_get_enum_codeset (const DB_VALUE * value);
extern int db_get_enum_collation (const DB_VALUE * value);


extern void db_date_decode (const DB_DATE * date, int *monthp, int *dayp, int *yearp);
extern int db_date_weekday (DB_DATE * date);
extern int db_date_to_string (char *buf, int bufsize, DB_DATE * date);
extern bool db_string_check_explicit_date (const char *str, int str_len);
extern int db_string_to_date (const char *buf, DB_DATE * date);
extern int db_string_to_date_ex (const char *buf, int str_len, DB_DATE * date);
extern int db_date_parse_date (char const *str, int str_len, DB_DATE * date);

/* DB_DATETIME functions */
extern int db_datetime_encode (DB_DATETIME * datetime, int month, int day, int year, int hour, int minute, int second,
			       int millisecond);
extern int db_datetime_decode (const DB_DATETIME * datetime, int *month, int *day, int *year, int *hour, int *minute,
			       int *second, int *millisecond);
extern int db_datetime_to_string (char *buf, int bufsize, DB_DATETIME * datetime);
extern int db_datetimetz_to_string (char *buf, int bufsize, DB_DATETIME * dt, const TZ_ID * tz_id);
extern int db_datetimeltz_to_string (char *buf, int bufsize, DB_DATETIME * dt);
extern int db_datetime_to_string2 (char *buf, int bufsize, DB_DATETIME * datetime);
extern int db_string_to_datetime (const char *str, DB_DATETIME * datetime);
extern int db_string_to_datetime_ex (const char *str, int str_len, DB_DATETIME * datetime);
extern int db_string_to_datetimetz (const char *str, DB_DATETIMETZ * dt_tz, bool * has_zone);
extern int db_string_to_datetimetz_ex (const char *str, int str_len, DB_DATETIMETZ * dt_tz, bool * has_zone);
extern int db_string_to_datetimeltz (const char *str, DB_DATETIME * datetime);
extern int db_string_to_datetimeltz_ex (const char *str, int str_len, DB_DATETIME * datetime);
extern int db_date_parse_datetime_parts (char const *str, int str_len, DB_DATETIME * date, bool * is_explicit_time,
					 bool * has_explicit_msec, bool * fits_as_timestamp, char const **endp);
extern int db_date_parse_datetime (char const *str, int str_len, DB_DATETIME * datetime);
extern int db_subtract_int_from_datetime (DB_DATETIME * dt1, DB_BIGINT i2, DB_DATETIME * result_datetime);
extern int db_add_int_to_datetime (DB_DATETIME * datetime, DB_BIGINT i2, DB_DATETIME * result_datetime);
/* DB_TIMESTAMP functions */
extern int db_timestamp_encode (DB_TIMESTAMP * utime, DB_DATE * date, DB_TIME * timeval);
extern int db_timestamp_encode_ses (const DB_DATE * date, const DB_TIME * timeval, DB_TIMESTAMP * utime,
				    TZ_ID * dest_tz_id);
extern int db_timestamp_encode_utc (const DB_DATE * date, const DB_TIME * timeval, DB_TIMESTAMP * utime);
extern int db_timestamp_decode_ses (const DB_TIMESTAMP * utime, DB_DATE * date, DB_TIME * timeval);
extern void db_timestamp_decode_utc (const DB_TIMESTAMP * utime, DB_DATE * date, DB_TIME * timeval);
extern int db_timestamp_decode_w_reg (const DB_TIMESTAMP * utime, const TZ_REGION * tz_region, DB_DATE * date,
				      DB_TIME * timeval);
extern int db_timestamp_decode_w_tz_id (const DB_TIMESTAMP * utime, const TZ_ID * tz_id, DB_DATE * date,
					DB_TIME * timeval);
extern int db_timestamp_to_string (char *buf, int bufsize, DB_TIMESTAMP * utime);
extern int db_timestamptz_to_string (char *buf, int bufsize, DB_TIMESTAMP * utime, const TZ_ID * tz_id);
extern int db_timestampltz_to_string (char *buf, int bufsize, DB_TIMESTAMP * utime);
extern int db_string_to_timestamp (const char *buf, DB_TIMESTAMP * utime);
extern int db_string_to_timestamp_ex (const char *buf, int buf_len, DB_TIMESTAMP * utime);
extern int db_date_parse_timestamp (char const *str, int str_len, DB_TIMESTAMP * utime);
extern int db_string_to_timestamptz (const char *str, DB_TIMESTAMPTZ * ts_tz, bool * has_zone);
extern int db_string_to_timestamptz_ex (const char *str, int str_len, DB_TIMESTAMPTZ * ts_tz, bool * has_zone);
extern int db_string_to_timestampltz (const char *str, DB_TIMESTAMP * ts);
extern int db_string_to_timestampltz_ex (const char *str, int str_len, DB_TIMESTAMP * ts);

/* DB_TIME functions */
extern int db_time_encode (DB_TIME * timeval, int hour, int minute, int second);
extern void db_time_decode (DB_TIME * timeval, int *hourp, int *minutep, int *secondp);
extern int db_time_to_string (char *buf, int bufsize, DB_TIME * dbtime);
extern int db_timetz_to_string (char *buf, int bufsize, DB_TIME * dbtime, const TZ_ID * tz_id);
extern int db_timeltz_to_string (char *buf, int bufsize, DB_TIME * time);
extern bool db_string_check_explicit_time (const char *str, int str_len);
extern int db_string_to_time (const char *buf, DB_TIME * dbtime);
extern int db_string_to_time_ex (const char *buf, int buf_len, DB_TIME * dbtime);
extern int db_string_to_timetz (const char *buf, DB_TIMETZ * time_tz, bool * has_zone);
extern int db_string_to_timetz_ex (const char *buf, int buf_len, DB_TIMETZ * time_tz, bool * has_zone);
extern int db_string_to_timeltz (const char *buf, DB_TIME * time);
extern int db_string_to_timeltz_ex (const char *buf, int buf_len, DB_TIME * time);
extern int db_date_parse_time (char const *str, int str_len, DB_TIME * time, int *milisec);

/* Unix-like functions */
extern time_t db_mktime (DB_DATE * date, DB_TIME * timeval);
extern int db_strftime (char *s, int smax, const char *fmt, DB_DATE * date, DB_TIME * timeval);
extern void db_localtime (time_t * epoch_time, DB_DATE * date, DB_TIME * timeval);
extern void db_localdatetime (time_t * epoch_time, DB_DATETIME * datetime);


/* generic calculation functions */
extern int julian_encode (int m, int d, int y);
extern void julian_decode (int jul, int *monthp, int *dayp, int *yearp, int *weekp);
extern int day_of_week (int jul_day);
extern bool is_leap_year (int year);
extern int db_tm_encode (struct tm *c_time_struct, DB_DATE * date, DB_TIME * timeval);
extern int db_get_day_of_year (int year, int month, int day);
extern int db_get_day_of_week (int year, int month, int day);
extern int db_get_week_of_year (int year, int month, int day, int mode);
extern int db_check_time_date_format (const char *format_s);
extern int db_add_weeks_and_days_to_date (int *day, int *month, int *year, int weeks, int day_week);

/* DB_ELO function */
extern int db_create_fbo (DB_VALUE * value, DB_TYPE type);
extern int db_elo_copy_structure (const DB_ELO * src, DB_ELO * dest);
extern void db_elo_free_structure (DB_ELO * elo);

extern int db_elo_copy (const DB_ELO * src, DB_ELO * dest);
extern int db_elo_delete (DB_ELO * elo);

extern int64_t db_elo_size (DB_ELO * elo);
extern int db_elo_read (const DB_ELO * elo, int64_t pos, void *buf, size_t count);
extern int db_elo_write (DB_ELO * elo, int64_t pos, void *buf, size_t count);

/* Unix-like functions */
extern time_t db_mktime (DB_DATE * date, DB_TIME * timeval);
extern int db_strftime (char *s, int smax, const char *fmt, DB_DATE * date, DB_TIME * timeval);
extern void db_localtime (time_t * epoch_time, DB_DATE * date, DB_TIME * timeval);

/* generic calculation functions */
extern int db_tm_encode (struct tm *c_time_struct, DB_DATE * date, DB_TIME * timeval);



typedef struct cache_time CACHE_TIME;
struct cache_time
{
  int sec;
  int usec;
};

#define CACHE_TIME_EQ(T1, T2)               \
        (((T1)->sec != 0) &&                \
         ((T1)->sec == (T2)->sec) &&        \
         ((T1)->usec == (T2)->usec))

#define CACHE_TIME_RESET(T)     \
        do {                    \
          (T)->sec = 0;         \
          (T)->usec = 0;        \
        } while (0)

#define CACHE_TIME_MAKE(CT, TV)         \
        do {                            \
          (CT)->sec = (TV)->tv_sec;     \
          (CT)->usec = (TV)->tv_usec;   \
        } while (0)

#define OR_CACHE_TIME_SIZE      (OR_INT_SIZE * 2)

#define OR_PACK_CACHE_TIME(PTR, T)                      \
        do {                                            \
          if ((CACHE_TIME *) (T) != NULL) {                                      \
            PTR = or_pack_int(PTR, (T)->sec);        \
            PTR = or_pack_int(PTR, (T)->usec);       \
          }                                             \
          else {                                        \
            PTR = or_pack_int(PTR, 0);                  \
            PTR = or_pack_int(PTR, 0);                  \
          }                                             \
        } while (0)

#define OR_UNPACK_CACHE_TIME(PTR, T)                    \
        do {                                            \
          if ((CACHE_TIME *) (T) != NULL) {                                      \
            PTR = or_unpack_int(PTR, &((T)->sec));      \
            PTR = or_unpack_int(PTR, &((T)->usec));     \
          }                                             \
        } while (0)



extern bool db_is_client_cache_reusable (DB_QUERY_RESULT * result);
extern int db_query_seek_tuple (DB_QUERY_RESULT * result, int offset, int seek_mode);
extern int db_query_get_cache_time (DB_QUERY_RESULT * result, CACHE_TIME * cache_time);


typedef enum
{
  TRAN_UNKNOWN_ISOLATION = 0x00,	/* 0 0000 */

  TRAN_READ_COMMITTED = 0x04,	/* 0 0100 */
  TRAN_REP_CLASS_COMMIT_INSTANCE = 0x04,	/* Alias of above */
  TRAN_CURSOR_STABILITY = 0x04,	/* Alias of above */

  TRAN_REPEATABLE_READ = 0x05,	/* 0 0101 */
  TRAN_REP_READ = 0x05,		/* Alias of above */
  TRAN_REP_CLASS_REP_INSTANCE = 0x05,	/* Alias of above */
  TRAN_DEGREE_2_9999_CONSISTENCY = 0x05,	/* Alias of above */

  TRAN_SERIALIZABLE = 0x06,	/* 0 0110 */
  TRAN_DEGREE_3_CONSISTENCY = 0x06,	/* Alias of above */
  TRAN_NO_PHANTOM_READ = 0x06,	/* Alias of above */

  TRAN_DEFAULT_ISOLATION = TRAN_READ_COMMITTED,
  MVCC_TRAN_DEFAULT_ISOLATION = TRAN_READ_COMMITTED,

  TRAN_MINVALUE_ISOLATION = 0x04,	/* internal use only */
  TRAN_MAXVALUE_ISOLATION = 0x06	/* internal use only */
} DB_TRAN_ISOLATION;

/* Memory reclamation functions */
extern void db_objlist_free (DB_OBJLIST * list);
extern void db_string_free (char *string);

/* Session control */
extern int db_auth_login (char *signed_data, int len);
extern int db_auth_logout (void);

extern int db_login (const char *name, const char *password);
extern int db_restart (const char *program, int print_version, const char *volume);
extern int db_restart_ex (const char *program, const char *db_name, const char *db_user, const char *db_password,
			  const char *preferred_hosts, int client_type);
extern SESSION_ID db_get_session_id (void);
extern void db_set_session_id (const SESSION_ID session_id);
extern int db_end_session (void);
extern int db_find_or_create_session (const char *db_user, const char *program_name);
extern int db_get_row_count_cache (void);
extern void db_update_row_count_cache (const int row_count);
extern int db_get_row_count (int *row_count);
extern int db_get_last_insert_id (DB_VALUE * value);
extern int db_get_variable (DB_VALUE * name, DB_VALUE * value);
extern int db_shutdown (void);
extern int db_ping_server (int client_val, int *server_val);
extern int db_disable_modification (void);
extern int db_enable_modification (void);
extern int db_commit_transaction (void);
extern int db_abort_transaction (void);
extern int db_commit_is_needed (void);
extern int db_savepoint_transaction (const char *savepoint_name);
extern int db_abort_to_savepoint (const char *savepoint_name);
extern int db_set_global_transaction_info (int gtrid, void *info, int size);
extern int db_get_global_transaction_info (int gtrid, void *buffer, int size);
extern int db_2pc_start_transaction (void);
extern int db_2pc_prepare_transaction (void);
extern int db_2pc_prepared_transactions (int gtrids[], int size);
extern int db_2pc_prepare_to_commit_transaction (int gtrid);
extern int db_2pc_attach_transaction (int gtrid);
extern void db_set_interrupt (int set);
extern int db_set_suppress_repl_on_transaction (int set);
extern int db_freepgs (const char *vlabel);
extern int db_totalpgs (const char *vlabel);
extern char *db_vol_label (int volid, char *vol_fullname);
extern void db_warnspace (const char *vlabel);
extern int db_add_volume (const char *ext_path, const char *ext_name, const char *ext_comments, const int ext_npages,
			  const DB_VOLPURPOSE ext_purpose);
extern int db_num_volumes (void);
extern void db_print_stats (void);

extern void db_preload_classes (const char *name1, ...);
extern void db_link_static_methods (DB_METHOD_LINK * methods);
extern void db_unlink_static_methods (DB_METHOD_LINK * methods);
extern void db_flush_static_methods (void);

extern const char *db_error_string (int level);
extern int db_error_code (void);
extern int db_error_init (const char *logfile);

extern int db_set_lock_timeout (int seconds);
extern int db_set_isolation (DB_TRAN_ISOLATION isolation);
extern void db_synchronize_cache (void);
extern void db_get_tran_settings (int *lock_wait, DB_TRAN_ISOLATION * tran_isolation);

/* Authorization */
extern DB_OBJECT *db_get_user (void);
extern DB_OBJECT *db_get_owner (DB_OBJECT * classobj);
extern char *db_get_user_name (void);
extern char *db_get_user_and_host_name (void);
extern DB_OBJECT *db_find_user (const char *name);
extern int db_find_user_to_drop (const char *name, DB_OBJECT ** user);
extern DB_OBJECT *db_add_user (const char *name, int *exists);
extern int db_drop_user (DB_OBJECT * user);
extern int db_add_member (DB_OBJECT * user, DB_OBJECT * member);
extern int db_drop_member (DB_OBJECT * user, DB_OBJECT * member);
extern int db_set_password (DB_OBJECT * user, const char *oldpass, const char *newpass);
extern int db_set_user_comment (DB_OBJECT * user, const char *comment);
extern int db_grant (DB_OBJECT * user, DB_OBJECT * classobj, DB_AUTH auth, int grant_option);
extern int db_revoke (DB_OBJECT * user, DB_OBJECT * classobj, DB_AUTH auth);
extern int db_check_authorization (DB_OBJECT * op, DB_AUTH auth);
extern int db_check_authorization_and_grant_option (MOP op, DB_AUTH auth);
extern int db_get_class_privilege (DB_OBJECT * op, unsigned int *auth);

/*  Serial value manipulation */
extern int db_get_serial_current_value (const char *serial_name, DB_VALUE * serial_value);
extern int db_get_serial_next_value (const char *serial_name, DB_VALUE * serial_value);
extern int db_get_serial_next_value_ex (const char *serial_name, DB_VALUE * serial_value, int num_alloc);

/* Instance manipulation */
extern DB_OBJECT *db_create (DB_OBJECT * obj);
extern DB_OBJECT *db_create_by_name (const char *name);
extern int db_get (DB_OBJECT * object, const char *attpath, DB_VALUE * value);
extern int db_put (DB_OBJECT * obj, const char *name, DB_VALUE * value);
extern int db_drop (DB_OBJECT * obj);
extern int db_get_expression (DB_OBJECT * object, const char *expression, DB_VALUE * value);
extern void db_print (DB_OBJECT * obj);
extern void db_fprint (FILE * fp, DB_OBJECT * obj);
extern DB_OBJECT *db_find_unique (DB_OBJECT * classobj, const char *attname, DB_VALUE * value);
extern DB_OBJECT *db_find_unique_write_mode (DB_OBJECT * classobj, const char *attname, DB_VALUE * value);
extern DB_OBJECT *db_find_multi_unique (DB_OBJECT * classobj, int size, char *attnames[], DB_VALUE * values[],
					DB_FETCH_MODE purpose);
extern DB_OBJECT *db_dfind_unique (DB_OBJECT * classobj, DB_ATTDESC * attdesc, DB_VALUE * value, DB_FETCH_MODE purpose);
extern DB_OBJECT *db_dfind_multi_unique (DB_OBJECT * classobj, int size, DB_ATTDESC * attdesc[], DB_VALUE * values[],
					 DB_FETCH_MODE purpose);
extern DB_OBJECT *db_find_primary_key (MOP classmop, const DB_VALUE ** values, int size, DB_FETCH_MODE purpose);

extern int db_send (DB_OBJECT * obj, const char *name, DB_VALUE * returnval, ...);
extern int db_send_arglist (DB_OBJECT * obj, const char *name, DB_VALUE * returnval, DB_VALUE_LIST * args);
extern int db_send_argarray (DB_OBJECT * obj, const char *name, DB_VALUE * returnval, DB_VALUE ** args);

/* Explicit lock & fetch functions */
extern int db_lock_read (DB_OBJECT * op);
extern int db_lock_write (DB_OBJECT * op);

extern int db_fetch_array (DB_OBJECT ** objects, DB_FETCH_MODE mode, int quit_on_error);
extern int db_fetch_list (DB_OBJLIST * objects, DB_FETCH_MODE mode, int quit_on_error);
extern int db_fetch_set (DB_COLLECTION * set, DB_FETCH_MODE mode, int quit_on_error);
extern int db_fetch_seq (DB_SEQ * set, DB_FETCH_MODE mode, int quit_on_error);
extern int db_fetch_composition (DB_OBJECT * object, DB_FETCH_MODE mode, int max_level, int quit_on_error);

/* Collection functions */
extern DB_COLLECTION *db_col_create (DB_TYPE type, int size, DB_DOMAIN * domain);
extern DB_COLLECTION *db_col_copy (DB_COLLECTION * col);
extern int db_col_filter (DB_COLLECTION * col);
extern int db_col_free (DB_COLLECTION * col);

extern int db_col_coerce (DB_COLLECTION * col, DB_DOMAIN * domain);

extern int db_col_size (DB_COLLECTION * col);
extern int db_col_cardinality (DB_COLLECTION * col);
extern DB_TYPE db_col_type (DB_COLLECTION * col);
extern DB_DOMAIN *db_col_domain (DB_COLLECTION * col);
extern int db_col_ismember (DB_COLLECTION * col, DB_VALUE * value);
extern int db_col_find (DB_COLLECTION * col, DB_VALUE * value, int starting_index, int *found_index);
extern int db_col_add (DB_COLLECTION * col, DB_VALUE * value);
extern int db_col_drop (DB_COLLECTION * col, DB_VALUE * value, int all);
extern int db_col_drop_element (DB_COLLECTION * col, int element_index);

extern int db_col_drop_nulls (DB_COLLECTION * col);

extern int db_col_get (DB_COLLECTION * col, int element_index, DB_VALUE * value);
extern int db_col_put (DB_COLLECTION * col, int element_index, DB_VALUE * value);
extern int db_col_insert (DB_COLLECTION * col, int element_index, DB_VALUE * value);

extern int db_col_print (DB_COLLECTION * col);
extern int db_col_fprint (FILE * fp, DB_COLLECTION * col);

/* Set and sequence functions.
   These are now obsolete. Please use the generic collection functions
   "db_col*" instead */
extern DB_COLLECTION *db_set_create (DB_OBJECT * classobj, const char *name);
extern DB_COLLECTION *db_set_create_basic (DB_OBJECT * classobj, const char *name);
extern DB_COLLECTION *db_set_create_multi (DB_OBJECT * classobj, const char *name);
extern DB_COLLECTION *db_seq_create (DB_OBJECT * classobj, const char *name, int size);
extern int db_set_free (DB_COLLECTION * set);
extern int db_set_filter (DB_COLLECTION * set);
extern int db_set_add (DB_COLLECTION * set, DB_VALUE * value);
extern int db_set_get (DB_COLLECTION * set, int element_index, DB_VALUE * value);
extern int db_set_drop (DB_COLLECTION * set, DB_VALUE * value);
extern int db_set_size (DB_COLLECTION * set);
extern int db_set_cardinality (DB_COLLECTION * set);
extern int db_set_ismember (DB_COLLECTION * set, DB_VALUE * value);
extern int db_set_isempty (DB_COLLECTION * set);
extern int db_set_print (DB_COLLECTION * set);
extern DB_TYPE db_set_type (DB_COLLECTION * set);
extern DB_COLLECTION *db_set_copy (DB_COLLECTION * set);
extern int db_seq_get (DB_COLLECTION * set, int element_index, DB_VALUE * value);
extern int db_seq_put (DB_COLLECTION * set, int element_index, DB_VALUE * value);
extern int db_seq_insert (DB_COLLECTION * set, int element_index, DB_VALUE * value);
extern int db_seq_drop (DB_COLLECTION * set, int element_index);
extern int db_seq_size (DB_COLLECTION * set);
extern int db_seq_cardinality (DB_COLLECTION * set);
extern int db_seq_print (DB_COLLECTION * set);
extern int db_seq_find (DB_COLLECTION * set, DB_VALUE * value, int element_index);
extern int db_seq_free (DB_SEQ * seq);
extern int db_seq_filter (DB_SEQ * seq);
extern DB_SEQ *db_seq_copy (DB_SEQ * seq);

/* Class definition */
extern DB_OBJECT *db_create_class (const char *name);
extern DB_OBJECT *db_create_vclass (const char *name);
extern int db_drop_class (DB_OBJECT * classobj);
extern int db_drop_class_ex (DB_OBJECT * classobj, bool is_cascade_constraints);
extern int db_rename_class (DB_OBJECT * classobj, const char *new_name);

extern int db_add_index (DB_OBJECT * classobj, const char *attname);
extern int db_drop_index (DB_OBJECT * classobj, const char *attname);

extern int db_add_super (DB_OBJECT * classobj, DB_OBJECT * super);
extern int db_drop_super (DB_OBJECT * classobj, DB_OBJECT * super);
extern int db_drop_super_connect (DB_OBJECT * classobj, DB_OBJECT * super);

extern int db_rename (DB_OBJECT * classobj, const char *name, int class_namespace, const char *newname);

extern int db_add_attribute (DB_OBJECT * obj, const char *name, const char *domain, DB_VALUE * default_value);
extern int db_add_shared_attribute (DB_OBJECT * obj, const char *name, const char *domain, DB_VALUE * default_value);
extern int db_add_class_attribute (DB_OBJECT * obj, const char *name, const char *domain, DB_VALUE * default_value);
extern int db_add_set_attribute_domain (DB_OBJECT * classobj, const char *name, int class_attribute,
					const char *domain);
extern int db_drop_attribute (DB_OBJECT * classobj, const char *name);
extern int db_drop_class_attribute (DB_OBJECT * classobj, const char *name);
extern int db_change_default (DB_OBJECT * classobj, const char *name, DB_VALUE * value);

extern int db_constrain_non_null (DB_OBJECT * classobj, const char *name, int class_attribute, int on_or_off);
extern int db_constrain_unique (DB_OBJECT * classobj, const char *name, int on_or_off);
extern int db_add_method (DB_OBJECT * classobj, const char *name, const char *implementation);
extern int db_add_class_method (DB_OBJECT * classobj, const char *name, const char *implementation);
extern int db_drop_method (DB_OBJECT * classobj, const char *name);
extern int db_drop_class_method (DB_OBJECT * classobj, const char *name);
extern int db_add_argument (DB_OBJECT * classobj, const char *name, int class_method, int arg_index,
			    const char *domain);
extern int db_add_set_argument_domain (DB_OBJECT * classobj, const char *name, int class_method, int arg_index,
				       const char *domain);
extern int db_change_method_implementation (DB_OBJECT * classobj, const char *name, int class_method,
					    const char *newname);
extern int db_set_loader_commands (DB_OBJECT * classobj, const char *commands);
extern int db_add_method_file (DB_OBJECT * classobj, const char *name);
extern int db_drop_method_file (DB_OBJECT * classobj, const char *name);
extern int db_drop_method_files (DB_OBJECT * classobj);

extern int db_add_resolution (DB_OBJECT * classobj, DB_OBJECT * super, const char *name, const char *alias);
extern int db_add_class_resolution (DB_OBJECT * classobj, DB_OBJECT * super, const char *name, const char *alias);
extern int db_drop_resolution (DB_OBJECT * classobj, DB_OBJECT * super, const char *name);
extern int db_drop_class_resolution (DB_OBJECT * classobj, DB_OBJECT * super, const char *name);
extern int db_add_constraint (MOP classmop, DB_CONSTRAINT_TYPE constraint_type, const char *constraint_name,
			      const char **att_names, int class_attributes);
extern int db_drop_constraint (MOP classmop, DB_CONSTRAINT_TYPE constraint_type, const char *constraint_name,
			       const char **att_names, int class_attributes);

/* Browsing functions */
extern char *db_get_database_name (void);
extern const char *db_get_database_comments (void);
extern void db_set_client_type (int client_type);
extern void db_set_preferred_hosts (const char *hosts);
extern int db_get_client_type (void);
extern const char *db_get_type_name (DB_TYPE type_id);
extern DB_TYPE db_type_from_string (const char *name);
extern int db_get_schema_def_dbval (DB_VALUE * result, DB_VALUE * name_val);
extern const char *db_default_expression_string (DB_DEFAULT_EXPR_TYPE default_expr_type);

extern DB_OBJECT *db_find_class_of_index (const char *index, DB_CONSTRAINT_TYPE type);
extern DB_OBJECT *db_find_class (const char *name);
extern DB_OBJECT *db_get_class (DB_OBJECT * obj);
extern DB_OBJLIST *db_get_all_objects (DB_OBJECT * classobj);
extern DB_OBJLIST *db_get_all_classes (void);
extern DB_OBJLIST *db_get_base_classes (void);
extern DB_OBJLIST *db_fetch_all_objects (DB_OBJECT * op, DB_FETCH_MODE mode);
extern DB_OBJLIST *db_fetch_all_classes (DB_FETCH_MODE mode);
extern DB_OBJLIST *db_fetch_base_classes (DB_FETCH_MODE mode);

extern int db_is_class (DB_OBJECT * obj);
extern int db_is_any_class (DB_OBJECT * obj);
extern int db_is_instance (DB_OBJECT * obj);
extern int db_is_instance_of (DB_OBJECT * obj, DB_OBJECT * classobj);
extern int db_is_subclass (DB_OBJECT * classobj, DB_OBJECT * supermop);
extern int db_is_superclass (DB_OBJECT * supermop, DB_OBJECT * classobj);
extern int db_is_partition (DB_OBJECT * classobj, DB_OBJECT * superobj);
extern int db_is_system_class (DB_OBJECT * op);
extern int db_is_deleted (DB_OBJECT * obj);

extern const char *db_get_class_name (DB_OBJECT * classobj);
extern DB_OBJLIST *db_get_superclasses (DB_OBJECT * obj);
extern DB_OBJLIST *db_get_subclasses (DB_OBJECT * obj);
extern DB_ATTRIBUTE *db_get_attribute (DB_OBJECT * obj, const char *name);
extern DB_ATTRIBUTE *db_get_attribute_by_name (const char *class_name, const char *attribute_name);
extern DB_ATTRIBUTE *db_get_attributes (DB_OBJECT * obj);
extern DB_ATTRIBUTE *db_get_class_attribute (DB_OBJECT * obj, const char *name);
extern DB_ATTRIBUTE *db_get_class_attributes (DB_OBJECT * obj);
extern DB_METHOD *db_get_method (DB_OBJECT * obj, const char *name);
extern DB_METHOD *db_get_class_method (DB_OBJECT * obj, const char *name);
extern DB_METHOD *db_get_methods (DB_OBJECT * obj);
extern DB_METHOD *db_get_class_methods (DB_OBJECT * obj);
extern DB_RESOLUTION *db_get_resolutions (DB_OBJECT * obj);
extern DB_RESOLUTION *db_get_class_resolutions (DB_OBJECT * obj);
extern DB_METHFILE *db_get_method_files (DB_OBJECT * obj);
extern const char *db_get_loader_commands (DB_OBJECT * obj);

extern DB_TYPE db_attribute_type (DB_ATTRIBUTE * attribute);
extern DB_ATTRIBUTE *db_attribute_next (DB_ATTRIBUTE * attribute);
extern const char *db_attribute_name (DB_ATTRIBUTE * attribute);
extern int db_attribute_id (DB_ATTRIBUTE * attribute);
extern int db_attribute_order (DB_ATTRIBUTE * attribute);
extern DB_DOMAIN *db_attribute_domain (DB_ATTRIBUTE * attribute);
extern DB_OBJECT *db_attribute_class (DB_ATTRIBUTE * attribute);
extern DB_VALUE *db_attribute_default (DB_ATTRIBUTE * attribute);
extern int db_attribute_is_unique (DB_ATTRIBUTE * attribute);
extern int db_attribute_is_primary_key (DB_ATTRIBUTE * attribute);
extern int db_attribute_is_foreign_key (DB_ATTRIBUTE * attribute);
extern int db_attribute_is_auto_increment (DB_ATTRIBUTE * attribute);
extern int db_attribute_is_reverse_unique (DB_ATTRIBUTE * attribute);
extern int db_attribute_is_non_null (DB_ATTRIBUTE * attribute);
extern int db_attribute_is_indexed (DB_ATTRIBUTE * attribute);
extern int db_attribute_is_reverse_indexed (DB_ATTRIBUTE * attribute);
extern int db_attribute_is_shared (DB_ATTRIBUTE * attribute);
extern int db_attribute_length (DB_ATTRIBUTE * attribute);
extern DB_DOMAIN *db_type_to_db_domain (DB_TYPE type);

extern DB_DOMAIN *db_domain_next (const DB_DOMAIN * domain);
extern DB_TYPE db_domain_type (const DB_DOMAIN * domain);
extern DB_OBJECT *db_domain_class (const DB_DOMAIN * domain);
extern DB_DOMAIN *db_domain_set (const DB_DOMAIN * domain);
extern int db_domain_precision (const DB_DOMAIN * domain);
extern int db_domain_scale (const DB_DOMAIN * domain);
extern int db_domain_codeset (const DB_DOMAIN * domain);

extern DB_METHOD *db_method_next (DB_METHOD * method);
extern const char *db_method_name (DB_METHOD * method);
extern const char *db_method_function (DB_METHOD * method);
extern DB_OBJECT *db_method_class (DB_METHOD * method);
extern DB_DOMAIN *db_method_return_domain (DB_METHOD * method);
extern DB_DOMAIN *db_method_arg_domain (DB_METHOD * method, int arg);
extern int db_method_arg_count (DB_METHOD * method);

extern DB_RESOLUTION *db_resolution_next (DB_RESOLUTION * resolution);
extern DB_OBJECT *db_resolution_class (DB_RESOLUTION * resolution);
extern const char *db_resolution_name (DB_RESOLUTION * resolution);
extern const char *db_resolution_alias (DB_RESOLUTION * resolution);
extern int db_resolution_isclass (DB_RESOLUTION * resolution);

extern DB_METHFILE *db_methfile_next (DB_METHFILE * methfile);
extern const char *db_methfile_name (DB_METHFILE * methfile);

extern DB_OBJLIST *db_objlist_next (DB_OBJLIST * link);
extern DB_OBJECT *db_objlist_object (DB_OBJLIST * link);


extern int db_get_class_num_objs_and_pages (DB_OBJECT * classmop, int approximation, int *nobjs, int *npages);
extern int db_get_btree_statistics (DB_CONSTRAINT * cons, int *num_leaf_pages, int *num_total_pages, int *num_keys,
				    int *height);

/* Constraint Functions */
extern DB_CONSTRAINT *db_get_constraints (DB_OBJECT * obj);
extern DB_CONSTRAINT *db_constraint_next (DB_CONSTRAINT * constraint);
extern DB_CONSTRAINT *db_constraint_find_primary_key (DB_CONSTRAINT * constraint);
extern DB_CONSTRAINT_TYPE db_constraint_type (DB_CONSTRAINT * constraint);
extern const char *db_constraint_name (DB_CONSTRAINT * constraint);
extern DB_ATTRIBUTE **db_constraint_attributes (DB_CONSTRAINT * constraint);
extern const int *db_constraint_asc_desc (DB_CONSTRAINT * constraint);

extern const char *db_get_foreign_key_action (DB_CONSTRAINT * constraint, DB_FK_ACTION_TYPE type);
extern DB_OBJECT *db_get_foreign_key_ref_class (DB_CONSTRAINT * constraint);

/* Trigger functions */
extern DB_OBJECT *db_create_trigger (const char *name, DB_TRIGGER_STATUS status, double priority,
				     DB_TRIGGER_EVENT event, DB_OBJECT * class_obj, const char *attr,
				     DB_TRIGGER_TIME cond_time, const char *cond_source, DB_TRIGGER_TIME action_time,
				     DB_TRIGGER_ACTION action_type, const char *action_source);

extern int db_drop_trigger (DB_OBJECT * obj);
extern int db_rename_trigger (DB_OBJECT * obj, const char *newname);

extern DB_OBJECT *db_find_trigger (const char *name);
extern int db_find_all_triggers (DB_OBJLIST ** list);
extern int db_find_event_triggers (DB_TRIGGER_EVENT event, DB_OBJECT * class_obj, const char *attr, DB_OBJLIST ** list);
extern int db_alter_trigger_priority (DB_OBJECT * trobj, double priority);
extern int db_alter_trigger_status (DB_OBJECT * trobj, DB_TRIGGER_STATUS status);

extern int db_execute_deferred_activities (DB_OBJECT * trigger_obj, DB_OBJECT * target);
extern int db_drop_deferred_activities (DB_OBJECT * trigger_obj, DB_OBJECT * target);

extern int db_trigger_name (DB_OBJECT * trobj, char **name);
extern int db_trigger_status (DB_OBJECT * trobj, DB_TRIGGER_STATUS * status);
extern int db_trigger_priority (DB_OBJECT * trobj, double *priority);
extern int db_trigger_event (DB_OBJECT * trobj, DB_TRIGGER_EVENT * event);
extern int db_trigger_class (DB_OBJECT * trobj, DB_OBJECT ** class_obj);
extern int db_trigger_attribute (DB_OBJECT * trobj, char **attr);
extern int db_trigger_condition (DB_OBJECT * trobj, char **condition);
extern int db_trigger_condition_time (DB_OBJECT * trobj, DB_TRIGGER_TIME * tr_time);
extern int db_trigger_action_type (DB_OBJECT * trobj, DB_TRIGGER_ACTION * type);
extern int db_trigger_action_time (DB_OBJECT * trobj, DB_TRIGGER_TIME * tr_time);
extern int db_trigger_action (DB_OBJECT * trobj, char **action);
extern int db_trigger_comment (DB_OBJECT * trobj, char **comment);

/* Schema template functions */
extern DB_CTMPL *dbt_create_class (const char *name);
extern DB_CTMPL *dbt_create_vclass (const char *name);
extern DB_CTMPL *dbt_edit_class (DB_OBJECT * classobj);
extern DB_OBJECT *dbt_finish_class (DB_CTMPL * def);
extern void dbt_abort_class (DB_CTMPL * def);

extern int dbt_add_attribute (DB_CTMPL * def, const char *name, const char *domain, DB_VALUE * default_value);
extern int dbt_add_shared_attribute (DB_CTMPL * def, const char *name, const char *domain, DB_VALUE * default_value);
extern int dbt_add_class_attribute (DB_CTMPL * def, const char *name, const char *domain, DB_VALUE * default_value);
extern int dbt_constrain_non_null (DB_CTMPL * def, const char *name, int class_attribute, int on_or_off);
extern int dbt_constrain_unique (DB_CTMPL * def, const char *name, int on_or_off);
extern int dbt_add_constraint (DB_CTMPL * def, DB_CONSTRAINT_TYPE constraint_type, const char *constraint_name,
			       const char **attnames, int class_attributes, const char *comment);
extern int dbt_drop_constraint (DB_CTMPL * def, DB_CONSTRAINT_TYPE constraint_type, const char *constraint_name,
				const char **attnames, int class_attributes);
extern int dbt_add_set_attribute_domain (DB_CTMPL * def, const char *name, int class_attribute, const char *domain);
extern int dbt_change_domain (DB_CTMPL * def, const char *name, int class_attribute, const char *domain);
extern int dbt_change_default (DB_CTMPL * def, const char *name, int class_attribute, DB_VALUE * value);
extern int dbt_drop_set_attribute_domain (DB_CTMPL * def, const char *name, int class_attribute, const char *domain);
extern int dbt_drop_attribute (DB_CTMPL * def, const char *name);
extern int dbt_drop_shared_attribute (DB_CTMPL * def, const char *name);
extern int dbt_drop_class_attribute (DB_CTMPL * def, const char *name);
extern int dbt_add_method (DB_CTMPL * def, const char *name, const char *implementation);
extern int dbt_add_class_method (DB_CTMPL * def, const char *name, const char *implementation);
extern int dbt_add_argument (DB_CTMPL * def, const char *name, int class_method, int arg_index, const char *domain);
extern int dbt_add_set_argument_domain (DB_CTMPL * def, const char *name, int class_method, int arg_index,
					const char *domain);
extern int dbt_change_method_implementation (DB_CTMPL * def, const char *name, int class_method, const char *newname);
extern int dbt_drop_method (DB_CTMPL * def, const char *name);
extern int dbt_drop_class_method (DB_CTMPL * def, const char *name);
extern int dbt_add_super (DB_CTMPL * def, DB_OBJECT * super);
extern int dbt_drop_super (DB_CTMPL * def, DB_OBJECT * super);
extern int dbt_drop_super_connect (DB_CTMPL * def, DB_OBJECT * super);
extern int dbt_rename (DB_CTMPL * def, const char *name, int class_namespace, const char *newname);
extern int dbt_add_method_file (DB_CTMPL * def, const char *name);
extern int dbt_drop_method_file (DB_CTMPL * def, const char *name);
extern int dbt_drop_method_files (DB_CTMPL * def);
extern int dbt_rename_method_file (DB_CTMPL * def, const char *new_name, const char *old_name);

extern int dbt_set_loader_commands (DB_CTMPL * def, const char *commands);
extern int dbt_add_resolution (DB_CTMPL * def, DB_OBJECT * super, const char *name, const char *alias);
extern int dbt_add_class_resolution (DB_CTMPL * def, DB_OBJECT * super, const char *name, const char *alias);
extern int dbt_drop_resolution (DB_CTMPL * def, DB_OBJECT * super, const char *name);
extern int dbt_drop_class_resolution (DB_CTMPL * def, DB_OBJECT * super, const char *name);

extern int dbt_add_query_spec (DB_CTMPL * def, const char *query);
extern int dbt_drop_query_spec (DB_CTMPL * def, const int query_no);
extern int dbt_change_query_spec (DB_CTMPL * def, const char *new_query, const int query_no);
extern int dbt_set_object_id (DB_CTMPL * def, DB_NAMELIST * id_list);
extern int dbt_add_foreign_key (DB_CTMPL * def, const char *constraint_name, const char **attnames,
				const char *ref_class, const char **ref_attrs, int del_action, int upd_action,
				const char *comment);

/* Object template functions */
extern DB_OTMPL *dbt_create_object (DB_OBJECT * classobj);
extern DB_OTMPL *dbt_edit_object (DB_OBJECT * object);
extern DB_OBJECT *dbt_finish_object (DB_OTMPL * def);
extern DB_OBJECT *dbt_finish_object_and_decache_when_failure (DB_OTMPL * def);
extern void dbt_abort_object (DB_OTMPL * def);

extern int dbt_put (DB_OTMPL * def, const char *name, DB_VALUE * value);
extern int dbt_set_label (DB_OTMPL * def, DB_VALUE * label);

/* Descriptor functions.
 * The descriptor interface offers an alternative to attribute & method
 * names that can be substantially faster for repetitive operations.
 */
extern int db_get_attribute_descriptor (DB_OBJECT * obj, const char *attname, int class_attribute, int for_update,
					DB_ATTDESC ** descriptor);
extern void db_free_attribute_descriptor (DB_ATTDESC * descriptor);

extern int db_get_method_descriptor (DB_OBJECT * obj, const char *methname, int class_method,
				     DB_METHDESC ** descriptor);
extern void db_free_method_descriptor (DB_METHDESC * descriptor);

extern int db_dget (DB_OBJECT * obj, DB_ATTDESC * attribute, DB_VALUE * value);
extern int db_dput (DB_OBJECT * obj, DB_ATTDESC * attribute, DB_VALUE * value);

extern int db_dsend (DB_OBJECT * obj, DB_METHDESC * method, DB_VALUE * returnval, ...);

extern int db_dsend_arglist (DB_OBJECT * obj, DB_METHDESC * method, DB_VALUE * returnval, DB_VALUE_LIST * args);

extern int db_dsend_argarray (DB_OBJECT * obj, DB_METHDESC * method, DB_VALUE * returnval, DB_VALUE ** args);

extern int db_dsend_quick (DB_OBJECT * obj, DB_METHDESC * method, DB_VALUE * returnval, int nargs, DB_VALUE ** args);

extern int dbt_dput (DB_OTMPL * def, DB_ATTDESC * attribute, DB_VALUE * value);

/* SQL/M API function*/
extern char *db_get_vclass_ldb_name (DB_OBJECT * op);

extern int db_add_query_spec (DB_OBJECT * vclass, const char *query);
extern int db_drop_query_spec (DB_OBJECT * vclass, const int query_no);
extern DB_NAMELIST *db_get_object_id (DB_OBJECT * vclass);

extern int db_namelist_add (DB_NAMELIST ** list, const char *name);
extern int db_namelist_append (DB_NAMELIST ** list, const char *name);
extern void db_namelist_free (DB_NAMELIST * list);

extern int db_is_vclass (DB_OBJECT * op);

extern DB_OBJLIST *db_get_all_vclasses_on_ldb (void);
extern DB_OBJLIST *db_get_all_vclasses (void);

extern DB_QUERY_SPEC *db_get_query_specs (DB_OBJECT * obj);
extern DB_QUERY_SPEC *db_query_spec_next (DB_QUERY_SPEC * query_spec);
extern const char *db_query_spec_string (DB_QUERY_SPEC * query_spec);
extern int db_change_query_spec (DB_OBJECT * vclass, const char *new_query, const int query_no);

extern int db_validate (DB_OBJECT * vclass);
extern int db_validate_query_spec (DB_OBJECT * vclass, const char *query_spec);
extern int db_is_real_instance (DB_OBJECT * obj);
extern DB_OBJECT *db_real_instance (DB_OBJECT * obj);
extern int db_instance_equal (DB_OBJECT * obj1, DB_OBJECT * obj2);
extern int db_is_updatable_object (DB_OBJECT * obj);
extern int db_is_updatable_attribute (DB_OBJECT * obj, const char *attr_name);

extern int db_check_single_query (DB_SESSION * session);

/* query pre-processing functions */
extern int db_get_query_format (const char *CSQL_query, DB_QUERY_TYPE ** type_list, DB_QUERY_ERROR * query_error);
extern DB_QUERY_TYPE *db_query_format_next (DB_QUERY_TYPE * query_type);
extern DB_COL_TYPE db_query_format_col_type (DB_QUERY_TYPE * query_type);
extern char *db_query_format_name (DB_QUERY_TYPE * query_type);
extern DB_TYPE db_query_format_type (DB_QUERY_TYPE * query_type);
extern void db_query_format_free (DB_QUERY_TYPE * query_type);
extern DB_DOMAIN *db_query_format_domain (DB_QUERY_TYPE * query_type);
extern char *db_query_format_attr_name (DB_QUERY_TYPE * query_type);
extern char *db_query_format_spec_name (DB_QUERY_TYPE * query_type);
extern char *db_query_format_original_name (DB_QUERY_TYPE * query_type);
extern const char *db_query_format_class_name (DB_QUERY_TYPE * query_type);
extern int db_query_format_is_non_null (DB_QUERY_TYPE * query_type);

/* query processing functions */
extern int db_get_query_result_format (DB_QUERY_RESULT * result, DB_QUERY_TYPE ** type_list);
extern int db_query_next_tuple (DB_QUERY_RESULT * result);
extern int db_query_prev_tuple (DB_QUERY_RESULT * result);
extern int db_query_first_tuple (DB_QUERY_RESULT * result);
extern int db_query_last_tuple (DB_QUERY_RESULT * result);
extern int db_query_get_tuple_value_by_name (DB_QUERY_RESULT * result, char *column_name, DB_VALUE * value);
extern int db_query_get_tuple_value (DB_QUERY_RESULT * result, int tuple_index, DB_VALUE * value);

extern int db_query_get_tuple_oid (DB_QUERY_RESULT * result, DB_VALUE * db_value);

extern int db_query_get_tuple_valuelist (DB_QUERY_RESULT * result, int size, DB_VALUE * value_list);

extern int db_query_tuple_count (DB_QUERY_RESULT * result);

extern int db_query_column_count (DB_QUERY_RESULT * result);

extern int db_query_prefetch_columns (DB_QUERY_RESULT * result, int *columns, int col_count);

extern int db_query_format_size (DB_QUERY_TYPE * query_type);

extern int db_query_end (DB_QUERY_RESULT * result);

/* query post-processing functions */
extern int db_query_plan_dump_file (char *filename);

/* sql query routines */
extern DB_SESSION *db_open_buffer (const char *buffer);
extern DB_SESSION *db_open_file (FILE * file);
extern DB_SESSION *db_open_file_name (const char *name);

extern int db_statement_count (DB_SESSION * session);

extern int db_compile_statement (DB_SESSION * session);
extern void db_rewind_statement (DB_SESSION * session);

extern DB_SESSION_ERROR *db_get_errors (DB_SESSION * session);

extern DB_SESSION_ERROR *db_get_next_error (DB_SESSION_ERROR * errors, int *linenumber, int *columnnumber);

extern DB_SESSION_ERROR *db_get_warnings (DB_SESSION * session);

extern DB_SESSION_ERROR *db_get_next_warning (DB_SESSION_WARNING * errors, int *linenumber, int *columnnumber);

extern DB_PARAMETER *db_get_parameters (DB_SESSION * session, int statement_id);
extern DB_PARAMETER *db_parameter_next (DB_PARAMETER * param);
extern const char *db_parameter_name (DB_PARAMETER * param);
extern int db_bind_parameter_name (const char *name, DB_VALUE * value);

extern DB_QUERY_TYPE *db_get_query_type_list (DB_SESSION * session, int stmt);

extern int db_number_of_input_markers (DB_SESSION * session, int stmt);
extern int db_number_of_output_markers (DB_SESSION * session, int stmt);
extern DB_MARKER *db_get_input_markers (DB_SESSION * session, int stmt);
extern DB_MARKER *db_get_output_markers (DB_SESSION * session, int stmt);
extern DB_MARKER *db_marker_next (DB_MARKER * marker);
extern int db_marker_index (DB_MARKER * marker);
extern DB_DOMAIN *db_marker_domain (DB_MARKER * marker);
extern bool db_is_input_marker (DB_MARKER * marker);
extern bool db_is_output_marker (DB_MARKER * marker);

extern int db_get_start_line (DB_SESSION * session, int stmt);

extern int db_get_statement_type (DB_SESSION * session, int stmt);

/* constants for db_include_oid */
enum
{ DB_NO_OIDS, DB_ROW_OIDS, DB_COLUMN_OIDS /* deprecated constant */  };

extern void db_include_oid (DB_SESSION * session, int include_oid);

extern int db_push_values (DB_SESSION * session, int count, DB_VALUE * in_values);

extern int db_execute (const char *CSQL_query, DB_QUERY_RESULT ** result, DB_QUERY_ERROR * query_error);

extern int db_execute_oid (const char *CSQL_query, DB_QUERY_RESULT ** result, DB_QUERY_ERROR * query_error);

extern int db_query_produce_updatable_result (DB_SESSION * session, int stmtid);

extern int db_execute_statement (DB_SESSION * session, int stmt, DB_QUERY_RESULT ** result);

extern int db_execute_and_keep_statement (DB_SESSION * session, int stmt, DB_QUERY_RESULT ** result);
extern DB_CLASS_MODIFICATION_STATUS db_has_modified_class (DB_SESSION * session, int stmt_id);

extern int db_query_set_copy_tplvalue (DB_QUERY_RESULT * result, int copy);

extern void db_close_session (DB_SESSION * session);
extern void db_drop_statement (DB_SESSION * session, int stmt_id);

extern int db_object_describe (DB_OBJECT * obj, int num_attrs, const char **attrs, DB_QUERY_TYPE ** col_spec);

extern int db_object_fetch (DB_OBJECT * obj, int num_attrs, const char **attrs, DB_QUERY_RESULT ** result);

extern int db_set_client_cache_time (DB_SESSION * session, int stmt_ndx, CACHE_TIME * cache_time);
extern bool db_get_jdbccachehint (DB_SESSION * session, int stmt_ndx, int *life_time);
extern bool db_get_cacheinfo (DB_SESSION * session, int stmt_ndx, bool * use_plan_cache, bool * use_query_cache);

/* These are used by csql but weren't in the 2.0 dbi.h file, added
   it for the PC.  If we don't want them here, they should go somewhere
   else so csql.c doesn't have to have an explicit declaration.
*/
extern void db_free_query (DB_SESSION * session);
extern DB_QUERY_TYPE *db_get_query_type_ptr (DB_QUERY_RESULT * result);

/* OBSOLETE FUNCTIONS
 * These functions are no longer supported.
 * New applications should not use any of these functions of structures.
 * Old applications should change to use only the functions and structures
 * published in the CUBRID Application Program Interface Reference Guide.
 */

extern int db_query_execute (const char *CSQL_query, DB_QUERY_RESULT ** result, DB_QUERY_ERROR * query_error);

extern int db_list_length (DB_LIST * list);
extern DB_NAMELIST *db_namelist_copy (DB_NAMELIST * list);

extern int db_drop_shared_attribute (DB_OBJECT * classobj, const char *name);

extern int db_add_element_domain (DB_OBJECT * classobj, const char *name, const char *domain);
extern int db_drop_element_domain (DB_OBJECT * classobj, const char *name, const char *domain);
extern int db_rename_attribute (DB_OBJECT * classobj, const char *name, int class_attribute, const char *newname);
extern int db_rename_method (DB_OBJECT * classobj, const char *name, int class_method, const char *newname);
extern int db_set_argument_domain (DB_OBJECT * classobj, const char *name, int class_method, int arg_index,
				   const char *domain);
extern int db_set_method_arg_domain (DB_OBJECT * classobj, const char *name, int arg_index, const char *domain);
extern int db_set_class_method_arg_domain (DB_OBJECT * classobj, const char *name, int arg_index, const char *domain);
extern DB_NAMELIST *db_namelist_sort (DB_NAMELIST * names);
extern void db_namelist_remove (DB_NAMELIST ** list, const char *name);
extern DB_OBJECT *db_objlist_get (DB_OBJLIST * list, int psn);
extern void db_namelist_print (DB_NAMELIST * list);
extern void db_objlist_print (DB_OBJLIST * list);

extern DB_NAMELIST *db_get_attribute_names (DB_OBJECT * obj);
extern DB_NAMELIST *db_get_shared_attribute_names (DB_OBJECT * obj);
extern DB_NAMELIST *db_get_ordered_attribute_names (DB_OBJECT * obj);
extern DB_NAMELIST *db_get_class_attribute_names (DB_OBJECT * obj);
extern DB_NAMELIST *db_get_method_names (DB_OBJECT * obj);
extern DB_NAMELIST *db_get_class_method_names (DB_OBJECT * obj);
extern DB_NAMELIST *db_get_superclass_names (DB_OBJECT * obj);
extern DB_NAMELIST *db_get_subclass_names (DB_OBJECT * obj);
extern DB_NAMELIST *db_get_method_file_names (DB_OBJECT * obj);
extern const char *db_get_method_function (DB_OBJECT * obj, const char *name);

extern DB_DOMAIN *db_get_attribute_domain (DB_OBJECT * obj, const char *name);
extern DB_TYPE db_get_attribute_type (DB_OBJECT * obj, const char *name);
extern DB_OBJECT *db_get_attribute_class (DB_OBJECT * obj, const char *name);

extern void db_force_method_reload (DB_OBJECT * obj);

extern DB_ATTRIBUTE *db_get_shared_attribute (DB_OBJECT * obj, const char *name);
extern DB_ATTRIBUTE *db_get_ordered_attributes (DB_OBJECT * obj);
extern DB_ATTRIBUTE *db_attribute_ordered_next (DB_ATTRIBUTE * attribute);

extern int db_print_mop (DB_OBJECT * obj, char *buffer, int maxlen);

extern int db_get_shared (DB_OBJECT * object, const char *attpath, DB_VALUE * value);

extern DB_OBJECT *db_copy (DB_OBJECT * sourcemop);
extern char *db_get_method_source_file (DB_OBJECT * obj, const char *name);

extern int db_is_indexed (DB_OBJECT * classobj, const char *attname);

/* INTERNAL FUNCTIONS
 * These are part of the interface but are intended only for
 * internal use by CUBRID.  Applications should not use these
 * functions.
 */
extern DB_IDENTIFIER *db_identifier (DB_OBJECT * obj);
extern DB_OBJECT *db_object (DB_IDENTIFIER * oid);
extern int db_chn (DB_OBJECT * obj, DB_FETCH_MODE purpose);

extern int db_encode_object (DB_OBJECT * object, char *string, int allocated_length, int *actual_length);
extern int db_decode_object (const char *string, DB_OBJECT ** object);

extern int db_set_system_parameters (const char *data);
extern int db_get_system_parameters (char *data, int len);

extern char *db_get_host_connected (void);
extern int db_get_ha_server_state (char *buffer, int maxlen);

extern void db_clear_host_connected (void);
extern char *db_get_database_version (void);

extern bool db_enable_trigger (void);
extern bool db_disable_trigger (void);

extern void db_clear_host_status (void);
extern void db_set_host_status (char *hostname, int status);
extern void db_set_connected_host_status (char *host_connected);
extern bool db_does_connected_host_have_status (int status);
extern bool db_need_reconnect (void);
extern bool db_need_ignore_repl_delay (void);
#endif /* _DBI_COMPAT_H_ */
