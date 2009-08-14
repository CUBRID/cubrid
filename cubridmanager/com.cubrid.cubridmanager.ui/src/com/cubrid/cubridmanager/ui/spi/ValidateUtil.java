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
package com.cubrid.cubridmanager.ui.spi;

import com.cubrid.cubridmanager.core.cubrid.table.task.ModelUtil.OsInfoType;

/**
 * 
 * This class include common validation method and check data validation
 * 
 * @author pangqiren
 * @version 1.0 - 2009-6-4 created by pangqiren
 */
public class ValidateUtil {

	public final static int MAX_DB_NAME_LENGTH = 17;

	public final static int MAX_NAME_LENGTH = 32;// USER,DBNAME,FUNCTION,PROCEDURE
	// NAME
	public final static int MAX_PASSWORD_LENGTH = 31;//db user PASSWORD 

	/**
	 * 
	 * validate the db name
	 * 
	 * @param dbName
	 * @return
	 */
	public static boolean isValidDBName(String dbName) {
		if (dbName == null || dbName.equals(""))
			return false;
		/*
		 * it is better that unix file name does not contain space(" ")
		 * character
		 */
		if (dbName.indexOf(" ") >= 0)
			return false;
		/* Unix file name is not allowed to begin with "#" character */
		if (dbName.charAt(0) == '#')
			return false;
		/* Unix file name is not allowed to begin with "-" character */
		if (dbName.charAt(0) == '-')
			return false;
		/*
		 * 9 character(*&%$|^/~\) are not allowed in Unix file name if
		 * (dbName.matches(".*[*&%$\\|^/~\\\\].*")) { return false; } Unix file
		 * name is not allowed to be named as "." or ".."
		 */
		if (dbName.equals(".") || dbName.equals("..")) {
			return false;
		}
		return dbName.matches("[\\w\\-]*");

	}

	/**
	 * 
	 * validate the path name
	 * 
	 * @param pathName
	 * @return
	 */
	public static boolean isValidPathName(String pathName) {
		if (pathName == null || pathName.equals(""))
			return false;
		/*
		 * it is better that unix file name does not contain space(" ")
		 * character
		 */
		if (pathName.indexOf(" ") >= 0)
			return false;
		/* Unix file name is not allowed to begin with "#" character */
		if (pathName.charAt(0) == '#')
			return false;
		/* Unix file name is not allowed to begin with "-" character */
		if (pathName.charAt(0) == '-')
			return false;
		/* 9 character(*&%$|^~) are not allowed in Unix file name */
		if (pathName.matches(".*[*&%$|^].*")) {
			return false;
		}
		/* Unix file name is not allowed to be named as "." or ".." */
		if (pathName.equals(".") || pathName.equals("..")) {
			return false;
		}

		return true;
	}

	/**
	 * 
	 * validate the database name in the system
	 * 
	 * @param pathName
	 * @return
	 */
	public static boolean isValidDbNameLength(String fileName) {
		if (fileName.length() > MAX_DB_NAME_LENGTH)
			return false;
		else
			return true;
	}

	/**
	 * 
	 * validate the path name in the system
	 * 
	 * @param pathName
	 * @return
	 */
	public static boolean isValidPathNameLength(String pathName) {

		pathName = separatorsToWindows(pathName);
		String[] path = pathName.split("\\\\");
		for (String tmp : path) {
			if (tmp != null)
				return false;
		}
		return true;
	}

	/**
	 * 
	 * Converts all separators to the Windows separator of backslash.
	 * 
	 * @param path
	 * @return
	 */
	public static String separatorsToWindows(String path) {
		if (path == null) {
			return null;
		}

		return FileNameUtils.separatorsToWindows(path);

	}

	/**
	 * 
	 * Converts all separators to the Unix separator of forward slash.
	 * 
	 * @param path
	 * @return
	 */
	public static String separatorsToUnix(String path) {
		if (path == null) {
			return null;
		}
		return FileNameUtils.separatorsToUnix(path);

	}

	/**
	 * 
	 * Converts all separators to the Server system(NT||OTHERS) separator of
	 * forward slash.
	 * 
	 * @param path
	 * @param serverType
	 * @return
	 */
	public static String separatorsPathToServerSystem(String path,
			OsInfoType serverType) {
		if (path == null) {
			return null;
		}
		if (serverType == OsInfoType.NT) {
			return separatorsToWindows(path);
		} else {
			return separatorsToUnix(path);
		}
	}

	/**
	 * 
	 * Return whether the string is double type
	 * 
	 * @param str
	 * @return
	 */
	public static boolean isDouble(String str) {
		if (str == null || "".equals(str)) {
			return false;
		}
		String reg = "^[-\\+]?\\d+(\\.\\d+)?$";
		return str.matches(reg);
	}

	/**
	 * 
	 * Return whether the string is positive double type
	 * 
	 * @param str
	 * @return
	 */
	public static boolean isPositiveDouble(String str) {
		if (str == null || "".equals(str)) {
			return false;
		}
		String reg = "^\\d+(\\.\\d+)?$";
		return str.matches(reg);
	}

	/**
	 * 
	 * Return whether the string is integer type
	 * 
	 * @param str
	 * @return
	 */
	public static boolean isInteger(String str) {
		if (str == null || "".equals(str)) {
			return false;
		}
		String reg = "^[-\\+]?\\d+$";
		return str.matches(reg);
	}

	/**
	 * 
	 * Return whether the string is number type
	 * 
	 * @param str
	 * @return
	 */
	public static boolean isNumber(String str) {
		if (str == null || str.equals("")) {
			return false;
		}
		String reg = "^\\d+$";
		return str.matches(reg);
	}

	/**
	 * 
	 * Return whether the string is validate ip address
	 * 
	 * @param str
	 * @return
	 */
	public static boolean isIP(String str) {
		if (str == null || str.equals("")) {
			return false;
		}
		String reg = "^([\\d]{1,3})\\.([\\d]{1,3})\\.([\\d]{1,3})\\.([\\d]{1,3})$";
		if (!str.matches(reg)) {
			return false;
		}
		String[] ipArray = str.split("\\.");
		if (ipArray == null) {
			return false;
		}
		for (int i = 0; i < ipArray.length; i++) {
			if (Integer.parseInt(ipArray[i]) > 255) {
				return false;
			}
			if (ipArray[i].length() != 1 && ipArray[i].indexOf(0) == 0) {
				return false;
			}
		}
		if (Integer.parseInt(ipArray[0]) > 223) {
			return false;
		}
		return true;
	}

	/**
	 * Return whether the string is validate double or scientific notation
	 * 
	 * @param str
	 * @return
	 */
	public static boolean isSciDouble(String str) {
		if (str == null || str.equals("")) {
			return false;
		}
		String reg = "^[-||+]?\\d+(\\.\\d+)?([e||E]\\d+)?$";
		return str.matches(reg);
	}
}
