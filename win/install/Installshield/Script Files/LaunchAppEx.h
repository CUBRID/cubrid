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
