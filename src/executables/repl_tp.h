/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 */

/*
 * repl_tp.h - replication
 */

#ifndef _REPL_TP_H_
#define _REPL_TP_H_

#ident "$Id$"

#include <pthread.h>
#include <sys/time.h>

#define PTHREAD_MUTEX_INIT(mutex) do {                                       \
      if(pthread_mutex_init(&(mutex), NULL) != 0) {                          \
         fprintf(err_Log_fp, "%s at \"%s\":%d: %s\n",                        \
                 "pthread mutex init", __FILE__, __LINE__, strerror(errno)); \
         fflush(err_Log_fp);                                                 \
         abort();                                                            \
      }                                                                      \
   } while(0)

#define PTHREAD_MUTEX_LOCK(mutex) do {                                       \
      if(pthread_mutex_lock(&(mutex)) != 0) {                                \
         fprintf(err_Log_fp, "%s at \"%s\":%d: %s\n",                        \
                 "pthread mutex lock", __FILE__, __LINE__, strerror(errno)); \
         fflush(err_Log_fp);                                                 \
         abort();                                                            \
      }                                                                      \
   } while(0)

#define PTHREAD_MUTEX_UNLOCK(mutex) do {                                     \
      if(pthread_mutex_unlock(&(mutex)) != 0) {                              \
         fprintf(err_Log_fp, "%s at \"%s\":%d: %s\n",                        \
                 "pthread mutex unlock", __FILE__, __LINE__,strerror(errno));\
         fflush(err_Log_fp);                                                 \
         abort();                                                            \
      }                                                                      \
   } while(0)

#define PTHREAD_MUTEX_DESTROY(mutex) do {                                    \
      if(pthread_mutex_destroy(&(mutex)) != 0) {                             \
         fprintf(err_Log_fp, "%s at \"%s\":%d: %s\n",                        \
                 "pthread mutex destroy",__FILE__, __LINE__,strerror(errno));\
         fflush(err_Log_fp);                                                 \
         abort();                                                            \
      }                                                                      \
   } while(0)

#define PTHREAD_COND_INIT(cond) do {                                         \
      if(pthread_cond_init(&(cond), NULL) != 0){                             \
         fprintf(err_Log_fp, "%s at \"%s\":%d: %s\n",                        \
                 "pthread cond init", __FILE__, __LINE__, strerror(errno));  \
         fflush(err_Log_fp);                                                 \
         abort();                                                            \
      }                                                                      \
   } while(0)

#define PTHREAD_COND_WAIT(cond, mutex) do {                                  \
      if(pthread_cond_wait(&(cond), &(mutex)) != 0){                         \
         fprintf(err_Log_fp, "%s at \"%s\":%d: %s\n",                        \
                 "pthread cond wait", __FILE__, __LINE__, strerror(errno));  \
         fflush(err_Log_fp);                                                 \
         abort();                                                            \
      }                                                                      \
   } while(0)

#define PTHREAD_COND_TIMEDWAIT(cond, mutex) do {                             \
      struct timeval now;                                                    \
      struct timespec timeout;                                               \
      gettimeofday(&now, NULL);                                              \
      timeout.tv_sec = now.tv_sec+1;                                         \
      timeout.tv_nsec = now.tv_usec * 1000;                                  \
      pthread_cond_timedwait(&(cond), &(mutex), &timeout);                   \
   } while(0)

#define PTHREAD_COND_BROADCAST(cond) do {                                    \
      if(pthread_cond_broadcast(&(cond)) != 0){                              \
         fprintf(err_Log_fp, "%s at \"%s\":%d: %s\n",                        \
                 "pthread cond broadcast",__FILE__,__LINE__,strerror(errno));\
         fflush(err_Log_fp);                                                 \
         abort();                                                            \
      }                                                                      \
   } while(0)

#define PTHREAD_COND_SIGNAL(cond) do {                                       \
      if(pthread_cond_signal(&(cond)) != 0){                                 \
         fprintf(err_Log_fp, "%s at \"%s\":%d: %s\n",                        \
                 "pthread cond signal",__FILE__,__LINE__,strerror(errno));   \
         fflush(err_Log_fp);                                                 \
         abort();                                                            \
      }                                                                      \
   } while(0)

#define PTHREAD_COND_DESTROY(cond) do {                                      \
      if(pthread_cond_destroy(&(cond)) != 0){                                \
         fprintf(err_Log_fp, "%s at \"%s\":%d: %s\n",                        \
                 "pthread cond destroy",__FILE__, __LINE__, strerror(errno));\
         fflush(err_Log_fp);                                                 \
         abort();                                                            \
      }                                                                      \
   } while(0)

#define PTHREAD_KEY_CREATE(key, delete) do {                                 \
      if(pthread_key_create(&(key), delete) != 0){                           \
         fprintf(err_Log_fp, "%s at \"%s\":%d: %s\n",                        \
                 "pthread key create", __FILE__, __LINE__, strerror(errno)); \
         fflush(err_Log_fp);                                                 \
         abort();                                                            \
      }                                                                      \
   } while(0)

#define PTHREAD_KEY_DELETE(key) pthread_key_delete(key)

#define PTHREAD_CREATE(thread, attr, routine, arg) do {                      \
      if(pthread_create(&(thread), attr, routine, arg) != 0){                \
         fprintf(err_Log_fp, "%s at \"%s\":%d: %s\n",                        \
                 "pthread create", __FILE__, __LINE__, strerror(errno));     \
         fflush(err_Log_fp);                                                 \
         abort();                                                            \
      }                                                                      \
   } while(0)

#define PTHREAD_JOIN(thread) do {                                            \
      if(pthread_join((thread), NULL) != 0){                                 \
         fprintf(err_Log_fp, "%s at \"%s\":%d: %s\n",                        \
                 "pthread join", __FILE__, __LINE__, strerror(errno));       \
         fflush(err_Log_fp);                                                 \
         abort();                                                            \
      }                                                                      \
   } while(0)

#define PTHREAD_EXIT pthread_exit(NULL)

#define PTHREAD_SETSPECIFIC(key, arg) do {                                   \
      if(pthread_setspecific((key), arg) != 0){                              \
         fprintf(err_Log_fp, "%s at \"%s\":%d: %s\n",                        \
                 "pthread setspecific", __FILE__, __LINE__, strerror(errno));\
         fflush(err_Log_fp);                                                 \
         abort();                                                            \
      }                                                                      \
   } while(0)

#define PTHREAD_GETSPECIFIC(key) pthread_getspecific(key)

#endif /* _REPL_TP_H_ */
