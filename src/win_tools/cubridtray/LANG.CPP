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
#include <stdafx.h>
#include "resource.h"
#include "lang.h"

CLang::CLang()
{
}

CLang::~CLang()
{
}

bool CLang::ReadMessage(char *cubrid, char *lang)
{
CString	filename;	
CStdioFile	file;
CString	tmpStr, tmpMsg;
int		ptr, count;

	if (!_strnicmp(lang, "ko_KR.euckr", 11) ||
		!_strnicmp(lang, "ko_KR.utf8", 10)) {
		m_menu = IDR_POPUP_KOREAN;
		filename.Format("%s\\msg\\%s\\cubridtray.msg", cubrid, lang);
	} else {
		m_menu = IDR_POPUP_ENGLISH;
		filename.Format("%s\\msg\\en_US\\cubridtray.msg", cubrid);
	}
	
	if (file.Open(filename, CStdioFile::modeRead)) {
		while (file.ReadString(tmpStr)) {
			tmpStr.TrimLeft();
			tmpStr.TrimRight();
			if (tmpStr.IsEmpty() || tmpStr.GetAt(0) == '#') continue;
			ptr = tmpStr.Find(" ");
			count = atoi(LPCSTR(tmpStr.Left(ptr)));
			m_message[count-1] = tmpStr.Mid(ptr+1, tmpStr.GetLength());
			m_message[count-1].TrimLeft();
			m_message[count-1].Replace("/n", "\r\n");
			m_message[count-1].Replace("/t", "\t");
		}
		file.Close();
		return true;
	} else {
		return false;
	}
}


CString CLang::GetMessage(int msgnum)
{
	return m_message[msgnum-1];
}

int CLang::GetMenu()
{
	return m_menu;
}
