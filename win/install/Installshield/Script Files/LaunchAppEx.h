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

////////////////////////////////////////////////////////////////////////////////
//
//          File: LaunchAppEx.h                                          	
//                                                                    	
//	 Description: This file contains prototypes and definitions for the 
//                _LaunchAppEx function
//
// Last revision: November 29, 2000
//
//     Copyright: (c) 2000 by Dipl.-Ing. Stefan Krueger <skrueger@installsite.org>
//                You have the non-exclusive royalty-free right to use this code
//                in your setup prgram. You are not allowed to sell, publish or
//                redistribute this code in any form except as compiled part of
//                your setup program.
//
//         Notes: This function is based on the HideAppAndWait script by Elihu 
//                Rozen <Elihu@PSUAlum.com> and the LaunchAppGetExitCode script
//                by Ed Smiley <esmiley@cruzio.com>.
//                
////////////////////////////////////////////////////////////////////////////////

#ifndef _LAUNCHAPPEX_H
#define _LAUNCHAPPEX_H

#ifndef STILL_ACTIVE
	#define STILL_ACTIVE    0x00000103
#endif

#if _ISCRIPT_VER < 0x600
	#ifndef BYVAL
		#define BYVAL
	#endif
#endif

     prototype _LaunchAppEx(STRING, STRING, NUMBER, NUMBER, NUMBER, BYREF NUMBER);
     prototype BOOL kernel32.CreateProcessA(POINTER, BYVAL STRING, POINTER, 
               POINTER, BOOL, NUMBER, POINTER, POINTER, POINTER, 
               POINTER);
     prototype BOOL kernel32.GetExitCodeProcess(NUMBER, POINTER);
     prototype NUMBER kernel32.WaitForSingleObject(NUMBER, NUMBER);
     prototype BOOL kernel32.CloseHandle(HWND);
/*
    typedef STARTUPINFO
    begin
        NUMBER   cb;
        POINTER  lpReserved;
        POINTER  lpDesktop;
        POINTER  lpTitle;
        NUMBER   dwX;
        NUMBER   dwY;
        NUMBER   dwXSize;
        NUMBER   dwYSize;
        NUMBER   dwXCountChars;
        NUMBER   dwYCountChars;
        NUMBER   dwFillAttribute;
        NUMBER   dwFlags;
        // the following is actually two words, but we know 
        // we want 0 as the value, so we cheat & create one NUMBER
        NUMBER    wShowWindow;
        //WORD    cbReserved2;
        POINTER  lpReserved2;
        HWND  hStdInput;
        HWND  hStdOutput;
        HWND  hStdError;
    end;

    typedef PROCESS_INFORMATION
    begin
        NUMBER hProcess;
        HWND hThread;
        NUMBER dwProcessId;
        NUMBER dwThreadId;
    end;
  */
#endif
