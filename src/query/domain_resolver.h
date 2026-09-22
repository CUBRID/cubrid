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

#ifndef _DOMAIN_RESOLVER_H_
#define _DOMAIN_RESOLVER_H_

#if !defined (SERVER_MODE) && !defined (SA_MODE)
#error Belongs only to server or stand-alone modules.
#endif

#include "object_domain.h" /* TP_DOMAIN_STATUS and shared value types, no client API. */

typedef TP_DOMAIN_STATUS (*DOMAIN_CONV_FUNC) (const DB_VALUE *, DB_VALUE *, const TP_DOMAIN *);

enum DOMAIN_CTX
{
  DOMAIN_CTX_ARITH, DOMAIN_CTX_COMPARE, DOMAIN_CTX_ASSIGN, DOMAIN_CTX_COMMON_VALUE,
  DOMAIN_CTX_AGG, DOMAIN_CTX_ANALYTIC, DOMAIN_CTX_FUNC_ARG, DOMAIN_CTX_LIST_COLUMN, DOMAIN_CTX_KEY_ELEM
};

struct RESOLVED_DOMAIN
{
  const TP_DOMAIN *domain;
  DOMAIN_CONV_FUNC conv[3];
  const TP_DOMAIN *operand_domain[3];
  const TP_DOMAIN *setdomain;
};

/* Context-to-mode adapter; the value-dependent grid is added by dpin-07. */
DOMAIN_CONV_FUNC domain_lookup_converter (DB_TYPE source, const TP_DOMAIN *target, DOMAIN_CTX context);

#endif /* _DOMAIN_RESOLVER_H_ */
