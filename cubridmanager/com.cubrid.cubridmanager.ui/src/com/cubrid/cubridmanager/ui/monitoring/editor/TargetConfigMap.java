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
package com.cubrid.cubridmanager.ui.monitoring.editor;

import java.util.EnumMap;

/**
 * 
 * Stores all the target configuration info
 * 
 * @author lizhiqiang
 * @version 1.0 - 2009-5-7 created by lizhiqiang
 */
public final class TargetConfigMap {
	private String dbCategory = "database";
	private String casCategory = "broker";
	private String queryCategory = "server_query";
	private String connCategory = "server_connection";
	private String bufferCategory = "server_buffer";
	private String lockCategory = "server_lock";
	private String brokerCategory = "broker";
	private String showOpenedPage = "opened_page";
	private String showSlowQuery = "slow_query";
	private String showFullScan = "full_scan";
	private String showCliRequest = "client_request";
	private String showAboutedClients = "aborted_clients";
	private String showConnReq = "conn_request";
	private String showConnRej = "conn_rejected";
	private String showPageWrite = "buffer_page_write";
	private String showPageRead = "buffer_page_read";
	private String showLockDeadlock = "deadlock";
	private String showLockRequest = "lock_request";
	private String showStRequest = "request per second";//"Requests/Sec";
	private String showStTransaction = "transaction per second";//"Transactions/Sec";
	private String showStActiveSession = "active session count";
	private String showStQuery = "query per second";//"Queries/Sec"
	private String showStLongQuery = "long query count";
	private String showStLongTran = "long transaction count";
	private String showStErrQuery = "error query count";

	private EnumMap<EnumTargetConfig, TargetConfig> enumMap;
	private static TargetConfigMap instance;

	/**
	 * The Constructor
	 */
	private TargetConfigMap() {
		enumMap = new EnumMap<EnumTargetConfig, TargetConfig>(
				EnumTargetConfig.class);
		init();
	}

