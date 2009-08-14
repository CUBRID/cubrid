/*
 * Copyright (C) 2009 Search Solution Corporation. All rights reserved by Search Solution. 
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
package com.cubrid.cubridmanager.core.common;

import java.io.UnsupportedEncodingException;
import java.net.URLDecoder;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;

/**
 * 
 * String manipulation utilities
 * 
 * @author pcraft
 * @version 1.0 - 2009-4-19 created by pcraft
 */
public class StringUtil {

	/**
	 * join a 'pieces' string array to a string
	 * 
	 * @param glue
	 * @param pieces
	 * @return
	 */
	public static String implode(String glue, String[] pieces) {
		StringBuffer sb = new StringBuffer();
		for (int i = 0; i < pieces.length; i++) {
			if (sb.length() > 0)
				sb.append(glue);
			sb.append(pieces[i]);
		}
		return sb.toString();
	}

	/**
	 * returning a MD5 hash string
	 * 
	 * @param text
	 * @return
	 */
	public static String md5(String text) {
		try {
			if (text == null) {
				return "";
			}

			byte[] digest = MessageDigest.getInstance("MD5").digest(
					text.getBytes());
			StringBuffer sb = new StringBuffer();

			for (int i = 0; i < digest.length; i++) {
				sb.append(Integer.toString((digest[i] & 0xf0) >> 4, 16));
				sb.append(Integer.toString(digest[i] & 0x0f, 16));
			}

			return sb.toString();
		} catch (NoSuchAlgorithmException nsae) {
			return null;
		}
	}

	/**
	 * String replacement for all range on a string.
	 * 
	 * @param string
	 * @param oldString
	 * @param newString
	 * @return
	 */
	public static String replace(String string, String oldString,
			String newString) {
		if (string == null) {
			return null;
		}

		if (oldString == null || oldString.length() == 0 || newString == null) {
			return string;
		}

		int i = string.lastIndexOf(oldString);
		if (i < 0)
			return string;

		StringBuffer sb = new StringBuffer(string);
		while (i >= 0) {
			sb.replace(i, (i + oldString.length()), newString);
			i = string.lastIndexOf(oldString, i - 1);
		}

		return sb.toString();
	}

	/**
	 * String replacement for all range on a string. (StringBuilder version)
	 * 
	 * @param string
	 * @param oldString
	 * @param newString
	 */
	public static void replace(StringBuilder string, String oldString,
			String newString) {
		if (string == null || string.length() == 0) {
			return;
		}

		if (oldString == null || oldString.length() == 0 || newString == null
				|| newString.length() == 0) {
			return;
		}

		int i = string.lastIndexOf(oldString);
		if (i < 0)
			return;

		while (i >= 0) {
			string.replace(i, (i + oldString.length()), newString);
			i = string.lastIndexOf(oldString, i - 1);
		}
	}

	/**
	 * Repeat a string
	 * 
	 * @param str
	 * @param length
	 * @return
	 */
	public static String repeat(String str, int length) {
		StringBuffer sb = new StringBuffer();
		for (int i = 0; i < length; i++)
			sb.append(str);
		return sb.toString();
	}

	/**
	 * This method is to convert a long value with a string.
	 * 
	 * @param s
	 * @return
	 */
	public static long longValue(String s) {
		if (s == null)
			return 0;

		try {
			return Long.parseLong(s);
		} catch (Exception ex) {
			return 0;
		}
	}

	/**
	 * This method is to convert a float value with a string.
	 * 
	 * @param s
	 * @return
	 */
	public static float floatValue(String s) {
		if (s == null)
			return 0;

		try {
			return Float.parseFloat(s);
		} catch (Exception ex) {
			return 0;
		}
	}

	/**
	 * This method is to convert a boolean value with a string.
	 * 
	 * @param s
	 * @return
	 */
	public static boolean booleanValue(String s) {
		if (s == null)
			return false;

		try {
			return Integer.parseInt(s) > 0;
		} catch (Exception ex) {
			return false;
		}
	}
	
