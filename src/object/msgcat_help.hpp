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
 * msgcat_help.hpp
 */

#ifndef _MSGCAT_HELP_HPP_
#define _MSGCAT_HELP_HPP_

#if defined(SERVER_MODE)
#error Does not belong to server module
#endif //defined(SERVER_MODE)

/*
 * Message id in the set MSGCAT_SET_HELP
 * in the message catalog MSGCAT_CATALOG_CUBRID (file cubrid.msg).
 */

//ToDo: enum
#define MSGCAT_HELP_ROOTCLASS_TITLE     (1)
#define MSGCAT_HELP_CLASS_TITLE         (2)
#define MSGCAT_HELP_SUPER_CLASSES       (3)
#define MSGCAT_HELP_SUB_CLASSES         (4)
#define MSGCAT_HELP_ATTRIBUTES          (5)
#define MSGCAT_HELP_METHODS             (6)
#define MSGCAT_HELP_CLASS_ATTRIBUTES    (7)
#define MSGCAT_HELP_CLASS_METHODS       (8)
#define MSGCAT_HELP_RESOLUTIONS         (9)
#define MSGCAT_HELP_METHOD_FILES        (10)
#define MSGCAT_HELP_QUERY_SPEC          (11)
#define MSGCAT_HELP_OBJECT_TITLE        (12)
#define MSGCAT_HELP_CMD_DESCRIPTION     (13)
#define MSGCAT_HELP_CMD_STRUCTURE       (14)
#define MSGCAT_HELP_CMD_EXAMPLE         (15)
#define MSGCAT_HELP_META_CLASS_HEADER   (16)
#define MSGCAT_HELP_CLASS_HEADER        (17)
#define MSGCAT_HELP_VCLASS_HEADER       (18)
#define MSGCAT_HELP_LDB_VCLASS_HEADER   (19)
#define MSGCAT_HELP_GENERAL_TXT         (20)

#endif // _MSGCAT_HELP_HPP_
