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

class CIndexesRow
{
public:
	WCHAR m_szTableCatalog[1];
	WCHAR m_szTableSchema[1];
	WCHAR m_szTableName[256];
	WCHAR m_szIndexCatalog[1];
	WCHAR m_szIndexSchema[1];
	WCHAR m_szIndexName[256];
	VARIANT_BOOL m_bPrimaryKey;
	VARIANT_BOOL m_bUnique;
	VARIANT_BOOL m_bClustered;
	USHORT m_uType;
	int m_FillFactor;
	int m_InitialSize;
	int m_Nulls;
	VARIANT_BOOL m_bSortBookmarks;
	VARIANT_BOOL m_bAutoUpdate;
	int m_NullCollation;
	UINT m_uOrdinalPosition;
	WCHAR m_szColumnName[256];
	GUID m_ColumnGUID;
	UINT m_uColumnPropID;
	short m_Collation;
	ULARGE_INTEGER m_ulCardinality;
	int m_Pages;
	WCHAR m_szFilterCondition[1];
	VARIANT_BOOL m_bIntegrated;
	
	CIndexesRow()
	{
		m_szTableCatalog[0] = NULL;
		m_szTableSchema[0] = NULL;
		m_szTableName[0] = NULL;
		m_szIndexCatalog[0] = NULL;
		 m_szIndexSchema[0] = NULL;
		m_szIndexName[0] = NULL;
		m_bPrimaryKey = ATL_VARIANT_FALSE;
		m_bUnique = ATL_VARIANT_FALSE;
		m_bClustered = ATL_VARIANT_FALSE;
		m_uType = DBPROPVAL_IT_OTHER;
		m_FillFactor = 0;
		m_InitialSize = 0;
		m_Nulls = DBPROPVAL_IN_ALLOWNULL;
		m_bSortBookmarks = ATL_VARIANT_FALSE;
		m_bAutoUpdate = ATL_VARIANT_FALSE;
		m_NullCollation = DBPROPVAL_NC_START;
		m_uOrdinalPosition = 0;
		m_Collation = DB_COLLATION_ASC;
		m_ulCardinality.QuadPart = 0;
		m_szColumnName[0] = NULL;
		m_Pages = 0;
		m_szFilterCondition[0] = NULL;
		m_bIntegrated = ATL_VARIANT_FALSE;
	}

BEGIN_PROVIDER_COLUMN_MAP(CIndexesRow)
	PROVIDER_COLUMN_ENTRY_WSTR("TABLE_CATALOG", 1, m_szTableCatalog)
	PROVIDER_COLUMN_ENTRY_WSTR("TABLE_SCHEMA", 2, m_szTableSchema)
	PROVIDER_COLUMN_ENTRY_WSTR("TABLE_NAME", 3, m_szTableName)
	PROVIDER_COLUMN_ENTRY_WSTR("INDEX_CATALOG", 4, m_szIndexCatalog)
	PROVIDER_COLUMN_ENTRY_WSTR("INDEX_SCHEMA", 5, m_szIndexSchema)
	PROVIDER_COLUMN_ENTRY_WSTR("INDEX_NAME", 6, m_szIndexName)
	PROVIDER_COLUMN_ENTRY_TYPE_PS("PRIMARY_KEY", 7, DBTYPE_BOOL, 0xFF, 0xFF, m_bPrimaryKey)
	PROVIDER_COLUMN_ENTRY_TYPE_PS("UNIQUE", 8, DBTYPE_BOOL, 0xFF, 0xFF, m_bUnique)
	PROVIDER_COLUMN_ENTRY_TYPE_PS("CLUSTERED", 9, DBTYPE_BOOL, 0xFF, 0xFF, m_bClustered)
	PROVIDER_COLUMN_ENTRY_TYPE_PS("TYPE", 10, DBTYPE_UI2, 5, ~0, m_uType)
	PROVIDER_COLUMN_ENTRY_TYPE_PS("FILL_FACTOR", 11, DBTYPE_I4, 10, ~0, m_FillFactor)
	PROVIDER_COLUMN_ENTRY_TYPE_PS("INITIAL_SIZE", 12, DBTYPE_I4, 10, ~0, m_InitialSize)
	PROVIDER_COLUMN_ENTRY_TYPE_PS("NULLS", 13, DBTYPE_I4, 10, ~0, m_Nulls)
	PROVIDER_COLUMN_ENTRY_TYPE_PS("SORT_BOOKMARKS", 14, DBTYPE_BOOL, 0xFF, 0xFF, m_bSortBookmarks)
	PROVIDER_COLUMN_ENTRY_TYPE_PS("AUTO_UPDATE", 15, DBTYPE_BOOL, 0xFF, 0xFF, m_bAutoUpdate)
	PROVIDER_COLUMN_ENTRY_TYPE_PS("NULL_COLLATION", 16, DBTYPE_I4, 10, ~0, m_NullCollation)
	PROVIDER_COLUMN_ENTRY_TYPE_PS("ORDINAL_POSITION", 17, DBTYPE_UI4, 10, ~0, m_uOrdinalPosition)
	PROVIDER_COLUMN_ENTRY_WSTR("COLUMN_NAME", 18, m_szColumnName)
	PROVIDER_COLUMN_ENTRY_TYPE_PS("COLUMN_GUID", 19, DBTYPE_GUID, 0xFF, 0xFF, m_ColumnGUID)
	PROVIDER_COLUMN_ENTRY_TYPE_PS("COLUMN_PROPID", 20, DBTYPE_UI4, 10, ~0, m_uColumnPropID)
	PROVIDER_COLUMN_ENTRY_TYPE_PS("COLLATION", 21, DBTYPE_I2, 5, ~0, m_Collation)
	PROVIDER_COLUMN_ENTRY_TYPE_PS("CARDINALITY", 22, DBTYPE_UI8, 20, ~0, m_ulCardinality)
	PROVIDER_COLUMN_ENTRY_TYPE_PS("PAGES", 23, DBTYPE_I4, 10, ~0, m_Pages)
	PROVIDER_COLUMN_ENTRY_WSTR("FILTER_CONDITION", 24, m_szFilterCondition)
	PROVIDER_COLUMN_ENTRY_TYPE_PS("INTEGRATED", 25, DBTYPE_BOOL, 0xFF, 0xFF, m_bIntegrated)
END_PROVIDER_COLUMN_MAP()
};

class CSRIndexes :
	public CSchemaRowsetImpl<CSRIndexes, CIndexesRow, CCUBRIDSession>
{
public:
	~CSRIndexes();

	SR_PROPSET_MAP(CSRIndexes)

	HRESULT FillRowData(int hConn, int hReq, LONG* pcRowsAffected, const char *table_name, const char *index_name);
	HRESULT Execute(LONG* pcRowsAffected, ULONG cRestrictions, const VARIANT *rgRestrictions);
	DBSTATUS GetDBStatus(CSimpleRow*, ATLCOLUMNINFO* pInfo);
};