/*
Module : NTRAY.CPP
Purpose: implementation for a MFC class to encapsulate Shell_NotifyIcon
Created: PJN / NOTSHELL/1 / 14-05-1997
History: None

Copyright (c) 1997 by PJ Naughter.  
All rights reserved.

*/

/////////////////////////////////  Includes  //////////////////////////////////
#include "stdafx.h"
#include "resource.h"
#include "ntray.h"



/////////////////////////////////  Macros /////////////////////////////////////

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

#define MAX_TRAY_REPEAT_COUNT 15
IMPLEMENT_DYNAMIC(CTrayNotifyIcon, CObject)



///////////////////////////////// Implementation //////////////////////////////

CTrayNotifyIcon::CTrayNotifyIcon()
{
  memset(&m_NotifyIconData, 0, sizeof(m_NotifyIconData));
  m_bCreated = FALSE;
  m_pWnd = NULL;
  bSetIconFailed = FALSE;
}


CTrayNotifyIcon::~CTrayNotifyIcon()
{
  RemoveIcon();
}


void CTrayNotifyIcon::RemoveIcon()
{
  if (m_bCreated)
  {
    m_NotifyIconData.uFlags = 0;
    Shell_NotifyIcon(NIM_DELETE, &m_NotifyIconData);
    m_bCreated = FALSE;
  }
}


BOOL CTrayNotifyIcon::Create(CWnd* pNotifyWnd, UINT uID, LPCTSTR pszTooltipText, HICON hIcon, UINT nNotifyMessage)
{
  int rVal, repeat_count;
  DWORD ErrorCode;

  //Make sure Notification window is valid
  ASSERT(pNotifyWnd && ::IsWindow(pNotifyWnd->GetSafeHwnd()));
  m_pWnd = pNotifyWnd;

  //Make sure we avoid conflict with other messages
  ASSERT(nNotifyMessage >= WM_USER);

  //Tray only supports tooltip text up to 64 characters
  ASSERT(_tcslen(pszTooltipText) <= 64);

  m_NotifyIconData.cbSize = sizeof(m_NotifyIconData);
  m_NotifyIconData.hWnd = pNotifyWnd->GetSafeHwnd();
  m_NotifyIconData.uID = uID;
  m_NotifyIconData.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
  m_NotifyIconData.uCallbackMessage = nNotifyMessage;
  m_NotifyIconData.hIcon = hIcon;
  _tcscpy(m_NotifyIconData.szTip, pszTooltipText);

  repeat_count = 0;
  rVal = Shell_NotifyIcon(NIM_ADD, &m_NotifyIconData);
  ErrorCode = GetLastError();
  if ((rVal == 0) && (ErrorCode == ERROR_TIMEOUT || ErrorCode == ERROR_FILE_NOT_FOUND || ErrorCode == ERROR_SUCCESS )) {
	  do {
		  repeat_count++;
		  Sleep(2000);
		  rVal = Shell_NotifyIcon(NIM_MODIFY, &m_NotifyIconData);
	  } while ((rVal==0) && (repeat_count <= MAX_TRAY_REPEAT_COUNT));
  }

  if (rVal == 0) {
	  DWORD format_retval;
	  LPVOID lpMsgBuf;
	  format_retval = 
		  FormatMessage( 
		  FORMAT_MESSAGE_ALLOCATE_BUFFER | 
		  FORMAT_MESSAGE_FROM_SYSTEM | 
		  FORMAT_MESSAGE_IGNORE_INSERTS,
		  NULL,
		  GetLastError(),
		  0, // Default language
		  (LPTSTR) &lpMsgBuf,
		  0,
		  NULL 
		  );

	  if (format_retval > 0) {
		  m_NotifyMsg = (LPTSTR)lpMsgBuf;
		  LocalFree( lpMsgBuf );
	  }
	  else {
		  m_NotifyMsg = "";
	  }
  }

  m_bCreated = rVal;
  return rVal;
}


BOOL CTrayNotifyIcon::SetTooltipText(LPCTSTR pszTooltipText)
{
  if (!m_bCreated)
    return FALSE;

  m_NotifyIconData.uFlags = NIF_TIP;
  _tcscpy(m_NotifyIconData.szTip, pszTooltipText);

  return Shell_NotifyIcon(NIM_MODIFY, &m_NotifyIconData);
}


