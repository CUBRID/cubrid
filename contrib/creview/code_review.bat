@echo on

set creviewdrive=D:
set creviewdir=D:\code_review
set diffprogram="C:\Program Files\WinMerge\WinMergeU.exe"

if %1 == "" goto exit

%creviewdrive%
@cd %creviewdir%

%creviewdir%\gzip -d -c %1 | %creviewdir%\tar xfm -

:RETRY

set DiffDir=CODE_REVIEW_%RANDOM%
rename CODE_REVIEW %DiffDir%

if not %ERRORLEVEL% == 0 goto retry

%diffprogram% "%DiffDir%\old" "%DiffDir%\new"

rmdir /S /Q %DiffDir%

:EXIT

