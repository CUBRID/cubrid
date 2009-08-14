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

/**
 * 
 * Status template target config information
 * 
 * @author by lizhiqiang 2009-4-29
 */
public class TargetConfigInfo {

	private String[] server_query_open_page;
	private String[] server_query_opened_page;
	private String[] server_query_slow_query;
	private String[] server_query_full_scan;
	private String[] server_conn_cli_request;
	private String[] server_conn_aborted_clients;
	private String[] server_conn_conn_req;
	private String[] server_conn_conn_reject;
	private String[] server_buffer_page_write;
	private String[] server_buffer_page_read;
	private String[] server_lock_deadlock;
	private String[] server_lock_request;
	private String[] cas_st_request;
	private String[] cas_st_transaction;
	private String[] cas_st_active_session;
	private String[] cas_st_query;
	private String[] cas_st_long_query;
	private String[] cas_st_long_tran;
	private String[] cas_st_error_query;

	private List<String[]> list = new ArrayList<String[]>();;

	public String[] getServer_query_open_page() {
		return server_query_open_page;
	}

	public void setServer_query_open_page(String[] server_query_open_page) {
		this.server_query_open_page = server_query_open_page;
		if (null != server_query_open_page) {
			String[] strings = new String[3];
			strings[0] = "server_query_open_page";
			strings[1] = server_query_open_page[0];
			strings[2] = server_query_open_page[1];
			list.add(strings);
		}
	}

	public String[] getServer_query_opened_page() {
		return server_query_opened_page;
	}

	public void setServer_query_opened_page(String[] server_query_opened_page) {
		this.server_query_opened_page = server_query_opened_page;
		if (null != server_query_opened_page) {
			String[] strings = new String[3];
			strings[0] = "server_query_opened_page";
			strings[1] = server_query_opened_page[0];
			strings[2] = server_query_opened_page[1];
			list.add(strings);
		}
	}

	public String[] getServer_query_slow_query() {
		return server_query_slow_query;
	}

	public void setServer_query_slow_query(String[] server_query_slow_query) {
		this.server_query_slow_query = server_query_slow_query;
		if (null != server_query_slow_query) {
			String[] strings = new String[3];
			strings[0] = "server_query_slow_query";
			strings[1] = server_query_slow_query[0];
			strings[2] = server_query_slow_query[1];
			list.add(strings);
		}
	}

	public String[] getServer_query_full_scan() {
		return server_query_full_scan;
	}

	public void setServer_query_full_scan(String[] server_query_full_scan) {
		this.server_query_full_scan = server_query_full_scan;
		if (null != server_query_full_scan) {
			String[] strings = new String[3];
			strings[0] = "server_query_full_scan";
			strings[1] = server_query_full_scan[0];
			strings[2] = server_query_full_scan[1];
			list.add(strings);
		}
	}

	public String[] getServer_conn_cli_request() {
		return server_conn_cli_request;
	}

	public void setServer_conn_cli_request(String[] server_conn_cli_request) {
		this.server_conn_cli_request = server_conn_cli_request;
		if (null != server_conn_cli_request) {
			String[] strings = new String[3];
			strings[0] = "server_conn_cli_request";
			strings[1] = server_conn_cli_request[0];
			strings[2] = server_conn_cli_request[1];
			list.add(strings);
		}
	}

	public String[] getServer_conn_aborted_clients() {
		return server_conn_aborted_clients;
	}

	public void setServer_conn_aborted_clients(
			String[] server_conn_aborted_clients) {
		this.server_conn_aborted_clients = server_conn_aborted_clients;
		if (null != server_conn_aborted_clients) {
			String[] strings = new String[3];
			strings[0] = "server_conn_aborted_clients";
			strings[1] = server_conn_aborted_clients[0];
			strings[2] = server_conn_aborted_clients[1];
			list.add(strings);
		}
	}

	public String[] getServer_conn_conn_req() {
		return server_conn_conn_req;
	}

	public void setServer_conn_conn_req(String[] server_conn_conn_req) {
		this.server_conn_conn_req = server_conn_conn_req;
		if (null != server_conn_conn_req) {
			String[] strings = new String[3];
			strings[0] = "server_conn_conn_req";
			strings[1] = server_conn_conn_req[0];
			strings[2] = server_conn_conn_req[1];
			list.add(strings);
		}
	}

	public String[] getServer_conn_conn_reject() {
		return server_conn_conn_reject;
	}

	public void setServer_conn_conn_reject(String[] server_conn_conn_reject) {
		this.server_conn_conn_reject = server_conn_conn_reject;
		if (null != server_conn_conn_reject) {
			String[] strings = new String[3];
			strings[0] = "server_conn_conn_reject";
			strings[1] = server_conn_conn_reject[0];
			strings[2] = server_conn_conn_reject[1];
			list.add(strings);
		}
	}

	public String[] getServer_buffer_page_write() {
		return server_buffer_page_write;
	}

