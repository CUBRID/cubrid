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
 * broker_error.h - Error code and message handling module
 *           This file contains exported stuffs from the error code and message
 *           handling modules.
 */

#ifndef	_BROKER_ERROR_H_
#define	_BROKER_ERROR_H_

#ident "$Id$"

#define UW_ER_NO_ERROR                           0
#define UW_ER_NO_REQUEST_METHOD                  -11001
#define UW_ER_NOT_SUPPORTED_REQUEST_METHOD       -11002
#define UW_ER_NO_APPL_NAME                       -11003
#define UW_ER_CANT_CREATE_SOCKET                 -11004
#define UW_ER_CANT_CONNECT                       -11005
#define UW_ER_CANT_BIND                          -11006
#define UW_ER_SOCKET_NOT_INITIALIZED             -11007
#define UW_ER_CANT_ACCEPT                        -11008
#define UW_ER_NO_MORE_MEMORY                     -11009
#define UW_ER_NO_FREE_UTS                        -11010
#define UW_ER_NO_SESSION_UTS                     -11011
#define UW_ER_SESSION_NOT_FOUND                  -11012
#define UW_ER_SHM_OPEN                           -11013
#define UW_ER_SCRIPT_FILE                        -11014
#define UW_ER_UNKNOWN_FILE_C                     -11015
#define UW_ER_UNKNOWN_FILE_W                     -11016
#define UW_ER_DB_NOT_INITIALIZED                 -11017
#define UW_ER_SQL_HANDLE_NOT_FOUND               -11018
#define UW_ER_INVALID_OBJECT                     -11019
#define UW_ER_INVALID_BIT                        -11020
#define UW_ER_INVALID_CHAR                       -11021
#define UW_ER_INVALID_DOUBLE                     -11022
#define UW_ER_INVALID_FLOAT                      -11023
#define UW_ER_INVALID_INT                        -11024
#define UW_ER_INVALID_MONETARY                   -11025
#define UW_ER_INVALID_NUMERIC                    -11026
#define UW_ER_INVALID_SET                        -11027
#define UW_ER_INVALID_SET_DOM                    -11028
#define UW_ER_INVALID_DOMAIN                     -11029
#define UW_ER_CMD_ARGS                           -11030
#define UW_ER_NOT_SUPPORT_SH_CMD                 -11031
#define UW_ER_COMMUNICATION                      -11032
#define UW_ER_DOMAIN_NON_SET                     -11033
#define UW_ER_DIVIDE_BY_ZERO                     -11034
#define UW_ER_FILE_MNG                           -11035
#define UW_ER_FILE_REMOVE                        -11036
#define UW_ER_FILE_COPY                          -11037
#define UW_ER_FILE_MOVE                          -11038
#define UW_ER_DIR_REMOVE                         -11039
#define UW_ER_DIR_MAKE                           -11040
#define UW_ER_FILE_CREATE                        -11041
#define UW_ER_FCNTL                              -11042
#define UW_ER_POST_DATA                          -11043
#define UW_ER_OPEN_TEMP_FILE                     -11044
#define UW_ER_FILE_UPLOAD                        -11045
#define UW_ER_FILE_WRITE                         -11046
#define UW_ER_SERVER_INFO                        -11047
#define UW_ER_DISP_WORK_DIR                      -11048
#define UW_ER_WIN_SERVER_CONF                    -11049
#define UW_ER_INVALID_CLIENT                     -11050
#define UW_ER_SHM_OPEN_MAGIC                     -11051

#define UW_MIN_ERROR_CODE                        -11000
#define UW_MAX_ERROR_CODE                        -11051

#define	UW_SET_ERROR_CODE(code, os_errno) \
		uw_set_error_code(__FILE__, __LINE__, (code), (os_errno))

extern void uw_set_error_code (const char *file_name, int line_no,
			       int error_code, int os_errno);
extern int uw_get_error_code (void);
extern int uw_get_os_error_code (void);
extern const char *uw_get_error_message (int error_code, int os_errno);
#if defined (ENABLE_UNUSED_FUNCTION)
extern const char *uw_error_message (int error_code);
extern void uw_error_message_r (int error_code, char *err_msg);
extern void uw_os_err_msg (int err_code, char *err_msg);
#endif

#endif /* _BROKER_ERROR_H_ */
