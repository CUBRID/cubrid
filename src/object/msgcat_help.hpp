/*
 * Copyright 2008 Search Solution Corporation
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
