/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution. 
 *
 *   This program is free software; you can redistribute it and/or modify 
 *   it under the terms of the GNU General Public License as published by 
 *   the Free Software Foundation; version 2 of the License. 
 *
 *  This program is distributed in the hope that it will be useful, 
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of 
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the 
 *  GNU General Public License for more details. 
 *
 *  You should have received a copy of the GNU General Public License 
 *  along with this program; if not, write to the Free Software 
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA 
 *
 */


/*
 * condition_handler_err.h : CO error condition codes and formats
 *
 */

#ifndef _CONDITION_HANDLER_ERR_H_
#define _CONDITION_HANDLER_ERR_H_

#ident "$Id$"

#include <errno.h>
#include "condition_handler_code.h"

#define CO_ERR_BAD_FORMAT             CO_CODE(CO_MODULE_CO, 1)
#define CO_ER_FMT_BAD_FORMAT            \
   "Condition signalled with unknown parameter type `%s'."

#define CO_ERR_CATALOG_NAME_EXISTS    CO_CODE(CO_MODULE_CO, 2)
#define CO_ER_FMT_CATALOG_NAME_EXISTS   \
   "Can't register catalog `%s' with module %d -- already registered with %d."

#define CO_ERR_CATALOG_CODE_EXISTS    CO_CODE(CO_MODULE_CO, 3)
#define CO_ER_FMT_CATALOG_CODE_EXISTS   \
   "Can't register module %d with catalog `%s' -- already registered with `%s'."

#define CO_ERR_BAD_MODULE             CO_CODE(CO_MODULE_CO, 4)
#define CO_ER_FMT_BAD_MODULE            \
   "Invalid module identifier %d -- must be non-negative and less than %d."

#define CO_ERR_BAD_CODE               CO_CODE(CO_MODULE_CO, 5)
#define CO_ER_FMT_BAD_CODE              \
   "Invalid condition code %d -- must be less than zero."

#define CO_ERR_BAD_DETAIL             CO_CODE(CO_MODULE_CO, 6)
#define CO_ER_FMT_BAD_DETAIL            \
   "Unknown detail level %d."

#define CO_ERR_BAD_POSITION           CO_CODE(CO_MODULE_CO, 7)
#define CO_ER_FMT_BAD_POSITION          \
   "Default message format cannot contain a position specifier."

/* File system errors */

#define CO_SYS_ERR_NOT_OWNER           CO_CODE(CO_MODULE_SYS, EPERM)
#define CO_SYS_ER_FMT_NOT_OWNER          \
  "Can't access `%s' -- you are not the owner."

#define CO_SYS_ERR_FILE_NOT_FOUND      CO_CODE(CO_MODULE_SYS, ENOENT)
#define CO_SYS_ER_FMT_FILE_NOT_FOUND     \
  "Can't access `%s' -- the file was not found."

#define CO_SYS_ERR_IO                  CO_CODE(CO_MODULE_SYS, EIO)
#define CO_SYS_ER_FMT_IO                 \
  "Can't access `%s' -- an I/O error occurred."

#define CO_SYS_ERR_DEVICE_NOT_FOUND    CO_CODE(CO_MODULE_SYS, ENXIO)
#define CO_SYS_ER_FMT_DEVICE_NOT_FOUND   \
  "Can't access `%s' -- the device was not found."

#define CO_SYS_ERR_PERMISSION_DENIED   CO_CODE(CO_MODULE_SYS, EACCES)
#define CO_SYS_ER_FMT_PERMISSION_DENIED  \
  "Can't access `%s' -- permission was denied."

#define CO_SYS_ERR_BLOCK_REQUIRED      CO_CODE(CO_MODULE_SYS, ENOTBLK)
#define CO_SYS_ER_FMT_BLOCK_REQUIRED     \
  "Can't access `%s' -- a block device is required."

#define CO_SYS_ERR_BUSY                CO_CODE(CO_MODULE_SYS, EBUSY)
#define CO_SYS_ER_FMT_BUSY               \
  "Can't access `%s' -- the mount device is busy."

#define CO_SYS_ERR_EXISTS              CO_CODE(CO_MODULE_SYS, EEXIST)
#define CO_SYS_ER_FMT_EXISTS             \
  "Can't create `%s' -- the file already exists."

#define CO_SYS_ERR_CROSS_LINK          CO_CODE(CO_MODULE_SYS, EXDEV)
#define CO_SYS_ER_FMT_CROSS_LINK         \
  "Can't access `%s' -- it is a cross-device link."

#define CO_SYS_ERR_NO_DEVICE           CO_CODE(CO_MODULE_SYS, ENODEV)
#define CO_SYS_ER_FMT_NO_DEVICE          CO_SYS_ER_FMT_DEVICE_NOT_FOUND

#define CO_SYS_ERR_NOT_DIR             CO_CODE(CO_MODULE_SYS, ENOTDIR)
#define CO_SYS_ER_FMT_NOT_DIR            \
  "Can't access `%s' -- it is not a directory."

