#include <StdAfx.h>
#include <afxtempl.h>
#include "ORDBList.h"
#include "env.h"
#include "message.h"
#include <time.h>


extern CEnv theEnv;
/////////////////////////////////////////////////////////////////////////////////////////
//
//
CORDBList::CORDBList()
{
	sprintf(m_ordblist, "%s\\%s", theEnv.GetCUBRID_DATABASES(), FILE_ORDBLIST);
	sprintf(m_ordbinfo, "%s\\%s", theEnv.GetCUBRID_DATABASES(), FILE_ORDBINFO);
	m_LastTime = (time_t) 0;
}

CORDBList::~CORDBList()
{
CDBInfo	*db;

	while (!m_List.IsEmpty()) {
		db = (CDBInfo *)m_List.RemoveHead();
		delete db;
	}

}

bool CORDBList::ReadDBInfo()
{
CDBInfo			*db;
CStdioFile		listfile, infofile;
CFileStatus		fstatus;
CFileException	e;
CPtrList		plist;
char			data[1025];
char			*tmp;


	if (infofile.Open(m_ordbinfo, CFile::modeRead)) {
		while (1) {
			memset(data, 0x00, 1024);
			TRY {
				if (infofile.ReadString(data, 1024) == NULL) break;
				data[strlen(data)-1] = NULL;

				db = new CDBInfo();
				tmp = strtok(data, " ");	// db name
				if (tmp) {
					db->m_dbname.Format("%s", tmp);
				}
				tmp = strtok(NULL, " "); // permanent
				if (tmp) {
					if (!strcmp(tmp, "1")) db->m_permanent = true;
					else				  db->m_permanent = false;
				} 
				plist.AddTail(db);
			}
			CATCH_ALL(e)
			{
				break;
			}
			END_CATCH_ALL
		}
		infofile.Close();
	} 

	if (listfile.Open(m_ordblist, CFile::modeRead)) {
		listfile.GetStatus(fstatus);
		m_LastTime = fstatus.m_mtime;
	} else {
		return false;
	}

	while (1) {
		memset(data, 0x00, 1024);
		TRY {
			if (listfile.ReadString(data, 1024) == NULL) break;
			data[strlen(data)-1] = NULL;

			db = new CDBInfo();
			tmp = strtok(data, " ");	// db name
			if (tmp) {
				db->m_dbname.Format("%s", tmp);
			}
			tmp = strtok(NULL, " "); // db path
			if (tmp) {
				db->m_dbpath.Format("%s", tmp);
			}
			tmp = strtok(NULL, " "); // host
			if (tmp) {
				db->m_host.Format("%s", tmp);
			}
			tmp = strtok(NULL, " "); // log path
			if (tmp) {
				db->m_logpath.Format("%s", tmp);
			}
			int cnt = plist.GetCount();
			if (cnt > 0) {
				for (int i = 0; i < cnt; i++) {
					CDBInfo *p = (CDBInfo *)plist.GetAt(plist.FindIndex(i));
					if (db->m_dbname == p->m_dbname) {
						db->m_permanent = p->m_permanent;
						break;
					}
				}
			}
			m_List.AddTail(db);
		}
		CATCH_ALL(e)
		{
			break;
		}
		END_CATCH_ALL
	}

	listfile.Close();

	while (!plist.IsEmpty()) {
		CDBInfo *p = (CDBInfo *)plist.RemoveHead();
		delete p;
	}

	return true;
}

bool CORDBList::ReReadDBInfo()
{
CDBInfo			*db;
CFileStatus		fstatus;

	if (!CFile::GetStatus(m_ordblist, fstatus)) return false;
	if (m_LastTime >= fstatus.m_mtime) return true;

	while (!m_List.IsEmpty()) {
		db = (CDBInfo *)m_List.RemoveHead();
		delete db;
	}

	return ReadDBInfo();
}
/*
bool CORDBList::WriteDBInfo()
{
CDBInfo *db;
CString	element;
int		count;

	if (m_List.IsEmpty()) return false;

	TRY {
		m_File.Open(m_ordblist, CFile::modeWrite);
	} 
	CATCH_ALL(e)
	{
		m_File.Abort();
		return false;
	}
	END_CATCH_ALL

	count = m_List.GetCount();
	for (int i = 0; i < count; i++) {
		db = (CDBInfo *)m_List.GetAt(m_List.FindIndex(i));
		element.Format("%s %s %s %s %s\n", db->m_dbname, db->m_dbpath, db->m_host, db->m_logpath, db->m_permanent ? "1" : "0");
		m_File.WriteString(LPCSTR(element));
	}

	m_File.Close();

	return false;
}
*/
bool CORDBList::WriteDBInfo()
{
CDBInfo *db;
CStdioFile infofile;
CString	element;
CFileException	e;
int		count;

	if (m_List.IsEmpty()) return false;

	if (infofile.Open(m_ordbinfo, CFile::modeCreate | CFile::modeWrite, &e)) {
		count = m_List.GetCount();
		for (int i = 0; i < count; i++) {
			db = (CDBInfo *)m_List.GetAt(m_List.FindIndex(i));
			if (!db->m_permanent) continue;
			element.Format("%s %s\n", db->m_dbname, db->m_permanent ? "1" : "0");
			infofile.WriteString(LPCSTR(element));
		}
		infofile.Close();
	} else {
		return false;
	}

	return true;
}

bool CORDBList::GetPermanentField(CString dbname)
{
CDBInfo *db;
int	count;

	if (m_List.IsEmpty()) return false;

	count = m_List.GetCount();
	for (int i = 0; i < count; i++) {
		db = (CDBInfo *)m_List.GetAt(m_List.FindIndex(i));
		if (db->m_dbname == dbname) {
			return	db->m_permanent;
		}
	}

	return false;
}

void CORDBList::SetPermanentField(CString dbname, bool permanent)
{
CDBInfo *db;
int	count;

	if (m_List.IsEmpty()) return;

	count = m_List.GetCount();
	for (int i = 0; i < count; i++) {
		db = (CDBInfo *)m_List.GetAt(m_List.FindIndex(i));
		if (db->m_dbname == dbname) {
			db->m_permanent = permanent;
		}
	}

	return;
}

/////////////////////////////////////////////////////////////////////////////////////////
//
//
CDBInfo::CDBInfo()
{
	m_dbname.Empty();
	m_dbpath.Empty();
	m_host.Empty();
	m_logpath.Empty();
	m_permanent = false;
}

CDBInfo::~CDBInfo()
{
}
