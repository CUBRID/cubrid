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
  /* byte is the minor version.  */
  if (LOBYTE (wVersionRequested) < 1 || (LOBYTE (wVersionRequested) == 1 && HIBYTE (wVersionRequested) < 1))
    {
      return -1;
    }
  /* Since we only support 1.1, set both wVersion and */
  /* wHighVersion to 1.1. */
  /*
   * lpWsaData->wVersion = MAKEWORD( 1, 1 ); lpWsaData->wHighVersion = MAKEWORD( 1, 1 ); */

  return 0;
}
