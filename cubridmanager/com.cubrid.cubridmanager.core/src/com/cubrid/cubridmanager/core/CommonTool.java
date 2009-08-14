/*
 * Copyright (C) 2009 Search Solution Corporation. All rights reserved by Search
 * Solution.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met: -
 * Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer. - Redistributions in binary
 * form must reproduce the above copyright notice, this list of conditions and
 * the following disclaimer in the documentation and/or other materials provided
 * with the distribution. - Neither the name of the <ORGANIZATION> nor the names
 * of its contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 * 
 */

package com.cubrid.cubridmanager.core;

import java.lang.reflect.Field;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.text.DateFormat;
import java.text.ParseException;
import java.text.ParsePosition;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.Locale;
import java.util.TimeZone;

import org.apache.log4j.Logger;

import com.cubrid.cubridmanager.core.common.log.LogUtil;

/**
 * This is common class including a lot of common convenient method
 * 
 * @author pangqiren
 * @version 1.0 - 2009-6-4 created by pangqiren
 */
public class CommonTool {

	private static final Logger logger = LogUtil.getLogger(CommonTool.class);
	public static final String newLine = System.getProperty("line.separator");

	/**
	 * 
	 * Convert string to int
	 * 
	 * @param str
	 * @return
	 */
	public static int str2Int(String str) {
		String reg = "^[-\\+]?\\d+$";
		if (str.matches(reg)) {
			return Integer.parseInt(str);
		}
		return 0;
	}

	/**
	 * 
	 * Convert string to double
	 * 
	 * @param inval
	 * @return
	 */
	public static double str2Double(String inval) {
		String sciReg = "^[-||+]?\\d+(\\.\\d+)?([e||E]\\d+)?$";
		String plainReg = "^[-\\+]?\\d+(\\.\\d+)?$";
		if (inval.matches(sciReg) || inval.matches(plainReg)) {
			return Double.parseDouble(inval);
		}
		return 0.0;
	}

	/**
	 * 
	 * Convert string Y or N value to boolean
	 * 
	 * @param inval
	 * @return
	 */
	public static boolean strYN2Boolean(String inval) {
		if (inval == null) {
			return false;
		}
		boolean ret = inval.equalsIgnoreCase("y") ? true : false;
		return ret;
	}

	/**
	 * Reflect the src field value into dest object
	 * 
	 * @param src
	 * @param dest
	 * @throws IllegalAccessException
	 * @throws InvocationTargetException
	 */
	public final static void copyBean2Bean(Object src, Object dest) throws IllegalAccessException,
			InvocationTargetException {
		try {

			Method[] srcMtds = src.getClass().getMethods();
			Method[] destMtds = src.getClass().getMethods();

			Field[] yy = dest.getClass().getDeclaredFields();
			String[] proname = new String[yy.length];
			for (int j = 0; j < yy.length; j++) {
				proname[j] = yy[j].getName();
			}
			for (int ff = 0; ff < yy.length; ff++) {
				/**
				 * **get the set method from dest object and ,get mothod from
				 * src object***
				 */
				String fieldName = yy[ff].getName();
				Method srcGetMethod = null;
				Method destSetMethod = null;
				for (Method m : srcMtds) {
					if (m.getName().equalsIgnoreCase("get" + fieldName)) {
						srcGetMethod = m;
						break;
					}
				}
				for (Method m : destMtds) {
					if (m.getName().equalsIgnoreCase("set" + fieldName)) {
						destSetMethod = m;
						break;
					}
				}
				if (srcGetMethod == null || destSetMethod == null)
					continue;
				/** **get the value from the dest object*** */

				Class<?>[] descParams = destSetMethod.getParameterTypes();
				Class<?>[] srcParams = srcGetMethod.getParameterTypes();

				if (srcParams.length != 0 || descParams.length != 1)
					continue;

				Object value = srcGetMethod.invoke(src);

				boolean flag = true;
				if (value == null)
					flag = false;
				if (flag && value != null && value.getClass() == Integer.class
						&& descParams[0] == int.class)
					flag = false;
				if (flag && value != null && value.getClass() == Double.class
						&& descParams[0] == double.class)
					flag = false;
				if (flag && value != null && descParams[0] != value.getClass())
					continue;
				destSetMethod.invoke(dest, new Object[] { value });

			}

		} catch (IllegalArgumentException e) {
			throw e;
		} catch (IllegalAccessException e) {
			throw e;
		} catch (InvocationTargetException e) {
			throw e;
		}
	}

	static String[] supportedInputTimePattern = { "hh:mm:ss a", "a hh:mm:ss",
			"HH:mm:ss", "hh:mm a", "a hh:mm", "HH:mm", "''hh:mm:ss a''",
			"''a hh:mm:ss''", "''HH:mm:ss''", "''hh:mm a''", "''a hh:mm''",
			"''HH:mm''", };

