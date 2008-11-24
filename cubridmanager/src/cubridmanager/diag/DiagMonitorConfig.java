/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution. 
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

package cubridmanager.diag;

import org.eclipse.swt.SWT;

public class DiagMonitorConfig {
	public boolean copy(DiagMonitorConfig config) {
		if ((casData.copy(config.casData)) == false)
			return false;

		return true;
	}

	public class dbMonitorData {
		// status data
		public boolean need_mon_cub_query = false;
		public boolean query_open_page = false;
		public int query_open_page_color = SWT.COLOR_DARK_RED;
		public float query_open_page_magnification = (float) 1;
		public final String status_query_open_pageString = "open_page";
		public boolean query_opened_page = false;
		public int query_opened_page_color = SWT.COLOR_DARK_GREEN;
		public float query_opened_page_magnification = (float) 1;
		public final String status_query_opened_pageString = "opened_page";
		public boolean query_slow_query = false;
		public int query_slow_query_color = SWT.COLOR_DARK_YELLOW;
		public float query_slow_query_magnification = (float) 1;
		public final String status_query_slow_queryString = "slow_query";
		public boolean query_full_scan = false;
		public int query_full_scan_color = SWT.COLOR_DARK_BLUE;
		public float query_full_scan_magnification = (float) 1;
		public final String status_query_full_scanString = "full_scan";
		public boolean need_mon_cub_conn = false;
		public boolean conn_cli_request = false;
		public int conn_cli_request_color = SWT.COLOR_DARK_MAGENTA;
		public float conn_cli_request_magnification = (float) 1;
		public final String status_need_mon_cub_connString = "client_request";
		public boolean conn_aborted_clients = false;
		public int conn_aborted_clients_color = SWT.COLOR_DARK_CYAN;
		public float conn_aborted_clients_magnification = (float) 1;
		public final String status_conn_aborted_clientsString = "aborted_clients";
		public boolean conn_conn_req = false;
		public int conn_conn_req_color = SWT.COLOR_DARK_GRAY;
		public float conn_conn_req_magnification = (float) 1;
		public final String status_conn_conn_reqString = "conn_request";
		public boolean conn_conn_reject = false;
		public int conn_conn_reject_color = SWT.COLOR_WHITE;
		public float conn_conn_reject_magnification = (float) 1;
		public final String status_conn_conn_rejectString = "conn_rejected";
		public boolean need_mon_cub_buffer = false;
		public boolean buffer_page_write = false;
		public int buffer_page_write_color = SWT.COLOR_RED;
		public float buffer_page_write_magnification = (float) 1;
		public final String status_buffer_page_writeString = "buffer_page_write";
		public boolean buffer_page_read = false;
		public int buffer_page_read_color = SWT.COLOR_BLACK;
		public float buffer_page_read_magnification = (float) 1;
		public final String status_buffer_page_readString = "buffer_page_read";
		public boolean need_mon_cub_lock = false;
		public boolean lock_deadlock = false;
		public int lock_deadlock_color = SWT.COLOR_YELLOW;
		public float lock_deadlock_magnification = (float) 1;
		public final String status_lock_deadlockString = "deadlock";
		public boolean lock_request = false;
		public int lock_request_color = SWT.COLOR_BLUE;
		public float lock_request_magnification = (float) 1;
		public final String status_lock_requestString = "lock_request";
		// activity data
		public boolean needCubActivity = false;
		public boolean act_query_fullscan = false;
		public final String act_query_fullscanString = "query_fullscan";
		public boolean act_lock_deadlock = false;
		public final String act_lock_deadlockString = "deadlock";
		public boolean act_buffer_page_read = false;
		public final String act_buffer_page_readString = "buffer_page_read";
		public boolean act_buffer_page_write = false;
		public final String act_buffer_page_writeString = "buffer_page_write";
	}

	public class casMonitorData {
		public boolean needCasStatus = false;
		public boolean needCasActivity = false;
		public boolean status_request = false;
		public int req_color = SWT.COLOR_MAGENTA;
		public float req_magnification = (float) 1;
		public final String status_requestString = "Broker request";
		public boolean status_transaction_sec = false;
		public int tran_color = SWT.COLOR_CYAN;
		public float tran_magnification = (float) 1;
		public final String status_transaction_secString = "Broker transaction_sec";
		public boolean status_active_session = false;
		public int active_session_color = SWT.COLOR_GRAY;
		public float active_session_magnification = (float) 1;
		public final String status_active_sessionString = "Broker active_session";
		public boolean activity_tran = false;
		public final String activity_tranString = "Broker transaction";
		public boolean activity_req = false;
		public final String activity_reqString = "Broker request";

