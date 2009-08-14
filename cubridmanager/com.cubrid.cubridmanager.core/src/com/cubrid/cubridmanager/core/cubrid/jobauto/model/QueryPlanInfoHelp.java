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
package com.cubrid.cubridmanager.core.cubrid.jobauto.model;

/**
 * A help class that implements convert from a instance of QueryPlanInfo to the
 * value in the relevant dialog
 * 
 * @author lizhiqiang 2009-4-10
 */
public class QueryPlanInfoHelp {
	private QueryPlanInfo queryPlanInfo;
	private String hour;
	private String minute;

	/**
	 * Gets the instance of QueryPlanInfo
	 * 
	 * @return the queryPlanInfo
	 */
	public QueryPlanInfo getQueryPlanInfo() {
		return queryPlanInfo;
	}

	/**
	 * @param queryPlanInfo
	 *            the queryPlanInfo to set
	 */
	public void setQueryPlanInfo(QueryPlanInfo queryPlanInfo) {
		this.queryPlanInfo = queryPlanInfo;
	}

	/**
	 * Builds the message to a query plan
	 * 
	 */
	public String buildMsg() {
		StringBuffer msg = new StringBuffer();
		msg.append("query_id:");
		msg.append(queryPlanInfo.getQuery_id());
		msg.append("\n");
		msg.append("period:");
		msg.append(queryPlanInfo.getPeriod());
		msg.append("\n");
		msg.append("detail:");
		msg.append(queryPlanInfo.getDetail());
		msg.append("\n");
		msg.append("query_string:");
		msg.append(queryPlanInfo.getQuery_string());
		msg.append("\n");
		return msg.toString();
	}

	/**
	 * Gets the time from the instance of QueryPlanInfo;
	 * 
	 */
	public String getTime() {
		String string = queryPlanInfo.getDetail();
		String time = string.substring(string.indexOf(" ") + 1); // the first
																	// blank
		return time;
	}

	/**
	 * Gets the hour from the instance of QueryPlanInfo;
	 * 
	 */
	public int getHour() {
		String time = getTime();
		hour = time.substring(0, time.indexOf(":"));
		if ("null".equals(hour)) {
			hour = "0";
		} else if (hour.startsWith("0")) {
			hour = hour.substring(1);
		}
		return Integer.valueOf(hour);
	}

	/**
	 * Gets the minute from the instance of QueryPlanInfo;
	 * 
	 */
	public int getMinute() {
		String time = getTime();
		minute = time.substring(time.indexOf(":") + 1);
		if ("null".equals(minute)) {
			minute = "0";
		} else if (minute.startsWith("0")) {
			minute = minute.substring(1);
		}
		return Integer.valueOf(minute);
	}

	/**
	 * @return
	 * @see com.cubrid.cubridmanager.core.cubrid.jobauto.model.QueryPlanInfo#getDbname()
	 */
	public String getDbname() {
		return queryPlanInfo.getDbname();
	}

	/**
	 * @return
	 * @see com.cubrid.cubridmanager.core.cubrid.jobauto.model.QueryPlanInfo#getDetail()
	 */
	public String getDetail() {
		String string = queryPlanInfo.getDetail();
		String detail = string.substring(0, string.indexOf(" "));// the first
																	// blank
		String newDetail = detail;
		if (queryPlanInfo.getPeriod().equalsIgnoreCase("WEEK")) {
			if (detail.equalsIgnoreCase("SUN")) {
				newDetail = "sunday";
			} else if (detail.equalsIgnoreCase("MON")) {
				newDetail = "monday";
			} else if (detail.equalsIgnoreCase("TUE")) {
				newDetail = "tuesday";
			} else if (detail.equalsIgnoreCase("WED")) {
				newDetail = "wednesday";
			} else if (detail.equalsIgnoreCase("THU")) {
				newDetail = "thursday";
			} else if (detail.equalsIgnoreCase("FRI")) {
				newDetail = "friday";
			} else if (detail.equalsIgnoreCase("SAT")) {
				newDetail = "saturday";
			}
		} else if (queryPlanInfo.getPeriod().equalsIgnoreCase("DAY")) {
			newDetail = "";
		} else if(queryPlanInfo.getPeriod().equalsIgnoreCase("ONE")){
			newDetail = detail.replace("/", "-");
		}
		return newDetail;
	}

