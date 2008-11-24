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

public class DiagStatusResult {
	public String cas_request_sec = new String();
	public String cas_transaction_sec = new String();
	public String cas_active_session = new String();
	public String server_query_open_page = new String();
	public String server_query_opened_page = new String();
	public String server_query_slow_query = new String();
	public String server_query_full_scan = new String();
	public String server_conn_cli_request = new String();
	public String server_conn_aborted_clients = new String();
	public String server_conn_conn_req = new String();
	public String server_conn_conn_reject = new String();
	public String server_buffer_page_write = new String();
	public String server_buffer_page_read = new String();
	public String server_lock_deadlock = new String();
	public String server_lock_request = new String();

	public void InitStatusResult() {
		cas_request_sec = "0";
		cas_transaction_sec = "0";
		cas_active_session = "0";
		server_query_open_page = "0";
		server_query_opened_page = "0";
		server_query_slow_query = "0";
		server_query_full_scan = "0";
		server_conn_cli_request = "0";
		server_conn_aborted_clients = "0";
		server_conn_conn_req = "0";
		server_conn_conn_reject = "0";
		server_buffer_page_write = "0";
		server_buffer_page_read = "0";
		server_lock_deadlock = "0";
		server_lock_request = "0";
	}

	public DiagStatusResult() {
		cas_request_sec = "0";
		cas_transaction_sec = "0";
		cas_active_session = "0";
		server_query_open_page = "0";
		server_query_opened_page = "0";
		server_query_slow_query = "0";
		server_query_full_scan = "0";
		server_conn_cli_request = "0";
		server_conn_aborted_clients = "0";
		server_conn_conn_req = "0";
		server_conn_conn_reject = "0";
		server_buffer_page_write = "0";
		server_buffer_page_read = "0";
		server_lock_deadlock = "0";
		server_lock_request = "0";
	}

	public DiagStatusResult(DiagStatusResult clone) {
		cas_request_sec = clone.cas_request_sec;
		cas_transaction_sec = clone.cas_transaction_sec;
		cas_active_session = clone.cas_active_session;
		server_query_open_page = clone.server_query_open_page;
		server_query_opened_page = clone.server_query_opened_page;
		server_query_slow_query = clone.server_query_slow_query;
		server_query_full_scan = clone.server_query_full_scan;
		server_conn_cli_request = clone.server_conn_cli_request;
		server_conn_aborted_clients = clone.server_conn_aborted_clients;
		server_conn_conn_req = clone.server_conn_conn_req;
		server_conn_conn_reject = clone.server_conn_conn_reject;
		server_buffer_page_write = clone.server_buffer_page_write;
		server_buffer_page_read = clone.server_buffer_page_read;
		server_lock_deadlock = clone.server_lock_deadlock;
		server_lock_request = clone.server_lock_request;
	}

	public void copy_from(DiagStatusResult clone) {
		cas_request_sec = clone.cas_request_sec;
		cas_transaction_sec = clone.cas_transaction_sec;
		cas_active_session = clone.cas_active_session;
		server_query_open_page = clone.server_query_open_page;
		server_query_opened_page = clone.server_query_opened_page;
		server_query_slow_query = clone.server_query_slow_query;
		server_query_full_scan = clone.server_query_full_scan;
		server_conn_cli_request = clone.server_conn_cli_request;
		server_conn_aborted_clients = clone.server_conn_aborted_clients;
		server_conn_conn_req = clone.server_conn_conn_req;
		server_conn_conn_reject = clone.server_conn_conn_reject;
		server_buffer_page_write = clone.server_buffer_page_write;
		server_buffer_page_read = clone.server_buffer_page_read;
		server_lock_deadlock = clone.server_lock_deadlock;
		server_lock_request = clone.server_lock_request;
	}

