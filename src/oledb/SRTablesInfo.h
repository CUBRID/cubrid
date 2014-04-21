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

class CTablesInfoRow
{
public:
	WCHAR m_szTableCatalog[1];
	WCHAR m_szTableSchema[1];
	WCHAR m_szTableName[256];
	WCHAR m_szTableType[16];
	GUID m_tableGUID;
	VARIANT_BOOL m_bBookmarks;
	int m_bBookmarkType;
	short m_bBookmarkDatatype;
	ULONG m_bBookmarkMaximumLength;
	ULONG m_bBookmarkInformation;
	ULARGE_INTEGER m_tableVersion;
	ULARGE_INTEGER m_ulCardinality;
	WCHAR m_szDescription[1];
	ULONG m_bTablePropID;

	CTablesInfoRow()
	{
		m_szTableCatalog[0] = NULL;
		m_szTableSchema[0] = NULL;
		m_szTableName[0] = NULL;
		m_szTableType[0] = NULL;
		m_bBookmarks = VARIANT_FALSE;
		m_bBookmarkType = 0;
		m_bBookmarkMaximumLength = 0;
		m_bBookmarkInformation = 0;
		m_tableVersion.QuadPart = 1;
		m_ulCardinality.QuadPart = 0;
		m_szDescription[0] = NULL;
		m_bTablePropID = 0;
	}

BEGIN_PROVIDER_COLUMN_MAP(CTablesInfoRow)
	PROVIDER_COLUMN_ENTRY_WSTR("TABLE_CATALOG", 1, m_szTableCatalog)
	PROVIDER_COLUMN_ENTRY_WSTR("TABLE_SCHEMA", 2, m_szTableSchema)
	PROVIDER_COLUMN_ENTRY_WSTR("TABLE_NAME", 3, m_szTableName)
	PROVIDER_COLUMN_ENTRY_WSTR("TABLE_TYPE", 4, m_szTableType)
	PROVIDER_COLUMN_ENTRY_TYPE_PS("TABLE_GUID", 5, DBTYPE_GUID, 0xFF, 0xFF, m_tableGUID)
	PROVIDER_COLUMN_ENTRY_TYPE_PS("BOOKMARKS", 6, DBTYPE_BOOL, 0xFF, 0xFF, m_bBookmarks)
	PROVIDER_COLUMN_ENTRY_TYPE_PS("BOOKMARK_TYPE", 7, DBTYPE_I4, 10, ~0, m_bBookmarkType)
	PROVIDER_COLUMN_ENTRY_TYPE_PS("BOOKMARK_DATATYPE", 8, DBTYPE_UI2, 5, ~0, m_bBookmarkDatatype)
	PROVIDER_COLUMN_ENTRY_TYPE_PS("BOOKMARK_MAXIMUM_LENGTH", 9, DBTYPE_UI4, 10, ~0, m_bBookmarkMaximumLength)
	PROVIDER_COLUMN_ENTRY_TYPE_PS("BOOKMARK_INFORMATION", 10, DBTYPE_UI4, 10, ~0, m_bBookmarkInformation)
	PROVIDER_COLUMN_ENTRY_TYPE_PS("TABLE_VERSION", 11, DBTYPE_I8, 19, ~0, m_tableVersion)
	PROVIDER_COLUMN_ENTRY_TYPE_PS("CARDINALITY", 12, DBTYPE_UI8, 20, ~0, m_ulCardinality)
	PROVIDER_COLUMN_ENTRY_WSTR("DESCRIPTION", 13, m_szDescription)
	PROVIDER_COLUMN_ENTRY_TYPE_PS("TABLE_PROPID", 14, DBTYPE_UI4, 10, ~0, m_bTablePropID)
END_PROVIDER_COLUMN_MAP()
};

class CSRTablesInfo :
	public CSchemaRowsetImpl<CSRTablesInfo, CTablesInfoRow, CCUBRIDSession>
{
public:
	~CSRTablesInfo();

	SR_PROPSET_MAP(CSRTablesInfo)

	HRESULT Execute(LONG* pcRowsAffected, ULONG cRestrictions, const VARIANT *rgRestrictions);
	DBSTATUS GetDBStatus(CSimpleRow*, ATLCOLUMNINFO* pInfo);
};