	/**
	 * support multi input time string, return the timestamp, a long type with
	 * unit second
	 * <li>"hh:mm[:ss] a"
	 * <li>"a hh:mm[:ss]"
	 * <li>"HH:mm[:ss]"
	 * 
	 * @param timestring String time string eg: 11:12:13 am
	 * @return long timestamp
	 * @throws ParseException
	 */
	public static long getTime(String timestring) throws ParseException {

		for (String datepattern : supportedInputTimePattern) {
			if (validateTimestamp(timestring, datepattern)) {
				try {
					return getTimestamp(timestring, datepattern);
				} catch (Exception e) {
					//it is designed not to run at here,so throws nothing
					logger.error("an unexpected exception is throwed.\n"
							+ e.getMessage());
				}
			}
		}
		throw new ParseException("Unparseable date: \"" + timestring + "\"", 0);
	}

	static String[] supportedInputDatePattern = { "MM/dd/yyyy", "yyyy/MM/dd",
			"yyyy-MM-dd", "''MM/dd/yyyy''", "''yyyy/MM/dd''", "''yyyy-MM-dd''" };

	/**
	 * support multi input date string, return the timestamp, a long type with
	 * unit second
	 * <li>"MM/dd/yyyy",
	 * <li>"yyyy/MM/dd",
	 * <li>"yyyy-MM-dd"
	 * 
	 * @param datestring String date string eg: 2009-02-20
	 * @return long timestamp
	 */
	public static long getDate(String datestring) throws ParseException {

		for (String datepattern : supportedInputDatePattern) {
			if (validateTimestamp(datestring, datepattern)) {
				try {
					return getTimestamp(datestring, datepattern);
				} catch (Exception e) {
					//it is designed not to run at here,so throws nothing
					logger.error("an unexpected exception is throwed.\n"
							+ e.getMessage());
				}
			}
		}
		throw new ParseException("Unparseable date: \"" + datestring + "\"", 0);
	}

	static String[] supportedInputTimeStampPattern = { "yyyy/MM/dd a hh:mm:ss",
			"yyyy/MM/dd hh:mm:ss a", "yyyy-MM-dd a hh:mm:ss",
			"yyyy-MM-dd hh:mm:ss a", "yyyy/MM/dd HH:mm:ss",
			"yyyy-MM-dd HH:mm:ss", "hh:mm:ss a MM/dd/yyyy",
			"a hh:mm:ss MM/dd/yyyy", "HH:mm:ss MM/dd/yyyy",
			"yyyy/MM/dd a hh:mm", "yyyy-MM-dd a hh:mm", "yyyy/MM/dd HH:mm",
			"yyyy-MM-dd HH:mm", "hh:mm a MM/dd/yyyy", "HH:mm MM/dd/yyyy",
			"MM/dd/yyyy hh:mm:ss a", "MM/dd/yyyy a hh:mm:ss",
			"MM/dd/yyyy HH:mm:ss", "MM-dd-yyyy hh:mm:ss a",
			"MM-dd-yyyy a hh:mm:ss", "MM-dd-yyyy HH:mm:ss",
			"''yyyy/MM/dd a hh:mm:ss''", "''yyyy-MM-dd a hh:mm:ss''",
			"''yyyy/MM/dd HH:mm:ss''", "''yyyy-MM-dd HH:mm:ss''",
			"''hh:mm:ss a MM/dd/yyyy''", "''HH:mm:ss MM/dd/yyyy''",
			"''yyyy/MM/dd a hh:mm''", "''yyyy-MM-dd a hh:mm''",
			"''yyyy/MM/dd HH:mm''", "''yyyy-MM-dd HH:mm''",
			"''hh:mm a MM/dd/yyyy''", "''HH:mm MM/dd/yyyy''",
			"''MM/dd/yyyy hh:mm:ss a''", "''MM/dd/yyyy a hh:mm:ss''",
			"''MM/dd/yyyy HH:mm:ss''", "''MM-dd-yyyy hh:mm:ss a''",
			"''MM-dd-yyyy a hh:mm:ss''", "''MM-dd-yyyy HH:mm:ss''" };