	/**
	 * Initializes the fields
	 * 
	 */
	private void init() {

		TargetConfig open_page = new TargetConfig();
		open_page.setName("server_query_open_page");
		open_page.setTransName(open_page.getName());
		open_page.setDisplayName(showOpenedPage);
		open_page.setTopCategory(dbCategory);
		open_page.setCategory(queryCategory);
		open_page.setMonitorName("mon_cub_query_open_page");
		open_page.setMagnification(1);
		open_page.setChartTitle("DB Open Page");
		enumMap.put(EnumTargetConfig.SERVER_QUERY_OPEN_PAGE, open_page);

		TargetConfig opened_page = new TargetConfig();
		opened_page.setName("server_query_opened_page");
		opened_page.setTransName(opened_page.getName());
		opened_page.setDisplayName(showOpenedPage);
		opened_page.setTopCategory(dbCategory);
		opened_page.setCategory(queryCategory);
		opened_page.setMonitorName("mon_cub_query_opened_page");
		opened_page.setMagnification(1);
		opened_page.setChartTitle("DB Opened Page");
		enumMap.put(EnumTargetConfig.SERVER_QUERY_OPENED_PAGE, opened_page);

		TargetConfig slow_query = new TargetConfig();
		slow_query.setName("server_query_slow_query");
		slow_query.setTransName(slow_query.getName());
		slow_query.setDisplayName(showSlowQuery);
		slow_query.setTopCategory(dbCategory);
		slow_query.setCategory(queryCategory);
		slow_query.setMonitorName("mon_cub_query_full_scan");
		slow_query.setMagnification(1);
		slow_query.setChartTitle("DB Slow Query");
		enumMap.put(EnumTargetConfig.SERVER_QUERY_SLOW_QUERY, slow_query);

		TargetConfig full_scan = new TargetConfig();
		full_scan.setName("server_query_full_scan");
		full_scan.setTransName(full_scan.getName());
		full_scan.setDisplayName(showFullScan);
		full_scan.setTopCategory(dbCategory);
		full_scan.setCategory(queryCategory);
		full_scan.setMonitorName("mon_cub_query_full_scan");
		full_scan.setMagnification(1);
		full_scan.setChartTitle("DB Full Scan");
		enumMap.put(EnumTargetConfig.SERVER_QUERY_FULL_SCAN, full_scan);

		TargetConfig aborted_clients = new TargetConfig();
		aborted_clients.setName("server_conn_aborted_clients");
		aborted_clients.setTransName(aborted_clients.getName());
		aborted_clients.setDisplayName(showAboutedClients);
		aborted_clients.setTopCategory(dbCategory);
		aborted_clients.setCategory(connCategory);
		aborted_clients.setMonitorName("mon_cub_conn_aborted_clients");
		aborted_clients.setMagnification(1);
		aborted_clients.setChartTitle("DB Aborted Clients");
		enumMap.put(EnumTargetConfig.SERVER_CONN_ABORTED_CLIENTS,
				aborted_clients);

		TargetConfig client_request = new TargetConfig();
		client_request.setName("server_conn_cli_request");
		client_request.setTransName(client_request.getName());
		client_request.setDisplayName(showCliRequest);
		client_request.setTopCategory(dbCategory);
		client_request.setCategory(connCategory);
		client_request.setMonitorName("mon_cub_conn_cli_request");
		client_request.setMagnification(1);
		client_request.setChartTitle("DB Client Request");
		enumMap.put(EnumTargetConfig.SERVER_CONN_CLI_REQUEST, client_request);

		TargetConfig conn_req = new TargetConfig();
		conn_req.setName("server_conn_conn_req");
		conn_req.setTransName(conn_req.getName());
		conn_req.setDisplayName(showConnReq);
		conn_req.setTopCategory(dbCategory);
		conn_req.setCategory(connCategory);
		conn_req.setMonitorName("mon_cub_conn_conn_req");
		conn_req.setMagnification(1);
		conn_req.setChartTitle("DB Connection Request");
		enumMap.put(EnumTargetConfig.SERVER_CONN_CONN_REQ, conn_req);

		TargetConfig conn_rejected = new TargetConfig();
		conn_rejected.setName("server_conn_conn_reject");
		conn_rejected.setTransName(conn_rejected.getName());
		conn_rejected.setDisplayName(showConnRej);
		conn_rejected.setTopCategory(dbCategory);
		conn_rejected.setCategory(connCategory);
		conn_rejected.setMonitorName("mon_cub_conn_conn_reject");
		conn_rejected.setMagnification(1);
		conn_rejected.setChartTitle("DB Connection Reject");
		enumMap.put(EnumTargetConfig.SERVER_CONN_CONN_REJ, conn_rejected);

		TargetConfig page_write = new TargetConfig();
		page_write.setName("server_buffer_page_write");
		page_write.setTransName(page_write.getName());
		page_write.setDisplayName(showPageWrite);
		page_write.setTopCategory(dbCategory);
		page_write.setCategory(bufferCategory);
		page_write.setMonitorName("mon_cub_buffer_page_write");
		page_write.setMagnification(1);
		page_write.setChartTitle("DB Buffer Write");
		enumMap.put(EnumTargetConfig.SERVER_BUFFER_PAGE_WRITE, page_write);

		TargetConfig page_read = new TargetConfig();
		page_read.setName("server_buffer_page_read");
		page_read.setTransName(page_read.getName());
		page_read.setDisplayName(showPageRead);
		page_read.setTopCategory(dbCategory);
		page_read.setCategory(bufferCategory);
		page_read.setMonitorName("mon_cub_buffer_page_read");
		page_read.setMagnification(1);
		page_read.setChartTitle("DB Buffer Read");
		enumMap.put(EnumTargetConfig.SERVER_BUFFER_PAGE_READ, page_read);

		TargetConfig deadlock = new TargetConfig();
		deadlock.setName("server_lock_deadlock");
		deadlock.setTransName(deadlock.getName());
		deadlock.setDisplayName(showLockDeadlock);
		deadlock.setTopCategory(dbCategory);
		deadlock.setCategory(lockCategory);
		deadlock.setMonitorName("mon_cub_lock_deadlock");
		deadlock.setMagnification(1);
		deadlock.setChartTitle("DB Deadlock");
		enumMap.put(EnumTargetConfig.SERVER_LOCK_DEADLOCK, deadlock);

		TargetConfig lock_request = new TargetConfig();
		lock_request.setName("server_lock_request");
		lock_request.setTransName(page_write.getName());
		lock_request.setDisplayName(showLockRequest);
		lock_request.setTopCategory(dbCategory);
		lock_request.setCategory(lockCategory);
		lock_request.setMonitorName("mon_cub_lock_request");
		lock_request.setMagnification(1);
		lock_request.setChartTitle("DB Lock Request");
		enumMap.put(EnumTargetConfig.SERVER_LOCK_REQUEST, lock_request);

		TargetConfig cas_request_sec = new TargetConfig();
		cas_request_sec.setName("cas_st_request");// cas_request_sec
		cas_request_sec.setTransName(cas_request_sec.getName());
		cas_request_sec.setDisplayName(showStRequest);
		cas_request_sec.setTopCategory(casCategory);
		cas_request_sec.setCategory(brokerCategory);
		cas_request_sec.setMonitorName("cas_mon_req");
		cas_request_sec.setMagnification(1);
		cas_request_sec.setChartTitle("Broker Request per Second");
		enumMap.put(EnumTargetConfig.CAS_ST_REQUEST, cas_request_sec);

		TargetConfig cas_active_session = new TargetConfig();
		cas_active_session.setName("cas_st_active_session");// cas_active_session
		cas_active_session.setTransName(cas_active_session.getName());
		cas_active_session.setDisplayName(showStActiveSession);
		cas_active_session.setTopCategory(casCategory);
		;
		cas_active_session.setCategory(brokerCategory);
		cas_active_session.setMonitorName("cas_mon_act_session");
		cas_active_session.setMagnification(1);
		cas_active_session.setChartTitle("Broker Active Session Count");
		enumMap.put(EnumTargetConfig.CAS_ST_ACTIVE_SESSION, cas_active_session);

		TargetConfig cas_transaction_sec = new TargetConfig();
		cas_transaction_sec.setName("cas_st_transaction");// cas_transaction_sec
		cas_transaction_sec.setTransName(cas_transaction_sec.getName());
		cas_transaction_sec.setDisplayName(showStTransaction);
		cas_transaction_sec.setTopCategory(casCategory);
		cas_transaction_sec.setCategory(brokerCategory);
		cas_transaction_sec.setMonitorName("cas_mon_tran");
		cas_transaction_sec.setMagnification(1);
		cas_transaction_sec.setChartTitle("Broker Transaction per Second");
		enumMap.put(EnumTargetConfig.CAS_ST_TRANSACTION, cas_transaction_sec);

		TargetConfig cas_query_sec = new TargetConfig();
		cas_query_sec.setName("cas_st_query");// cas_query_sec
		cas_query_sec.setTransName(cas_query_sec.getName());
		cas_query_sec.setDisplayName(showStQuery);
		cas_query_sec.setTopCategory(casCategory);
		cas_query_sec.setCategory(brokerCategory);
		cas_query_sec.setMonitorName("cas_mon_query");
		cas_query_sec.setMagnification(1);
		cas_query_sec.setChartTitle("Broker Query per Second");
		enumMap.put(EnumTargetConfig.CAS_ST_QUERY, cas_query_sec);

		TargetConfig cas_st_long_query = new TargetConfig();
		cas_st_long_query.setName("cas_st_long_query");// cas_st_long_query
		cas_st_long_query.setTransName(cas_st_long_query.getName());
		cas_st_long_query.setDisplayName(showStLongQuery);
		cas_st_long_query.setTopCategory(casCategory);
		cas_st_long_query.setCategory(brokerCategory);
		cas_st_long_query.setMonitorName("cas_mon_long_query");
		cas_st_long_query.setMagnification(1);
		cas_st_long_query.setChartTitle("Broker Long Query Count");
		enumMap.put(EnumTargetConfig.CAS_ST_LONG_QUERY, cas_st_long_query);

		TargetConfig cas_st_long_tran = new TargetConfig();
		cas_st_long_tran.setName("cas_st_long_tran");// cas_st_long_tran
		cas_st_long_tran.setTransName(cas_st_long_tran.getName());
		cas_st_long_tran.setDisplayName(showStLongTran);
		cas_st_long_tran.setTopCategory(casCategory);
		cas_st_long_tran.setCategory(brokerCategory);
		cas_st_long_tran.setMonitorName("cas_mon_long_tran");
		cas_st_long_tran.setMagnification(1);
		cas_st_long_tran.setChartTitle("Broker Long Transaction Count");
		enumMap.put(EnumTargetConfig.CAS_ST_LONG_TRAN, cas_st_long_tran);

		TargetConfig cas_st_error_query = new TargetConfig();
		cas_st_error_query.setName("cas_st_error_query");// cas_st_error_query
		cas_st_error_query.setTransName(cas_st_error_query.getName());
		cas_st_error_query.setDisplayName(showStErrQuery);
		cas_st_error_query.setTopCategory(casCategory);
		cas_st_error_query.setCategory(brokerCategory);
		cas_st_error_query.setMonitorName("cas_mon_error_query");
		cas_st_error_query.setMagnification(1);
		cas_st_error_query.setChartTitle("Broker Error Query Count");
		enumMap.put(EnumTargetConfig.CAS_ST_ERROR_QUERY, cas_st_error_query);
	}

