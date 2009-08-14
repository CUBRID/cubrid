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
package com.cubrid.cubridmanager.core.monitoring.model;

import java.util.ArrayList;
import java.util.List;

import com.cubrid.cubridmanager.core.SetupEnvTestCase;

/**
 * TODO: how to write comments The purpose of the class Known bugs The
 * development/maintenance history of the class Document applicable invariants
 * The concurrency strategy
 * 
 * @author lizhiqiang 2009-5-11
 */
public class MonitorModelTest extends SetupEnvTestCase {
	public void testModelDiagActivityResult() {
		DiagActivityResult bean = new DiagActivityResult();
		bean = new DiagActivityResult(new DiagActivityResult());
		bean.setEventClass("eventClass");
		assertEquals(bean.getEventClass(), "eventClass");
		bean.setTextData("textData");
		assertEquals(bean.getTextData(), "textData");
		bean.setBinData("binData");
		assertEquals(bean.getBinData(), "binData");
		bean.setIntegerData("integerData");
		assertEquals(bean.getIntegerData(), "integerData");
		bean.setTime("time");
		assertEquals(bean.getTime(), "time");
	}

	public void testModelDiagStatusResult() {
		DiagStatusResult bean = new DiagStatusResult();
		bean.initStatusResult();
		bean.setCas_mon_req("cas_mon_req");
		assertEquals(bean.getCas_mon_req(), "cas_mon_req");
		bean.setCas_mon_act_session("cas_mon_act_session");
		assertEquals(bean.getCas_mon_act_session(), "cas_mon_act_session");
		bean.setCas_mon_tran("cas_mon_tran");
		assertEquals(bean.getCas_mon_tran(), "cas_mon_tran");
		bean.setCas_mon_query("cas_mon_query");
		assertEquals(bean.getCas_mon_query(), "cas_mon_query");
		bean.setServer_query_open_page("server_query_open_page");
		assertEquals(bean.getServer_query_open_page(), "server_query_open_page");
		bean.setServer_query_opened_page("server_query_opened_page");
		assertEquals(bean.getServer_query_opened_page(), "server_query_opened_page");
		bean.setServer_query_slow_query("server_query_slow_query");
		assertEquals(bean.getServer_query_slow_query(), "server_query_slow_query");
		bean.setServer_query_full_scan("server_query_full_scan");
		assertEquals(bean.getServer_query_full_scan(), "server_query_full_scan");
		bean.setServer_conn_cli_request("server_conn_cli_request");
		assertEquals(bean.getServer_conn_cli_request(), "server_conn_cli_request");
		bean.setServer_conn_aborted_clients("server_conn_aborted_clients");
		assertEquals(bean.getServer_conn_aborted_clients(), "server_conn_aborted_clients");
		bean.setServer_conn_conn_req("server_conn_conn_req");
		assertEquals(bean.getServer_conn_conn_req(), "server_conn_conn_req");
		bean.setServer_conn_conn_reject("server_conn_conn_reject");
		assertEquals(bean.getServer_conn_conn_reject(), "server_conn_conn_reject");
		bean.setServer_buffer_page_write("server_buffer_page_write");
		assertEquals(bean.getServer_buffer_page_write(), "server_buffer_page_write");
		bean.setServer_buffer_page_read("server_buffer_page_read");
		assertEquals(bean.getServer_buffer_page_read(), "server_buffer_page_read");
		bean.setServer_lock_deadlock("server_lock_deadlock");
		assertEquals(bean.getServer_lock_deadlock(), "server_lock_deadlock");
		bean.setServer_lock_request("server_lock_request");
		assertEquals(bean.getServer_lock_request(), "server_lock_request");
		bean.initStatusResult();
		bean.copy_from(new DiagStatusResult());
		bean.getDelta(new DiagStatusResult(), new DiagStatusResult());
		bean.getDiagStatusResultMap();
	}

	public void testModelStatusTemplateInfo() {
		StatusTemplateInfo bean = new StatusTemplateInfo();
		bean.setName("name");
		assertEquals(bean.getName(), "name");
		bean.setDesc("desc");
		assertEquals(bean.getDesc(), "desc");
		bean.setDb_name("db_name");
		assertEquals(bean.getDb_name(), "db_name");
		bean.setSampling_term("sampling_term");
		assertEquals(bean.getSampling_term(), "sampling_term");
		bean.getTargetConfigInfoList();
		bean.addTarget_config(new TargetConfigInfo());
	}

	public void testModelStatusTemplateInfoList() {
		StatusTemplateInfoList bean = new StatusTemplateInfoList();
		bean.addTemplate(new StatusTemplateInfo());
		assertEquals(bean.getStatusTemplateInfoList() != null, true);
	}

	public void testModelStatusTemplateInfos() {
		StatusTemplateInfos bean = new StatusTemplateInfos();
		bean.getStatusTemplateInfoList();
		assertEquals(bean.getTaskName(), "getstatustemplate");
		bean.addTemplateList(new StatusTemplateInfoList());
	}

	public void testModelTargetConfigInfo() {
		TargetConfigInfo bean = new TargetConfigInfo();
		String[] s = new String[]
			{
			        "server_query_open_page",
			        "server_query_open_page"
			};
		bean.setServer_query_open_page(new String[]
			{
			        "server_query_open_page",
			        "server_query_open_page"
			});
		assertEquals(bean.getServer_query_open_page()[0], "server_query_open_page");
		bean.setServer_query_opened_page(s);
		assertEquals(bean.getServer_query_opened_page()[0], "server_query_open_page");
		bean.setServer_query_slow_query(s);
		assertEquals(bean.getServer_query_slow_query()[0], "server_query_open_page");
		bean.setServer_query_full_scan(s);
		assertEquals(bean.getServer_query_full_scan()[0], "server_query_open_page");
		bean.setServer_conn_cli_request(s);
		assertEquals(bean.getServer_conn_cli_request()[0], "server_query_open_page");
		bean.setServer_conn_aborted_clients(s);
		assertEquals(bean.getServer_conn_aborted_clients()[0], "server_query_open_page");
		bean.setServer_conn_conn_req(s);
		assertEquals(bean.getServer_conn_conn_req()[0], "server_query_open_page");
		bean.setServer_conn_conn_reject(s);
		assertEquals(bean.getServer_conn_conn_reject()[0], "server_query_open_page");
		bean.setServer_buffer_page_write(s);
		assertEquals(bean.getServer_buffer_page_write()[0], "server_query_open_page");
		bean.setServer_buffer_page_read(s);
		assertEquals(bean.getServer_buffer_page_read()[0], "server_query_open_page");
		bean.setServer_lock_deadlock(s);
		assertEquals(bean.getServer_lock_deadlock()[0], "server_query_open_page");
		bean.setServer_lock_request(s);
		assertEquals(bean.getServer_lock_request()[0], "server_query_open_page");
		bean.setCas_st_request(s);
		assertEquals(bean.getCas_st_request()[0], "server_query_open_page");
		bean.setCas_st_transaction(s);
		assertEquals(bean.getCas_st_transaction()[0], "server_query_open_page");
		bean.setCas_st_active_session(s);
		assertEquals(bean.getCas_st_active_session()[0], "server_query_open_page");
		bean.setCas_st_query(s);
		assertEquals(bean.getCas_st_query()[0], "server_query_open_page");
		bean.getList();
	}
}
