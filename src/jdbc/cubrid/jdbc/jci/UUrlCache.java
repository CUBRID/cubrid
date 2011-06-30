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

import java.util.ArrayList;
import java.util.Hashtable;

public class UUrlCache {
	private Hashtable stmt_cache_table;
	private ArrayList stmt_cache_remove_list;

	UUrlCache() {
		stmt_cache_table = new Hashtable(100, 5);
		stmt_cache_remove_list = new ArrayList(100);
	}

	UStmtCache getStmtCache(String sql) {
		UStmtCache stmt_cache;
		synchronized (stmt_cache_table) {
			stmt_cache = (UStmtCache) stmt_cache_table.get(sql);
			if (stmt_cache == null) {
				stmt_cache = new UStmtCache(sql);
				stmt_cache_table.put(sql, stmt_cache);
				synchronized (stmt_cache_remove_list) {
					stmt_cache_remove_list.add(stmt_cache);
				}
			}
			stmt_cache.incr_ref_count();
		}

		return stmt_cache;
	}

	void remove_expired_stmt(long checkTime) {
		UStmtCache sc;

		for (int i = 0; i < stmt_cache_remove_list.size(); i++) {
			sc = (UStmtCache) stmt_cache_remove_list.get(i);
			int res_count = sc.remove_expired_res(checkTime);
			synchronized (stmt_cache_table) {
				if (res_count <= 0 && sc.ref_count <= 0) {
					stmt_cache_table.remove(sc.key);

					synchronized (stmt_cache_remove_list) {
						Object lastObj = stmt_cache_remove_list
								.remove(stmt_cache_remove_list.size() - 1);
						if (i < stmt_cache_remove_list.size()) {
							stmt_cache_remove_list.set(i, lastObj);
							i--;
						}
					}
				}
			}
		}
	}

}