	public void getDelta(DiagStatusResult a, DiagStatusResult b) {
		try {
			cas_request_sec = String.valueOf(Integer.parseInt(a
					.GetCAS_Request_Sec())
					- Integer.parseInt(b.GetCAS_Request_Sec()));
		} catch (Exception ee) {
			cas_request_sec = "0";
		}

		try {
			cas_transaction_sec = String.valueOf(Integer.parseInt(a
					.GetCAS_Transaction_Sec())
					- Integer.parseInt(b.GetCAS_Transaction_Sec()));
		} catch (Exception ee) {
			cas_transaction_sec = "0";
		}

		cas_active_session = a.cas_active_session;

		try {
			server_query_open_page = String.valueOf(Integer
					.parseInt(a.server_query_open_page)
					- Integer.parseInt(b.server_query_open_page));
		} catch (Exception ee) {
			server_query_open_page = "0";
		}
		try {
			server_query_opened_page = String.valueOf(Integer
					.parseInt(a.server_query_opened_page)
					- Integer.parseInt(b.server_query_opened_page));
		} catch (Exception ee) {
			server_query_opened_page = "0";
		}
		try {
			server_query_slow_query = String.valueOf(Integer
					.parseInt(a.server_query_slow_query)
					- Integer.parseInt(b.server_query_slow_query));
		} catch (Exception ee) {
			server_query_slow_query = "0";
		}
		try {
			server_query_full_scan = String.valueOf(Integer
					.parseInt(a.server_query_full_scan)
					- Integer.parseInt(b.server_query_full_scan));
		} catch (Exception ee) {
			server_query_full_scan = "0";
		}
		try {
			server_conn_cli_request = String.valueOf(Integer
					.parseInt(a.server_conn_cli_request)
					- Integer.parseInt(b.server_conn_cli_request));
		} catch (Exception ee) {
			server_conn_cli_request = "0";
		}
		try {
			server_conn_aborted_clients = String.valueOf(Integer
					.parseInt(a.server_conn_aborted_clients)
					- Integer.parseInt(b.server_conn_aborted_clients));
		} catch (Exception ee) {
			server_conn_aborted_clients = "0";
		}
		try {
			server_conn_conn_req = String.valueOf(Integer
					.parseInt(a.server_conn_conn_req)
					- Integer.parseInt(b.server_conn_conn_req));
		} catch (Exception ee) {
			server_conn_conn_req = "0";
		}
		try {
			server_conn_conn_reject = String.valueOf(Integer
					.parseInt(a.server_conn_conn_reject)
					- Integer.parseInt(b.server_conn_conn_reject));
		} catch (Exception ee) {
			server_conn_conn_reject = "0";
		}
		try {
			server_buffer_page_write = String.valueOf(Integer
					.parseInt(a.server_buffer_page_write)
					- Integer.parseInt(b.server_buffer_page_write));
		} catch (Exception ee) {
			server_buffer_page_write = "0";
		}
		try {
			server_buffer_page_read = String.valueOf(Integer
					.parseInt(a.server_buffer_page_read)
					- Integer.parseInt(b.server_buffer_page_read));
		} catch (Exception ee) {
			server_buffer_page_read = "0";
		}
		try {
			server_lock_deadlock = String.valueOf(Integer
					.parseInt(a.server_lock_deadlock)
					- Integer.parseInt(b.server_lock_deadlock));
		} catch (Exception ee) {
			server_lock_deadlock = "0";
		}
		try {
			server_lock_request = String.valueOf(Integer
					.parseInt(a.server_lock_request)
					- Integer.parseInt(b.server_lock_request));
		} catch (Exception ee) {
			server_lock_request = "0";
		}
	}

	public void SetCAS_Request_Sec(String value) {
		cas_request_sec = value;
	}

	public String GetCAS_Request_Sec() {
		return cas_request_sec;
	}

	public void SetCAS_Transaction_Sec(String value) {
		cas_transaction_sec = value;
	}

	public String GetCAS_Transaction_Sec() {
		return cas_transaction_sec;
	}

	public void SetCAS_Active_Session(String value) {
		cas_active_session = value;
	}

	public String GetCAS_Active_Session() {
		return cas_active_session;
	}

	public void Set_server_query_opened_page(String value) {
		server_query_opened_page = value;
	}

	public String Get_server_query_opened_page() {
		return server_query_opened_page;
	}

	public void Set_server_query_slow_query(String value) {
		server_query_slow_query = value;
	}

	public String Get_server_query_slow_query() {
		return server_query_slow_query;
	}

	public void Set_server_query_full_scan(String value) {
		server_query_full_scan = value;
	}

	public String Get_server_query_full_scan() {
		return server_query_full_scan;
	}

	public void Set_server_conn_cli_request(String value) {
		server_conn_cli_request = value;
	}

	public String Get_server_conn_cli_request() {
		return server_conn_cli_request;
	}

	public void Set_server_conn_aborted_clients(String value) {
		server_conn_aborted_clients = value;
	}

	public String Get_server_conn_aborted_clients() {
		return server_conn_aborted_clients;
	}

	public void Set_server_conn_conn_req(String value) {
		server_conn_conn_req = value;
	}

	public String Get_server_conn_conn_req() {
		return server_conn_conn_req;
	}

	public void Set_server_conn_conn_reject(String value) {
		server_conn_conn_reject = value;
	}

	public String Get_server_conn_conn_reject() {
		return server_conn_conn_reject;
	}

	public void Set_server_buffer_page_write(String value) {
		server_buffer_page_write = value;
	}

	public String Get_server_buffer_page_write() {
		return server_buffer_page_write;
	}

	public void Set_server_buffer_page_read(String value) {
		server_buffer_page_read = value;
	}

	public String Get_server_buffer_page_read() {
		return server_buffer_page_read;
	}

	public void Set_server_lock_deadlock(String value) {
		server_lock_deadlock = value;
	}

	public String Get_server_lock_deadlock() {
		return server_lock_deadlock;
	}

	public void Set_server_lock_request(String value) {
		server_lock_request = value;
	}

	public String Get_server_lock_request() {
		return server_lock_request;
	}
}
