#ifndef _NTRAY_H__
#define _NTRAY_H__

class CTrayNotifyIcon : public CObject
{
public:
	BOOL bSetIconFailed;
	CString m_NotifyMsg;
  //Constructors / Destructors
  CTrayNotifyIcon();
  ~CTrayNotifyIcon();

  //Create the tray icon
  BOOL Create(CWnd* pNotifyWnd, UINT uID, LPCTSTR pszTooltipText, HICON hIcon, UINT nNotifyMessage);

  //Change or retrieve the Tooltip text
  BOOL    SetTooltipText(LPCTSTR pszTooltipText);
  BOOL    SetTooltipText(UINT nID);
  CString GetTooltipText() const;

  //Change or retrieve the icon displayed
  BOOL  SetIcon(HICON hIcon);
  BOOL  SetIcon(LPCTSTR lpIconName);
  BOOL  SetIcon(UINT nIDResource);
  BOOL  SetStandardIcon(LPCTSTR lpIconName);
  BOOL  SetStandardIcon(UINT nIDResource);
  HICON GetIcon() const;

  //Change or retrieve the window to send notification messages to
  BOOL  SetNotificationWnd(CWnd* pNotifyWnd);
  CWnd* GetNotificationWnd() const;

  //Remove the tray icon (Called in the destructor anyway)
  void RemoveIcon();

  //Default handler for tray notification message
  virtual LRESULT OnTrayNotification(WPARAM uID, LPARAM lEvent);

protected:
  NOTIFYICONDATA m_NotifyIconData;
  BOOL           m_bCreated;
  CWnd*          m_pWnd;

  DECLARE_DYNAMIC(CTrayNotifyIcon)
};




#endif //_NTRAY_H__