	/**
	 * support multi input data string, return the timestamp, a long type with
	 * unit second
	 * <li>"hh:mm[:ss] a MM/dd/yyyy",
	 * <li>"HH:mm[:ss] MM/dd/yyyy",
	 * <li>"yyyy/MM/dd a hh:mm[:ss]",
	 * <li>"yyyy-MM-dd a hh:mm[:ss]",
	 * <li>"yyyy/MM/dd HH:mm[:ss]",
	 * <li>"yyyy-MM-dd HH:mm[:ss]"
	 * 
	 * @param datestring String date string eg: 2009-02-20 16:42:46
	 * @return long timestamp
	 */
	public static long getTimestamp(String datestring) throws ParseException {
		for (String datepattern : supportedInputTimeStampPattern) {
			if (validateTimestamp(datestring, datepattern)) {
				try {
					return getTimestamp(datestring, datepattern);
				} catch (Exception e) {
					//it is designed not to run at here,so throws nothing
					logger.error("an unexpected exception is throwed.\n"
							+ e.getMessage());
				}
			}
		}
		throw new ParseException("Unparseable date: \"" + datestring + "\"", 0);
	}

	static String[] supportedInputDateTimePattern = {
			"yyyy/MM/dd a hh:mm:ss.SSS", "yyyy/MM/dd hh:mm:ss.SSS a",
			"yyyy-MM-dd a hh:mm:ss.SSS", "yyyy-MM-dd hh:mm:ss.SSS a",
			"yyyy/MM/dd HH:mm:ss.SSS", "yyyy-MM-dd HH:mm:ss.SSS",
			"hh:mm:ss.SSS a MM/dd/yyyy", "a hh:mm:ss.SSS MM/dd/yyyy",
			"HH:mm:ss.SSS MM/dd/yyyy", "yyyy/MM/dd a hh:mm",
			"''yyyy/MM/dd a hh:mm:ss.SSS''", "''yyyy/MM/dd hh:mm:ss.SSS a''",
			"''yyyy-MM-dd a hh:mm:ss.SSS''", "''yyyy-MM-dd hh:mm:ss.SSS a''",
			"''yyyy/MM/dd HH:mm:ss.SSS''", "''yyyy-MM-dd HH:mm:ss.SSS''",
			"''hh:mm:ss.SSS a MM/dd/yyyy''", "''HH:mm:ss.SSS MM/dd/yyyy''",
			"yyyy-MM-dd a hh:mm:ss", "yyyy-MM-dd hh:mm:ss a",
			"yyyy/MM/dd HH:mm:ss", "yyyy-MM-dd HH:mm:ss",
			"hh:mm:ss a MM/dd/yyyy", "HH:mm:ss MM/dd/yyyy",
			"yyyy/MM/dd a hh:mm", "yyyy-MM-dd a hh:mm", "yyyy/MM/dd HH:mm",
			"yyyy-MM-dd HH:mm", "hh:mm a MM/dd/yyyy", "HH:mm MM/dd/yyyy",
			"''yyyy/MM/dd a hh:mm:ss''", "''yyyy/MM/dd hh:mm:ss a''",
			"''yyyy-MM-dd a hh:mm:ss''", "''yyyy-MM-dd hh:mm:ss a''",
			"''yyyy/MM/dd HH:mm:ss''", "''yyyy-MM-dd HH:mm:ss''",
			"''hh:mm:ss a MM/dd/yyyy''", "''HH:mm:ss MM/dd/yyyy''",
			"''yyyy/MM/dd a hh:mm''", "''yyyy-MM-dd a hh:mm''",
			"''yyyy/MM/dd HH:mm''", "''yyyy-MM-dd HH:mm''",
			"''hh:mm a MM/dd/yyyy''", "''HH:mm MM/dd/yyyy''" };

	/**
	 * support multi input data string, return the timestamp, a long type with
	 * unit second
	 * <li>"hh:mm[:ss].[SSS] a MM/dd/yyyy",
	 * <li>"HH:mm[:ss].[SSS] MM/dd/yyyy",
	 * <li>"yyyy/MM/dd a hh:mm[:ss].[SSS]",
	 * <li>"yyyy-MM-dd a hh:mm[:ss].[SSS]",
	 * <li>"yyyy/MM/dd HH:mm[:ss].[SSS]",
	 * <li>"yyyy-MM-dd HH:mm[:ss].[SSS]"
	 * 
	 * @param datestring String date string eg: 2009-02-20 16:42:46
	 * @return long timestamp
	 */
	public static long getDatetime(String datestring) throws ParseException {
		for (String datepattern : supportedInputDateTimePattern) {
			if (validateTimestamp(datestring, datepattern)) {
				try {
					DateFormat formatter = getDateFormat(datepattern);
					Date date = formatter.parse(datestring);
					long time = date.getTime();
					return time;
				} catch (Exception e) {
					//it is designed not to run at here,so throws nothing
					logger.error("an unexpected exception is throwed.\n"
							+ e.getMessage());
				}
			}
		}
		throw new ParseException("Unparseable date: \"" + datestring + "\"", 0);
	}

