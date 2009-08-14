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
package com.cubrid.cubridmanager.core.logs.task;

import com.cubrid.cubridmanager.core.SetupEnvTestCase;
import com.cubrid.cubridmanager.core.logs.model.GetExecuteCasRunnerResultInfo;


/**
 * Test GetExecuteCasRunnerResultTask
 * 
 * @author cn12978
 * @version 1.0 - 2009-6-24 created by cn12978
 */
public class GetExecuteCasRunnerResultTaskTest extends
		SetupEnvTestCase {
	public void testExecute() {
		GetExecuteCasRunnerContentResultTask task = new GetExecuteCasRunnerContentResultTask(site);
		task.setBrokerName("query_editor");
		task.setUserName("admin");
		task.setPasswd("1111");
		task.setNumThread("1");
		task.setRepeatCount("3");
		task.setShowQueryResult("");
		task.setShowQueryResult("");
		task.setDbName("demodb");
		task.setExecuteLogFile("no");
		String queryString = "Select class_name,owner_name,class_type,is_system_class,partitioned from db_class as a where a.class_name not in (select partition_class_name from db_partition) and is_system_class='NO' and class_type='CLASS'";
		String[] queryStringArr = queryString.split("\\r\\n");
		task.setLogstring(queryStringArr);
        task.execute();  
        GetExecuteCasRunnerResultInfo info =  task.getContent();
        assertEquals(info.getTaskName(),task.getTaskname());
        assertNotNull(info.getQueryResultFile());
        assertNotNull(info.getQueryResultFileNum());
        assertNotNull(info.getResult());
     
	 
	}
}
