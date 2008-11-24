/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution. 
 *
 * Redistribution and use in source and binary forms, with or without modification, 
 * are permitted provided that the following conditions are met: 
 *
 * - Redistributions of source code must retain the above copyright notice, 
 *   this list of conditions and the following disclaimer. 
 *
 * - Redistributions in binary form must reproduce the above copyright notice, 
 *   this list of conditions and the following disclaimer in the documentation 
 *   and/or other materials provided with the distribution. 
 *
 * - Neither the name of the <ORGANIZATION> nor the names of its contributors 
 *   may be used to endorse or promote products derived from this software without 
 *   specific prior written permission. 
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND 
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED 
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. 
 * IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, 
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, 
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, 
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, 
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) 
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY 
 * OF SUCH DAMAGE. 
 *
 */

package cubrid.jdbc.jci;

import java.util.Hashtable;
import java.util.ArrayList;

public class UStmtCache
{
  String key;

  private Hashtable res_cache_table;
  private ArrayList res_cache_remove_list;
  int ref_count;

  UStmtCache(String key)
  {
    this.key = key;

    res_cache_table = new Hashtable(30);
    res_cache_remove_list = new ArrayList(100);
    ref_count = 0;
  }

  public UResCache get(UBindKey key)
  {
    UResCache res_cache;

    synchronized (res_cache_table)
    {
      res_cache = (UResCache) res_cache_table.get(key);
      if (res_cache == null)
      {
        res_cache = new UResCache(key);
        res_cache_table.put(key, res_cache);
        synchronized (res_cache_remove_list)
        {
          res_cache_remove_list.add(res_cache);
        }
      }
      return res_cache;
    }
  }

  synchronized void incr_ref_count()
  {
    ref_count++;
  }

  synchronized void decr_ref_count()
  {
    ref_count--;
  }

  void clear()
  {
    synchronized (res_cache_table)
    {
      res_cache_table.clear();
      synchronized (res_cache_remove_list)
      {
        res_cache_remove_list.clear();
      }
    }
  }

  int remove_expired_res(long checkTime)
  {
    UResCache rc;

    for (int i = 0; i < res_cache_remove_list.size(); i++)
    {
      rc = (UResCache) res_cache_remove_list.get(i);
      if (rc.isExpired(checkTime))
      {
        res_cache_table.remove(rc.key);

        synchronized (res_cache_remove_list)
        {
          Object lastObj = res_cache_remove_list.remove(res_cache_remove_list
              .size() - 1);
          if (i < res_cache_remove_list.size())
          {
            res_cache_remove_list.set(i, lastObj);
            i--;
          }
        }
      }
    }
    return res_cache_remove_list.size();
  }
}