	/**
	 * return the datetime pattern for a given datetime string
	 * 
	 * @param datetimeString
	 * @return
	 */
	public static String getDatetimeFormatPattern(String datetimeString) {
		for (String datepattern : supportedInputDateTimePattern) {
			if (validateTimestamp(datetimeString, datepattern)) {
				return datepattern;
			}
		}
		return null;
	}

	/**
	 * format a datetime string to another datetime string <br>
	 * Note: use SimpleDateFormat to parse a "2009/12/12 12:33:00.4", the result
	 * is "2009/12/12 12:33:00.004", but expected "2009/12/12 12:33:00.400",
	 * <br>
	 * so this function first to check whether the millisecond part is 3
	 * digital, if not, padding with 0
	 * 
	 * @param datestring
	 * @param newDatetimePattern
	 * @return
	 */
	public static String formatDateTime(String datestring,
			String newDatetimePattern) {
		String srcDatetimePattern = getDatetimeFormatPattern(datestring);
		if (srcDatetimePattern == null) {
			return null;
		}
		long timestamp = 0;
		int start = srcDatetimePattern.indexOf("SSS");
		String paddingDateString = datestring;
		if (-1 != start) {
			String firstPartPattern = srcDatetimePattern.substring(0, start);
			ParsePosition pp = new ParsePosition(0);
			DateFormat formatter = getDateFormat(firstPartPattern);
			formatter.parse(datestring, pp);
			int firstIndex = pp.getIndex();
			StringBuffer bf = new StringBuffer();
			int i = 0;
			bf.append(datestring.substring(0, firstIndex));
			int count = 0;
			for (i = firstIndex; i < datestring.length(); i++) {
				char c = datestring.charAt(i);
				if (c >= '0' && c <= '9') {
					bf.append(c);
					count++;
				} else {
					break;
				}
			}
			if (count < 3) {
				for (int j = 0; j < 3 - count; j++) {
					bf.append("0");
				}
			}
			for (; i < datestring.length(); i++) {
				char c = datestring.charAt(i);
				bf.append(c);
			}
			paddingDateString = bf.toString();
		}
		try {
			DateFormat formatter = getDateFormat(srcDatetimePattern);
			Date date = formatter.parse(paddingDateString);
			timestamp = date.getTime();
		} catch (ParseException e) {
			//ignored, for datestring has been checked ahead
		}
		return getDatetimeString(timestamp, newDatetimePattern);

	}

	/**
	 * validate whether a date string can be parsed by a given date pattern
	 * 
	 * @param datestring String a date string
	 * @param datepattern String a given date pattern
	 * @return boolean true: can be parsed; false: can not
	 */
	public static boolean validateTimestamp(String datestring,
			String datepattern) {
		ParsePosition pp = new ParsePosition(0);

		DateFormat formatter = getDateFormat(datepattern);
		formatter.parse(datestring, pp);
		if (pp.getIndex() == datestring.length()) {
			return true;
		} else {
			return false;
		}
	}

	/**
	 * return a standard DateFormat instance, which has a given Local, TimeZone,
	 * and with a strict check
	 * 
	 * @param datepattern
	 * @return
	 */
	private static DateFormat getDateFormat(String datepattern) {
		DateFormat formatter = new SimpleDateFormat(datepattern, Locale.US);
		formatter.setLenient(false);
		formatter.setTimeZone(TimeZone.getTimeZone("GMT"));
		return formatter;
	}

	/**
	 * parse date string with a given date pattern, return long type timestamp,
	 * unit:second
	 * 
	 * precondition: it is better to call
	 * cubridmanager.CommonTool.validateTimestamp(String, String) first to void
	 * throwing an ParseException
	 * 
	 * @param datestring String date string eg: 2009-02-20 16:42:46
	 * @param datepattern String date pattern eg: yyyy-MM-dd HH:mm:ss
	 * @return long timestamp
	 * @throws ParseException
	 */
	public static long getTimestamp(String datestring, String datepattern) throws ParseException {
		DateFormat formatter = getDateFormat(datepattern);
		Date date = formatter.parse(datestring);
		long time = date.getTime() / 1000;
		return time;
	}

	/**
	 * format a timestamp into a given date pattern string
	 * 
	 * @param timestamp long type timestamp, unit:second
	 * @param datepattern a given date pattern
	 * @return
	 */
	public static String getTimestampString(long timestamp, String datepattern) {
		DateFormat formatter = getDateFormat(datepattern);
		Date date = new Date(timestamp * 1000);
		return formatter.format(date);
	}

	/**
	 * format a timestamp into a given date pattern string
	 * 
	 * @param timestamp long type timestamp, unit:second
	 * @param datepattern a given date pattern
	 * @return
	 */
	public static String getDatetimeString(long timestamp, String datepattern) {
		DateFormat formatter = getDateFormat(datepattern);
		Date date = new Date(timestamp);
		return formatter.format(date);
	}
}
