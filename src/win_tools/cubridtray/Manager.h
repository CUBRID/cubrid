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

// Manager.h: interface for the CEasyManager class.
//
//////////////////////////////////////////////////////////////////////

#if !defined(AFX_EASYMANAGER_H__B3EFB8FE_D30C_4287_A318_169F43DBE8EE__INCLUDED_)
#define AFX_EASYMANAGER_H__B3EFB8FE_D30C_4287_A318_169F43DBE8EE__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

//#include "ems.h"
//#include "OptionDialog.h"	// Added by ClassView
//#include "LogDialog.h"
//#include "resource.h"       // main symbols

class CEasyManager  
{
private:
	bool bCheckEMSAuto();
	bool bCheckEMSJS();

public:
	bool bEasyManagerServerCheckOnly();
	CEasyManager();
	virtual ~CEasyManager();

	bool bInstallStatus();

	bool bCheckEasyManagerServer();

	bool bStartEasyManagerServer();
	bool bStopEasyManagerServer();
};

#endif // !defined(AFX_EASYMANAGER_H__B3EFB8FE_D30C_4287_A318_169F43DBE8EE__INCLUDED_)



