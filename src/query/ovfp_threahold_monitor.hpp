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
 * ovfp_threahold_monitor.hpp - (at Server).
 *
 */
#ifndef _OVFP_THRESHOLD_MONITOR_H_
#define _OVFP_THRESHOLD_MONITOR_H_

#if !defined (SERVER_MODE)
#error Belongs to server module
#endif

#if defined (SERVER_MODE)

#include <mutex>
#include "vacuum.h"

class ovfp_monitor_lock
{
#define LOCK_FREE_OWNER_ID (-1)
#define LOCK_ALL_OWNER_ID  (VACUUM_MAX_WORKER_COUNT)
#define LOCK_ITEMS_SIZE    (VACUUM_MAX_WORKER_COUNT)
  private:
    std::mutex  m_ovfp_monitor_mutex;
    int         m_lock_arr[LOCK_ITEMS_SIZE];

  public:
    ovfp_monitor_lock();
    void lock (int lock_index, int owner_id);
    void unlock (int lock_index, int owner_id);
};

typedef struct index_ovfp_info INDEX_OVFP_INFO;
struct index_ovfp_info
{
  OID      class_oid;
  BTID     btid;

  int64_t  hit_cnt;
#define RECENT_POS  (0)
#define MAX_POS   (1)
  int      read_pages[2];
  time_t   event_time[2];

  INDEX_OVFP_INFO *next;
};

class ovfp_threshold
{
  private:
    INDEX_OVFP_INFO    *m_free_head;
    ovfp_monitor_lock  *m_lock_ptr;

  protected:
    int                 m_lock_idx;  // 0 ... (VACUUM_MAX_WORKER_COUNT)
    INDEX_OVFP_INFO    *m_head;
    INDEX_OVFP_INFO    *m_prev;

  private:
    void clear (INDEX_OVFP_INFO *root);
    void free_info_mem (INDEX_OVFP_INFO *pt);
    INDEX_OVFP_INFO   *add (BTID *btid, OID *class_oid, time_t time_now, int npages);

  protected:
    INDEX_OVFP_INFO    *alloc_info_mem();
    INDEX_OVFP_INFO    *find (BTID *btid, OID *class_oid);

  public:
    ovfp_threshold();
    ~ovfp_threshold();

    void set_worker_idx (int idx, ovfp_monitor_lock *lock_mgr_p);
    INDEX_OVFP_INFO   *get_head ();
    void add_info (BTID *btid, OID *class_oid, int npages);
    void check_over_duration_times (time_t *over_tm);
};

class ovfp_printer: public ovfp_threshold
{
  public:
    void sort();
    void add_info (INDEX_OVFP_INFO *new_info);
};

class ovfp_threshold_mgr
{
  private:
    ovfp_monitor_lock    m_ovfp_lock;
    ovfp_threshold       m_ovfp_threshold[VACUUM_MAX_WORKER_COUNT];

    UINT64 m_over_secs;
    char   m_since_time[32];

  public:
    int   m_threshold_pages;

  private:
    bool   get_classoid (THREAD_ENTRY *thread_p, BTID *btid, OID *class_oid);
    void   get_class_name_index_name (THREAD_ENTRY *thread_p, BTID *btid, OID *class_oid, char **class_name,
				      char **index_name);
    char *time_to_string (time_t er_time, char *buf, int size);
    void   print (THREAD_ENTRY *thread_p, FILE *outfp, const INDEX_OVFP_INFO *head_ptr);
  public:
    ovfp_threshold_mgr();
    void init();
    void add_read_pages_count (THREAD_ENTRY *thread_p, int worker_idx, BTID *btid, int npages);
    void dump (THREAD_ENTRY *thread_p, FILE *outfp);
};
#endif // #if defined (SERVER_MODE)
#endif /* _OVFP_THRESHOLD_MONITOR_H_ */