	/**
	 * This method is to convert a boolean value with a y or n string.
	 * 
	 * @param s
	 * @return
	 */
	public static boolean booleanValueWithYN(String s) {
		if (s == null)
			return false;

		try {
			return s.toLowerCase().equals("y");
		} catch (Exception ex) {
			return false;
		}
	}

	/**
	 * This method is to convert a int value with a string.
	 * 
	 * @param s
	 * @return
	 */
	public static int intValue(String s) {
		if (s == null)
			return 0;

		try {
			return Integer.parseInt(s);
		} catch (Exception ex) {
			return 0;
		}
	}

	/**
	 * Returning a number of character counts for a string.
	 * 
	 * @param s
	 * @return
	 */
	public static int countSpace(String s) {

		int count = 0;

		for (int i = 0, len = s.length(); i < len; i++) {
			char c = s.charAt(i);
			if (c != ' ') {
				break;
			}

			count++;
		}

		return count;

	}

	/**
	 * Returning a exception stacktrace like string.
	 * 
	 * @param ex
	 * @param delimiter
	 * @return
	 */
	public static String getStackTrace(Exception ex, String delimiter) {
		StringBuilder sb = new StringBuilder();
		sb.append(ex);
		StackTraceElement[] ste = ex.getStackTrace();
		for (int i = 0, len = ste.length; i < len; i++) {
			sb.append(delimiter).append("\tat " + ste[i]);
		}

		return sb.toString();
	}

	/**
	 * return true value whether string is empty.
	 * 
	 * @param string
	 * @return
	 */
	public static boolean isEmpty(String string) {

		return string == null || string.trim().length() == 0;

	}

	/**
	 * return false value whether string is empty.
	 * 
	 * @param string
	 * @return
	 */
	public static boolean isNotEmpty(String string) {

		return string != null && string.trim().length() > 0;

	}

	/**
	 * return a true if a string A and B is equal.
	 * 
	 * @param a
	 * @param b
	 * @return
	 */
	public static boolean isEqual(String a, String b) {

		if (a == null || b == null) {
			return false;
		}

		return a.equals(b);

	}

	/**
	 * return a true if a string A and B is equal.
	 * (with a except space characters)
	 * 
	 * @param a
	 * @param b
	 * @return
	 */
	public static boolean isTrimEqual(String a, String b) {

		if (a == null || b == null) {
			return false;
		}

		return a.trim().equals(b.trim());

	}

	/**
	 * If the parameter is null, it will return replaceWhenNull string
	 * 
	 * @param string
	 * @param replacementWhenNull
	 * @return
	 */
	public static String nvl(String string, String replacementWhenNull) {

		if (string == null) {
			return replacementWhenNull;
		}

		return string;

	}

	/**
	 * If the parameter is null, it will return no-string value(eg. "")
	 * 
	 * @param string
	 * @return
	 */
	public static String nvl(String string) {

		return nvl(string, "");

	}

	/**
	 * This method is to convert a y or n string with a boolean value.
	 * 
	 * @param isYes
	 * @return
	 */
	public static String yn(boolean isYes) {

		return isYes ? "y" : "n";

	}

	/**
	 * This method is to convert a YES or NO string with a boolean value.
	 * 
	 * @param isYes
	 * @return
	 */
	public static String YESNO(boolean isYes) {

		return isYes ? "YES" : "NO";

	}
	
	/**
	 * URLDecoder
	 * 
	 * @param s
	 * @param charset
	 * @return
	 */
	public static String urldecode(String s, String charset)
	{

		String result = null;
		if (s == null)
			return null;

		try
		{
			result = URLDecoder.decode(s, charset);
		}
		catch (UnsupportedEncodingException uee)
		{
			return null;
		}

		return result;

	}

}