BOOL CTrayNotifyIcon::SetTooltipText(UINT nID)
{
  CString sToolTipText;
  VERIFY(sToolTipText.LoadString(nID));

  return SetTooltipText(sToolTipText);
}


BOOL CTrayNotifyIcon::SetIcon(HICON hIcon)
{
  BOOL ret_val;

  if (!m_bCreated)
    return FALSE;

  m_NotifyIconData.uFlags = NIF_ICON;
  m_NotifyIconData.hIcon = hIcon;

  ret_val = Shell_NotifyIcon(NIM_MODIFY, &m_NotifyIconData);

  if (ret_val == FALSE) {
	  bSetIconFailed = TRUE;
  }
  else {
	  bSetIconFailed = FALSE;
  }

  return ret_val;
}


BOOL CTrayNotifyIcon::SetIcon(LPCTSTR lpIconName)
{
  HICON hIcon = AfxGetApp()->LoadIcon(lpIconName);

  return SetIcon(hIcon);
}


BOOL CTrayNotifyIcon::SetIcon(UINT nIDResource)
{
  HICON hIcon = AfxGetApp()->LoadIcon(nIDResource);

  return SetIcon(hIcon);
}


BOOL CTrayNotifyIcon::SetStandardIcon(LPCTSTR lpIconName)
{
  HICON hIcon = LoadIcon(NULL, lpIconName);

  return SetIcon(hIcon);
}


BOOL CTrayNotifyIcon::SetStandardIcon(UINT nIDResource)
{
  HICON hIcon = LoadIcon(NULL, MAKEINTRESOURCE(nIDResource));

  return SetIcon(hIcon);
}
 

BOOL CTrayNotifyIcon::SetNotificationWnd(CWnd* pNotifyWnd)
{
  if (!m_bCreated)
    return FALSE;

  //Make sure Notification window is valid
  ASSERT(pNotifyWnd && ::IsWindow(pNotifyWnd->GetSafeHwnd()));

  m_pWnd = pNotifyWnd;
  m_NotifyIconData.hWnd = pNotifyWnd->GetSafeHwnd();
  m_NotifyIconData.uFlags = 0;

  return Shell_NotifyIcon(NIM_MODIFY, &m_NotifyIconData);
}


CString CTrayNotifyIcon::GetTooltipText() const
{
  CString sText;
  if (m_bCreated)
    sText = m_NotifyIconData.szTip;

  return sText;
}


HICON CTrayNotifyIcon::GetIcon() const
{
  HICON hIcon = NULL;
  if (m_bCreated)
    hIcon = m_NotifyIconData.hIcon;

  return hIcon;
}


CWnd* CTrayNotifyIcon::GetNotificationWnd() const
{
  return m_pWnd;
}


LRESULT CTrayNotifyIcon::OnTrayNotification(WPARAM wID, LPARAM lEvent)
{
  //Return quickly if its not for this tray icon
  if (wID != m_NotifyIconData.uID)
    return 0L;

  //As a default action use a menu resource with the same id 
  //as this was created with
  CMenu menu;
  if (!menu.LoadMenu(m_NotifyIconData.uID))
    return 0;

  CMenu* pSubMenu = menu.GetSubMenu(0);
  if (!pSubMenu) 
    return 0;

  if (lEvent == WM_RBUTTONUP)
  {
    //Clicking with right button brings up a context menu

    // Make first menu item the default (bold font)
    ::SetMenuDefaultItem(pSubMenu->m_hMenu, 0, TRUE);

    //Display and track the popup menu
    CPoint pos;
    GetCursorPos(&pos);
    ::SetForegroundWindow(m_NotifyIconData.hWnd);  
    ::TrackPopupMenu(pSubMenu->m_hMenu, 0, pos.x, pos.y, 0, m_NotifyIconData.hWnd, NULL);

  } 
  else if (lEvent == WM_LBUTTONDBLCLK) 
  {
    // double click received, the default action is to execute first menu item
    ::SendMessage(m_NotifyIconData.hWnd, WM_COMMAND, pSubMenu->GetMenuItemID(0), 0);
  }

  return 1; // handled
}


