DLLPATH=.

FILES = timezones.obj timezone_lib_common.h

######################################
# Set flags, filenames, folders etc. #
######################################
OBJS = timezones.obj
OUTFILE=timezones.dll

##################################
# CL - Set the flags and folders # 
##################################
C_PARAMS=/D "_WINDLL" /D "_USRDLL" /Fo".\Output\\" /Fd".\Output\tzlib.pdb" /W3 /nologo /c /Zi /TC /errorReport:prompt

##############################################################
# CL - Set the flags and folders for 64bit debug and release #
##############################################################
C_PARAMS_64BIT=$(C_PARAMS)
C_PARAMS_64BIT_DEBUG=/Od /D "_DEBUG" /Gm /RTC1 /MDd
C_PARAMS_64BIT_RELEASE=/O2 /Oi /GL /D "_CONSOLE" /FD /MD /Gy

C_PARAMS_DEBUG_64BIT=$(C_PARAMS_64BIT) $(C_PARAMS_64BIT_DEBUG)
C_PARAMS_RELEASE_64BIT=$(C_PARAMS_64BIT) $(C_PARAMS_64BIT_RELEASE)

####################################
# LINK - Set the flags and folders #
####################################
LINK_PARAMS=/OUT:".\Output\tzlib.dll" /DLL /MANIFEST /MANIFESTFILE:".\Output\tzlib.dll.intermediate.manifest" \
/MANIFESTUAC:"level='asInvoker' uiAccess='false'" /DEBUG /PDB:".\Output\tzlib.pdb" /DYNAMICBASE \
/NXCOMPAT /ERRORREPORT:PROMPT

################################################################
# LINK - Set the flags and folders for 64bit debug and release #
################################################################
LINK_PARAMS_64BIT=$(LINK_PARAMS) /MACHINE:X64
LINK_PARAMS_64BIT_DEBUG=
LINK_PARAMS_64BIT_RELEASE=/OPT:REF /OPT:ICF /LTCG

LINK_PARAMS_DEBUG_64BIT=/NOLOGO $(LINK_PARAMS_64BIT) $(LINK_PARAMS_64BIT_DEBUG)
LINK_PARAMS_RELEASE_64BIT=$(LINK_PARAMS_64BIT) $(LINK_PARAMS_64BIT_RELEASE)

release: $(OUTFILE)_release
    echo $(OUTFILE) built successfully  

debug: $(OUTFILE)_debug
    echo $(OUTFILE) built successfully  
	
clean:
    @echo Cleaning Output Folder
    @del .\Output\*.obj
    @del .\Output\*.idb
    @del .\Output\*.pdb
    @del .\Output\*.pch
    @del .\Output\*.lib
    @del .\Output\*.exp
    @del .\Output\*.htm
    @del .\Output\$(OUTFILE)
    

$(OUTFILE)_debug : timezones.c
	@echo off
	cl.exe $(C_PARAMS_DEBUG_64BIT) .\timezones.c /nologo
	link.exe $(LINK_PARAMS_DEBUG_64BIT) .\Output\timezones.obj
	mt.exe /outputresource:".\Output\tzlib.dll;#2" /manifest .\Output\tzlib.dll.intermediate.manifest

$(OUTFILE)_release : timezones.c
	@echo off
	cl.exe $(C_PARAMS_RELEASE_64BIT) .\timezones.c
	link.exe $(LINK_PARAMS_RELEASE_64BIT) .\Output\timezones.obj
	mt.exe /outputresource:".\Output\tzlib.dll;#2" /manifest .\Output\tzlib.dll.intermediate.manifest
	