		public boolean copy(casMonitorData casData) {
			needCasStatus = casData.needCasStatus;
			needCasActivity = casData.needCasActivity;
			status_request = casData.status_request;
			req_color = casData.req_color;
			req_magnification = casData.req_magnification;

			status_transaction_sec = casData.status_transaction_sec;
			tran_color = casData.tran_color;
			tran_magnification = casData.tran_magnification;

			status_active_session = casData.status_active_session;
			active_session_color = casData.active_session_color;
			active_session_magnification = casData.active_session_magnification;

			activity_tran = casData.activity_tran;
			activity_req = casData.activity_req;

			return true;
		}

		public void init_cas_data() {
			needCasStatus = false;
			needCasActivity = false;
			status_request = false;
			req_color = 1;
			req_magnification = (float) 1;

			status_transaction_sec = false;
			tran_color = 1;
			tran_magnification = (float) 1;

			status_active_session = false;
			active_session_color = 1;
			active_session_magnification = (float) 1;

			activity_tran = false;
			activity_req = false;
		}
	}

	public casMonitorData casData = new casMonitorData();
	public dbMonitorData dbData = new dbMonitorData();
	public void SET_CLIENT_MONITOR_INFO_CAS() {
		casData.needCasStatus = true;
	}

	public void SET_CLIENT_ACTINFO_CAS() {
		casData.needCasActivity = true;
	}

	public boolean NEED_CAS_MON_DATA() {
		return casData.needCasStatus;
	}

	public boolean NEED_CAS_ACT_DATA() {
		return casData.needCasActivity;
	}

	public void SET_CLIENT_MONITOR_INFO_CAS_REQ() {
		casData.status_request = true;
	}

	public void SET_CLIENT_MONITOR_INFO_CAS_REQ_COLOR(int color) {
		casData.req_color = color;
	}

	public int GET_CLIENT_MONITOR_INFO_CAS_REQ_COLOR() {
		return casData.req_color;
	}

	public void SET_CLIENT_MONITOR_INFO_CAS_REQ_MAG(float value) {
		casData.req_magnification = value;
	}

	public float GET_CLIENT_MONITOR_INFO_CAS_REQ_MAG() {
		return casData.req_magnification;
	}

	public String GET_CLIENT_MONITOR_INFO_CAS_REQ_STRING() {
		return casData.status_requestString;
	}

	public void SET_CLIENT_MONITOR_INFO_CAS_TRAN() {
		casData.status_transaction_sec = true;
	}

	public void SET_CLIENT_MONITOR_INFO_CAS_TRAN_COLOR(int color) {
		casData.tran_color = color;
	}

	public int GET_CLIENT_MONITOR_INFO_CAS_TRAN_COLOR() {
		return casData.tran_color;
	}

	public void SET_CLIENT_MONITOR_INFO_CAS_TRAN_MAG(float value) {
		casData.tran_magnification = value;
	}

	public float GET_CLIENT_MONITOR_INFO_CAS_TRAN_MAG() {
		return casData.tran_magnification;
	}

	public String GET_CLIENT_MONITOR_INFO_CAS_TRAN_STRING() {
		return casData.status_transaction_secString;
	}

	public void SET_CLIENT_MONITOR_INFO_CAS_ACTIVE_SESSION() {
		casData.status_active_session = true;
	}

	public void SET_CLIENT_MONITOR_INFO_CAS_ACTIVE_SESSION_COLOR(int color) {
		casData.active_session_color = color;
	}

	public int GET_CLIENT_MONITOR_INFO_CAS_ACTIVE_SESSION_COLOR() {
		return casData.active_session_color;
	}

	public void SET_CLIENT_MONITOR_INFO_CAS_ACTIVE_SESSION_MAG(float value) {
		casData.active_session_magnification = value;
	}

	public float GET_CLIENT_MONITOR_INFO_CAS_ACTIVE_SESSION_MAG() {
		return casData.active_session_magnification;
	}

	public String GET_CLIENT_MONITOR_INFO_CAS_ACTIVE_SESSION_STRING() {
		return casData.status_active_sessionString;
	}

	public boolean NEED_CAS_MON_DATA_REQ() {
		return casData.status_request;
	}

	public boolean NEED_CAS_MON_DATA_TRAN() {
		return casData.status_transaction_sec;
	}

	public boolean NEED_CAS_MON_DATA_ACT_SESSION() {
		return casData.status_active_session;
	}

	public void SET_CLIENT_ACTINFO_CAS_REQ() {
		casData.activity_req = true;
	}

	public void SET_CLIENT_ACTINFO_CAS_TRAN() {
		casData.activity_tran = true;
	}

	public boolean NEED_CAS_ACT_DATA_REQ() {
		return casData.activity_req;
	}

	public String GET_CLIENT_ACTINFO_CAS_REQ_STRING() {
		return casData.activity_reqString;
	}

	public String GET_CLIENT_ACTINFO_CAS_TRAN_STRING() {
		return casData.activity_tranString;
	}

	public boolean NEED_CAS_ACT_DATA_TRAN() {
		return casData.activity_tran;
	}

	public void init_client_monitor_config() {
		casData.init_cas_data();
	}
}
