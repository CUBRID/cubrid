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
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA 
 *
 */

// ToolManage.h: interface for the CUniToolManage class.
//
//////////////////////////////////////////////////////////////////////

#if !defined(AFX_UNITOOLMANAGE_H__058665D6_5812_4FE1_8EE2_647FC440052E__INCLUDED_)
#define AFX_UNITOOLMANAGE_H__058665D6_5812_4FE1_8EE2_647FC440052E__INCLUDED_



#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000


//#include "unitray_comm.h"


class CUniToolManage  
{
public:
	CUniToolManage();
	virtual ~CUniToolManage();

	char * cubridmanager_path;

	bool bStartVSQL();
	bool bStartEasyManage();

	bool bCheckInstallVSQL();
	bool bCheckInstallEasyManage();
};

#endif // !defined(AFX_UNITOOLMANAGE_H__058665D6_5812_4FE1_8EE2_647FC440052E__INCLUDED_)




