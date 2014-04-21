#if !defined(__LANGUAGE)
#define	__LANGUAGE
#include "message.h"

/////////////////////////////////////////////////////////////////////////////
// CLang

class CLang {
// Construction
public:
	CLang();
	~CLang();
	bool	ReadMessage(char *CUBRID, char *lang);
	int		GetMenu();
	CString	GetMessage(int msgnum);

public:
	int		m_menu;
	CString	m_message[MAXMSG];
};

#endif