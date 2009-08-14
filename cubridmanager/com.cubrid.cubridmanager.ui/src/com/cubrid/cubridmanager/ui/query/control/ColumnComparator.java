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
package com.cubrid.cubridmanager.ui.query.control;

import java.text.DateFormat;
import java.text.ParseException;
import java.text.SimpleDateFormat;
import java.util.Comparator;
import java.util.Date;
import java.util.Map;

import org.apache.log4j.Logger;

import com.cubrid.cubridmanager.core.common.log.LogUtil;

/**
 * comparator for query editor column sorter
 * 
 * @author pangqiren
 * 
 */
@SuppressWarnings("unchecked")
public class ColumnComparator implements
		Comparator {
	private static final Logger logger = LogUtil.getLogger(ColumnComparator.class);
	private String columnIndex;
	private String columnType;
	private boolean isAsc = true;

	/**
	 * 
	 * @param columnIndex
	 * @param columnType
	 * @param isAsc
	 */
	public ColumnComparator(String columnIndex, String columnType, boolean isAsc) {
		this.columnIndex = columnIndex;
		this.columnType = columnType.trim().toUpperCase();
		this.isAsc = isAsc;
	}

	/**
	 * 
	 * @param isAsc
	 */
	public void setAsc(boolean isAsc) {
		this.isAsc = isAsc;
	}

	/**
	 * 
	 * @return boolean
	 */
	public boolean isAsc() {
		return this.isAsc;
	}

	public int compare(Object o1, Object o2) {
		Map map1 = (Map) o1;
		Map map2 = (Map) o2;
		String str1 = ((String) map1.get(columnIndex)).trim();
		String str2 = ((String) map2.get(columnIndex)).trim();
		if ((str1.length() == 0 && str2.length() > 0)
				|| (str1.equals("NULL") && !str2.equals("NULL"))) {
			if (isAsc)
				return -1;
			else
				return 1;
		} else if ((str1.length() == 0 && str2.length() == 0)
				|| (str1.equals("NULL") && str2.equals("NULL"))) {
			return 0;
		} else if ((str1.length() > 0 && str2.length() == 0)
				|| (!str1.equals("NULL") && str2.equals("NULL"))) {
			if (isAsc)
				return 1;
			else
				return -1;
		}
		if (columnType.equals("OID")) {
			if (str1.equals("NONE") && !str2.equals("NONE")) {
				if (isAsc)
					return -1;
				else
					return 1;
			} else if (str1.equals("NONE") && str2.equals("NONE")) {
				return 0;
			} else if (!str1.equals("NONE") && str2.equals("NONE")) {
				if (isAsc)
					return 1;
				else
					return -1;
			}
			String[] str1Arr = str1.split("\\|");
			String[] str2Arr = str2.split("\\|");
			if (str1Arr.length < 3 || str2Arr.length < 3) {
				return 0;
			}
			String str11 = str1Arr[0].replace("@", "").trim();
			String str21 = str2Arr[0].replace("@", "").trim();
			Integer int11 = Integer.valueOf(str11);
			Integer int21 = Integer.valueOf(str21);
			if (int11.compareTo(int21) == 0) {
				Integer int12 = Integer.valueOf(str1Arr[1].trim());
				Integer int22 = Integer.valueOf(str2Arr[1].trim());
				if (int12.compareTo(int22) == 0) {
					Integer int13 = Integer.valueOf(str1Arr[2].trim());
					Integer int23 = Integer.valueOf(str2Arr[2].trim());
					if (isAsc)
						return int13.compareTo(int23);
					else
						return int23.compareTo(int13);
				} else {
					if (isAsc)
						return int12.compareTo(int22);
					else
						return int22.compareTo(int12);
				}
			} else {
				if (isAsc)
					return int11.compareTo(int21);
				else
					return int21.compareTo(int11);
			}
		} else if (columnType.equals("INTEGER")
				|| columnType.equals("SAMALLINT")) {
			Integer int1 = Integer.valueOf(str1);
			Integer int2 = Integer.valueOf(str2);
			if (isAsc)
				return int1.compareTo(int2);
			else
				return int2.compareTo(int1);

		} else if (columnType.equals("NUMERIC") || columnType.equals("FLOAT")
				|| columnType.equals("DOUBLE")) {
			Double double1 = Double.valueOf(str1);
			Double dobule2 = Double.valueOf(str2);
			if (isAsc)
				return double1.compareTo(dobule2);
			else
				return dobule2.compareTo(double1);

		} else if (columnType.equals("DATE") || columnType.equals("TIME")
				|| columnType.equals("TIMESTAMP")) {
			DateFormat dateFormat = null;
			if (columnType.equals("DATE")) {
				dateFormat = new SimpleDateFormat("yyyy-MM-dd");
			} else if (columnType.equals("TIME")) {
				dateFormat = new SimpleDateFormat("hh:mm:ss");
			} else {
				dateFormat = new SimpleDateFormat("yyyy-MM-dd hh:mm:ss");
			}
			try {
				Date date1 = dateFormat.parse(str1);
				Date date2 = dateFormat.parse(str2);
				if (isAsc)
					return date1.compareTo(date2);
				else
					return date2.compareTo(date1);
			} catch (ParseException e) {
				logger.error(e);
			}
			return 0;
		} else {
			if (isAsc)
				return str1.compareTo(str2);
			else
				return str2.compareTo(str1);
		}
	}
}
