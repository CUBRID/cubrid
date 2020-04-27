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

import java.util.Map.Entry;
import java.util.Vector;
import java.util.concurrent.ConcurrentHashMap;

public class UStatementHandlerCache {
	private ConcurrentHashMap<String, Vector<UStatementEntry>> stmtHandlerCache;

	public UStatementHandlerCache() {
		stmtHandlerCache = new ConcurrentHashMap<String, Vector<UStatementEntry>> ();
	}

	public ConcurrentHashMap<String, Vector<UStatementEntry>> getCache() {
		return stmtHandlerCache;
	}
	
	public Vector<UStatementEntry> getEntry (String sql) {
		if (!stmtHandlerCache.containsKey(sql)) {
		   Vector<UStatementEntry> vec = new Vector<UStatementEntry>();
		   stmtHandlerCache.put(sql, vec);
		}
		
		return stmtHandlerCache.get(sql);
	}
	
	public void clearEntry () {
		stmtHandlerCache.clear();
		stmtHandlerCache = null;
	}
	
	public void clearStatus () {
		for (Entry<String, Vector<UStatementEntry>> entry : stmtHandlerCache.entrySet()) {
			Vector<UStatementEntry> cacheEntries = entry.getValue();
			for (UStatementEntry e: cacheEntries) {
				e.setStatus(UStatementEntry.AVAILABLE);
			}
		}
	}

	@Override
	public String toString() {
		return "UStatementHandlerCache [stmtHandlerCache=" + stmtHandlerCache + "]";
	}
	
	
}