	/**
	 * Gets the instance of TargetConfigMap
	 * 
	 * @return
	 */
	public static TargetConfigMap getInstance() {
		if (instance == null) {
			instance = new TargetConfigMap();
		}
		return instance;
	}

	/**
	 * Gets the value of enumMap
	 * 
	 * @return
	 */
	public EnumMap<EnumTargetConfig, TargetConfig> getMap() {
		return enumMap;
	}

	/**
	 * Gets the value of dbCategory
	 * 
	 * @return
	 */
	public String getDbCategory() {
		return dbCategory;
	}

	/**
	 * Gets the value of casCategory
	 * 
	 * @return
	 */
	public String getCasCategory() {
		return casCategory;
	}

	/**
	 * Gets the value of queryCategory
	 * 
	 * @return
	 */
	public String getQueryCategory() {
		return queryCategory;
	}

	/**
	 * Gets the value of connCategory
	 * 
	 * @return
	 */
	public String getConnCategory() {
		return connCategory;
	}

	/**
	 * Gets the value of bufferCategory
	 * 
	 * @return
	 */
	public String getBufferCategory() {
		return bufferCategory;
	}

	/**
	 * Gets the value of lockCategory
	 * 
	 * @return
	 */
	public String getLockCategory() {
		return lockCategory;
	}

