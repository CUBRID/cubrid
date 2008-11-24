/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution. 
 *
 *   This program is free software; you can redistribute it and/or modify 
 *   it under the terms of the GNU General Public License as published by 
 *   the Free Software Foundation; version 2 of the License. 
 *
 *  This program is distributed in the hope that it will be useful, 
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of 
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the 
 *  GNU General Public License for more details. 
 *
 *  You should have received a copy of the GNU General Public License 
 *  along with this program; if not, write to the Free Software 
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA 
 *
 */


/*
 * broker_wsa_init.c -
 */

#ident "$Id$"

#include 	<winsock2.h>
#include	<windows.h>
#include	<winsock.h>

int
wsa_initialize ()
{
  WORD wVersionRequested;
  WSADATA wsaData;
  int err;

  wVersionRequested = MAKEWORD (1, 1);

  err = WSAStartup (wVersionRequested, &wsaData);

  if (err != 0)
    return -1;

  /* Confirm that the Windows Sockets DLL supports 1.1. */
  /* Note that if the DLL supports versions greater */
  /* than 1.1 in addition to 1.1, it will still return */
  /* 1.1 in wVersion since that is the version we */
  /* requested. */
  if (LOBYTE (wsaData.wVersion) != 1 || HIBYTE (wsaData.wVersion) != 1)
    {
      WSACleanup ();
      return -1;
    }
  /* The Windows Sockets DLL is acceptable. Proceed. */


  /* Make sure that the version requested is >= 1.1. */
  /* The low byte is the major version and the high */
  /* byte is the minor version.   */
  if (LOBYTE (wVersionRequested) < 1
      || (LOBYTE (wVersionRequested) == 1 && HIBYTE (wVersionRequested) < 1))
    {
      return -1;
    }
  /* Since we only support 1.1, set both wVersion and */
  /* wHighVersion to 1.1. */
  /*
     lpWsaData->wVersion = MAKEWORD( 1, 1 );
     lpWsaData->wHighVersion = MAKEWORD( 1, 1 );
   */

  return 0;
}
