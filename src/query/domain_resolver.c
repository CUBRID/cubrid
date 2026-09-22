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

#include "config.h"
#include "domain_resolver.h"
#include "object_domain_convert.h"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

/* D-325-01/02: CAST and pre-cast consumers supply ASSIGN explicitly. */
DOMAIN_CONV_FUNC
domain_lookup_converter (DB_TYPE source, const TP_DOMAIN * target, DOMAIN_CTX context)
{
  DOMAIN_CONVERT_MODE mode = context == DOMAIN_CTX_ASSIGN ? DOMAIN_CONVERT_ASSIGN
    : context == DOMAIN_CTX_COMPARE || context == DOMAIN_CTX_KEY_ELEM ? DOMAIN_CONVERT_COMPARE : DOMAIN_CONVERT_OPERAND;
  return domain_lookup_converter (source, target, mode);
}
