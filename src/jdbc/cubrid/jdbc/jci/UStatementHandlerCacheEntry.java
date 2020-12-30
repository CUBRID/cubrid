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
package cubrid.jdbc.jci;

public class UStatementHandlerCacheEntry {
	public static final int AVAILABLE = 0;
	public static final int HOLDING = 1;
	
	private UStatement stmt;
	boolean isAvailable;
	
	public UStatementHandlerCacheEntry (UStatement entry) {
		this.stmt = entry;
		this.isAvailable = false;
	}

	public String getSql() {
		return stmt.getQuery();
	}
	
	public UStatement getStatement() {
		return stmt;
	}
	
	public synchronized void setAvailable (boolean isAvailable) {
		this.isAvailable = isAvailable;
	}
	
	public boolean isAvailable () {
		return isAvailable;
	}
}