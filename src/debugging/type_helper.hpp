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

#ifndef TYPE_HELPER_HPP
#define TYPE_HELPER_HPP

/* macros and helper function to generate, at preprocessor/compile time, the
 * readable name of a type;
 * eg: can be used to print the actual name of a template argument in an
 * instantiated template function/class
 *
 * usage:
 *    DBG_REGISTER_PARSE_TYPE_NAME (<type>);
 *    ...
 *    constexpr const char *type_name = dbg_parse_type_name < type > ();
 */
#if !defined(NDEBUG)

#if !defined(MAKE_STRING)
#define MAKE_STRING_IMPL(x) #x
#define MAKE_STRING(x) MAKE_STRING_IMPL(x)
#endif

#if !defined(DBG_REGISTER_PARSE_TYPE_NAME)
template <typename T> constexpr const char *dbg_parse_type_name ();
#define DBG_REGISTER_PARSE_TYPE_NAME(_TYPE_) \
  template <> \
  constexpr const char* dbg_parse_type_name<_TYPE_>() \
  { \
    return MAKE_STRING(_TYPE_); \
  }
#endif

#endif

#endif // TYPE_HELPER_HPP
