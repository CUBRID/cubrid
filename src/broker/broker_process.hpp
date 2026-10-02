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


// An invocation owns all startup channels through its existing readiness waits.
#ifndef _BROKER_PROCESS_HPP_
#define _BROKER_PROCESS_HPP_
#if defined(WINDOWS)
#include <windows.h>
#endif
class broker_process_group
{
  public:
    broker_process_group () = default;
#if !defined(WINDOWS)
    ~broker_process_group ();
    int start (const char *path, const char *name, char **source_env, int env_count,
	       const char *first, const char *second, const char *service, bool reset_sigchld = false, bool *exec_failed = nullptr);
    void wait (int milliseconds);
    int finish (int result);

  private:
    struct entry;
    entry *m_first = nullptr;
    int m_error = 0;
#else

    int finish (int result)
    {
      return result;
    }
#endif
    broker_process_group (const broker_process_group &) = delete;
    broker_process_group &operator= (const broker_process_group &) = delete;
};
#if defined(WINDOWS)
inline void broker_process_pump (void *, int milliseconds)
{
  Sleep (milliseconds);
}
#else
void broker_process_pump (void *owner, int milliseconds);
#endif
#if !defined(WINDOWS)
// Internal broker children retain only the dedicated service stdout/stderr.
int broker_process_restart (const char *path, const char *name, const char *first, const char *second);
#endif
#endif
