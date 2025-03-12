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
 * strict_warnings.h - A utility header that treats warnings as errors for specific files to facilitate debugging.
 * WARNING: Include this header at the top of your file, preferably after any other includes.
 * NOTE: This header must be used in conjunction with strict_warnings_off.h
 * INFO: Header guards are not necessary as this file is intended to be included multiple times.
 */

#if defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic error "-Wall"
#pragma GCC diagnostic error "-Wextra"
#pragma GCC diagnostic error "-Wconversion"

#elif defined(__GNUC__)
// Common warnings that are part of -Wall
#pragma GCC diagnostic error "-Waddress"
#pragma GCC diagnostic error "-Warray-bounds"
#pragma GCC diagnostic error "-Wbool-compare"
#pragma GCC diagnostic error "-Wbuiltin-declaration-mismatch"
#pragma GCC diagnostic error "-Wchar-subscripts"
#pragma GCC diagnostic error "-Wcomment"
#pragma GCC diagnostic error "-Wduplicated-branches"
#pragma GCC diagnostic error "-Wduplicated-cond"
#pragma GCC diagnostic error "-Wfloat-equal"
#pragma GCC diagnostic error "-Wimplicit-fallthrough"
#pragma GCC diagnostic error "-Winit-self"
#pragma GCC diagnostic error "-Wlogical-op"
#pragma GCC diagnostic error "-Wmain"
#pragma GCC diagnostic error "-Wmaybe-uninitialized"
#pragma GCC diagnostic error "-Wmissing-field-initializers"
#pragma GCC diagnostic error "-Wnull-dereference"
#pragma GCC diagnostic error "-Wparentheses"
#pragma GCC diagnostic error "-Wredundant-decls"
#pragma GCC diagnostic error "-Wreturn-type"
#pragma GCC diagnostic error "-Wsequence-point"
#pragma GCC diagnostic error "-Wsign-compare"
#pragma GCC diagnostic error "-Wstrict-overflow"
#pragma GCC diagnostic error "-Wswitch"
#pragma GCC diagnostic error "-Wtrigraphs"
#pragma GCC diagnostic error "-Wuninitialized"
#pragma GCC diagnostic error "-Wunknown-pragmas"
#pragma GCC diagnostic error "-Wunused-function"
#pragma GCC diagnostic error "-Wunused-label"
#pragma GCC diagnostic error "-Wunused-result"
#pragma GCC diagnostic error "-Wunused-variable"
#pragma GCC diagnostic error "-Wunused-parameter"
#pragma GCC diagnostic error "-Wvarargs"
#ifdef __cplusplus
  // C++ specific warnings
#else
#pragma GCC diagnostic error "-Wmissing-parameter-type"
#pragma GCC diagnostic error "-Wpointer-sign"
#endif

// Extra warnings (from -Wextra)
#pragma GCC diagnostic error "-Wclobbered"
#pragma GCC diagnostic error "-Wdeprecated"
#pragma GCC diagnostic error "-Wfloat-conversion"
#pragma GCC diagnostic error "-Wignored-qualifiers"
#pragma GCC diagnostic error "-Wlogical-not-parentheses"
#pragma GCC diagnostic error "-Wmissing-include-dirs"
#pragma GCC diagnostic error "-Wold-style-cast"
#pragma GCC diagnostic error "-Woverloaded-virtual"
#pragma GCC diagnostic error "-Wpointer-arith"
#pragma GCC diagnostic error "-Wsign-conversion"
#pragma GCC diagnostic error "-Wswitch-default"
#pragma GCC diagnostic error "-Wundef"
#pragma GCC diagnostic error "-Wuseless-cast"
#pragma GCC diagnostic error "-Wzero-as-null-pointer-constant"

#ifdef __cplusplus
  // C++ specific warnings
#else
  // C specific warnings
#endif

#elif defined(_MSC_VER)
#pragma warning(push, 4)
#endif
