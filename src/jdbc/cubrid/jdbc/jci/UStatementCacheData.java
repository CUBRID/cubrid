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

public class UStatementCacheData {
	int tuple_count;
	UResultTuple[] tuples;
	UResultInfo[] resultInfo;
	long srvCacheTime;

	UStatementCacheData(UStatementCacheData cache_data) {
		if (cache_data == null) {
			this.tuple_count = 0;
			this.tuples = null;
			this.resultInfo = null;
			this.srvCacheTime = 0L;
		} else {
			this.tuple_count = cache_data.tuple_count;
			this.tuples = cache_data.tuples;
			this.resultInfo = cache_data.resultInfo;
			this.srvCacheTime = cache_data.srvCacheTime;
		}
	}

	void setCacheData(int tuple_count, UResultTuple[] tuples,
			UResultInfo[] resultInfo) {
		this.tuple_count = tuple_count;
		this.tuples = tuples;
		this.resultInfo = resultInfo;
		if (resultInfo.length == 1)
			this.srvCacheTime = resultInfo[0].getSrvCacheTime();
		else
			this.srvCacheTime = 0L;
	}
}
