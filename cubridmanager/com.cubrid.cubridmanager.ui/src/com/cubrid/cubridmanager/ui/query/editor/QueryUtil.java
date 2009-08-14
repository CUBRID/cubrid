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
package com.cubrid.cubridmanager.ui.query.editor;

import java.sql.Connection;
import java.sql.ResultSet;
import java.sql.Statement;

import org.apache.log4j.Logger;

import com.cubrid.cubridmanager.core.common.log.LogUtil;

/**
 * 
 * JDBC SQL utility
 * 
 * QueryUtil Description
 * 
 * @author pcraft
 * @version 1.0 - 2009. 06. 06 created by pcraft
 */
public class QueryUtil {
	private static final Logger logger = LogUtil.getLogger(QueryUtil.class);

	public static void freeQuery(Connection conn, Statement stmt, ResultSet rs) {
		try {
			if (rs != null)
				rs.close();
			rs = null;
		} catch (Exception ignored) {
			logger.error(ignored);
		}
		try {
			if (stmt != null)
				stmt.close();
			stmt = null;
		} catch (Exception ignored) {
			logger.error(ignored);
		}
		try {
			if (conn != null)
				conn.close();
			conn = null;
		} catch (Exception ignored) {
			logger.error(ignored);
		}
	}

	public static void freeQuery(Connection conn, Statement stmt) {
		try {
			if (stmt != null)
				stmt.close();
			stmt = null;
		} catch (Exception ignored) {
			logger.error(ignored);
		}
		try {
			if (conn != null)
				conn.close();
			conn = null;
		} catch (Exception ignored) {
			logger.error(ignored);
		}
	}

	public static void freeQuery(Statement stmt, ResultSet rs) {
		try {
			if (rs != null)
				rs.close();
			rs = null;
		} catch (Exception ignored) {
			logger.error(ignored);
		}
		try {
			if (stmt != null)
				stmt.close();
			stmt = null;
		} catch (Exception ignored) {
			logger.error(ignored);
		}
	}

	public static void freeQuery(Connection conn) {
		try {
			if (conn != null)
				conn.close();
			conn = null;
		} catch (Exception ignored) {
			logger.error(ignored);
		}
	}

	public static void freeQuery(Statement stmt) {
		try {
			if (stmt != null)
				stmt.close();
			stmt = null;
		} catch (Exception ignored) {
			logger.error(ignored);
		}
	}

	public static void freeQuery(ResultSet rs) {
		try {
			if (rs != null)
				rs.close();
			rs = null;
		} catch (Exception ignored) {
			logger.error(ignored);
		}
	}

}
