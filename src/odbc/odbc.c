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

#include	<windows.h>
#include	<stdio.h>
#include	"odbc_portable.h"
#include	"odbcinst.h"
#include	"odbc_resource.h"
#include	"odbc_connection.h"
#include	"odbc_util.h"

PUBLIC BOOL CALLBACK
ConfigDSNDlgProc (HWND hwndParent, UINT message, WPARAM wParam,
		  LPARAM lParam);


PRIVATE BOOL FAR PASCAL AddDSNProc (HWND hwndParent);

/************************************************************************
 * name:  ConfigDriver
 * arguments:
 * returns/side-effects:
 * description:
 *		SQLConfigDriver에 대한 driver-specific routine이다.
 *		현재 ODBC Driver 3.51 for CUBRID에서 내부적으로 
 *		수행하는 routine은 존재하지 않는다.
 * NOTE:
 *		INTERFACE는 ODBCINST.h에 정의되어 있다.
 ************************************************************************/
ODBC_INTERFACE INSTAPI
ConfigDriver (HWND hwndParent,
	      WORD fRequest,
	      LPCSTR lpszDriver,
	      LPCSTR lpszArgs, LPSTR lpszMsg, WORD cbMsgMax, WORD * pcbMsgOut)
{
  OutputDebugString ("ConfigDriver called\n");

  return TRUE;
}

/************************************************************************
 * name:  ConfigDSN
 * arguments:
 * returns/side-effects:
 * description:
 * NOTE:
 *		INTERFACE는 ODBCINST.h에 정의되어 있다.
 ************************************************************************/
ODBC_INTERFACE INSTAPI
ConfigDSN (HWND hwndParent,
	   WORD fRequest, LPCSTR lpszDriver, LPCSTR lpszAttributes)
{
  UWORD wConfigMode;
  int dlgrc;
  CUBRIDDSNItem dsn_item;
  const char *pt;


  OutputDebugString ("ConfigDSN called\n");

  switch (fRequest)
    {
    case ODBC_ADD_DSN:

      memset (&dsn_item, 0, sizeof (CUBRIDDSNItem));

      sprintf (dsn_item.driver, "%s", lpszDriver);
      sprintf (dsn_item.fetch_size, "%d", 100);

      dlgrc = DialogBoxParam (hInstance, (LPCTSTR) IDD_CONFIGDSN, hwndParent,
			      ConfigDSNDlgProc, (LPARAM) & dsn_item);
      if (dlgrc < 0)
	return FALSE;

      break;

    case ODBC_CONFIG_DSN:
      memset (&dsn_item, 0, sizeof (CUBRIDDSNItem));

      sprintf (dsn_item.driver, "%s", lpszDriver);

      pt = element_value_by_key (lpszAttributes, KEYWORD_DSN);
      if (pt == NULL)
	{
	  SQLPostInstallerError (ODBC_ERROR_INVALID_KEYWORD_VALUE, NULL);
	  return (FALSE);
	}

      sprintf (dsn_item.dsn, "%s", pt);

      SQLGetPrivateProfileString (dsn_item.dsn, KEYWORD_DESCRIPTION,
				  "Not Found Field", dsn_item.description,
				  ITEMBUFLEN, "ODBC.INI");
      SQLGetPrivateProfileString (dsn_item.dsn, KEYWORD_DBNAME,
				  "Not Found Field", dsn_item.db_name,
				  ITEMBUFLEN, "ODBC.INI");
      SQLGetPrivateProfileString (dsn_item.dsn, KEYWORD_USER,
				  "Not Found Field", dsn_item.user,
				  ITEMBUFLEN, "ODBC.INI");
      SQLGetPrivateProfileString (dsn_item.dsn, KEYWORD_PASSWORD,
				  "Not Found Field", dsn_item.password,
				  ITEMBUFLEN, "ODBC.INI");
      SQLGetPrivateProfileString (dsn_item.dsn, KEYWORD_SERVER,
				  "Not Found Field", dsn_item.server,
				  ITEMBUFLEN, "ODBC.INI");
      SQLGetPrivateProfileString (dsn_item.dsn, KEYWORD_PORT,
				  "Not Found Field", dsn_item.port,
				  ITEMBUFLEN, "ODBC.INI");
      SQLGetPrivateProfileString (dsn_item.dsn, KEYWORD_FETCH_SIZE,
				  "Not Found Field", dsn_item.fetch_size,
				  ITEMBUFLEN, "ODBC.INI");
      dlgrc =
	DialogBoxParam (hInstance, (LPCTSTR) IDD_CONFIGDSN, hwndParent,
			ConfigDSNDlgProc, (LPARAM) & dsn_item);
      if (dlgrc < 0)
	return FALSE;

      break;

    case ODBC_REMOVE_DSN:
      pt = element_value_by_key (lpszAttributes, KEYWORD_DSN);
      if (pt == NULL)
	{
	  SQLPostInstallerError (ODBC_ERROR_INVALID_KEYWORD_VALUE, NULL);
	  return (FALSE);
	}
      SQLRemoveDSNFromIni (pt);
      break;

    default:
      SQLPostInstallerError (ODBC_ERROR_INVALID_REQUEST_TYPE, NULL);
      return FALSE;
    }

  SQLGetConfigMode (&wConfigMode);

  return TRUE;
}

