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
package com.cubrid.cubridmanager.core.cubrid.table.model;

import java.util.ArrayList;
import java.util.List;

import com.cubrid.cubridmanager.core.cubrid.database.model.DatabaseInfo;
import com.cubrid.cubridmanager.core.cubrid.table.model.SchemeChangeLog.SchemeInnerType;

/**
 * to manager changes to an exist schema
 * 
 * @author moulinwang
 * @version 1.0 - 2009-6-4 created by moulinwang
 */
public class SchemaChangeManager {
	List<SchemeChangeLog> changeList = null;
	DatabaseInfo database = null;

	boolean isNewTableFlag;

	/**
	 * a construct method for testing
	 */
	public SchemaChangeManager() {

	}

	/**
	 * a construct for use
	 * 
	 * @param database
	 * @param isNewTableFlag
	 */
	public SchemaChangeManager(DatabaseInfo database, boolean isNewTableFlag) {
		super();
		this.changeList = new ArrayList<SchemeChangeLog>();
		this.database = database;

		this.isNewTableFlag = isNewTableFlag;
	}

	public List<SchemeChangeLog> getChangeList() {
		if (null == changeList) {
			return new ArrayList<SchemeChangeLog>();
		}
		return changeList;
	}

	public void setChangeList(List<SchemeChangeLog> changeList) {
		this.changeList = changeList;
	}

	/**
	 * return whether object with a type and a given value is a new added object
	 * 
	 * @param type
	 * @param value
	 * @return
	 */
	public boolean isNewAdded(SchemeInnerType type, String value) {
		SchemeChangeLog slog = findModifySchemeChangeLog(type, value, false);
		if (slog != null) {
			if (slog.getOldValue() == null) {
				return true;
			} else {
				return false;
			}
		} else {
			return false;
		}
	}

	/**
	 * return whether an attribute(with class or instance type) is a new added
	 * object
	 * 
	 * @param type
	 * @param value
	 * @return
	 */
	public boolean isNewAdded(String attrName, boolean isClassAttr) {
		if (isClassAttr) {
			return isNewAdded(SchemeInnerType.TYPE_CLASSATTRIBUTE, attrName);
		} else {
			return isNewAdded(SchemeInnerType.TYPE_ATTRIBUTE, attrName);
		}

	}

	/**
	 * return all changes about index of a schema
	 * 
	 * @return
	 */
	public List<SchemeChangeLog> getIndexChangeLogs() {
		List<SchemeChangeLog> list = new ArrayList<SchemeChangeLog>();
		list.addAll(getChangeLogs(SchemeInnerType.TYPE_INDEX));
		return list;
	}

	/**
	 * return all changes about FK of a schema
	 * 
	 * @return
	 */
	public List<SchemeChangeLog> getFKChangeLogs() {
		List<SchemeChangeLog> list = new ArrayList<SchemeChangeLog>();
		list.addAll(getChangeLogs(SchemeInnerType.TYPE_FK));
		return list;
	}

	/**
	 * return all changes about attribute of a schema
	 * 
	 * @return
	 */
	public List<SchemeChangeLog> getAttrChangeLogs() {
		List<SchemeChangeLog> list = new ArrayList<SchemeChangeLog>();
		list.addAll(getChangeLogs(SchemeInnerType.TYPE_ATTRIBUTE));
		return list;
	}

	/**
	 * return all changes about attribute of a schema
	 * 
	 * @return
	 */
	public List<SchemeChangeLog> getClassAttrChangeLogs() {
		List<SchemeChangeLog> list = new ArrayList<SchemeChangeLog>();
		list.addAll(getChangeLogs(SchemeInnerType.TYPE_CLASSATTRIBUTE));
		return list;
	}

	/**
	 * return all changes about a given type of a schema
	 * 
	 * @return
	 */
	public List<SchemeChangeLog> getChangeLogs(SchemeInnerType type) {
		List<SchemeChangeLog> list = new ArrayList<SchemeChangeLog>();
		List<SchemeChangeLog> changeLogList = getChangeList();
		for (SchemeChangeLog log : changeLogList) {
			if (log.getType().equals(type)) {
				list.add(log);
			}
		}
		return list;
	}

	/**
	 * find object in change list with a given type and a given value to new
	 * value or old value
	 * 
	 * @param type
	 * @param value
	 * @param isOld
	 * @return
	 */
	private SchemeChangeLog findModifySchemeChangeLog(SchemeInnerType type,
			String value, boolean isOld) {
		if (value == null) {
			return null;
		}
		List<SchemeChangeLog> changeLogList = getChangeList();
		for (SchemeChangeLog log : changeLogList) {
			if (log.getType().equals(type)) {
				String queryValue = (isOld ? log.getOldValue()
						: log.getNewValue());
				if (value.equals(queryValue)) {
					return log;
				}
			}
		}
		return null;
	}

	/**
	 * add a change log to change list
	 * <li> 1-2, 2-3 -->1-3
	 * 
	 * @param log
	 */
	public void addSchemeChangeLog(SchemeChangeLog log) {
		if (!isNewTableFlag) {
			SchemeChangeLog slog = findModifySchemeChangeLog(log.getType(),
					log.getOldValue(), false);
			if (slog != null) {
				changeList.remove(slog);
				log.setOldValue(slog.getOldValue());
			}
			if (log.getOldValue() != null || log.getNewValue() != null) {
				changeList.add(log);
			}
		}
	}

	public boolean isNewTableFlag() {
		return isNewTableFlag;
	}

	public void setNewTableFlag(boolean isNewTableFlag) {
		this.isNewTableFlag = isNewTableFlag;
	}

	public DatabaseInfo getDatabase() {
		return database;
	}

	public void setDatabase(DatabaseInfo database) {
		this.database = database;
	}

}