	public void setServer_buffer_page_write(String[] server_buffer_page_write) {
		this.server_buffer_page_write = server_buffer_page_write;
		if (null != server_buffer_page_write) {
			String[] strings = new String[3];
			strings[0] = "server_buffer_page_write";
			strings[1] = server_buffer_page_write[0];
			strings[2] = server_buffer_page_write[1];
			list.add(strings);
		}
	}

	public String[] getServer_buffer_page_read() {
		return server_buffer_page_read;
	}

	public void setServer_buffer_page_read(String[] server_buffer_page_read) {
		this.server_buffer_page_read = server_buffer_page_read;
		if (null != server_buffer_page_read) {
			String[] strings = new String[3];
			strings[0] = "server_buffer_page_read";
			strings[1] = server_buffer_page_read[0];
			strings[2] = server_buffer_page_read[1];
			list.add(strings);
		}
	}

	public String[] getServer_lock_deadlock() {
		return server_lock_deadlock;
	}

	public void setServer_lock_deadlock(String[] server_lock_deadlock) {
		this.server_lock_deadlock = server_lock_deadlock;
		if (null != server_lock_deadlock) {
			String[] strings = new String[3];
			strings[0] = "server_lock_deadlock";
			strings[1] = server_lock_deadlock[0];
			strings[2] = server_lock_deadlock[1];
			list.add(strings);
		}
	}

	public String[] getServer_lock_request() {
		return server_lock_request;
	}

	public void setServer_lock_request(String[] server_lock_request) {
		this.server_lock_request = server_lock_request;
		if (null != server_lock_request) {
			String[] strings = new String[3];
			strings[0] = "server_lock_request";
			strings[1] = server_lock_request[0];
			strings[2] = server_lock_request[1];
			list.add(strings);
		}
	}

	public String[] getCas_st_request() {
		return cas_st_request;
	}

	public void setCas_st_request(String[] cas_st_request) {
		this.cas_st_request = cas_st_request;
		if (null != cas_st_request) {
			String[] strings = new String[3];
			strings[0] = "cas_st_request";
			strings[1] = cas_st_request[0];
			strings[2] = cas_st_request[1];
			list.add(strings);
		}
	}

	public String[] getCas_st_transaction() {
		return cas_st_transaction;
	}

	public void setCas_st_transaction(String[] cas_st_transaction) {
		this.cas_st_transaction = cas_st_transaction;
		if (null != cas_st_transaction) {
			String[] strings = new String[3];
			strings[0] = "cas_st_transaction";
			strings[1] = cas_st_transaction[0];
			strings[2] = cas_st_transaction[1];
			list.add(strings);
		}
	}

	public String[] getCas_st_active_session() {
		return cas_st_active_session;

	}

	public void setCas_st_active_session(String[] cas_st_active_session) {
		this.cas_st_active_session = cas_st_active_session;
		if (null != cas_st_active_session) {
			String[] strings = new String[3];
			strings[0] = "cas_st_active_session";
			strings[1] = cas_st_active_session[0];
			strings[2] = cas_st_active_session[1];
			list.add(strings);
		}
	}

	public String[] getCas_st_query() {
		return cas_st_query;
	}

	public void setCas_st_query(String[] cas_st_query) {
		this.cas_st_query = cas_st_query;
		if (null != cas_st_query) {
			String[] strings = new String[3];
			strings[0] = "cas_st_query";
			strings[1] = cas_st_query[0];
			strings[2] = cas_st_query[1];
			list.add(strings);
		}
	}

	public String[] getCas_st_long_query() {
		return cas_st_long_query;
	}

	public void setCas_st_long_query(String[] cas_st_long_query) {
		this.cas_st_long_query = cas_st_long_query;
		if (null != cas_st_long_query) {
			String[] strings = new String[3];
			strings[0] = "cas_st_long_query";
			strings[1] = cas_st_long_query[0];
			strings[2] = cas_st_long_query[1];
			list.add(strings);
		}
	}

	public String[] getCas_st_long_tran() {
		return cas_st_long_tran;
	}

	public void setCas_st_long_tran(String[] cas_st_long_tran) {
		this.cas_st_long_tran = cas_st_long_tran;
		if (null != cas_st_long_tran) {
			String[] strings = new String[3];
			strings[0] = "cas_st_long_tran";
			strings[1] = cas_st_long_tran[0];
			strings[2] = cas_st_long_tran[1];
			list.add(strings);
		}
	}

	public String[] getCas_st_error_query() {
		return cas_st_error_query;
	}

	public void setCas_st_error_query(String[] cas_st_error_query) {
		this.cas_st_error_query = cas_st_error_query;
		if (null != cas_st_error_query) {
			String[] strings = new String[3];
			strings[0] = "cas_st_error_query";
			strings[1] = cas_st_error_query[0];
			strings[2] = cas_st_error_query[1];
			list.add(strings);
		}
	}

	public List<String[]> getList() {
		return list;
	}
}