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

class CStatisticsRow
{
public:
	WCHAR m_szTableCatalog[1];
	WCHAR m_szTableSchema[1];
	WCHAR m_szTableName[256];
	ULARGE_INTEGER m_ulCardinality;
	
	CStatisticsRow()
	{
		m_szTableCatalog[0] = NULL;
		m_szTableSchema[0] = NULL;
		m_szTableName[0] = NULL;
		m_ulCardinality.QuadPart = 0;
	}

BEGIN_PROVIDER_COLUMN_MAP(CStatisticsRow)
	PROVIDER_COLUMN_ENTRY_WSTR("TABLE_CATALOG", 1, m_szTableCatalog)
	PROVIDER_COLUMN_ENTRY_WSTR("TABLE_SCHEMA", 2, m_szTableSchema)
	PROVIDER_COLUMN_ENTRY_WSTR("TABLE_NAME", 3, m_szTableName)
	PROVIDER_COLUMN_ENTRY_TYPE_PS("CARDINALITY", 4, DBTYPE_UI8, 20, ~0, m_ulCardinality)
END_PROVIDER_COLUMN_MAP()
};

class CSRStatistics :
	public CSchemaRowsetImpl<CSRStatistics, CStatisticsRow, CCUBRIDSession>
{
public:
	~CSRStatistics();

	SR_PROPSET_MAP(CSRStatistics)

	HRESULT Execute(LONG* pcRowsAffected, ULONG cRestrictions, const VARIANT *rgRestrictions);
	DBSTATUS GetDBStatus(CSimpleRow*, ATLCOLUMNINFO* pInfo);
};