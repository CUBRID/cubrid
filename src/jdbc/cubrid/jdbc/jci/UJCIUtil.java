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
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import cubrid.jdbc.driver.*;

abstract public class UJCIUtil {

	private static boolean bServerSide;
	private static boolean bConsoleDebug;
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

	public static class TimeInfo{
	    public String	time;
	    public String	timezone;
	    public boolean	isDatetime;
	    public boolean	isPM;
	};	
	 
	public static class TimePattern{
		/* YYYY-MM-DD HH:MI:SS[.msec] [AM|PM] */
		final static String format_1 = "\\d\\d\\d\\d\\-\\d\\d\\-\\d\\d\\s++\\d\\d\\:\\d\\d\\:\\d\\d(\\.\\d*)?";
		/* HH:MI:SS[.msec] [AM|PM] YYYY-MM-DD */
		final static String format_2 = "\\d\\d\\:\\d\\d\\:\\d\\d(\\.\\d*)?\\s++\\d\\d\\d\\d\\-\\d\\d\\-\\d\\d";
		/* MM/DD/YYYY HH:MI:SS[.msec] [AM|PM] */
		final static String format_3 = "\\d\\d\\/\\d\\d\\/\\d\\d\\d\\d\\s++\\d\\d\\:\\d\\d\\:\\d\\d(\\.\\d*)?";
		/* HH:MI:SS[.msec] [AM|PM] MM/DD/YYYY */
		final static String format_4 = "\\d\\d\\:\\d\\d\\:\\d\\d(\\.\\d*)?\\s++\\d\\d\\/\\d\\d\\/\\d\\d\\d\\d";
		/* HH:MI:SS [AM|PM]  - time format */
		final static String format_5 = "\\d\\d\\:\\d\\d\\:\\d\\d";

		public final static Pattern pattern_time = Pattern.compile((format_1+"|"+format_2+"|"+format_3+"|"+format_4+"|"+format_5).toString());
		public final static Pattern pattern_ampm = Pattern.compile("[aApP][mM][ \0]");
		public final static Pattern pattern_millis = Pattern.compile("[.]");
	} 
	
	public static TimeInfo parseStringTime(String str_time) throws CUBRIDException {
			TimeInfo timeinfo = new TimeInfo();
			String str_timestamp = "", str_timezone = "";
			int timestamp_count = 0;
			boolean isDateTime = false, isPM = false;

			Matcher matcher = TimePattern.pattern_ampm.matcher(str_time);
			if (matcher.find()) {
				String found = matcher.group();
				str_time = str_time.replace(found, "");
				str_time = str_time.trim();
				if ((found.charAt(0) == 'p') || (found.charAt(0) == 'P')){
					isPM = true;
				}
			}

			matcher = TimePattern.pattern_time.matcher(str_time);
			while (matcher.find()) {
				str_timestamp = matcher.group().trim();
				str_timezone = str_time.substring(str_timestamp.length(), str_time.length()).trim();
				timestamp_count++;
				if (timestamp_count > 1) {
					throw new CUBRIDException(CUBRIDJDBCErrorCode.invalid_value);
				}
			}

			if (timestamp_count == 0) {
				throw new CUBRIDException(CUBRIDJDBCErrorCode.invalid_value);
			}

			matcher = TimePattern.pattern_millis.matcher(str_timestamp);
			if (matcher.find()) {
				isDateTime = true;
			}

			timeinfo.time = str_timestamp;
			timeinfo.timezone = str_timezone;
			timeinfo.isDatetime = isDateTime;
			timeinfo.isPM = isPM;
			return timeinfo;
	};

	public static String getJavaCharsetName(byte cubridCharset){
		switch (cubridCharset){
		case 0: return "ASCII";
		case 2: return "BINARY";
		case 3: return "ISO8859_1";
		case 4: return "EUC_KR";
		case 5: return "UTF8";
		default : 
		}
		return null;
	}
}