	/**
	 * @return
	 * @see com.cubrid.cubridmanager.core.cubrid.jobauto.model.QueryPlanInfo#getPeriod()
	 */
	public String getPeriod() {
		String period = queryPlanInfo.getPeriod();
		String newPeriod = "";
		if (period.equalsIgnoreCase("MONTH")) {
			newPeriod = "Monthly";
		} else if (period.equalsIgnoreCase("WEEK")) {
			newPeriod = "Weekly";
		} else if (period.equalsIgnoreCase("DAY")) {
			newPeriod = "Daily";
		} else {
			newPeriod = "Special";
		}
		return newPeriod;
	}

	/**
	 * @return
	 * @see com.cubrid.cubridmanager.core.cubrid.jobauto.model.QueryPlanInfo#getQuery_id()
	 */
	public String getQuery_id() {
		return queryPlanInfo.getQuery_id();
	}

	/**
	 * @return
	 * @see com.cubrid.cubridmanager.core.cubrid.jobauto.model.QueryPlanInfo#getQuery_string()
	 */
	public String getQuery_string() {
		return queryPlanInfo.getQuery_string();
	}

	/**
	 * @param dbname
	 * @see com.cubrid.cubridmanager.core.cubrid.jobauto.model.QueryPlanInfo#setDbname(java.lang.String)
	 */
	public void setDbname(String dbname) {
		queryPlanInfo.setDbname(dbname);
	}

	/**
	 * @param detail
	 * @see com.cubrid.cubridmanager.core.cubrid.jobauto.model.QueryPlanInfo#setDetail(java.lang.String)
	 */
	public void setDetail(String detail) {
		String newDetail = "";
		if (queryPlanInfo.getPeriod().equalsIgnoreCase("WEEK")) {
			if (detail.equalsIgnoreCase("sunday")) {
				newDetail = "SUN";
			} else if (detail.equalsIgnoreCase("monday")) {
				newDetail = "MON";
			} else if (detail.equalsIgnoreCase("tuesday")) {
				newDetail = "TUE";
			} else if (detail.equalsIgnoreCase("wednesday")) {
				newDetail = "WED";
			} else if (detail.equalsIgnoreCase("thursday")) {
				newDetail = "THU";
			} else if (detail.equalsIgnoreCase("friday")) {
				newDetail = "FRI";
			} else if (detail.equalsIgnoreCase("saturday")) {
				newDetail = "SAT";
			}
		} else if (queryPlanInfo.getPeriod().equalsIgnoreCase("DAY")) {
			newDetail = "EVERYDAY";
		} else if (queryPlanInfo.getPeriod().equalsIgnoreCase("ONE")) {
			newDetail = detail.replace("-", "/");
		} else {
			newDetail = detail;
		}
		newDetail = newDetail + " " + setTime();
		queryPlanInfo.setDetail(newDetail);
	}

	/**
	 * @param period
	 * @see com.cubrid.cubridmanager.core.cubrid.jobauto.model.QueryPlanInfo#setPeriod(java.lang.String)
	 */
	public void setPeriod(String period) {
		String newPeriod = "";
		if (period.equalsIgnoreCase("Monthly")) {
			newPeriod = "MONTH";
		} else if (period.equalsIgnoreCase("Weekly")) {
			newPeriod = "WEEK";
		} else if (period.equalsIgnoreCase("Daily")) {
			newPeriod = "DAY";
		} else {
			newPeriod = "ONE";
		}
		queryPlanInfo.setPeriod(newPeriod);
	}

	/**
	 * @param query_id
	 * @see com.cubrid.cubridmanager.core.cubrid.jobauto.model.QueryPlanInfo#setQuery_id(java.lang.String)
	 */
	public void setQuery_id(String query_id) {
		queryPlanInfo.setQuery_id(query_id);
	}

	/**
	 * @param query_string
	 * @see com.cubrid.cubridmanager.core.cubrid.jobauto.model.QueryPlanInfo#setQuery_string(java.lang.String)
	 */
	public void setQuery_string(String query_string) {
		queryPlanInfo.setQuery_string(query_string);
	}

	/**
	 * Sets time
	 * 
	 */
	public String setTime() {
		String time = hour + ":" + minute;
		return time;
	}

	/**
	 * @param hour
	 *            the hour to set
	 */
	public void setHour(String hour) {
		this.hour = hour;
	}

	/**
	 * @param minute
	 *            the minute to set
	 */
	public void setMinute(String minute) {
		this.minute = minute;
	}

}
