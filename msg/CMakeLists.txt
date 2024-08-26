#
#  Copyright 2008 Search Solution Corporation
#  Copyright 2016 CUBRID Corporation
# 
#   Licensed under the Apache License, Version 2.0 (the "License");
#   you may not use this file except in compliance with the License.
#   You may obtain a copy of the License at
# 
#       http://www.apache.org/licenses/LICENSE-2.0
# 
#   Unless required by applicable law or agreed to in writing, software
#   distributed under the License is distributed on an "AS IS" BASIS,
#   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#   See the License for the specific language governing permissions and
#   limitations under the License.
# 
#

set(LOCALE_DIRS 
  en_US.utf8
  en_US
  ko_KR.utf8
  ko_KR.euckr
  )

set(MSG_FILES
  cubrid
  csql
  utils
  esql
  )

if(WIN32)
  set(ICONV_CONTENT "
    param (
      [string] $sourceFilePath,
      [string] $destinationFilePath
    )

    \$content = Get-Content -Path $sourceFilePath -Raw -Encoding UTF8
    [System.Text.Encoding]::GetEncoding('euc-kr').GetBytes(\$content) | Set-Content -Path $destinationFilePath -Encoding Byte
    Write-Output 'File encoding converion from UTF-8 to EUC-KR completed'
  ")
  set(ICONV_SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/iconv.ps1")
  file(WRITE ${ICONV_SCRIPT} ${ICONV_CONTENT})
endif(WIN32)

foreach(locale ${LOCALE_DIRS})
  if(${locale} STREQUAL "en_US")
    set(SRC_LOCALE "en_US.utf8")
    set(NEED_ICONV 0)
  elseif(${locale} STREQUAL "ko_KR.euckr")
    set(SRC_LOCALE "ko_KR.utf8")
    set(NEED_ICONV 1)
  else()
    set(SRC_LOCALE "${locale}")
    set(NEED_ICONV 0)
  endif()

  foreach(msgfile ${MSG_FILES})
    if(NEED_ICONV EQUAL 1)
      if(WIN32)
        add_custom_command(
          OUTPUT ${locale}/${msgfile}.msg
          COMMAND ${CMAKE_COMMAND} -E make_directory ${locale}
          COMMAND powershell -ExecutionPolicy Bypass -File ${ICONV_SCRIPT} -sourceFilePath ${CMAKE_CURRENT_SOURCE_DIR}/${SRC_LOCALE}/${msgfile}.msg -destinationFilePath ${CMAKE_CURRENT_BINARY_DIR}/${locale}/${msgfile}.msg
          COMMENT "Conversion ${msgfile}.msg in ${locale}"
          DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${SRC_LOCALE}/${msgfile}.msg
          VERBATIM
          )
      else(WIN32)
        find_program(ICONV_COMMAND iconv) 
        if(NOT ICONV_COMMAND)
          message(FATAL_ERROR "iconv command not found. Please install iconv.")
        endif(NOT ICONV_COMMAND)

        add_custom_command(
          OUTPUT ${locale}/${msgfile}.msg
          COMMAND ${CMAKE_COMMAND} -E make_directory ${locale}
          COMMAND ${ICONV_COMMAND} -f utf-8 -t euckr ${CMAKE_CURRENT_SOURCE_DIR}/${SRC_LOCALE}/${msgfile}.msg > ${CMAKE_CURRENT_BINARY_DIR}/${locale}/${msgfile}.msg
          COMMENT "Conversion ${msgfile}.msg in ${locale}"
          DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${SRC_LOCALE}/${msgfile}.msg
          )
      endif(WIN32)
    else(NEED_ICONV EQUAL 1)
      add_custom_command(
        OUTPUT ${locale}/${msgfile}.msg
        COMMAND ${CMAKE_COMMAND} -E make_directory ${locale}
        COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/${SRC_LOCALE}/${msgfile}.msg ${CMAKE_CURRENT_BINARY_DIR}/${locale}/${msgfile}.msg
        COMMENT "Copy ${msgfile}.msg in ${locale}"
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${SRC_LOCALE}/${msgfile}.msg
        )
    endif(NEED_ICONV EQUAL 1)

    add_custom_command(
      OUTPUT ${locale}/${msgfile}.cat
      COMMAND ${CMAKE_COMMAND} -E make_directory ${locale}
      COMMAND gencat ${locale}/${msgfile}.cat ${CMAKE_CURRENT_BINARY_DIR}/${locale}/${msgfile}.msg
      COMMENT "Generating ${msgfile}.cat for ${locale}"
      DEPENDS gencat ${CMAKE_CURRENT_BINARY_DIR}/${locale}/${msgfile}.msg
      )
  endforeach(msgfile)

  add_custom_target(gen_msgs_${locale} ALL DEPENDS ${locale}/cubrid.cat ${locale}/csql.cat ${locale}/utils.cat ${locale}/esql.cat)

  install(DIRECTORY
    ${CMAKE_CURRENT_BINARY_DIR}/${locale}
    DESTINATION ${CUBRID_LOCALEDIR}
    FILES_MATCHING PATTERN "*.msg")
  install(DIRECTORY
    ${CMAKE_CURRENT_BINARY_DIR}/${locale}
    DESTINATION ${CUBRID_LOCALEDIR}
    FILES_MATCHING PATTERN "*.cat")
endforeach(locale)
