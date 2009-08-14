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

import java.util.List;

import com.cubrid.cubridmanager.core.SetupEnvTestCase;
import com.cubrid.cubridmanager.core.logs.model.GetExecuteCasRunnerResultInfo;
import com.cubrid.cubridmanager.core.logs.model.LogContentInfo;
import com.cubrid.cubridmanager.core.logs.model.ManagerLogInfo;
import com.cubrid.cubridmanager.core.logs.model.ManagerLogInfoList;
import com.cubrid.cubridmanager.core.logs.model.ManagerLogInfos;


/**
 * Test GetManagerLogListTask
 * 
 * @author cn12978
 * @version 1.0 - 2009-6-24 created by cn12978
 */
public class GetManagerLogListTaskTest extends
		SetupEnvTestCase {
	public void testExecute() {
		GetManagerLogListTask task = new GetManagerLogListTask(site);
        task.execute();  
    	ManagerLogInfos managerLogInfos = (ManagerLogInfos) task.getLogContent();
    	assertNotNull(managerLogInfos);
    	ManagerLogInfoList accesss = managerLogInfos.getAccessLog();
    	assertNotNull(accesss);
    	List<ManagerLogInfo> listAccess = accesss.getManagerLogInfoList();
    	assertNotNull(listAccess);
    	ManagerLogInfo infoAccess = listAccess.get(0);
    	assertNotNull(infoAccess);
    	assertEquals(infoAccess.getUser(),"admin");
    	assertNotNull(infoAccess.getTaskName());
    	assertNotNull(infoAccess.getTime());
    	assertNull(infoAccess.getErrorNote());
    	
    	ManagerLogInfoList error = managerLogInfos.getErrorLog();
     	assertNotNull(error);
    	List<ManagerLogInfo> listError = accesss.getManagerLogInfoList();
    	assertNotNull(listError);
    	ManagerLogInfo infoError = listAccess.get(0);
    	assertNotNull(infoError);
    	assertEquals(infoError.getUser(),"admin");
    	assertNotNull(infoError.getTaskName());
    	assertNotNull(infoError.getTime());
    	assertNull(infoError.getErrorNote());
	 
	}
}