/************************************************************************
 * name:  ConfigDSNDlgProc
 * arguments:
 * returns/side-effects:						
 * description:
 *  SQLDriverConnect시 사용되는 dialog box를 띄운다.
 * NOTE:
 ************************************************************************/
PUBLIC BOOL CALLBACK
ConfigDSNDlgProc (HWND hwndParent, UINT message, WPARAM wParam, LPARAM lParam)
{
  CUBRIDDSNItem *ptDSNItem;
  BOOL rc;
  HWND hCtrlDSN;
  char ibuf[32];

  switch (message)
    {
      HCURSOR hOldCursor;	// Default Cursor Shape

    case WM_INITDIALOG:
      hOldCursor = SetCursor (LoadCursor ((HINSTANCE) NULL, IDC_WAIT));
      ptDSNItem = (CUBRIDDSNItem *) lParam;

      hCtrlDSN = GetDlgItem (hwndParent, IDC_DSN);

      if (ptDSNItem->save_file[0] != '\0')
	{			// FILEDSN
	  SetDlgItemText (hwndParent, IDC_DSN, "");
	  SetDlgItemText (hwndParent, IDC_SAVE_FILE, ptDSNItem->save_file);
	  EnableWindow (hCtrlDSN, FALSE);
	}
      else
	{
	  SetDlgItemText (hwndParent, IDC_DSN, ptDSNItem->dsn);
	  SetDlgItemText (hwndParent, IDC_SAVE_FILE, "");
	}

      SetDlgItemText (hwndParent, IDC_DRIVER, ptDSNItem->driver);
      SetDlgItemText (hwndParent, IDC_DESCRIPTION, ptDSNItem->description);
      SetDlgItemText (hwndParent, IDC_DBNAME, ptDSNItem->db_name);
      SetDlgItemText (hwndParent, IDC_DBUSER, ptDSNItem->user);
      SetDlgItemText (hwndParent, IDC_PASSWORD, ptDSNItem->password);
      SetDlgItemText (hwndParent, IDC_SERVER, ptDSNItem->server);
      SetDlgItemText (hwndParent, IDC_PORT, ptDSNItem->port);
      SetDlgItemText (hwndParent, IDC_FETCH_SIZE, ptDSNItem->fetch_size);
      sprintf (ibuf, "%p", ptDSNItem);
      SetDlgItemText (hwndParent, IDC_PT_DSNITEM, ibuf);


      /*
         DisplayDatabases(GetDlgItem(hwndParent, IDCOMBO_DATASOURCE));
       */
      SetCursor (hOldCursor);
      break;

    case WM_COMMAND:
      switch (LOWORD (wParam))
	{
	case IDOK:		// make a connection using the supplied values
	  hOldCursor = SetCursor (LoadCursor ((HINSTANCE) NULL, IDC_WAIT));

	  GetDlgItemText (hwndParent, IDC_PT_DSNITEM, ibuf, sizeof (ibuf));
	  sscanf (ibuf, "%p", &ptDSNItem);

	  GetDlgItemText (hwndParent, IDC_DBNAME, ptDSNItem->db_name,
			  ITEMBUFLEN);
	  GetDlgItemText (hwndParent, IDC_DESCRIPTION, ptDSNItem->description,
			  ITEMBUFLEN * 2);
	  GetDlgItemText (hwndParent, IDC_DBUSER, ptDSNItem->user,
			  ITEMBUFLEN);
	  GetDlgItemText (hwndParent, IDC_PASSWORD, ptDSNItem->password,
			  ITEMBUFLEN);
	  GetDlgItemText (hwndParent, IDC_SERVER, ptDSNItem->server,
			  ITEMBUFLEN);
	  GetDlgItemText (hwndParent, IDC_PORT, ptDSNItem->port, ITEMBUFLEN);
	  GetDlgItemText (hwndParent, IDC_FETCH_SIZE, ptDSNItem->fetch_size,
			  ITEMBUFLEN);

	  rc = EndDialog (hwndParent, AddDSNProc (hwndParent));
	  SetCursor (hOldCursor);
	  return rc;

	case IDCANCEL:
	  EndDialog (hwndParent, FALSE);
	  break;

	default:
	  return (FALSE);
	}
      break;

    default:
      return (FALSE);
    }

  return (TRUE);
}


