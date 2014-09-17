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
C_PARAMS=/D "_WINDLL" /D "_USRDLL" /Fo".\Output\\" /Fd".\Output\tzlib.pdb" /W3 /nologo /c /TC

##############################################################
# CL - Set the flags and folders for 32bit debug and release #
##############################################################
C_PARAMS_32BIT=$(C_PARAMS)
C_PARAMS_32BIT_DEBUG=/D "_DEBUG" /Od /Gm /RTC1 /MDd /Za /MDd /ZI
C_PARAMS_32BIT_RELEASE=/D "_CONSOLE" /O2 /Oi /GL /FD /MD /Gy /Zi

C_PARAMS_DEBUG_32BIT=$(C_PARAMS_32BIT) $(C_PARAMS_32BIT_DEBUG)
C_PARAMS_RELEASE_32BIT=$(C_PARAMS_32BIT) $(C_PARAMS_32BIT_RELEASE) /errorReport:prompt

####################################
# LINK - Set the flags and folders #
####################################
LINK_PARAMS=/OUT:".\Output\tzlib.dll" /NOLOGO /DLL /DYNAMICBASE /NXCOMPAT /ERRORREPORT:PROMPT \
/DEBUG /PDB:".\Output\tzlib.pdb" /MANIFEST /MANIFESTFILE:".\Output\tzlib.dll.intermediate.manifest" /MANIFESTUAC:"level='asInvoker' uiAccess='false'"

################################################################
# LINK - Set the flags and folders for 32bit debug and release #
################################################################
LINK_PARAMS_32BIT=$(LINK_PARAMS) /MACHINE:X86
LINK_PARAMS_32BIT_DEBUG=
LINK_PARAMS_32BIT_RELEASE=/OPT:REF /OPT:ICF /LTCG

LINK_PARAMS_DEBUG_32BIT=$(LINK_PARAMS_32BIT) $(LINK_PARAMS_32BIT_DEBUG)
LINK_PARAMS_RELEASE_32BIT=$(LINK_PARAMS_32BIT) $(LINK_PARAMS_32BIT_RELEASE)

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
	cl.exe $(C_PARAMS_DEBUG_32BIT) .\timezones.c
	link.exe $(LINK_PARAMS_DEBUG_32BIT) .\Output\timezones.obj

$(OUTFILE)_release : timezones.c
	@echo off
	cl.exe $(C_PARAMS_RELEASE_32BIT) .\timezones.c
	link.exe $(LINK_PARAMS_RELEASE_32BIT) .\Output\timezones.obj
	mt.exe /outputresource:".\Output\tzlib.dll;#2" /manifest .\Output\tzlib.dll.intermediate.manifest

