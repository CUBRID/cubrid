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
 * 
 * @author lizhiqiang
 * 2009-4-14
 */
public class GetAutoAddVolumeInfo implements IModel{
    private String dbname;
    private String data;
    private String data_warn_outofspace;
    private String data_ext_page;
    
    private String index;
    private String index_warn_outofspace;
    private String index_ext_page;
    
	public String getTaskName() {
	    return "getautoaddvol";
    }

	public String getDbname() {
    	return dbname;
    }

	public void setDbname(String dbname) {
    	this.dbname = dbname;
    }

	public String getData() {
    	return data;
    }

	public void setData(String data) {
    	this.data = data;
    }

	public String getData_warn_outofspace() {
    	return data_warn_outofspace;
    }

	public void setData_warn_outofspace(String data_warn_outofspace) {
    	this.data_warn_outofspace = data_warn_outofspace;
    }

	public String getData_ext_page() {
    	return data_ext_page;
    }

	public void setData_ext_page(String data_ext_page) {
    	this.data_ext_page = data_ext_page;
    }

	public String getIndex() {
    	return index;
    }

	public void setIndex(String index) {
    	this.index = index;
    }

	public String getIndex_warn_outofspace() {
    	return index_warn_outofspace;
    }

	public void setIndex_warn_outofspace(String index_warn_outofspace) {
    	this.index_warn_outofspace = index_warn_outofspace;
    }

	public String getIndex_ext_page() {
    	return index_ext_page;
    }

	public void setIndex_ext_page(String index_ext_page) {
    	this.index_ext_page = index_ext_page;
    }

}
