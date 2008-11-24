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

package cubridmanager;

import java.util.Hashtable;

public class CubridException extends Exception {
	static final long serialVersionUID = 7L;
	private static final int COUNT_ER = 3;
	public static final int ER_NORMAL = 0;
	public static final int ER_UNKNOWNHOST = 1;
	public static final int ER_CONNECT = 2;
	private static Hashtable messageString = null;
	public int ErrCode;

	CubridException(int err) {
		super();
		ErrCode = err;
	}

	public String getErrorMessage() {
		if (messageString == null)
			setMessageHash();
		return (String) messageString.get(new Integer(ErrCode));
	}

	public static String getErrorMessage(int index) {
		if (messageString == null)
			setMessageHash();
		return (String) messageString.get(new Integer(index));
	}

	private static void setMessageHash() {
		messageString = new Hashtable(COUNT_ER + 1);

		messageString.put(new Integer(COUNT_ER), "Error"); // last index
		messageString.put(new Integer(ER_NORMAL), "No Error");
		messageString.put(new Integer(ER_UNKNOWNHOST), Messages
				.getString("ERROR.UNKNOWNHOST"));
		messageString.put(new Integer(ER_CONNECT), Messages
				.getString("ERROR.CONNECTFAIL"));
	}

}
