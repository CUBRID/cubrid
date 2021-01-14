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

package cubrid.jdbc.driver;

import java.util.Hashtable;
import java.util.Vector;

import javax.transaction.xa.Xid;

abstract class CUBRIDXidTable {
	private static Hashtable<String, Vector<CUBRIDXidInfo>> xaTable;

	static {
		xaTable = new Hashtable<String, Vector<CUBRIDXidInfo>>();
	}

	static boolean putXidInfo(String key, CUBRIDXidInfo xidInfo) {
		Vector<CUBRIDXidInfo> xidArray;

		synchronized (xaTable) {
			xidArray = xaTable.get(key);

			if (xidArray == null) {
				xidArray = new Vector<CUBRIDXidInfo>();
				xaTable.put(key, xidArray);
			}
		}

		synchronized (xidArray) {
			for (int i = 0; i < xidArray.size(); i++) {
				if (xidInfo.compare(xidArray.get(i)))
					return false;
			}
			xidArray.add(xidInfo);
		}
		return true;
	}

	static CUBRIDXidInfo getXid(String key, Xid xid) {
		Vector<CUBRIDXidInfo> xidArray;

		synchronized (xaTable) {
			xidArray = xaTable.get(key);
			if (xidArray == null)
				return null;
		}

		synchronized (xidArray) {
			CUBRIDXidInfo xidInfo;
			for (int i = 0; i < xidArray.size(); i++) {
				xidInfo = xidArray.get(i);
				if (xidInfo.compare(xid))
					return xidInfo;
			}
		}

		return null;
	}

	static void removeXid(String key, Xid xid) {
		Vector<CUBRIDXidInfo> xidArray;

		synchronized (xaTable) {
			xidArray = xaTable.get(key);
			if (xidArray == null)
				return;
		}

		synchronized (xidArray) {
			CUBRIDXidInfo xidInfo;
			for (int i = 0; i < xidArray.size(); i++) {
				xidInfo = xidArray.get(i);
				if (xidInfo.compare(xid)) {
					xidArray.remove(i);
					return;
				}
			}
		}
	}
}
