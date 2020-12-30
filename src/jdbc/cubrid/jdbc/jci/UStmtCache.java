/*
 * Copyright (C) 2008 Search Solution Corporation. 
 * Copyright (c) 2016 CUBRID Corporation.
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

import java.util.ArrayList;
import java.util.Hashtable;

public class UStmtCache {
	String key;

	private Hashtable<UBindKey, UResCache> res_cache_table;
	private ArrayList<UResCache> res_cache_remove_list;
	int ref_count;

	UStmtCache(String key) {
		this.key = key;

		res_cache_table = new Hashtable<UBindKey, UResCache>(30);
		res_cache_remove_list = new ArrayList<UResCache>(100);
		ref_count = 0;
	}

	public UResCache get(UBindKey key) {
		UResCache res_cache;

		synchronized (res_cache_table) {
			res_cache = res_cache_table.get(key);
			if (res_cache == null) {
				res_cache = new UResCache(key);
				res_cache_table.put(key, res_cache);
				synchronized (res_cache_remove_list) {
					res_cache_remove_list.add(res_cache);
				}
			}
			return res_cache;
		}
	}

	synchronized void incr_ref_count() {
		ref_count++;
	}

	synchronized void decr_ref_count() {
		ref_count--;
	}

	void clear() {
		synchronized (res_cache_table) {
			res_cache_table.clear();
			synchronized (res_cache_remove_list) {
				res_cache_remove_list.clear();
			}
		}
	}

	/* for QA test case */
	int remove_expired_res(long checkTime) {
		return 0;
	}
	
	int remove_expired_res(long checkTime, UUrlCache uc) {
		UResCache rc;
		UResCache victim = null;
		int v_idx = -1;

		for (int i = 0; i < res_cache_remove_list.size(); i++) {
			rc = res_cache_remove_list.get(i);
			if (rc.isExpired(checkTime)) {
				if (victim == null || rc.getCacheTime() < victim.getCacheTime()) {
					victim = rc;
					v_idx = i;
				}
			}
		}

		if (victim != null) {
			uc.addCacheSize(-victim.getCacheSize());
			synchronized (res_cache_remove_list) {
				res_cache_table.remove(victim.key);
				UResCache lastObj = res_cache_remove_list.remove(res_cache_remove_list.size() - 1);
				if (v_idx < res_cache_remove_list.size()) {
					res_cache_remove_list.set(v_idx, lastObj);
				}
			}
		}

		return res_cache_remove_list.size();
	}
}
