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

#ifndef ORACLEMOCK_H_
#define ORACLEMOCK_H_

#include "dbgw3/client/Mock.h"

#ifdef BUILD_MOCK
#define OCIEnvCreate OCIMockEnvCreate
#define OCIHandleAlloc OCIMockHandleAlloc
#define OCILogon OCIMockLogon
#define OCILogoff OCIMockLogoff
#define OCIDescriptorAlloc OCIMockDescriptorAlloc
#define OCILobRead OCIMockLobRead
#define OCILobCreateTemporary OCIMockLobCreateTemporary
#define OCILobWrite OCIMockLobWrite
#define OCITransCommit OCIMockTransCommit
#define OCITransRollback OCIMockTransRollback
#define OCINumberToText OCIMockNumberToText
#define OCIDateTimeGetTime OCIMockDateTimeGetTime
#define OCIDateTimeGetDate OCIMockDateTimeGetDate
#define OCIRowidToChar OCIMockRowidToChar
#define OCIBindByPos OCIMockBindByPos
#define OCIStmtPrepare OCIMockStmtPrepare
#define OCIStmtExecute OCIMockStmtExecute
#define OCIParamGet OCIMockParamGet
#define OCIAttrGet OCIMockAttrGet
#define OCIDefineByPos OCIMockDefineByPos
#define OCIStmtFetch2 OCIMockStmtFetch2
#endif

#endif
