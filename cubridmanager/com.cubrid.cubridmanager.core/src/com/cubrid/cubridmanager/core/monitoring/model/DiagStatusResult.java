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

package com.cubrid.cubridmanager.core.monitoring.model;

import java.util.HashMap;
import java.util.Map;

import org.apache.log4j.Logger;

import com.cubrid.cubridmanager.core.common.log.LogUtil;

/**
 * 
 * A class that include all the target messages.
 * 
 * DiagStatusResult Description
 * 
 * @author cn12978
 * @version 1.0 - 2009-5-14 created by cn12978
 */
public class DiagStatusResult {

	private static final Logger logger = LogUtil.getLogger(DiagStatusResult.class);
	private String cas_mon_req;
	private String cas_mon_act_session;
	private String cas_mon_tran;
	private String cas_mon_query;
	private String cas_mon_long_query;
	private String cas_mon_long_tran;
	private String cas_mon_error_query;
	private String server_query_open_page;
	private String server_query_opened_page;
	private String server_query_slow_query;
	private String server_query_full_scan;
	private String server_conn_cli_request;
	private String server_conn_aborted_clients;
	private String server_conn_conn_req;
	private String server_conn_conn_reject;
	private String server_buffer_page_write;
	private String server_buffer_page_read;
	private String server_lock_deadlock;
	private String server_lock_request;

	private Map<String, String> diagStatusResultMap;

	public void initStatusResult() {
		cas_mon_req = "0";
		cas_mon_tran = "0";
		cas_mon_act_session = "0";
		cas_mon_query = "0";
		cas_mon_long_query = "0";
		cas_mon_long_tran = "0";
		cas_mon_error_query = "0";
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
		cas_mon_req = "0";
		cas_mon_tran = "0";
		cas_mon_act_session = "0";
		cas_mon_query = "0";
		cas_mon_long_query = "0";
		cas_mon_long_tran = "0";
		cas_mon_error_query = "0";
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

		diagStatusResultMap = new HashMap<String, String>();

	}

