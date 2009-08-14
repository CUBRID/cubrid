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
package com.cubrid.cubridmanager.core.broker.task;

import java.util.HashMap;
import java.util.Map;

import com.cubrid.cubridmanager.core.SetupEnvTestCase;
import com.cubrid.cubridmanager.core.common.model.ConfConstants;

/**
 * Tests the StartBrokerTask type
 * @author lizhiqiang
 * @version 1.0 - 2009-5-15 created by lizhiqiang
 */
public class DeleteBrokerTaskTest extends
		SetupEnvTestCase {
	public void testExecute() {
		Map<String,Map<String,String>> confParaMap = new HashMap<String,Map<String,String>>();
		Map<String, String> basicMap = new HashMap<String, String>();
		basicMap.put(ConfConstants.MASTER_SHM_ID, "3001");
		basicMap.put(ConfConstants.ADMIN_LOG_FILE, "log/broker/cubrid_broker.log");
		
		confParaMap.put(ConfConstants.broker_sectionName, basicMap);
		
	
	    	Map<String,String> paraMap = new HashMap<String,String>();
	    	paraMap.put(ConfConstants.BROKER_PORT, "30000");
	    	paraMap.put(ConfConstants.SERVICE, "ON");
	    	paraMap.put(ConfConstants.MIN_NUM_APPL_SERVER, "5");
	    	paraMap.put(ConfConstants.MAX_NUM_APPL_SERVER, "40");
	    	paraMap.put(ConfConstants.APPL_SERVER_SHM_ID, "30000");
	    	paraMap.put(ConfConstants.APPL_SERVER_MAX_SIZE, "20");
	    	paraMap.put(ConfConstants.LOG_DIR, "log/broker/sql_log");
	    	paraMap.put(ConfConstants.ERROR_LOG_DIR,"log/broker/error_log");
	    	paraMap.put(ConfConstants.SQL_LOG, "ON");
	    	paraMap.put(ConfConstants.TIME_TO_KILL, "120");
	    	paraMap.put(ConfConstants.SESSION_TIMEOUT, "300");
	    	paraMap.put(ConfConstants.KEEP_CONNECTION,"AUTO");
	    	confParaMap.put("query_editor", paraMap);
	    	
	    	paraMap = new HashMap<String,String>();
	    	paraMap.put(ConfConstants.BROKER_PORT, "33000");
	    	paraMap.put(ConfConstants.SERVICE, "ON");
	    	paraMap.put(ConfConstants.MIN_NUM_APPL_SERVER, "5");
	    	paraMap.put(ConfConstants.MAX_NUM_APPL_SERVER, "40");
	    	paraMap.put(ConfConstants.APPL_SERVER_SHM_ID, "33000");
	    	paraMap.put(ConfConstants.APPL_SERVER_MAX_SIZE, "20");
	    	paraMap.put(ConfConstants.LOG_DIR, "log/broker/sql_log");
	    	paraMap.put(ConfConstants.ERROR_LOG_DIR,"log/broker/error_log");
	    	paraMap.put(ConfConstants.SQL_LOG, "ON");
	    	paraMap.put(ConfConstants.TIME_TO_KILL, "120");
	    	paraMap.put(ConfConstants.SESSION_TIMEOUT, "300");
	    	paraMap.put(ConfConstants.KEEP_CONNECTION,"AUTO");
	    	confParaMap.put("broker1", paraMap);
	    	
	    	paraMap = new HashMap<String,String>();
	    	paraMap.put(ConfConstants.BROKER_PORT, "35000");
	    	paraMap.put(ConfConstants.SERVICE, "ON");
	    	paraMap.put(ConfConstants.MIN_NUM_APPL_SERVER, "5");
	    	paraMap.put(ConfConstants.MAX_NUM_APPL_SERVER, "40");
	    	paraMap.put(ConfConstants.APPL_SERVER_SHM_ID, "35000");
	    	paraMap.put(ConfConstants.APPL_SERVER_MAX_SIZE, "20");
	    	paraMap.put(ConfConstants.LOG_DIR, "log/broker/sql_log");
	    	paraMap.put(ConfConstants.ERROR_LOG_DIR,"log/broker/error_log");
	    	paraMap.put(ConfConstants.SQL_LOG, "ON");
	    	paraMap.put(ConfConstants.TIME_TO_KILL, "120");
	    	paraMap.put(ConfConstants.SESSION_TIMEOUT, "300");
	    	paraMap.put(ConfConstants.KEEP_CONNECTION,"AUTO");
	    	confParaMap.put("broker2", paraMap);
	
		SetBrokerConfParameterTask setBrokerConfParameterTask = new SetBrokerConfParameterTask(site);
		setBrokerConfParameterTask.setConfParameters(confParaMap);
		setBrokerConfParameterTask.execute();
		
		DeleteBrokerTask task = new DeleteBrokerTask(site);
		task.setBname("broker2");
        task.execute();
		task.setUsingSpecialDelimiter(false);
		
       assertTrue(task.isSuccess());
	}
}
