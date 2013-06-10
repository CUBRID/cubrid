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

import java.lang.reflect.Constructor;
import java.lang.reflect.Method;

abstract public class UJCIUtil {

	private static boolean bServerSide;
	private static boolean bConsoleDebug;
	private static boolean bMMDB;
	private static boolean bSendAppInfo;
	private static boolean bJDBC4;
	private static Boolean bMysqlMode = null;
	private static Boolean bOracleMode = null;

	static {
		String value = System.getProperty("ConsoleDebug");
		if (value != null && value.equals("true")) {
			bConsoleDebug = true;
		} else {
			bConsoleDebug = false;
		}

		value = System.getProperty("MMDB");
		if (value != null && value.equals("true")) {
			bMMDB = true;
		} else {
			bMMDB = false;
		}

		value = System.getProperty("SendAppInfo");
		if (value != null && value.equals("true")) {
			bSendAppInfo = true;
		} else {
			bSendAppInfo = false;
		}

		try {
			Class.forName("com.cubrid.jsp.Server");
			bServerSide = true;
		} catch (Throwable t) {
			bServerSide = false;
		}

		try {
			Class.forName("java.sql.NClob");
			bJDBC4 = true;
		} catch (Throwable t) {
			bJDBC4 = false;
		}
	}

	static public int bytes2int(byte[] b, int startIndex) {
		int data = 0;
		int endIndex = startIndex + 4;

		for (int i = startIndex; i < endIndex; i++) {
			data <<= 8;
			data |= (b[i] & 0xff);
		}

		return data;
	}

	static public short bytes2short(byte[] b, int startIndex) {
		short data = 0;
		int endIndex = startIndex + 2;

		for (int i = startIndex; i < endIndex; i++) {
			data <<= 8;
			data |= (b[i] & 0xff);
		}
		return data;
	}

	static public void copy_bytes(byte[] dest, int dIndex, int cpSize, String src) {
		if (src == null)
			return;

		byte[] b = src.getBytes();
		cpSize = (cpSize > b.length) ? b.length : cpSize;
		System.arraycopy(b, 0, dest, dIndex, cpSize);
	}

	static public void copy_byte(byte[] dest, int dIndex, byte src) {
		if (dest.length < dIndex)
			return;

		dest[dIndex] = src;
	}

	public static boolean isMysqlMode(Class<?> c) {
		if (bMysqlMode == null) {
			String split[] = c.getName().split("\\.");
			if (split.length > 2 && split[2].equals("mysql")) {
				bMysqlMode = new Boolean(true);
			} else {
				bMysqlMode = new Boolean(false);
			}
		}

		return bMysqlMode.booleanValue();
	}

	public static boolean isOracleMode(Class<?> c) {
		if (bOracleMode == null) {
			String split[] = c.getName().split("\\.");
			if (split.length > 2 && split[2].equals("oracle")) {
				bOracleMode = new Boolean(true);
			} else {
				bOracleMode = new Boolean(false);
			}
		}

		return bOracleMode.booleanValue();
	}

	public static boolean isServerSide() {
		return bServerSide;
	}

	public static boolean isConsoleDebug() {
		return bConsoleDebug;
	}

	public static boolean isMMDB() {
		return bMMDB;
	}

	public static boolean isSendAppInfo() {
		return bSendAppInfo;
	}

	public static boolean isJDBC4() {
		return bJDBC4;
	}

	public static Object invoke(String cls_name, String method,
			Class<?>[] param_cls, Object cls, Object[] params) {
		try {
			Class<?> c = Class.forName(cls_name);
			Method m = c.getMethod(method, param_cls);
			return m.invoke(cls, params);
		} catch (Exception e) {
			throw new RuntimeException(e);
		}
	}

	public static Constructor<?> getConstructor(String cls_name, Class<?>[] param_cls) {
		try {
			Class<?> c = Class.forName(cls_name);
			return c.getConstructor(param_cls);
		} catch (Exception e) {
			return null;
		}
	}
}
