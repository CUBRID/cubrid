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


/*
 * cci_wsa_init.c -
 */

#ident "$Id$"

#include <winsock2.h>
#include <windows.h>
#include <winsock.h>

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
