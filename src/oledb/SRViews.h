/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution. 
 *
 * Redistribution and use in source and binary forms, with or without modification, 
 * are permitted provided that the following conditions are met: 
 *
 * - Redistributions of source code must retain the above copyright notice, 
 *   this list of conditions and the following disclaimer. 
 *
 * - Redistributions in binary form must reproduce the above copyright notice, 
 *   this list of conditions and the following disclaimer in the documentation 
 *   and/or other materials provided with the distribution. 
 *
 * - Neither the name of the <ORGANIZATION> nor the names of its contributors 
 *   may be used to endorse or promote products derived from this software without 
 *   specific prior written permission. 
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND 
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED 
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. 
 * IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, 
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, 
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, 
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, 
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) 
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY 
 * OF SUCH DAMAGE. 
 *
 */

#pragma once

class CViewsRow
{
public:
	WCHAR m_szTableCatalog[1];
	WCHAR m_szTableSchema[1];
	WCHAR m_szTableName[256];
	WCHAR m_szViewDefinition[5000];
	VARIANT_BOOL m_bCheckOption;
	VARIANT_BOOL m_bIsUpdatable;
	WCHAR m_szDescription[1];
	DATE m_szDateCreated;
	DATE m_szDateModified;
	
	CViewsRow()
	{
		m_szTableCatalog[0] = NULL;
		m_szTableSchema[0] = NULL;
		m_szTableName[0] = NULL;
		m_szViewDefinition[0] = NULL;
		m_bIsUpdatable = ATL_VARIANT_FALSE;
		m_szDescription[0] = NULL;
	}

BEGIN_PROVIDER_COLUMN_MAP(CViewsRow)
	PROVIDER_COLUMN_ENTRY_WSTR("TABLE_CATALOG", 1, m_szTableCatalog)
	PROVIDER_COLUMN_ENTRY_WSTR("TABLE_SCHEMA", 2, m_szTableSchema)
	PROVIDER_COLUMN_ENTRY_WSTR("TABLE_NAME", 3, m_szTableName)
	PROVIDER_COLUMN_ENTRY_WSTR("VIEW_DEFINITION", 4, m_szViewDefinition)
	PROVIDER_COLUMN_ENTRY_TYPE_PS("CHECK_OPTION", 5, DBTYPE_BOOL, 0xFF, 0xFF, m_bCheckOption)
	PROVIDER_COLUMN_ENTRY_TYPE_PS("IS_UPDATABLE", 6, DBTYPE_BOOL, 0xFF, 0xFF, m_bIsUpdatable)
	PROVIDER_COLUMN_ENTRY_WSTR("DESCRIPTION", 7, m_szDescription)
	PROVIDER_COLUMN_ENTRY_TYPE_PS("DATE_CREATED", 8, DBTYPE_DATE, 0xFF, 0xFF, m_szDateCreated)
	PROVIDER_COLUMN_ENTRY_TYPE_PS("DATE_MODIFIED", 9, DBTYPE_DATE, 0xFF, 0xFF, m_szDateModified)
END_PROVIDER_COLUMN_MAP()
};

class CSRViews :
	public CSchemaRowsetImpl<CSRViews, CViewsRow, CCUBRIDSession>
{
public:
	~CSRViews();

	SR_PROPSET_MAP(CSRViews)

	HRESULT Execute(LONG* pcRowsAffected, ULONG cRestrictions, const VARIANT *rgRestrictions);
	DBSTATUS GetDBStatus(CSimpleRow*, ATLCOLUMNINFO* pInfo);
};
