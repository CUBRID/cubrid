/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * wsa_init.c - 
 */

#ident "$Id$"

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
  if (LOBYTE (wVersionRequested) < 1 ||
      (LOBYTE (wVersionRequested) == 1 && HIBYTE (wVersionRequested) < 1))
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
