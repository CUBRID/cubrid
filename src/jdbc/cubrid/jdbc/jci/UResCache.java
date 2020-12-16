/*
 * Copyright (C) 2008 Search Solution Corporation
 * Copyright (C) 2016 CUBRID Corporation 
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

public class UResCache {
	UBindKey key;

	private boolean inUse;
	private UStatementCacheData cache_data;

	public UResCache(UBindKey key) {
		this.key = key;

		cache_data = null;
		inUse = true;
	}

	public UStatementCacheData getCacheData() {
		inUse = true;

		return (new UStatementCacheData(cache_data));
	}

	public long getCacheTime() {
		return cache_data.srvCacheTime;
	}

	public int getCacheSize() {
		return cache_data.size;
	}
	
	public void saveCacheData(UStatementCacheData cd) {
		if (cd.srvCacheTime <= 0)
			return;

		synchronized (this) {
			if (cache_data == null || cd.srvCacheTime > cache_data.srvCacheTime) {
				cache_data = cd;
			}
		}
	}

	public void setExpire() {
		inUse = false;
	}
	
	boolean isExpired(long checkTime) {
		if (cache_data != null && !inUse) {
			if (checkTime > cache_data.srvCacheTime) {
				return true;
			}
			else {
				return false;
			}
		} else {
			return false;
		}
	}
}
