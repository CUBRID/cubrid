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
 * longtime_index.hpp - (at Server).
 *
 */
#ifndef _LONGTIME_INDEX_H_
#define _LONGTIME_INDEX_H_

#if !defined (SERVER_MODE) && !defined (SA_MODE)
#error Belongs to server module
#endif /* !defined (SERVER_MODE) && !defined (SA_MODE) */

#include "dbtype_def.h"
#include <assert.h>


#define  read_ovfl_pages_limit  (2)


typedef struct {
   OID      class_oid;
   BTID     btid;
   int      cnt_ovfp_read;
   int      max_ovfp_read;      // max num of overflow oid page in one key.

   UINT64   max_elapsed_time;   // microseconds
   int64_t  hit_count;
   // time   access_time;
} LONGTIME_INDEX_INFO;

typedef struct lt_mgr LONGTIME_INDEX_MGR;
class CLongTimeIndexMgr {
   int                  m_alloc;
   int                  m_used;
   LONGTIME_INDEX_INFO* m_lt_info;
   TSC_TICKS            m_tsc_tick;

private:
   LONGTIME_INDEX_INFO* find(OID  *class_oid, BTID *btid) { 
        if(m_lt_info)
        {
            for(int i = 0; i < m_used; i++)
            {
                if(OID_EQ(&(m_lt_info[i].class_oid), class_oid) &&  BTID_IS_EQUAL(&(m_lt_info[i].btid), btid))
                ;


            }
        }

        return NULL;
   }
   void add(OID  *class_oid, BTID *btid) {}
      

public: 
   CLongTimeIndexMgr()  { m_lt_info = NULL; }
   ~CLongTimeIndexMgr() { clear(); }

   void clear()   { 
        if(m_lt_info) 
        {
           free(m_lt_info); 
           m_lt_info = NULL; 
           m_alloc = 0;} 
        };
   void add_read_pages_count(OID  *class_oid, BTID *btid, int npages) {       
           if(npages < read_ovfl_pages_limit)
           {
                return;
           } 

        LONGTIME_INDEX_INFO* t = find(class_oid, btid);
        }     


   void start()   { tsc_getticks (&m_tsc_tick);  }
   void elapsed(OID  *class_oid, BTID *btid) {
        TSC_TICKS  end_tsc_tick;
        UINT64 elapsed_time; 

        tsc_getticks (&end_tsc_tick); 
        elapsed_time = tsc_elapsed_utime (end_tsc_tick,  m_tsc_tick);
     }
};

struct lt_mgr_master {  
   CLongTimeIndexMgr m_lt_mgr[VACUUM_MAX_WORKER_COUNT];

public:   
   void init()   {  }
   void quit()   {
        for( int i = 0; i < VACUUM_MAX_WORKER_COUNT; i++)
        {
           m_lt_mgr[i].clear();
        }
   }
   CLongTimeIndexMgr*  get_longtime_index_mgr(int idx) { return &(m_lt_mgr[idx]); }   
};

extern struct lt_mgr_master g_longtime_mgr;

#endif /* _LONGTIME_INDEX_H_ */
