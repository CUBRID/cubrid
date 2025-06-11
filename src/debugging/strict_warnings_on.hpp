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
 * strict_warnings_on.hpp - A utility header that treats warnings as errors for specific files to facilitate debugging.
 * WARNING: Include this header at the top of your file, preferably after any other includes.
 * NOTE: This header must be used in conjunction with strict_warnings_off.hpp
 * INFO: Header guards are not necessary as this file is intended to be included multiple times.
 */

#if defined(__GNUC__) || defined(__clang__)
// Clang supports group warning options with diagnostic error.
#pragma GCC diagnostic push
#pragma GCC diagnostic error "-Wall"
#pragma GCC diagnostic error "-Wextra"
#pragma GCC diagnostic error "-Wpedantic"
#pragma GCC diagnostic ignored "-Wunused-parameter" // parameter names in function signatures improve readability, even if unused.

// GCC does not support turning -Wall and -Wextra into errors directly.
// Instead, we list each warning that those groups enable individually.
// Refer to: https://gcc.gnu.org/onlinedocs/gcc/Warning-Options.html

// Warnings from -Wall
// Both C and C++ Warning Flags as Pragmas
#pragma GCC diagnostic error "-Waddress"
#pragma GCC diagnostic error "-Warray-bounds=1"
#pragma GCC diagnostic error "-Warray-compare"
#pragma GCC diagnostic error "-Warray-parameter=2"
#pragma GCC diagnostic error "-Wbool-compare"
#pragma GCC diagnostic error "-Wbool-operation"
#pragma GCC diagnostic error "-Wc++11-compat"
#pragma GCC diagnostic error "-Wc++14-compat"
#pragma GCC diagnostic error "-Wc++17-compat"
#pragma GCC diagnostic error "-Wc++20-compat"
#pragma GCC diagnostic error "-Wchar-subscripts"
#pragma GCC diagnostic error "-Wcomment"
#pragma GCC diagnostic error "-Wdangling-else"
#pragma GCC diagnostic error "-Wdangling-pointer=2"
#pragma GCC diagnostic error "-Wformat=1"
#pragma GCC diagnostic error "-Wformat-contains-nul"
#pragma GCC diagnostic error "-Wformat-diag"
#pragma GCC diagnostic error "-Wformat-extra-args"
#pragma GCC diagnostic error "-Wformat-overflow=1"
#pragma GCC diagnostic error "-Wformat-truncation=1"
#pragma GCC diagnostic error "-Wformat-zero-length"
#pragma GCC diagnostic error "-Wframe-address"
#pragma GCC diagnostic error "-Winfinite-recursion"
#pragma GCC diagnostic error "-Wint-in-bool-context"
#pragma GCC diagnostic error "-Wlogical-not-parentheses"
#pragma GCC diagnostic error "-Wmaybe-uninitialized"
#pragma GCC diagnostic error "-Wmemset-elt-size"
#pragma GCC diagnostic error "-Wmemset-transposed-args"
#pragma GCC diagnostic error "-Wmisleading-indentation"
#pragma GCC diagnostic error "-Wmismatched-dealloc"
#pragma GCC diagnostic error "-Wmissing-attributes"
#pragma GCC diagnostic error "-Wmultistatement-macros"
#pragma GCC diagnostic error "-Wnonnull"
#pragma GCC diagnostic error "-Wnonnull-compare"
#pragma GCC diagnostic error "-Wopenmp-simd"
#pragma GCC diagnostic error "-Wpacked-not-aligned"
#pragma GCC diagnostic error "-Wparentheses"
#pragma GCC diagnostic error "-Wrestrict"
#pragma GCC diagnostic error "-Wreturn-type"
#pragma GCC diagnostic error "-Wsequence-point"
#pragma GCC diagnostic error "-Wsizeof-array-div"
#pragma GCC diagnostic error "-Wsizeof-pointer-div"
#pragma GCC diagnostic error "-Wsizeof-pointer-memaccess"
#pragma GCC diagnostic error "-Wstrict-aliasing"
#pragma GCC diagnostic error "-Wstrict-overflow=1"
#pragma GCC diagnostic error "-Wswitch"
#pragma GCC diagnostic error "-Wtautological-compare"
#pragma GCC diagnostic error "-Wtrigraphs"
#pragma GCC diagnostic error "-Wuninitialized"
#pragma GCC diagnostic error "-Wunknown-pragmas"
#pragma GCC diagnostic error "-Wunused"
#pragma GCC diagnostic error "-Wunused-but-set-variable"
#pragma GCC diagnostic error "-Wunused-function"
#pragma GCC diagnostic error "-Wunused-label"
#pragma GCC diagnostic error "-Wunused-local-typedefs"
#pragma GCC diagnostic error "-Wunused-value"
#pragma GCC diagnostic error "-Wunused-variable"
#pragma GCC diagnostic error "-Wuse-after-free=2"
#pragma GCC diagnostic error "-Wvla-parameter"
#pragma GCC diagnostic error "-Wvolatile-register-var"
#pragma GCC diagnostic error "-Wzero-length-bounds"