	/**
	 * Gets the value of brokerCategory
	 * 
	 * @return
	 */
	public String getBrokerCategory() {
		return brokerCategory;
	}

	/**
	 * Gets the value of showOpenedPage
	 * 
	 * @return
	 */
	public String getShowOpenedPage() {
		return showOpenedPage;
	}

	/**
	 * Gets the value of showSlowQuery
	 * 
	 * @return
	 */
	public String getShowSlowQuery() {
		return showSlowQuery;
	}

	/**
	 * Gets the value of showFullScan
	 * 
	 * @return
	 */
	public String getShowFullScan() {
		return showFullScan;
	}

	/**
	 * Gets the value of showCliRequest
	 * 
	 * @return
	 */
	public String getShowCliRequest() {
		return showCliRequest;
	}

	/**
	 * Gets the value of showAboutedClients
	 * 
	 * @return
	 */
	public String getShowAboutedClients() {
		return showAboutedClients;
	}

	/**
	 * Gets the value of showConnReq
	 * 
	 * @return
	 */
	public String getShowConnReq() {
		return showConnReq;
	}

	/**
	 * Gets the value of showConnRej
	 * 
	 * @return
	 */
	public String getShowConnRej() {
		return showConnRej;
	}

	/**
	 * Gets the value of showPageWrite
	 * 
	 * @return
	 */
	public String getShowPageWrite() {
		return showPageWrite;
	}

	/**
	 * Gets the value of showPageRead
	 * 
	 * @return
	 */
	public String getShowPageRead() {
		return showPageRead;
	}

	/**
	 * Gets the value of showLockDeadlock
	 * 
	 * @return
	 */
	public String getShowLockDeadlock() {
		return showLockDeadlock;
	}

	/**
	 * Gets the value of showLockRequest
	 * 
	 * @return
	 */
	public String getShowLockRequest() {
		return showLockRequest;
	}

	/**
	 * Gets the value of showStRequest
	 * 
	 * @return
	 */
	public String getShowStRequest() {
		return showStRequest;
	}

	/**
	 * Gets the value of showStTransaction
	 * 
	 * @return
	 */
	public String getShowStTransaction() {
		return showStTransaction;
	}

	/**
	 * Gets the value of showStActiveSession
	 * 
	 * @return
	 */
	public String getShowStActiveSession() {
		return showStActiveSession;
	}

	/**
	 * Gets the value of showStQuery
	 * 
	 * @return
	 */
	public String getShowStQuery() {
		return showStQuery;
	}

	/**
	 * Gets the value of showStLongQuery
	 * 
	 * @return
	 */
	public String getShowStLongQuery() {
		return showStLongQuery;
	}

	/**
	 * Gets the value of showStLongTran
	 * 
	 * @return
	 */
	public String getShowStLongTran() {
		return showStLongTran;
	}

	/**
	 * Gets the value of showStErrQuery
	 * 
	 * @return
	 */
	public String getShowStErrQuery() {
		return showStErrQuery;
	}
}
