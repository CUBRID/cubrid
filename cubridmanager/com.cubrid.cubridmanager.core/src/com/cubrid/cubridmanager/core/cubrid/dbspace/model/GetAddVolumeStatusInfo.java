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
package com.cubrid.cubridmanager.core.cubrid.dbspace.model;

import com.cubrid.cubridmanager.core.common.model.IModel;

/**
 * A Model class to get adding volume status message
 * 
 * @author lizhiqiang
 * 2009-4-17
 */
public class GetAddVolumeStatusInfo implements IModel {

	private String dbname;
	private String note;
	private String freespace;
	private String volpath;
	

	/* (non-Javadoc)
	 * @see com.cubrid.cubridmanager.core.common.model.IModel#getTaskName()
	 */
	public String getTaskName() {
		return "getaddvolstatus";
	}


	public String getDbname() {
    	return dbname;
    }


	public void setDbname(String dbname) {
    	this.dbname = dbname;
    }


	public String getNote() {
    	return note;
    }


	public void setNote(String note) {
    	this.note = note;
    }


	public String getFreespace() {
    	return freespace;
    }


	public void setFreespace(String freespace) {
    	this.freespace = freespace;
    }


	public String getVolpath() {
    	return volpath;
    }


	public void setVolpath(String volpath) {
    	this.volpath = volpath;
    }

}
