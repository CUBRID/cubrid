/*
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
 * client_session_context.cpp - see client_session_context.hpp
 */

#if defined (SERVER_MODE)

#include "client_session_context.hpp"

#include <cassert>
#include <cstddef>

static thread_local client_session_context *tl_Csc_active = NULL;

void
csc_activate (client_session_context *ctx)
{
  assert (ctx != NULL);
  assert (tl_Csc_active == NULL);
  tl_Csc_active = ctx;
}

void
csc_deactivate (void)
{
  assert (tl_Csc_active != NULL);
  tl_Csc_active = NULL;
}

client_session_context *
csc_current (void)
{
  assert (tl_Csc_active != NULL);
  return tl_Csc_active;
}

#endif /* SERVER_MODE */
