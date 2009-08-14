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
package com.cubrid.cubridmanager.core.cubrid.jobauto.task;

import java.util.List;

import com.cubrid.cubridmanager.core.common.model.ServerInfo;
import com.cubrid.cubridmanager.core.common.socket.SocketTask;

/**
 * This task is responsible for setting all query plans
 * 
 * @author lizhiqiang 2009-4-9
 */
public class SetQueryPlanListTask extends
		SocketTask {

	private String dbname;

	/**
	 * The constructor
	 * 
	 * @param serverInfo
	 */
	public SetQueryPlanListTask(ServerInfo serverInfo) {
		super("setautoexecquery", serverInfo);
	}

	/**
	 * 
	 * Sets the dbName
	 * 
	 * @param dbName
	 */
	public void setDbname(String dbname) {
		this.dbname = dbname;
	}

	/**
	 * Builds a message which includes the items exclusive of task,token and
	 * dbname
	 * 
	 * @param list
	 * @return
	 */
	public void buildMsg(List<String> list) {
		StringBuffer msg = new StringBuffer();
		msg.append("dbname:" + dbname);
		msg.append("\n");
		msg.append("open:planlist\n");
		for (String string : list) {
			msg.append("open:queryplan\n");
			msg.append(string);
			msg.append("close:queryplan\n");
		}
		msg.append("close:planlist\n");
		appendSendMsg = msg.toString();
	}

}
