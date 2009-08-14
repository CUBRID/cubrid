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
package com.cubrid.cubridmanager.core.cubrid.database.model;

import java.util.ArrayList;
import java.util.List;

/**
 * 
 * This class is responsible to cache CUBRID database unload information
 * 
 * @author pangqiren
 * @version 1.0 - 2009-6-4 created by pangqiren
 */
public class DbUnloadInfo {

	private String dbName;
	private List<String> schemaPathList = new ArrayList<String>();
	private List<String> schemaDateList = new ArrayList<String>();
	private List<String> objectPathList = new ArrayList<String>();
	private List<String> objectDateList = new ArrayList<String>();
	private List<String> indexPathList = new ArrayList<String>();
	private List<String> indexDateList = new ArrayList<String>();
	private List<String> triggerPathList = new ArrayList<String>();
	private List<String> triggerDateList = new ArrayList<String>();

	/**
	 * 
	 * Get database name
	 * 
	 * @return
	 */
	public String getDbName() {
		return dbName;
	}

	/**
	 * 
	 * Set database name
	 * 
	 * @param dbName
	 */
	public void setDbName(String dbName) {
		this.dbName = dbName;
	}

	/**
	 * 
	 * Get schema path list
	 * 
	 * @return
	 */
	public List<String> getSchemaPathList() {
		return schemaPathList;
	}

	/**
	 * 
	 * Set schema path list
	 * 
	 * @param schemaPathList
	 */
	public void setSchemaPathList(List<String> schemaPathList) {
		this.schemaPathList = schemaPathList;
	}

	/**
	 * 
	 * Get schema date list
	 * 
	 * @return
	 */
	public List<String> getSchemaDateList() {
		return schemaDateList;
	}

	/**
	 * 
	 * Set schema date list
	 * 
	 * @param schemaDateList
	 */
	public void setSchemaDateList(List<String> schemaDateList) {
		this.schemaDateList = schemaDateList;
	}

	/**
	 * 
	 * Get object path list
	 * 
	 * @return
	 */
	public List<String> getObjectPathList() {
		return objectPathList;
	}

	/**
	 * 
	 * Set object path list
	 * 
	 * @param objectPathList
	 */
	public void setObjectPathList(List<String> objectPathList) {
		this.objectPathList = objectPathList;
	}

	/**
	 * 
	 * Get object date list
	 * 
	 * @return
	 */
	public List<String> getObjectDateList() {
		return objectDateList;
	}

	/**
	 * 
	 * Set object date list
	 * 
	 * @param objectDateList
	 */
	public void setObjectDateList(List<String> objectDateList) {
		this.objectDateList = objectDateList;
	}

	/**
	 * 
	 * Get index path list
	 * 
	 * @return
	 */
	public List<String> getIndexPathList() {
		return indexPathList;
	}

	/**
	 * 
	 * Set index path list
	 * 
	 * @param indexPathList
	 */
	public void setIndexPathList(List<String> indexPathList) {
		this.indexPathList = indexPathList;
	}

	/**
	 * 
	 * Get index date list
	 * 
	 * @return
	 */
	public List<String> getIndexDateList() {
		return indexDateList;
	}

	/**
	 * 
	 * Set index date list
	 * 
	 * @param indexDateList
	 */
	public void setIndexDateList(List<String> indexDateList) {
		this.indexDateList = indexDateList;
	}

	/**
	 * 
	 * Get trigger path list
	 * 
	 * @return
	 */
	public List<String> getTriggerPathList() {
		return triggerPathList;
	}

	/**
	 * 
	 * Set trigger path list
	 * 
	 * @param triggerPathList
	 */
	public void setTriggerPathList(List<String> triggerPathList) {
		this.triggerPathList = triggerPathList;
	}

	/**
	 * 
	 * Get trigger date list
	 * 
	 * @return
	 */
	public List<String> getTriggerDateList() {
		return triggerDateList;
	}

	/**
	 * 
	 * Set trigger date list
	 * 
	 * @param triggerDateList
	 */
	public void setTriggerDateList(List<String> triggerDateList) {
		this.triggerDateList = triggerDateList;
	}

}
