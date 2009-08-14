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

/**
 * 
 * This class will cached database backup history information
 * 
 * @author pangqiren
 * @version 1.0 - 2009-6-4 created by pangqiren
 */
public class DbBackupHistoryInfo {

	private String level = null;
	private String path = null;
	private String size = null;
	private String date = null;

	/**
	 * The constructor
	 * 
	 * @param level
	 * @param path
	 * @param size
	 * @param date
	 */
	public DbBackupHistoryInfo(String level, String path, String size,
			String date) {
		super();
		this.level = level;
		this.path = path;
		this.size = size;
		this.date = date;
	}

	/**
	 * 
	 * Get backup level
	 * 
	 * @return
	 */
	public String getLevel() {
		return level;
	}

	/**
	 * 
	 * Set backup level
	 * 
	 * @param level
	 */
	public void setLevel(String level) {
		this.level = level;
	}

	/**
	 * 
	 * Get backup path
	 * 
	 * @return
	 */
	public String getPath() {
		return path;
	}

	/**
	 * 
	 * Set backup path
	 * 
	 * @param path
	 */
	public void setPath(String path) {
		this.path = path;
	}

	/**
	 * 
	 * Get backup volume size
	 * 
	 * @return
	 */
	public String getSize() {
		return size;
	}

	/**
	 * 
	 * Set backup volume size
	 * 
	 * @param size
	 */
	public void setSize(String size) {
		this.size = size;
	}

	/**
	 * 
	 * Get backup date
	 * 
	 * @return
	 */
	public String getDate() {
		return date;
	}

	/**
	 * 
	 * Set backup date
	 * 
	 * @param date
	 */
	public void setDate(String date) {
		this.date = date;
	}

}
