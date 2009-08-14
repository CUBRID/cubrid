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
package com.cubrid.cubridmanager.core.cubrid.dbspace.task;

import com.cubrid.cubridmanager.core.common.model.ServerInfo;
import com.cubrid.cubridmanager.core.common.socket.SocketTask;

/**
 * 
 * A task  that set auto adding volume
 * @author lizhiqiang
 * 2009-4-15
 */
public class SetAutoAddVolumeTask extends SocketTask {

	private static final String[] sendMSGItems = new String[] { 
		"task",
		"token",
		"dbname",
		"data",
	    "data_warn_outofspace",
	    "data_ext_page",
	    "index",
	    "index_warn_outofspace",
	    "index_ext_page"
	};
	
	public SetAutoAddVolumeTask(ServerInfo serverInfo) {
	    super("setautoaddvol", serverInfo,sendMSGItems);
    }

	/**
     * @param dbname the dbname to set
     */
    public void setDbname(String dbname) {
    	super.setMsgItem("dbname", dbname);
    }

	/**
     * @param data the data to set
     */
    public void setData(String data) {
    	super.setMsgItem("data", data);
    }

	/**
     * @param data_warn_outofspace the data_warn_outofspace to set
     */
    public void setData_warn_outofspace(String data_warn_outofspace) {
    	super.setMsgItem("data_warn_outofspace", data_warn_outofspace);
    }


	/**
     * @param data_ext_page the data_ext_page to set
     */
    public void setData_ext_page(String data_ext_page) {
    	super.setMsgItem("data_ext_page", data_ext_page);
    }

	/**
     * @param index the index to set
     */
    public void setIndex(String index) {
    	super.setMsgItem("index", index);
    }


	/**
     * @param index_warn_outofspace the index_warn_outofspace to set
     */
    public void setIndex_warn_outofspace(String index_warn_outofspace) {
    	super.setMsgItem("index_warn_outofspace", index_warn_outofspace);
    }

	/**
     * @param index_ext_page the index_ext_page to set
     */
    public void setIndex_ext_page(String index_ext_page) {
    	super.setMsgItem("index_ext_page", index_ext_page);
    }

}
