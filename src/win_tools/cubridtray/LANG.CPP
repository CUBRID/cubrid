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

	if (!strnicmp(lang, "ko_KR", 4)) {
		m_menu = IDR_POPUP_KOREAN;
	} else if (!strnicmp(lang, "en_US", 4)) {
		m_menu = IDR_POPUP_ENGLISH;
	} else {
		m_menu = IDR_POPUP_ENGLISH;
	}

	filename.Format("%s\\msg\\%s\\cubridtray.msg", cubrid, lang);
	
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
