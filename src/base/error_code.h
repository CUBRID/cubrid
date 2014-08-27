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
 * error_code.h - Error codes
 *
 * CAUTION!
 *
 * When an entry is added here please ensure that the msg/<locale>/cubrid.msg
 * files are updated with matching error strings. See message_catalog.c for
 * details.
 * The error codes must also be added to compat/dbi_compat.h
 * ER_LAST_ERROR must also be updated.
 */

#ifndef _ERROR_CODE_H_
#define _ERROR_CODE_H_

#ident "$Id$"

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
#define ER_LOG_POSTPONE_LOGGING_DURING_RECOVERY      -94
#define ER_LOG_UNDO_LOGGING_DURING_RECOVERY          -95
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
#define ER_SM_INHERITED_ATTMETH                     -277
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
#define ER_SM_CATALOG_SPACE                         -295
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

#define ER_QF_NOSPACE                               -331
#define ER_QF_EXTENDING                             -332
#define ER_QF_ABORT                                 -333
#define ER_QF_OUTRAGEOUS                            -334
#define ER_QF_ILLEGAL_POINTER                       -335
#define ER_QF_FREE_TWICE                            -336
#define ER_QF_NEGATIVE_SIZE                         -337

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
#define ER_CT_UNKNOWN_CLASSID                       -414
#define ER_CT_INVALID_CLASSID                       -415
#define ER_CT_UNKNOWN_REPRID                        -416
#define ER_CT_INVALID_REPRID                        -417
#define ER_CT_NOSPACE_FOR_ATTRDIR                   -418
#define ER_CT_REPRCNT_OVERFLOW                      -419
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
#define ER_LOG_BADSTATE_FOR_CLIENT_UNDO_OR_POSTPONE -645
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

#define ER_NOT_ENOUGH_SCANID_BIT                    -838

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
#define ER_FK_CANT_ASSIGN_CACHE_ATTR                -928
#define ER_FK_CANT_ON_VCLASS                        -929
#define ER_FK_CANT_DROP_CACHE_ATTR                  -930

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

/* Please note that error code -1023 is reserved for HA */
#define ER_HA_LW_FAILED_GET_LOG_PAGE                -1024
#define ER_HA_REPL_DELAY_DETECTED                   -1025
#define ER_HA_REPL_DELAY_RESOLVED                   -1026
#define ER_HA_LA_FAILED_TO_CHANGE_STATE             -1027
#define ER_HA_LA_UNEXPECTED_EOF_IN_ARCHIVE_LOG      -1028
#define ER_HA_LA_INVALID_REPL_LOG_PAGEID_OFFSET     -1029
#define ER_HA_LA_INVALID_REPL_LOG_RECORD            -1030
#define ER_HA_LA_FAILED_TO_APPLY_SCHEMA		    -1031
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

#define ER_LAST_ERROR                               -1159

/*
 * CAUTION!
 *
 * When an entry is added here please ensure that the msg/<locale>/cubrid.msg
 * files are updated with matching error strings. See message_catalog.c for
 * details.
 * The error codes must also be added to compat/dbi_compat.h
 * ER_LAST_ERROR must also be updated.
 */

#endif /* _ERROR_CODE_H_ */