/*	
 * AddDSNProc
 *		- FILEDSN은 SQLDriverConnect의 out connection string
 *		에 의해서 생성, 수정된다.  그 외의 추과과정은 필요없다.
 */
PRIVATE BOOL FAR PASCAL
AddDSNProc (HWND hwndParent)
{
  BOOL rc;

  CUBRIDDSNItem dsn_item;

  memset (&dsn_item, 0, sizeof (dsn_item));


  GetDlgItemText (hwndParent, IDC_SAVE_FILE, dsn_item.save_file,
		  sizeof (dsn_item.save_file));

  if (dsn_item.save_file[0] == '\0')
    {
      // User DSN, or system DSN
      GetDlgItemText (hwndParent, IDC_DRIVER, dsn_item.driver, ITEMBUFLEN);
      GetDlgItemText (hwndParent, IDC_DSN, dsn_item.dsn, ITEMBUFLEN);
      GetDlgItemText (hwndParent, IDC_DESCRIPTION, dsn_item.description,
		      sizeof (dsn_item.description));
      GetDlgItemText (hwndParent, IDC_DBNAME, dsn_item.db_name, ITEMBUFLEN);
      GetDlgItemText (hwndParent, IDC_DBUSER, dsn_item.user, ITEMBUFLEN);
      GetDlgItemText (hwndParent, IDC_PASSWORD, dsn_item.password,
		      ITEMBUFLEN);
      GetDlgItemText (hwndParent, IDC_SERVER, dsn_item.server, ITEMBUFLEN);
      GetDlgItemText (hwndParent, IDC_PORT, dsn_item.port, ITEMBUFLEN);
      GetDlgItemText (hwndParent, IDC_FETCH_SIZE, dsn_item.fetch_size,
		      ITEMBUFLEN);

      rc = SQLWriteDSNToIni (dsn_item.dsn, dsn_item.driver);
      if (rc == FALSE)
	{
	  return FALSE;
	}

      SQLWritePrivateProfileString (dsn_item.dsn, KEYWORD_DBNAME,
				    dsn_item.db_name, "ODBC.INI");
      SQLWritePrivateProfileString (dsn_item.dsn, KEYWORD_DESCRIPTION,
				    dsn_item.description, "ODBC.INI");
      SQLWritePrivateProfileString (dsn_item.dsn, KEYWORD_USER, dsn_item.user,
				    "ODBC.INI");
      SQLWritePrivateProfileString (dsn_item.dsn, KEYWORD_PASSWORD,
				    dsn_item.password, "ODBC.INI");
      SQLWritePrivateProfileString (dsn_item.dsn, KEYWORD_SERVER,
				    dsn_item.server, "ODBC.INI");
      SQLWritePrivateProfileString (dsn_item.dsn, KEYWORD_PORT, dsn_item.port,
				    "ODBC.INI");
      SQLWritePrivateProfileString (dsn_item.dsn, KEYWORD_FETCH_SIZE,
				    dsn_item.fetch_size, "ODBC.INI");
    }

  return (TRUE);
}
