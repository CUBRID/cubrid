/*
 * Copyright (C) 2009 Search Solution Corporation. All rights reserved by Search
 * Solution.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *  - Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *  - Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *  - Neither the name of the <ORGANIZATION> nor the names of its contributors
 * may be used to endorse or promote products derived from this software without
 * specific prior written permission.
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
package com.cubrid.cubridmanager.core.cubrid.dbspace.task;

import com.cubrid.cubridmanager.core.common.model.ServerInfo;
import com.cubrid.cubridmanager.core.common.socket.SocketTask;

/**
 * A task that add volume DB 
 * 
 * @author lizhiqiang 2009-4-22
 */
public class AddVolumeDbTask extends SocketTask {
	private static final String[] sendMSGItems = new String[] { 
		"task", 
		"token",
		"dbname",
		"volname", 
		"purpose",
	    "path",
	    "numberofpages",
	     "size_need_mb"
	    };

	/**
	 * The constructor
	 * 
	 * @param serverInfo
	 */
	public AddVolumeDbTask(ServerInfo serverInfo) {
		super("addvoldb", serverInfo, sendMSGItems);

	}

	public void setDbname(String dbname) {
		super.setMsgItem("dbname", dbname);
	}

	public void setVolname(String volname) {
		super.setMsgItem("volname", volname);
	}

	public void setPurpose(String purpose) {
		super.setMsgItem("purpose", purpose);
	}

	public void setPath(String path) {
		super.setMsgItem("path", path);
	}

	public void setNumberofpages(String numberofpages) {
		super.setMsgItem("numberofpages", numberofpages);
	}

	public void setSize_need_mb(String size_need_mb) {
		super.setMsgItem("size_need_mb", size_need_mb);
	}

}
