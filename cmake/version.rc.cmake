// Copyright (C) 2016 Search Solution Corporation. All rights reserved.
//
//   This program is free software; you can redistribute it and/or modify
//   it under the terms of the GNU General Public License as published by
//   the Free Software Foundation; either version 2 of the License, or
//   (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program; if not, write to the Free Software
//  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA

#include <windows.h>

#define VER_FILEVERSION @CUBRID_MAJOR_VERSION@,@CUBRID_MINOR_VERSION@,@CUBRID_PATCH_VERSION@,@CUBRID_EXTRA_VERSION@
#define VER_FILEVERSION_STR "@CUBRID_MAJOR_VERSION@.@CUBRID_MINOR_VERSION@.@CUBRID_PATCH_VERSION@.@CUBRID_EXTRA_VERSION@\0" 
#define VER_PRODUCTVERSION @CUBRID_MAJOR_VERSION@,@CUBRID_MINOR_VERSION@,@CUBRID_PATCH_VERSION@,0
#define VER_PRODUCTVERSION_STR "@CUBRID_MAJOR_VERSION@.@CUBRID_MINOR_VERSION@.@CUBRID_PATCH_VERSION@\0" 

VS_VERSION_INFO VERSIONINFO
FILEVERSION     VER_FILEVERSION
PRODUCTVERSION  VER_PRODUCTVERSION
FILEFLAGSMASK   VS_FFI_FILEFLAGSMASK
#ifdef _DEBUG
  FILEFLAGS VS_FF_DEBUG
#else
  FILEFLAGS 0x0L
#endif
FILEOS          VOS__WINDOWS32
FILETYPE        VFT_APP
FILESUBTYPE     VFT2_UNKNOWN
BEGIN
  BLOCK "StringFileInfo"
  BEGIN
    BLOCK "040904B0"
    BEGIN
      VALUE "CompanyName", "Search Solution Corporation"
      VALUE "FileVersion", VER_FILEVERSION_STR
      VALUE "LegalCopyright", "Copyright (C) 2016 Search Solution Corporation. All rights reserved."
      VALUE "ProductName", "CUBRID"
      VALUE "ProductVersion", VER_PRODUCTVERSION_STR
    END
  END
  BLOCK "VarFileInfo"
  BEGIN
    VALUE "Translation", 0x409, 1252
  END
END
