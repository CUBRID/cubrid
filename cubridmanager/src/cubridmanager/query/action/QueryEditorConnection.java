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

package cubridmanager.query.action;

import java.sql.Connection;
import java.sql.DriverManager;
import java.sql.SQLException;

import cubrid.jdbc.driver.CUBRIDConnection;
import cubrid.jdbc.driver.CUBRIDConnectionKey;
import cubrid.jdbc.driver.CUBRIDKeyTable;
import cubridmanager.CommonTool;
import cubridmanager.MainRegistry;
import cubridmanager.Messages;
import cubridmanager.cas.CASItem;
import cubridmanager.cubrid.DBUserInfo;

public class QueryEditorConnection {
	private String host;
	private String port;
	private String conStr;
	private Connection conn;
	private DBUserInfo ui = null;
	private static final String NEW_LINE = System.getProperty("line.separator");

	public QueryEditorConnection(DBUserInfo userinfo) throws SQLException,
			ClassNotFoundException {
		host = MainRegistry.HostAddr;
		
		if( MainRegistry.UserPort.get(MainRegistry.UserID) != null &&
			!MainRegistry.UserPort.get(MainRegistry.UserID).toString().trim().equals("")) {
			MainRegistry.queryEditorOption.casport = new Integer((String)MainRegistry.UserPort.get(MainRegistry.UserID)) ;
		} else {
			if (MainRegistry.CASinfo_find(MainRegistry.queryEditorOption.casport) == null) {
				CASItem casitem = MainRegistry.CASinfo_find("query_editor");
				if (casitem == null) {
					if (MainRegistry.CASinfo != null) {
						casitem = (CASItem)MainRegistry.CASinfo.get(0);
					}
				}
				if (casitem != null) {
					MainRegistry.queryEditorOption.casport = Integer.parseInt(casitem.port);
				}
			}
		}
		
		port = Integer.toString(MainRegistry.queryEditorOption.casport);
		ui = userinfo;

		if (ui != null) {
			if (MainRegistry.isProtegoBuild())
				conStr = "jdbc:cubrid:" + host + ":" + port + ":" + ui.dbname
						+ ":::";
			// + ":" + ":";
			else
				conStr = "jdbc:cubrid:" + host + ":" + port + ":" + ui.dbname
						+ ":" + ui.dbuser + ":" + ui.dbpassword + ":";
		}

		if (MainRegistry.queryEditorOption.charset != null)
			conStr += "charset=" + MainRegistry.queryEditorOption.charset;

		try {
			Class.forName("cubrid.jdbc.driver.CUBRIDDriver");
			conn = DriverManager.getConnection(conStr);
		} catch (SQLException e) {
			CommonTool.ErrorBox(Messages.getString("QEDIT.SETCONNERR")
					+ NEW_LINE + e.getErrorCode() + NEW_LINE + e.getMessage());
			throw e;
		} catch (ClassNotFoundException e) {
			CommonTool.ErrorBox(Messages.getString("QEDIT.SETCONNERR")
					+ NEW_LINE + e.getMessage());
			throw e;
		} finally {
			if (conn != null) {
				conn.close();
			}
		}
	}

	public String getConnectionStr() {
		return conStr;
	}

	public String getDBName() {
		return ui.dbname;
	}

	public String getUserName() {
		return ui.dbuser;
	}
}