#define CO_SYS_ERR_IS_DIR              CO_CODE(CO_MODULE_SYS, EISDIR)
#define CO_SYS_ER_FMT_IS_DIR             \
  "Can't access `%s' -- it is a directory."

#define CO_SYS_ERR_NOT_TYPEWRITER      CO_CODE(CO_MODULE_SYS, ENOTTY)
#define CO_SYS_ER_FMT_NOT_TYPEWRITER     \
  "Can't access `%s' -- it is not a typewriter."

#define CO_SYS_ERR_TEXT_BUSY           CO_CODE(CO_MODULE_SYS, ETXTBSY)
#define CO_SYS_ER_FMT_TEXT_BUSY          \
  "Can't access `%s' -- the file is busy."

#define CO_SYS_ERR_FILE_TOO_BIG        CO_CODE(CO_MODULE_SYS, EFBIG)
#define CO_SYS_ER_FMT_FILE_TOO_BIG       \
  "The file `%s' is too large."

#define CO_SYS_ERR_INVALID_SEEK        CO_CODE(CO_MODULE_SYS, ESPIPE)
#define CO_SYS_ER_FMT_INVALID_SEEK       \
  "Can't seek to %d -- illegal seek."

#define CO_SYS_ERR_TOO_MANY_LINKS      CO_CODE(CO_MODULE_SYS, EMLINK)
#define CO_SYS_ER_FMT_TOO_MANY_LINKS     \
  "Can't access `%s' -- too many links were encountered."

#define CO_SYS_ERR_BAD_FILE_NO         CO_CODE(CO_MODULE_SYS, EBADF)
#define CO_SYS_ER_FMT_BAD_FILE_NO        \
  "The file number %d is invalid."

/* Process errors */
#define CO_SYS_ERR_PROCESS_NOT_FOUND   CO_CODE(CO_MODULE_SYS, ESRCH)
#define CO_SYS_ER_FMT_PROCESS_NOT_FOUND  \
  "Can't find process %d."

#define CO_SYS_ERR_PROCESS_NO_CHILDREN CO_CODE(CO_MODULE_SYS, ECHILD)
#define CO_SYS_ER_FMT_PROCESS_NO_CHILDREN \
  "The process %d has no children."

/* System resource errors */

#define CO_SYS_ERR_NO_MEMORY           CO_CODE(CO_MODULE_SYS, ENOMEM)
#define CO_SYS_ER_FMT_NO_MEMORY          \
  "Core memory is exhausted."

#define CO_SYS_ERR_FILE_TABLE_OVERFLOW CO_CODE(CO_MODULE_SYS, ENFILE)
#define CO_SYS_ER_FMT_FILE_TABLE_OVERFLOW \
  "Error opening `%s' -- the file table is full."

#define CO_SYS_ERR_TOO_MANY_FILES      CO_CODE(CO_MODULE_SYS, EMFILE)
#define CO_SYS_ER_FMT_TOO_MANY_FILES     \
  "Error opening `%s' -- there are too many open files."

#define CO_SYS_ERR_NO_SPACE            CO_CODE(CO_MODULE_SYS, ENOSPC)
#define CO_SYS_ER_FMT_NO_SPACE           \
  "Error writing `%s' -- no space left on device."

#define CO_SYS_ERR_READ_ONLY_FS        CO_CODE(CO_MODULE_SYS, EROFS)
#define CO_SYS_ER_FMT_READ_ONLY_FS       \
  "Error mounting `%s' -- the file system is read-only."

#define CO_SYS_ERR_MSG_ID_REMOVED      CO_CODE(CO_MODULE_SYS, EIDRM)
#define CO_SYS_ER_FMT_MSG_ID_REMOVED     \
  "The message id %d was removed."

#define CO_SYS_ERR_DEADLOCK            CO_CODE(CO_MODULE_SYS, EDEADLK)
#define CO_SYS_ER_FMT_DEADLOCK           \
  "A deadlock situation was detected and avoided."

#define CO_SYS_ERR_UNABLE_TO_LOCK      CO_CODE(CO_MODULE_SYS, ENOLCK)
#define CO_SYS_ER_FMT_UNABLE_TO_LOCK     \
  "Unable to lock file %d -- record table is full."

/* Miscellaneous */

#define CO_SYS_ERR_ARG_LIST_TOO_LONG   CO_CODE(CO_MODULE_SYS, E2BIG)
#define CO_SYS_ER_FMT_ARG_LIST_TOO_LONG  \
  "The argument list for %s exceeds %d bytes."

#define CO_SYS_ERR_BAD_ARG             CO_CODE(CO_MODULE_SYS, EINVAL)
#define CO_SYS_ER_FMT_BAD_ARG            \
  "Invalid argument."

#endif /* _CONDITION_HANDLER_ERR_H_ */