#ifdef __cplusplus
// #pragma GCC diagnostic error "-Waligned-new" // not an option that controls warning
#pragma GCC diagnostic error "-Wcatch-value"
#pragma GCC diagnostic error "-Wclass-memaccess"
#pragma GCC diagnostic error "-Wdelete-non-virtual-dtor"
#pragma GCC diagnostic error "-Winit-self"
#pragma GCC diagnostic error "-Wmismatched-new-delete"
#pragma GCC diagnostic error "-Wnarrowing"
#pragma GCC diagnostic error "-Woverloaded-virtual=1"
#pragma GCC diagnostic error "-Wpessimizing-move"
#pragma GCC diagnostic error "-Wrange-loop-construct"
#pragma GCC diagnostic error "-Wreorder"
#pragma GCC diagnostic error "-Wself-move"
#pragma GCC diagnostic error "-Wsign-compare"
#else
#pragma GCC diagnostic error "-Wduplicate-decl-specifier"
#pragma GCC diagnostic error "-Wenum-compare"
#pragma GCC diagnostic error "-Wenum-int-mismatch"
#pragma GCC diagnostic error "-Wimplicit"
#pragma GCC diagnostic error "-Wimplicit-function-declaration"
#pragma GCC diagnostic error "-Wimplicit-int"
#pragma GCC diagnostic error "-Wmain"
#pragma GCC diagnostic error "-Wmissing-braces"
#pragma GCC diagnostic error "-Wpointer-sign"
#pragma GCC diagnostic error "-Wunused-const-variable=1"
#endif

// Warnings from -Wextra
#pragma GCC diagnostic error "-Walloc-size"
#pragma GCC diagnostic error "-Wcalloc-transposed-args"
#pragma GCC diagnostic error "-Wcast-function-type"
#pragma GCC diagnostic error "-Wclobbered"
#pragma GCC diagnostic error "-Wempty-body"
#pragma GCC diagnostic error "-Wexpansion-to-defined"
#pragma GCC diagnostic error "-Wimplicit-fallthrough=3"
#pragma GCC diagnostic error "-Wmaybe-uninitialized"
#pragma GCC diagnostic error "-Wmissing-field-initializers"
#pragma GCC diagnostic error "-Wstring-compare"
#pragma GCC diagnostic error "-Wtype-limits"
#pragma GCC diagnostic error "-Wuninitialized"

#ifdef __cplusplus
#pragma GCC diagnostic error "-Wdangling-reference"
#pragma GCC diagnostic error "-Wdeprecated-copy"
#pragma GCC diagnostic error "-Wredundant-move"
#pragma GCC diagnostic error "-Wsign-compare"
#pragma GCC diagnostic error "-Wsized-deallocation"
#else
#pragma GCC diagnostic error "-Wabsolute-value"
#pragma GCC diagnostic error "-Wenum-conversion"
#pragma GCC diagnostic error "-Wignored-qualifiers"
#pragma GCC diagnostic error "-Wmissing-parameter-name"
#pragma GCC diagnostic error "-Wmissing-parameter-type"
#pragma GCC diagnostic error "-Wold-style-declaration"
#pragma GCC diagnostic error "-Woverride-init"
#pragma GCC diagnostic error "-Wshift-negative-value"
#pragma GCC diagnostic error "-Wunused-but-set-parameter"
#pragma GCC diagnostic error "-Wunterminated-string-initialization"
#endif

// Warnings from -Wpedantic

#pragma GCC diagnostic error "-Wattributes"
#pragma GCC diagnostic error "-Wmain"
#pragma GCC diagnostic error "-Wpointer-arith"

#ifdef __cplusplus
#pragma GCC diagnostic error "-Wchanges-meaning"
#pragma GCC diagnostic error "-Wcomma-subscript"
#pragma GCC diagnostic error "-Welaborated-enum-base"
#pragma GCC diagnostic error "-Wnarrowing"
#pragma GCC diagnostic error "-Wregister"
#pragma GCC diagnostic error "-Wwrite-strings"
#else
#pragma GCC diagnostic error "-Wdeclaration-after-statement"
#pragma GCC diagnostic error "-Wimplicit-int"
#pragma GCC diagnostic error "-Wimplicit-function-declaration"
#pragma GCC diagnostic error "-Wincompatible-pointer-types"
#pragma GCC diagnostic error "-Wint-conversion"
#pragma GCC diagnostic error "-Wlong-long"
#pragma GCC diagnostic error "-Wpointer-sign"
#pragma GCC diagnostic error "-Wvla"
#endif

#else
// For MSVC (or other compilers), adjust as needed.
#if defined(_MSC_VER)
#pragma warning(push, 4)
#endif
#endif
