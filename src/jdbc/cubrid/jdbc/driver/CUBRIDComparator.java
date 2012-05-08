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

package cubrid.jdbc.driver;

import java.util.Comparator;

class CUBRIDComparator implements Comparator<Object> {
	private String dbmd_method;

	CUBRIDComparator(String whatfor) {
		dbmd_method = whatfor;
	}

	/*
	 * java.util.Comparator interface
	 */

	public int compare(Object o1, Object o2) {
		if (dbmd_method.endsWith("getTables"))
			return compare_getTables(o1, o2);
		if (dbmd_method.endsWith("getColumns"))
			return compare_getColumns(o1, o2);
		if (dbmd_method.endsWith("getColumnPrivileges"))
			return compare_getColumnPrivileges(o1, o2);
		if (dbmd_method.endsWith("getTablePrivileges"))
			return compare_getTablePrivileges(o1, o2);
		if (dbmd_method.endsWith("getBestRowIdentifier"))
			return compare_getBestRowIdentifier(o1, o2);
		if (dbmd_method.endsWith("getIndexInfo"))
			return compare_getIndexInfo(o1, o2);
		if (dbmd_method.endsWith("getSuperTables"))
			return compare_getSuperTables(o1, o2);
		return 0;
	}

	private int compare_getTables(Object o1, Object o2) {
		int t;
		t = ((String) ((Object[]) o1)[3])
				.compareTo((String) ((Object[]) o2)[3]);
		if (t != 0)
			return t;
		return ((String) ((Object[]) o1)[2])
				.compareTo((String) ((Object[]) o2)[2]);
	}

	private int compare_getColumns(Object o1, Object o2) {
		int t;
		t = ((String) ((Object[]) o1)[2])
				.compareTo((String) ((Object[]) o2)[2]);
		if (t != 0)
			return t;
		return ((Integer) ((Object[]) o1)[16])
				.compareTo((Integer) ((Object[]) o2)[16]);
	}

	private int compare_getColumnPrivileges(Object o1, Object o2) {
		int t;
		t = ((String) ((Object[]) o1)[3])
				.compareTo((String) ((Object[]) o2)[3]);
		if (t != 0)
			return t;
		return ((String) ((Object[]) o1)[6])
				.compareTo((String) ((Object[]) o2)[6]);
	}

	private int compare_getTablePrivileges(Object o1, Object o2) {
		int t;
		t = ((String) ((Object[]) o1)[2])
				.compareTo((String) ((Object[]) o2)[2]);
		if (t != 0)
			return t;
		return ((String) ((Object[]) o1)[5])
				.compareTo((String) ((Object[]) o2)[5]);
	}

	private int compare_getBestRowIdentifier(Object o1, Object o2) {
		return ((Short) ((Object[]) o1)[0])
				.compareTo((Short) ((Object[]) o2)[0]);
	}

	private int compare_getIndexInfo(Object o1, Object o2) {
		int t;

		if (((Boolean) ((Object[]) o1)[3]).booleanValue()
				&& !((Boolean) ((Object[]) o2)[3]).booleanValue())
			return 1;
		if (!((Boolean) ((Object[]) o1)[3]).booleanValue()
				&& ((Boolean) ((Object[]) o2)[3]).booleanValue())
			return -1;

		t = ((Short) ((Object[]) o1)[6]).compareTo((Short) ((Object[]) o2)[6]);
		if (t != 0)
			return t;

		if (((Object[]) o1)[5] == null)
			return 0;
		t = ((String) ((Object[]) o1)[5])
				.compareTo((String) ((Object[]) o2)[5]);
		if (t != 0)
			return t;

		return ((Integer) ((Object[]) o1)[7])
				.compareTo((Integer) ((Object[]) o2)[7]);
	}

	private int compare_getSuperTables(Object o1, Object o2) {
		int t;
		t = ((String) ((Object[]) o1)[2])
				.compareTo((String) ((Object[]) o2)[2]);
		if (t != 0)
			return t;
		return ((String) ((Object[]) o1)[3])
				.compareTo((String) ((Object[]) o2)[3]);
	}

}
