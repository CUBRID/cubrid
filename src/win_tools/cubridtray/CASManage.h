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

// CASManage.h: interface for the CUniCASManage class.
//
//////////////////////////////////////////////////////////////////////

#if !defined(AFX_UNICASMANAGE_H__359FF1E2_13DD_4BE7_BADC_D9E7C0E4848A__INCLUDED_)
#define AFX_UNICASMANAGE_H__359FF1E2_13DD_4BE7_BADC_D9E7C0E4848A__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

class CUniCASManage  
{
private:
	bool bUNICAS;
public:
	CUniCASManage();
	virtual ~CUniCASManage();

	bool bStatusUniCAS();
	bool bCheckUniCAS();

	bool bStopUniCAS();
	bool bStartUniCAS();

	bool bRestartUniCAS();

	bool bInstallStatus();
};

#endif // !defined(AFX_UNICASMANAGE_H__359FF1E2_13DD_4BE7_BADC_D9E7C0E4848A__INCLUDED_)