	public DiagStatusResult(DiagStatusResult clone) {
		cas_mon_req = clone.cas_mon_req;
		cas_mon_tran = clone.cas_mon_tran;
		cas_mon_act_session = clone.cas_mon_act_session;
		cas_mon_query = clone.cas_mon_query;
		cas_mon_long_query = clone.cas_mon_long_query;
		cas_mon_long_tran = clone.cas_mon_long_tran;
		cas_mon_error_query = clone.cas_mon_error_query;
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
		cas_mon_req = clone.cas_mon_req;
		cas_mon_tran = clone.cas_mon_tran;
		cas_mon_act_session = clone.cas_mon_act_session;
		cas_mon_query = clone.cas_mon_query;
		cas_mon_long_query = clone.cas_mon_long_query;
		cas_mon_long_tran = clone.cas_mon_long_tran;
		cas_mon_error_query = clone.cas_mon_error_query;
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

	/**
	 * Gets the delta by two bean of DiagStatusResult
	 * 
	 * @param a
	 * @param b
	 */
	public void getDelta(DiagStatusResult a, DiagStatusResult b) {
		try {
			cas_mon_req = String.valueOf(Long.parseLong(a.getCas_mon_req())
					- Long.parseLong(b.getCas_mon_req()));
		} catch (NumberFormatException ee) {
			cas_mon_req = "0";
		}
		try {
			cas_mon_query = String.valueOf(Long.parseLong(a.getCas_mon_query())
					- Long.parseLong(b.getCas_mon_query()));

		} catch (NumberFormatException ee) {
			cas_mon_query = "0";
		}
		try {
			cas_mon_tran = String.valueOf(Long.parseLong(a.getCas_mon_tran())
					- Long.parseLong(b.getCas_mon_tran()));

		} catch (NumberFormatException ee) {
			cas_mon_tran = "0";
		}

		cas_mon_act_session = a.cas_mon_act_session;

		try {
			cas_mon_long_query = String.valueOf(Long.parseLong(a.cas_mon_long_query)
					- Long.parseLong(b.cas_mon_long_query));

		} catch (NumberFormatException ee) {
			cas_mon_long_query = "0";
		}

		try {
			cas_mon_long_tran = String.valueOf(Long.parseLong(a.cas_mon_long_tran)
					- Long.parseLong(b.cas_mon_long_tran));

		} catch (NumberFormatException ee) {
			cas_mon_long_tran = "0";
		}

		try {
			cas_mon_error_query = String.valueOf(Long.parseLong(a.cas_mon_error_query)
					- Long.parseLong(b.cas_mon_error_query));

		} catch (NumberFormatException ee) {
			cas_mon_error_query = "0";
		}

		try {
			server_query_open_page = String.valueOf(Integer.parseInt(a.server_query_open_page)
					- Integer.parseInt(b.server_query_open_page));

		} catch (NumberFormatException ee) {
			server_query_open_page = "0";
		}
		try {
			server_query_opened_page = String.valueOf(Integer.parseInt(a.server_query_opened_page)
					- Integer.parseInt(b.server_query_opened_page));

		} catch (NumberFormatException ee) {
			server_query_opened_page = "0";
		}
		try {
			server_query_slow_query = String.valueOf(Integer.parseInt(a.server_query_slow_query)
					- Integer.parseInt(b.server_query_slow_query));

		} catch (NumberFormatException ee) {
			server_query_slow_query = "0";
		}
		try {
			server_query_full_scan = String.valueOf(Integer.parseInt(a.server_query_full_scan)
					- Integer.parseInt(b.server_query_full_scan));

		} catch (NumberFormatException ee) {
			server_query_full_scan = "0";
		}
		try {
			server_conn_cli_request = String.valueOf(Integer.parseInt(a.server_conn_cli_request)
					- Integer.parseInt(b.server_conn_cli_request));

		} catch (NumberFormatException ee) {
			server_conn_cli_request = "0";
		}
		try {
			server_conn_aborted_clients = String.valueOf(Integer.parseInt(a.server_conn_aborted_clients)
					- Integer.parseInt(b.server_conn_aborted_clients));

		} catch (NumberFormatException ee) {
			server_conn_aborted_clients = "0";
		}
		try {
			server_conn_conn_req = String.valueOf(Integer.parseInt(a.server_conn_conn_req)
					- Integer.parseInt(b.server_conn_conn_req));

		} catch (NumberFormatException ee) {
			server_conn_conn_req = "0";
		}
		try {
			server_conn_conn_reject = String.valueOf(Integer.parseInt(a.server_conn_conn_reject)
					- Integer.parseInt(b.server_conn_conn_reject));

		} catch (NumberFormatException ee) {
			server_conn_conn_reject = "0";
		}
		try {
			server_buffer_page_write = String.valueOf(Integer.parseInt(a.server_buffer_page_write)
					- Integer.parseInt(b.server_buffer_page_write));

		} catch (NumberFormatException ee) {
			server_buffer_page_write = "0";
		}
		try {
			server_buffer_page_read = String.valueOf(Integer.parseInt(a.server_buffer_page_read)
					- Integer.parseInt(b.server_buffer_page_read));

		} catch (NumberFormatException ee) {
			server_buffer_page_read = "0";
		}
		try {
			server_lock_deadlock = String.valueOf(Integer.parseInt(a.server_lock_deadlock)
					- Integer.parseInt(b.server_lock_deadlock));

		} catch (NumberFormatException ee) {
			server_lock_deadlock = "0";
		}
		try {
			server_lock_request = String.valueOf(Integer.parseInt(a.server_lock_request)
					- Integer.parseInt(b.server_lock_request));

		} catch (NumberFormatException ee) {
			server_lock_request = "0";
		}

		diagStatusResultMap.put("cas_mon_req", cas_mon_req);
		diagStatusResultMap.put("cas_mon_tran", cas_mon_tran);
		diagStatusResultMap.put("cas_mon_act_session", cas_mon_act_session);
		diagStatusResultMap.put("cas_mon_query", cas_mon_query);
		diagStatusResultMap.put("cas_mon_long_query", cas_mon_long_query);
		diagStatusResultMap.put("cas_mon_long_tran", cas_mon_long_tran);
		diagStatusResultMap.put("cas_mon_error_query", cas_mon_error_query);
		diagStatusResultMap.put("server_query_open_page",
				server_query_open_page);
		diagStatusResultMap.put("server_query_opened_page",
				server_query_opened_page);
		diagStatusResultMap.put("server_query_slow_query",
				server_query_slow_query);
		diagStatusResultMap.put("server_query_full_scan",
				server_query_full_scan);
		diagStatusResultMap.put("server_conn_cli_request",
				server_conn_cli_request);
		diagStatusResultMap.put("server_conn_aborted_clients",
				server_conn_aborted_clients);
		diagStatusResultMap.put("server_conn_conn_req", server_conn_conn_req);
		diagStatusResultMap.put("server_conn_conn_reject",
				server_conn_conn_reject);
		diagStatusResultMap.put("server_buffer_page_write",
				server_buffer_page_write);
		diagStatusResultMap.put("server_buffer_page_read",
				server_buffer_page_read);
		diagStatusResultMap.put("server_lock_deadlock", server_lock_deadlock);
		diagStatusResultMap.put("server_lock_request", server_lock_request);
	}

	/**
	 * 
	 * Gets the delta by three bean of DiagStatusResult
	 * 
	 * @param a
	 * @param b
	 * @param c
	 */
	public void getDelta(DiagStatusResult a, DiagStatusResult b,
			DiagStatusResult c) {
		try {
			if (Long.parseLong(a.cas_mon_req) < 0
					&& Long.parseLong(b.cas_mon_req) > 0) {
				long partA = Long.MAX_VALUE - Long.parseLong(b.cas_mon_req);
				long partB = Long.parseLong(c.cas_mon_req) - Long.MIN_VALUE;
				cas_mon_req = String.valueOf(partA + partB);
			} else {
				cas_mon_req = String.valueOf(Long.parseLong(a.cas_mon_req)
						- Long.parseLong(b.cas_mon_req));
				if (Long.parseLong(cas_mon_req) < 0) {
					cas_mon_req = String.valueOf(Long.parseLong(b.cas_mon_req)
							- Long.parseLong(c.cas_mon_req));
					long aValue = Long.parseLong(b.cas_mon_req)
							+ Long.parseLong(cas_mon_req);
					a.setCas_mon_req(Long.toString(aValue));
				}
			}

		} catch (NumberFormatException ee) {
			cas_mon_req = "0";
		}
		try {
			if (Long.parseLong(a.cas_mon_query) < 0
					&& Long.parseLong(b.cas_mon_query) > 0) {
				long partA = Long.MAX_VALUE - Long.parseLong(b.cas_mon_query);
				long partB = Long.parseLong(c.cas_mon_query) - Long.MIN_VALUE;
				cas_mon_query = String.valueOf(partA + partB);
			} else {
				cas_mon_query = String.valueOf(Long.parseLong(a.cas_mon_query)
						- Long.parseLong(b.cas_mon_query));
				if (Long.parseLong(cas_mon_query) < 0) {
					cas_mon_query = String.valueOf(Long.parseLong(b.cas_mon_query)
							- Long.parseLong(c.getCas_mon_query()));
					long aValue = Long.parseLong(b.cas_mon_query)
							+ Long.parseLong(cas_mon_query);
					a.setCas_mon_query(Long.toString(aValue));
				}
			}
		} catch (NumberFormatException ee) {
			cas_mon_query = "0";
		}
		try {
			if (Long.parseLong(a.cas_mon_tran) < 0
					&& Long.parseLong(b.cas_mon_tran) > 0) {
				long partA = Long.MAX_VALUE - Long.parseLong(b.cas_mon_tran);
				long partB = Long.parseLong(c.cas_mon_tran) - Long.MIN_VALUE;
				cas_mon_tran = String.valueOf(partA + partB);
			} else {
				cas_mon_tran = String.valueOf(Long.parseLong(a.cas_mon_tran)
						- Long.parseLong(b.getCas_mon_tran()));
				if (Long.parseLong(cas_mon_tran) < 0) {
					cas_mon_tran = String.valueOf(Long.parseLong(b.cas_mon_tran)
							- Long.parseLong(c.getCas_mon_tran()));
					long aValue = Long.parseLong(b.cas_mon_tran)
							+ Long.parseLong(cas_mon_tran);
					a.setCas_mon_tran(Long.toString(aValue));
				}
			}
		} catch (NumberFormatException ee) {
			cas_mon_tran = "0";
		}

		cas_mon_act_session = a.cas_mon_act_session;

		try {
			if (Long.parseLong(a.cas_mon_long_query) < 0
					&& Long.parseLong(b.cas_mon_long_query) > 0) {
				long partA = Long.MAX_VALUE
						- Long.parseLong(b.cas_mon_long_query);
				long partB = Long.parseLong(c.cas_mon_long_query)
						- Long.MIN_VALUE;
				cas_mon_long_query = String.valueOf(partA + partB);
			} else {
				cas_mon_long_query = String.valueOf(Long.parseLong(a.cas_mon_long_query)
						- Long.parseLong(b.cas_mon_long_query));
				if (Long.parseLong(cas_mon_long_query) < 0) {
					cas_mon_long_query = String.valueOf(Long.parseLong(b.cas_mon_long_query)
							- Long.parseLong(c.cas_mon_long_query));
					long aValue = Long.parseLong(b.cas_mon_long_query)
							+ Long.parseLong(cas_mon_long_query);
					a.setCas_mon_long_query(Long.toString(aValue));
				}
			}
		} catch (NumberFormatException ee) {
			cas_mon_long_query = "0";
		}

		try {
			if (Long.parseLong(a.cas_mon_long_tran) < 0
					&& Long.parseLong(b.cas_mon_long_tran) > 0) {
				long partA = Long.MAX_VALUE
						- Long.parseLong(b.cas_mon_long_tran);
				long partB = Long.parseLong(c.cas_mon_long_tran)
						- Long.MIN_VALUE;
				cas_mon_long_tran = String.valueOf(partA + partB);
			} else {
				cas_mon_long_tran = String.valueOf(Long.parseLong(a.cas_mon_long_tran)
						- Long.parseLong(b.cas_mon_long_tran));
				if (Long.parseLong(cas_mon_long_tran) < 0) {
					cas_mon_long_tran = String.valueOf(Long.parseLong(b.cas_mon_long_tran)
							- Long.parseLong(c.cas_mon_long_tran));
					long aValue = Long.parseLong(b.cas_mon_long_tran)
							+ Long.parseLong(cas_mon_long_tran);
					a.setCas_mon_long_tran(Long.toString(aValue));
				}
			}
		} catch (NumberFormatException ee) {
			cas_mon_long_tran = "0";
		}

		try {
			if (Long.parseLong(a.cas_mon_error_query) < 0
					&& Long.parseLong(b.cas_mon_error_query) > 0) {
				long partA = Long.MAX_VALUE
						- Long.parseLong(b.cas_mon_error_query);
				long partB = Long.parseLong(c.cas_mon_error_query)
						- Long.MIN_VALUE;
				cas_mon_error_query = String.valueOf(partA + partB);
			} else {
				cas_mon_error_query = String.valueOf(Long.parseLong(a.cas_mon_error_query)
						- Long.parseLong(b.cas_mon_error_query));
				if (Long.parseLong(cas_mon_error_query) < 0) {
					cas_mon_error_query = String.valueOf(Long.parseLong(b.cas_mon_error_query)
							- Long.parseLong(c.cas_mon_error_query));
					long aValue = Long.parseLong(b.cas_mon_error_query)
							+ Long.parseLong(cas_mon_error_query);
					a.setCas_mon_error_query(Long.toString(aValue));
				}
			}
		} catch (NumberFormatException ee) {
			cas_mon_error_query = "0";
		}

		try {
			if (Integer.parseInt(a.server_query_open_page) < 0
					&& Integer.parseInt(b.server_query_open_page) > 0) {
				int partA = Integer.MAX_VALUE
						- Integer.parseInt(b.server_query_open_page);
				int partB = Integer.parseInt(c.server_query_open_page)
						- Integer.MIN_VALUE;
				server_query_open_page = String.valueOf(partA + partB);
			} else {
				server_query_open_page = String.valueOf(Integer.parseInt(a.server_query_open_page)
						- Integer.parseInt(b.server_query_open_page));
				if (Integer.parseInt(server_query_open_page) < 0) {
					server_query_open_page = String.valueOf(Integer.parseInt(b.server_query_open_page)
							- Integer.parseInt(c.server_query_open_page));
					int aValue = Integer.parseInt(b.server_query_open_page)
							+ Integer.parseInt(server_query_open_page);
					a.setServer_query_open_page(Integer.toString(aValue));
				}
			}
		} catch (NumberFormatException ee) {
			server_query_open_page = "0";
		}
		try {
			if (Integer.parseInt(a.server_query_opened_page) < 0
					&& Integer.parseInt(b.server_query_opened_page) > 0) {
				int partA = Integer.MAX_VALUE
						- Integer.parseInt(b.server_query_opened_page);
				int partB = Integer.parseInt(c.server_query_opened_page)
						- Integer.MIN_VALUE;
				server_query_opened_page = String.valueOf(partA + partB);
			} else {
				server_query_opened_page = String.valueOf(Integer.parseInt(a.server_query_opened_page)
						- Integer.parseInt(b.server_query_opened_page));
				if (Integer.parseInt(server_query_opened_page) < 0) {
					server_query_opened_page = String.valueOf(Integer.parseInt(b.server_query_opened_page)
							- Integer.parseInt(c.server_query_opened_page));
					int aValue = Integer.parseInt(b.server_query_opened_page)
							+ Integer.parseInt(server_query_opened_page);
					a.setServer_query_opened_page(Integer.toString(aValue));
				}
			}
		} catch (NumberFormatException ee) {
			server_query_opened_page = "0";
		}
		try {
			if (Integer.parseInt(a.server_query_slow_query) < 0
					&& Integer.parseInt(b.server_query_slow_query) > 0) {
				int partA = Integer.MAX_VALUE
						- Integer.parseInt(b.server_query_slow_query);
				int partB = Integer.parseInt(c.server_query_slow_query)
						- Integer.MIN_VALUE;
				server_query_slow_query = String.valueOf(partA + partB);
			} else {
				server_query_slow_query = String.valueOf(Integer.parseInt(a.server_query_slow_query)
						- Integer.parseInt(b.server_query_slow_query));
				if (Integer.parseInt(server_query_slow_query) < 0) {
					server_query_slow_query = String.valueOf(Integer.parseInt(b.server_query_slow_query)
							- Integer.parseInt(c.server_query_slow_query));
					int aValue = Integer.parseInt(b.server_query_slow_query)
							+ Integer.parseInt(server_query_slow_query);
					a.setServer_query_slow_query(Integer.toString(aValue));
				}
			}
		} catch (NumberFormatException ee) {
			server_query_slow_query = "0";
		}
		try {
			if (Integer.parseInt(a.server_query_full_scan) < 0
					&& Integer.parseInt(b.server_query_full_scan) > 0) {
				int partA = Integer.MAX_VALUE
						- Integer.parseInt(b.server_query_full_scan);
				int partB = Integer.parseInt(c.server_query_full_scan)
						- Integer.MIN_VALUE;
				server_query_full_scan = String.valueOf(partA + partB);
			} else {
				server_query_full_scan = String.valueOf(Integer.parseInt(a.server_query_full_scan)
						- Integer.parseInt(b.server_query_full_scan));
				if (Integer.parseInt(server_query_full_scan) < 0) {
					server_query_full_scan = String.valueOf(Integer.parseInt(b.server_query_full_scan)
							- Integer.parseInt(c.server_query_full_scan));
					int aValue = Integer.parseInt(b.server_query_full_scan)
							+ Integer.parseInt(server_query_full_scan);
					a.setServer_query_full_scan(Integer.toString(aValue));
				}
			}
		} catch (NumberFormatException ee) {
			server_query_full_scan = "0";
		}
		try {
			if (Integer.parseInt(a.server_conn_cli_request) < 0
					&& Integer.parseInt(b.server_conn_cli_request) > 0) {
				int partA = Integer.MAX_VALUE
						- Integer.parseInt(b.server_conn_cli_request);
				int partB = Integer.parseInt(c.server_conn_cli_request)
						- Integer.MIN_VALUE;
				server_conn_cli_request = String.valueOf(partA + partB);
			} else {
				server_conn_cli_request = String.valueOf(Integer.parseInt(a.server_conn_cli_request)
						- Integer.parseInt(b.server_conn_cli_request));
				if (Integer.parseInt(server_conn_cli_request) < 0) {
					server_conn_cli_request = String.valueOf(Integer.parseInt(b.server_conn_cli_request)
							- Integer.parseInt(c.server_conn_cli_request));
					int aValue = Integer.parseInt(b.server_conn_cli_request)
							+ Integer.parseInt(server_conn_cli_request);
					a.setServer_conn_cli_request(Integer.toString(aValue));

				}
			}
		} catch (NumberFormatException ee) {
			server_conn_cli_request = "0";
		}
		try {
			if (Integer.parseInt(a.server_conn_aborted_clients) < 0
					&& Integer.parseInt(b.server_conn_aborted_clients) > 0) {
				int partA = Integer.MAX_VALUE
						- Integer.parseInt(b.server_conn_aborted_clients);
				int partB = Integer.parseInt(c.server_conn_aborted_clients)
						- Integer.MIN_VALUE;
				server_conn_aborted_clients = String.valueOf(partA + partB);
			} else {
				server_conn_aborted_clients = String.valueOf(Integer.parseInt(a.server_conn_aborted_clients)
						- Integer.parseInt(b.server_conn_aborted_clients));
				if (Integer.parseInt(server_conn_aborted_clients) < 0) {
					server_conn_aborted_clients = String.valueOf(Integer.parseInt(b.server_conn_aborted_clients)
							- Integer.parseInt(c.server_conn_aborted_clients));
					int aValue = Integer.parseInt(b.server_conn_aborted_clients)
							+ Integer.parseInt(server_conn_aborted_clients);
					a.setServer_conn_aborted_clients(Integer.toString(aValue));
				}
			}
		} catch (NumberFormatException ee) {
			server_conn_aborted_clients = "0";
		}
		try {
			if (Integer.parseInt(a.server_conn_conn_req) < 0
					&& Integer.parseInt(b.server_conn_conn_req) > 0) {
				int partA = Integer.MAX_VALUE
						- Integer.parseInt(b.server_conn_conn_req);
				int partB = Integer.parseInt(c.server_conn_conn_req)
						- Integer.MIN_VALUE;
				server_conn_conn_req = String.valueOf(partA + partB);
			} else {
				server_conn_conn_req = String.valueOf(Integer.parseInt(a.server_conn_conn_req)
						- Integer.parseInt(b.server_conn_conn_req));
				if (Integer.parseInt(server_conn_conn_req) < 0) {
					server_conn_conn_req = String.valueOf(Integer.parseInt(b.server_conn_conn_req)
							- Integer.parseInt(c.server_conn_conn_req));
					int aValue = Integer.parseInt(b.server_conn_conn_req)
							+ Integer.parseInt(server_conn_conn_req);
					a.setServer_conn_conn_req(Integer.toString(aValue));
				}
			}
		} catch (NumberFormatException ee) {
			server_conn_conn_req = "0";
		}
		try {
			if (Integer.parseInt(a.server_conn_conn_reject) < 0
					&& Integer.parseInt(b.server_conn_conn_reject) > 0) {
				int partA = Integer.MAX_VALUE
						- Integer.parseInt(b.server_conn_conn_reject);
				int partB = Integer.parseInt(c.server_conn_conn_reject)
						- Integer.MIN_VALUE;
				server_conn_conn_reject = String.valueOf(partA + partB);
			} else {
				server_conn_conn_reject = String.valueOf(Integer.parseInt(a.server_conn_conn_reject)
						- Integer.parseInt(b.server_conn_conn_reject));
				if (Integer.parseInt(server_conn_conn_reject) < 0) {
					server_conn_conn_reject = String.valueOf(Integer.parseInt(b.server_conn_conn_reject)
							- Integer.parseInt(c.server_conn_conn_reject));
					int aValue = Integer.parseInt(b.server_conn_conn_reject)
							+ Integer.parseInt(server_conn_conn_reject);
					a.setServer_conn_conn_reject(Integer.toString(aValue));
				}
			}
		} catch (NumberFormatException ee) {
			server_conn_conn_reject = "0";
		}
		try {
			if (Integer.parseInt(a.server_buffer_page_write) < 0
					&& Integer.parseInt(b.server_buffer_page_write) > 0) {
				int partA = Integer.MAX_VALUE
						- Integer.parseInt(b.server_buffer_page_write);
				int partB = Integer.parseInt(c.server_buffer_page_write)
						- Integer.MIN_VALUE;
				server_buffer_page_write = String.valueOf(partA + partB);
			} else {
				server_buffer_page_write = String.valueOf(Integer.parseInt(a.server_buffer_page_write)
						- Integer.parseInt(b.server_buffer_page_write));
				if (Integer.parseInt(server_buffer_page_write) < 0) {
					server_buffer_page_write = String.valueOf(Integer.parseInt(b.server_buffer_page_write)
							- Integer.parseInt(c.server_buffer_page_write));
					int aValue = Integer.parseInt(b.server_buffer_page_write)
							+ Integer.parseInt(server_buffer_page_write);
					a.setServer_buffer_page_write(Integer.toString(aValue));
				}
			}
		} catch (NumberFormatException ee) {
			server_buffer_page_write = "0";
		}
		try {
			if (Integer.parseInt(a.server_buffer_page_read) < 0
					&& Integer.parseInt(b.server_buffer_page_read) > 0) {
				int partA = Integer.MAX_VALUE
						- Integer.parseInt(b.server_buffer_page_read);
				int partB = Integer.parseInt(c.server_buffer_page_read)
						- Integer.MIN_VALUE;
				server_buffer_page_read = String.valueOf(partA + partB);
			} else {
				server_buffer_page_read = String.valueOf(Integer.parseInt(a.server_buffer_page_read)
						- Integer.parseInt(b.server_buffer_page_read));
				if (Integer.parseInt(server_buffer_page_read) < 0) {
					server_buffer_page_read = String.valueOf(Integer.parseInt(b.server_buffer_page_read)
							- Integer.parseInt(c.server_buffer_page_read));
					int aValue = Integer.parseInt(b.server_buffer_page_read)
							+ Integer.parseInt(server_buffer_page_read);
					a.setServer_buffer_page_read(Integer.toString(aValue));
				}
			}
		} catch (NumberFormatException ee) {
			server_buffer_page_read = "0";
		}
		try {
			if (Integer.parseInt(a.server_lock_deadlock) < 0
					&& Integer.parseInt(b.server_lock_deadlock) > 0) {
				int partA = Integer.MAX_VALUE
						- Integer.parseInt(b.server_lock_deadlock);
				int partB = Integer.parseInt(c.server_lock_deadlock)
						- Integer.MIN_VALUE;
				server_lock_deadlock = String.valueOf(partA + partB);
			} else {
				server_lock_deadlock = String.valueOf(Integer.parseInt(a.server_lock_deadlock)
						- Integer.parseInt(b.server_lock_deadlock));
				if (Integer.parseInt(server_lock_deadlock) < 0) {
					server_lock_deadlock = String.valueOf(Integer.parseInt(b.server_lock_deadlock)
							- Integer.parseInt(c.server_lock_deadlock));
					int aValue = Integer.parseInt(b.server_lock_deadlock)
							+ Integer.parseInt(server_lock_deadlock);
					a.setServer_lock_deadlock(Integer.toString(aValue));
				}
			}
		} catch (NumberFormatException ee) {
			server_lock_deadlock = "0";
		}
		try {
			if (Integer.parseInt(a.server_lock_request) < 0
					&& Integer.parseInt(b.server_lock_request) > 0) {
				int partA = Integer.MAX_VALUE
						- Integer.parseInt(b.server_lock_request);
				int partB = Integer.parseInt(c.server_lock_request)
						- Integer.MIN_VALUE;
				server_lock_request = String.valueOf(partA + partB);
			} else {
				server_lock_request = String.valueOf(Integer.parseInt(a.server_lock_request)
						- Integer.parseInt(b.server_lock_request));
				if (Integer.parseInt(server_lock_request) < 0) {
					server_lock_request = String.valueOf(Integer.parseInt(b.server_lock_request)
							- Integer.parseInt(c.server_lock_request));
					int aValue = Integer.parseInt(b.server_lock_request)
							+ Integer.parseInt(server_lock_request);
					a.setServer_lock_request(Integer.toString(aValue));
				}
			}
		} catch (NumberFormatException ee) {
			server_lock_request = "0";
		}

		diagStatusResultMap.put("cas_mon_req", cas_mon_req);
		diagStatusResultMap.put("cas_mon_tran", cas_mon_tran);
		diagStatusResultMap.put("cas_mon_act_session", cas_mon_act_session);
		diagStatusResultMap.put("cas_mon_query", cas_mon_query);
		diagStatusResultMap.put("cas_mon_long_query", cas_mon_long_query);
		diagStatusResultMap.put("cas_mon_long_tran", cas_mon_long_tran);
		diagStatusResultMap.put("cas_mon_error_query", cas_mon_error_query);
		diagStatusResultMap.put("server_query_open_page",
				server_query_open_page);
		diagStatusResultMap.put("server_query_opened_page",
				server_query_opened_page);
		diagStatusResultMap.put("server_query_slow_query",
				server_query_slow_query);
		diagStatusResultMap.put("server_query_full_scan",
				server_query_full_scan);
		diagStatusResultMap.put("server_conn_cli_request",
				server_conn_cli_request);
		diagStatusResultMap.put("server_conn_aborted_clients",
				server_conn_aborted_clients);
		diagStatusResultMap.put("server_conn_conn_req", server_conn_conn_req);
		diagStatusResultMap.put("server_conn_conn_reject",
				server_conn_conn_reject);
		diagStatusResultMap.put("server_buffer_page_write",
				server_buffer_page_write);
		diagStatusResultMap.put("server_buffer_page_read",
				server_buffer_page_read);
		diagStatusResultMap.put("server_lock_deadlock", server_lock_deadlock);
		diagStatusResultMap.put("server_lock_request", server_lock_request);
	}

	/**
	 * 
	 * Gets the delta by three bean of DiagStatusResult and the interval between
	 * getting the instance of DiagStatusResult
	 * 
	 * @param a
	 * @param b
	 * @param c
	 * @param inter
	 */

	public void getDelta(DiagStatusResult a, DiagStatusResult b,
			DiagStatusResult c, float inter) {
		try {
			long temp = 0;
			if (Long.parseLong(a.cas_mon_req) < 0
					&& Long.parseLong(b.cas_mon_req) > 0) {
				long partA = Long.MAX_VALUE - Long.parseLong(b.cas_mon_req);
				long partB = Long.parseLong(c.cas_mon_req) - Long.MIN_VALUE;
				temp = partA + partB;
			} else {
				temp = Long.parseLong(a.cas_mon_req)
						- Long.parseLong(b.cas_mon_req);
				if (temp < 0) {
					temp = Long.parseLong(b.cas_mon_req)
							- Long.parseLong(c.cas_mon_req);
					long aValue = (Long.parseLong(b.getCas_mon_req()) + temp);
					a.setCas_mon_req(Long.toString(aValue));
				}
			}
			logger.debug("cas_mon_req(before divided by interval) = " + temp);
			temp = (long) (temp / inter);
			cas_mon_req = String.valueOf(temp);
			logger.debug("cas_mon_req(after divided by interval) = "
					+ cas_mon_req);
		} catch (NumberFormatException ee) {
			cas_mon_req = "0";
		}
		try {
			long temp = 0;
			if (Long.parseLong(a.cas_mon_query) < 0
					&& Long.parseLong(b.cas_mon_query) > 0) {
				long partA = Long.MAX_VALUE - Long.parseLong(b.cas_mon_query);
				long partB = Long.parseLong(c.cas_mon_query) - Long.MIN_VALUE;
				temp = partA + partB;
			} else {
				temp = Long.parseLong(a.cas_mon_query)
						- Long.parseLong(b.cas_mon_query);
				if (temp < 0) {
					temp = Long.parseLong(b.cas_mon_query)
							- Long.parseLong(c.getCas_mon_query());
					long aValue = Long.parseLong(b.getCas_mon_query()) + temp;
					a.setCas_mon_query(Long.toString(aValue));
				}
			}
			logger.debug("cas_mon_query(before divided by interval) = " + temp);
			temp = (long) (temp / inter);
			cas_mon_query = String.valueOf(temp);
			logger.debug("cas_mon_query(after divided by interval) = "
					+ cas_mon_query);
		} catch (NumberFormatException ee) {
			cas_mon_query = "0";
		}
		try {
			long temp = 0;
			if (Long.parseLong(a.cas_mon_tran) < 0
					&& Long.parseLong(b.cas_mon_tran) > 0) {
				long partA = Long.MAX_VALUE - Long.parseLong(b.cas_mon_tran);
				long partB = Long.parseLong(c.cas_mon_tran) - Long.MIN_VALUE;
				temp = partA + partB;
			} else {
				temp = Long.parseLong(a.cas_mon_tran)
						- Long.parseLong(b.getCas_mon_tran());
				if (temp < 0) {
					temp = Long.parseLong(b.cas_mon_tran)
							- Long.parseLong(c.getCas_mon_tran());
					long aValue = Long.parseLong(b.getCas_mon_tran()) + temp;
					a.setCas_mon_tran(Long.toString(aValue));
				}
			}
			logger.debug("cas_mon_tran(before divided by interval) = " + temp);
			temp = (long) (temp / inter);
			cas_mon_tran = String.valueOf(temp);
			logger.debug("cas_mon_tran(after divided by interval) = "
					+ cas_mon_tran);
		} catch (NumberFormatException ee) {
			cas_mon_tran = "0";
		}

		cas_mon_act_session = a.cas_mon_act_session;

		try {
			long temp = 0;
			if (Long.parseLong(a.cas_mon_long_query) < 0
					&& Integer.parseInt(b.cas_mon_long_query) > 0) {
				long partA = Long.MAX_VALUE
						- Long.parseLong(b.cas_mon_long_query);
				long partB = Long.parseLong(c.cas_mon_long_query)
						- Long.MIN_VALUE;
				temp = partA + partB;
			} else {
				temp = Long.parseLong(a.cas_mon_long_query)
						- Long.parseLong(b.cas_mon_long_query);
				if (temp < 0) {
					temp = Long.parseLong(b.cas_mon_long_query)
							- Long.parseLong(c.cas_mon_long_query);
					long aValue = Long.parseLong(b.cas_mon_long_query) + temp;
					a.setCas_mon_long_query(Long.toString(aValue));
				}
			}
			temp = (long) (temp / inter);
			cas_mon_long_query = String.valueOf(temp);
		} catch (NumberFormatException ee) {
			cas_mon_long_query = "0";
		}

		try {
			long temp = 0;
			if (Long.parseLong(a.cas_mon_long_tran) < 0
					&& Long.parseLong(b.cas_mon_long_tran) > 0) {
				long partA = Long.MAX_VALUE
						- Long.parseLong(b.cas_mon_long_tran);
				long partB = Long.parseLong(c.cas_mon_long_tran)
						- Long.MIN_VALUE;
				temp = partA + partB;
			} else {
				temp = Long.parseLong(a.cas_mon_long_tran)
						- Long.parseLong(b.cas_mon_long_tran);
				if (temp < 0) {
					temp = Long.parseLong(b.cas_mon_long_tran)
							- Long.parseLong(c.cas_mon_long_tran);
					long aValue = Long.parseLong(b.cas_mon_long_tran) + temp;
					a.setCas_mon_long_tran(Long.toString(aValue));
				}
			}
			temp = (long) (temp / inter);
			cas_mon_long_tran = String.valueOf(temp);
		} catch (NumberFormatException ee) {
			cas_mon_long_tran = "0";
		}

		try {
			long temp = 0;
			if (Long.parseLong(a.cas_mon_error_query) < 0
					&& Long.parseLong(b.cas_mon_error_query) > 0) {
				long partA = Long.MAX_VALUE
						- Long.parseLong(b.cas_mon_error_query);
				long partB = Long.parseLong(c.cas_mon_error_query)
						- Long.MIN_VALUE;
				temp = partA + partB;
			} else {
				temp = Long.parseLong(a.cas_mon_error_query)
						- Long.parseLong(b.cas_mon_error_query);
				if (temp < 0) {
					temp = Long.parseLong(b.cas_mon_error_query)
							- Long.parseLong(c.cas_mon_error_query);
					long aValue = Long.parseLong(b.cas_mon_error_query) + temp;
					a.setCas_mon_error_query(Long.toString(aValue));
				}
			}
			temp = (long) (temp / inter);
			cas_mon_error_query = String.valueOf(temp);
		} catch (NumberFormatException ee) {
			cas_mon_error_query = "0";
		}

		try {
			int temp = 0;
			if (Integer.parseInt(a.server_query_open_page) < 0
					&& Integer.parseInt(b.server_query_open_page) > 0) {
				int partA = Integer.MAX_VALUE
						- Integer.parseInt(b.server_query_open_page);
				int partB = Integer.parseInt(c.server_query_open_page)
						- Integer.MIN_VALUE;
				temp = partA + partB;
			} else {
				temp = Integer.parseInt(a.server_query_open_page)
						- Integer.parseInt(b.server_query_open_page);
				if (temp < 0) {
					temp = Integer.parseInt(b.server_query_open_page)
							- Integer.parseInt(c.server_query_open_page);
					int aValue = Integer.parseInt(b.server_query_open_page)
							+ temp;
					a.setServer_query_open_page(Integer.toString(aValue));
				}
			}
			temp = (int) (temp / inter);
			server_query_open_page = String.valueOf(temp);
		} catch (NumberFormatException ee) {
			server_query_open_page = "0";
		}
		try {
			int temp = 0;
			if (Integer.parseInt(a.server_query_opened_page) < 0
					&& Integer.parseInt(b.server_query_opened_page) > 0) {
				int partA = Integer.MAX_VALUE
						- Integer.parseInt(b.server_query_opened_page);
				int partB = Integer.parseInt(c.server_query_opened_page)
						- Integer.MIN_VALUE;
				temp = partA + partB;
			} else {
				temp = Integer.parseInt(a.server_query_opened_page)
						- Integer.parseInt(b.server_query_opened_page);
				if (temp < 0) {
					temp = Integer.parseInt(b.server_query_opened_page)
							- Integer.parseInt(c.server_query_opened_page);
					int aValue = Integer.parseInt(b.server_query_opened_page)
							+ temp;
					a.setServer_query_opened_page(Integer.toString(aValue));
				}
			}
			temp = (int) (temp / inter);
			server_query_opened_page = String.valueOf(temp);
		} catch (NumberFormatException ee) {
			server_query_opened_page = "0";
		}
		try {
			int temp = 0;
			if (Integer.parseInt(a.server_query_slow_query) < 0
					&& Integer.parseInt(b.server_query_slow_query) > 0) {
				int partA = Integer.MAX_VALUE
						- Integer.parseInt(b.server_query_slow_query);
				int partB = Integer.parseInt(c.server_query_slow_query)
						- Integer.MIN_VALUE;
				temp = partA + partB;
			} else {
				temp = Integer.parseInt(a.server_query_slow_query)
						- Integer.parseInt(b.server_query_slow_query);
				if (temp < 0) {
					temp = Integer.parseInt(b.server_query_slow_query)
							- Integer.parseInt(c.server_query_slow_query);
					int aValue = Integer.parseInt(b.server_query_slow_query)
							+ temp;
					a.setServer_query_slow_query(Integer.toString(aValue));
				}
			}
			temp = (int) (temp / inter);
			server_query_slow_query = String.valueOf(temp);
		} catch (NumberFormatException ee) {
			server_query_slow_query = "0";
		}
		try {
			int temp = 0;
			if (Integer.parseInt(a.server_query_full_scan) < 0
					&& Integer.parseInt(b.server_query_full_scan) > 0) {
				int partA = Integer.MAX_VALUE
						- Integer.parseInt(b.server_query_full_scan);
				int partB = Integer.parseInt(c.server_query_full_scan)
						- Integer.MIN_VALUE;
				temp = partA + partB;
			} else {
				temp = Integer.parseInt(a.server_query_full_scan)
						- Integer.parseInt(b.server_query_full_scan);
				if (temp < 0) {
					temp = Integer.parseInt(b.server_query_full_scan)
							- Integer.parseInt(c.server_query_full_scan);
					int aValue = Integer.parseInt(b.server_query_full_scan)
							+ temp;
					a.setServer_query_full_scan(Integer.toString(aValue));
				}
			}
			temp = (int) (temp / inter);
			server_query_full_scan = String.valueOf(temp);
		} catch (NumberFormatException ee) {
			server_query_full_scan = "0";
		}
		try {
			int temp = 0;
			if (Integer.parseInt(a.server_conn_cli_request) < 0
					&& Integer.parseInt(b.server_conn_cli_request) > 0) {
				int partA = Integer.MAX_VALUE
						- Integer.parseInt(b.server_conn_cli_request);
				int partB = Integer.parseInt(c.server_conn_cli_request)
						- Integer.MIN_VALUE;
				temp = partA + partB;

			} else {
				temp = Integer.parseInt(a.server_conn_cli_request)
						- Integer.parseInt(b.server_conn_cli_request);
				if (temp < 0) {
					temp = Integer.parseInt(b.server_conn_cli_request)
							- Integer.parseInt(c.server_conn_cli_request);
					int aValue = Integer.parseInt(b.server_conn_cli_request)
							+ temp;
					a.setServer_conn_cli_request(Integer.toString(aValue));

				}
			}
			temp = (int) (temp / inter);
			server_conn_cli_request = String.valueOf(temp);
		} catch (NumberFormatException ee) {
			server_conn_cli_request = "0";
		}
		try {
			int temp = 0;
			if (Integer.parseInt(a.server_conn_aborted_clients) < 0
					&& Integer.parseInt(b.server_conn_aborted_clients) > 0) {
				int partA = Integer.MAX_VALUE
						- Integer.parseInt(b.server_conn_aborted_clients);
				int partB = Integer.parseInt(c.server_conn_aborted_clients)
						- Integer.MIN_VALUE;
				temp = partA + partB;
			} else {
				temp = Integer.parseInt(a.server_conn_aborted_clients)
						- Integer.parseInt(b.server_conn_aborted_clients);
				if (temp < 0) {
					temp = Integer.parseInt(b.server_conn_aborted_clients)
							- Integer.parseInt(c.server_conn_aborted_clients);
					int aValue = Integer.parseInt(b.server_conn_aborted_clients)
							+ temp;
					a.setServer_conn_aborted_clients(Integer.toString(aValue));
				}
			}
			temp = (int) (temp / inter);
			server_conn_aborted_clients = String.valueOf(temp);
		} catch (NumberFormatException ee) {
			server_conn_aborted_clients = "0";
		}
		try {
			int temp = 0;
			if (Integer.parseInt(a.server_conn_conn_req) < 0
					&& Integer.parseInt(b.server_conn_conn_req) > 0) {
				int partA = Integer.MAX_VALUE
						- Integer.parseInt(b.server_conn_conn_req);
				int partB = Integer.parseInt(c.server_conn_conn_req)
						- Integer.MIN_VALUE;
				temp = partA + partB;
			} else {
				temp = Integer.parseInt(a.server_conn_conn_req)
						- Integer.parseInt(b.server_conn_conn_req);
				if (Integer.parseInt(server_conn_conn_req) < 0) {
					temp = Integer.parseInt(b.server_conn_conn_req)
							- Integer.parseInt(c.server_conn_conn_req);
					int aValue = Integer.parseInt(b.server_conn_conn_req)
							+ temp;
					a.setServer_conn_conn_req(Integer.toString(aValue));
				}
			}
			temp = (int) (temp / inter);
			server_conn_conn_req = String.valueOf(temp);
		} catch (NumberFormatException ee) {
			server_conn_conn_req = "0";
		}
		try {
			int temp = 0;
			if (Integer.parseInt(a.server_conn_conn_reject) < 0
					&& Integer.parseInt(b.server_conn_conn_reject) > 0) {
				int partA = Integer.MAX_VALUE
						- Integer.parseInt(b.server_conn_conn_reject);
				int partB = Integer.parseInt(c.server_conn_conn_reject)
						- Integer.MIN_VALUE;
				temp = partA + partB;
			} else {
				temp = Integer.parseInt(a.server_conn_conn_reject)
						- Integer.parseInt(b.server_conn_conn_reject);
				if (temp < 0) {
					temp = Integer.parseInt(b.server_conn_conn_reject)
							- Integer.parseInt(c.server_conn_conn_reject);
					int aValue = Integer.parseInt(b.server_conn_conn_req)
							+ temp;
					a.setServer_conn_conn_reject(Integer.toString(aValue));
				}
			}
			temp = (int) (temp / inter);
			server_conn_conn_reject = String.valueOf(temp);
		} catch (NumberFormatException ee) {
			server_conn_conn_reject = "0";
		}
		try {
			int temp = 0;
			if (Integer.parseInt(a.server_buffer_page_write) < 0
					&& Integer.parseInt(b.server_buffer_page_write) > 0) {
				int partA = Integer.MAX_VALUE
						- Integer.parseInt(b.server_buffer_page_write);
				int partB = Integer.parseInt(c.server_buffer_page_write)
						- Integer.MIN_VALUE;
				temp = partA + partB;
			} else {
				temp = Integer.parseInt(a.server_buffer_page_write)
						- Integer.parseInt(b.server_buffer_page_write);
				if (temp < 0) {
					temp = Integer.parseInt(b.server_buffer_page_write)
							- Integer.parseInt(c.server_buffer_page_write);
					int aValue = Integer.parseInt(b.server_buffer_page_write)
							+ temp;
					a.setServer_buffer_page_write(Integer.toString(aValue));
				}
			}
			temp = (int) (temp / inter);
			server_buffer_page_write = String.valueOf(temp);
		} catch (NumberFormatException ee) {
			server_buffer_page_write = "0";
		}
		try {
			int temp = 0;
			if (Integer.parseInt(a.server_buffer_page_read) < 0
					&& Integer.parseInt(b.server_buffer_page_read) > 0) {
				int partA = Integer.MAX_VALUE
						- Integer.parseInt(b.server_buffer_page_read);
				int partB = Integer.parseInt(c.server_buffer_page_read)
						- Integer.MIN_VALUE;
				temp = partA + partB;
			} else {
				temp = Integer.parseInt(a.server_buffer_page_read)
						- Integer.parseInt(b.server_buffer_page_read);
				if (temp < 0) {
					temp = Integer.parseInt(b.server_buffer_page_read)
							- Integer.parseInt(c.server_buffer_page_read);
					int aValue = Integer.parseInt(b.server_buffer_page_read)
							+ temp;
					a.setServer_buffer_page_read(Integer.toString(aValue));
				}
			}
			temp = (int) (temp / inter);
			server_buffer_page_read = String.valueOf(temp);
		} catch (NumberFormatException ee) {
			server_buffer_page_read = "0";
		}
		try {
			int temp = 0;
			if (Integer.parseInt(a.server_lock_deadlock) < 0
					&& Integer.parseInt(b.server_lock_deadlock) > 0) {
				int partA = Integer.MAX_VALUE
						- Integer.parseInt(b.server_lock_deadlock);
				int partB = Integer.parseInt(c.server_lock_deadlock)
						- Integer.MIN_VALUE;
				temp = partA + partB;
			} else {
				temp = Integer.parseInt(a.server_lock_deadlock)
						- Integer.parseInt(b.server_lock_deadlock);
				if (temp < 0) {
					temp = Integer.parseInt(b.server_lock_deadlock)
							- Integer.parseInt(c.server_lock_deadlock);
					int aValue = Integer.parseInt(b.server_lock_deadlock)
							+ temp;
					a.setServer_lock_deadlock(Integer.toString(aValue));
				}
			}
			temp = (int) (temp / inter);
			server_lock_deadlock = String.valueOf(temp);
		} catch (NumberFormatException ee) {
			server_lock_deadlock = "0";
		}
		try {
			int temp = 0;
			if (Integer.parseInt(a.server_lock_request) < 0
					&& Integer.parseInt(b.server_lock_request) > 0) {
				int partA = Integer.MAX_VALUE
						- Integer.parseInt(b.server_lock_request);
				int partB = Integer.parseInt(c.server_lock_request)
						- Integer.MIN_VALUE;
				temp = partA + partB;
			} else {
				temp = Integer.parseInt(a.server_lock_request)
						- Integer.parseInt(b.server_lock_request);
				if (temp < 0) {
					temp = Integer.parseInt(b.server_lock_request)
							- Integer.parseInt(c.server_lock_request);
					int aValue = Integer.parseInt(b.server_lock_request) + temp;
					a.setServer_lock_request(Integer.toString(aValue));
				}
			}
			server_lock_request = String.valueOf(temp);
		} catch (NumberFormatException ee) {
			server_lock_request = "0";
		}

		diagStatusResultMap.put("cas_mon_req", cas_mon_req);
		diagStatusResultMap.put("cas_mon_tran", cas_mon_tran);
		diagStatusResultMap.put("cas_mon_act_session", cas_mon_act_session);
		diagStatusResultMap.put("cas_mon_query", cas_mon_query);
		diagStatusResultMap.put("cas_mon_long_query", cas_mon_long_query);
		diagStatusResultMap.put("cas_mon_long_tran", cas_mon_long_tran);
		diagStatusResultMap.put("cas_mon_error_query", cas_mon_error_query);
		diagStatusResultMap.put("server_query_open_page",
				server_query_open_page);
		diagStatusResultMap.put("server_query_opened_page",
				server_query_opened_page);
		diagStatusResultMap.put("server_query_slow_query",
				server_query_slow_query);
		diagStatusResultMap.put("server_query_full_scan",
				server_query_full_scan);
		diagStatusResultMap.put("server_conn_cli_request",
				server_conn_cli_request);
		diagStatusResultMap.put("server_conn_aborted_clients",
				server_conn_aborted_clients);
		diagStatusResultMap.put("server_conn_conn_req", server_conn_conn_req);
		diagStatusResultMap.put("server_conn_conn_reject",
				server_conn_conn_reject);
		diagStatusResultMap.put("server_buffer_page_write",
				server_buffer_page_write);
		diagStatusResultMap.put("server_buffer_page_read",
				server_buffer_page_read);
		diagStatusResultMap.put("server_lock_deadlock", server_lock_deadlock);
		diagStatusResultMap.put("server_lock_request", server_lock_request);
	}

	public String getCas_mon_req() {
		return cas_mon_req;
	}

	public void setCas_mon_req(String cas_mon_req) {
		this.cas_mon_req = cas_mon_req;
	}

	public String getCas_mon_tran() {
		return cas_mon_tran;
	}

	public void setCas_mon_tran(String cas_mon_tran) {
		this.cas_mon_tran = cas_mon_tran;
	}

	public String getCas_mon_act_session() {
		return cas_mon_act_session;
	}

	public void setCas_mon_act_session(String cas_mon_act_session) {
		this.cas_mon_act_session = cas_mon_act_session;
	}

	public String getServer_query_open_page() {
		return server_query_open_page;
	}

	public void setServer_query_open_page(String server_query_open_page) {
		this.server_query_open_page = server_query_open_page;
	}

	public String getServer_query_opened_page() {
		return server_query_opened_page;
	}

	public void setServer_query_opened_page(String server_query_opened_page) {
		this.server_query_opened_page = server_query_opened_page;
	}

	public String getServer_query_slow_query() {
		return server_query_slow_query;
	}

	public void setServer_query_slow_query(String server_query_slow_query) {
		this.server_query_slow_query = server_query_slow_query;
	}

	public String getServer_query_full_scan() {
		return server_query_full_scan;
	}

	public void setServer_query_full_scan(String server_query_full_scan) {
		this.server_query_full_scan = server_query_full_scan;
	}

	public String getServer_conn_cli_request() {
		return server_conn_cli_request;
	}

	public void setServer_conn_cli_request(String server_conn_cli_request) {
		this.server_conn_cli_request = server_conn_cli_request;
	}

	public String getServer_conn_aborted_clients() {
		return server_conn_aborted_clients;
	}

	public void setServer_conn_aborted_clients(
			String server_conn_aborted_clients) {
		this.server_conn_aborted_clients = server_conn_aborted_clients;
	}

	public String getServer_conn_conn_req() {
		return server_conn_conn_req;
	}

	public void setServer_conn_conn_req(String server_conn_conn_req) {
		this.server_conn_conn_req = server_conn_conn_req;
	}

	public String getServer_conn_conn_reject() {
		return server_conn_conn_reject;
	}

	public void setServer_conn_conn_reject(String server_conn_conn_reject) {
		this.server_conn_conn_reject = server_conn_conn_reject;
	}

	public String getServer_buffer_page_write() {
		return server_buffer_page_write;
	}

	public void setServer_buffer_page_write(String server_buffer_page_write) {
		this.server_buffer_page_write = server_buffer_page_write;
	}

	public String getServer_buffer_page_read() {
		return server_buffer_page_read;
	}

	public void setServer_buffer_page_read(String server_buffer_page_read) {
		this.server_buffer_page_read = server_buffer_page_read;
	}

	public String getServer_lock_deadlock() {
		return server_lock_deadlock;
	}

	public void setServer_lock_deadlock(String server_lock_deadlock) {
		this.server_lock_deadlock = server_lock_deadlock;
	}

	public String getServer_lock_request() {
		return server_lock_request;
	}

	public void setServer_lock_request(String server_lock_request) {
		this.server_lock_request = server_lock_request;
	}

	public String getCas_mon_query() {
		return cas_mon_query;
	}

	public void setCas_mon_query(String cas_mon_query) {
		this.cas_mon_query = cas_mon_query;
	}

	public Map<String, String> getDiagStatusResultMap() {
		return diagStatusResultMap;
	}

	public String getCas_mon_long_query() {
		return cas_mon_long_query;
	}

	public void setCas_mon_long_query(String cas_mon_long_query) {
		this.cas_mon_long_query = cas_mon_long_query;
	}

	public String getCas_mon_long_tran() {
		return cas_mon_long_tran;
	}

	public void setCas_mon_long_tran(String cas_mon_long_tran) {
		this.cas_mon_long_tran = cas_mon_long_tran;
	}

	public String getCas_mon_error_query() {
		return cas_mon_error_query;
	}

	public void setCas_mon_error_query(String cas_mon_error_query) {
		this.cas_mon_error_query = cas_mon_error_query;
	}

}
