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

package cubridmanager.cubrid.action;

import java.sql.Connection;
import java.sql.DriverManager;
import java.sql.SQLException;

import org.eclipse.jface.action.Action;

import cubrid.jdbc.driver.CUBRIDConnection;
import cubrid.jdbc.driver.CUBRIDConnectionKey;
import cubrid.jdbc.driver.CUBRIDKeyTable;
import cubridmanager.CommonTool;
import cubridmanager.MainRegistry;
import cubridmanager.Messages;
import cubridmanager.cubrid.view.CubridView;
import cubridmanager.cubrid.view.DBSchema;
import cubridmanager.query.action.QueryEditorConnection;
import cubridmanager.query.dialog.Export;

public class TableExportAction extends Action {
	public TableExportAction(String text, String img) {
		super(text);
		// The id is used to refer to the action in a menu or toolbar
		setId("TableExportAction");
		if (img != null)
			setImageDescriptor(cubridmanager.CubridmanagerPlugin
					.getImageDescriptor(img));
		setToolTipText(text);
	}

	public void run() {
		if (CubridView.Current_db == null
				|| DBSchema.CurrentObj == null
				|| MainRegistry.Authinfo_find(CubridView.Current_db).dbuser
						.length() <= 0)
			return;

		Connection con = null;
		QueryEditorConnection connector;
		try {
			Class.forName("cubrid.jdbc.driver.CUBRIDDriver");
			connector = new QueryEditorConnection(MainRegistry
					.getDBUserInfo(CubridView.Current_db));
			con = DriverManager.getConnection(connector.getConnectionStr());
			if (MainRegistry.isProtegoBuild()) {
				CUBRIDConnectionKey conKey = ((CUBRIDConnection) con)
						.Login(MainRegistry.UserSignedData);
				CUBRIDKeyTable.putValue(conKey);
			}
			con
					.setTransactionIsolation(Connection.TRANSACTION_READ_UNCOMMITTED);
			((CUBRIDConnection) con).setLockTimeout(1);
			new Export(con, DBSchema.CurrentObj);
		} catch (SQLException e) {
			CommonTool.ErrorBox(e.getErrorCode()
					+ System.getProperty("line.separator") + e.getMessage());
			CommonTool.debugPrint(e);
		} catch (ClassNotFoundException e) {
			CommonTool.ErrorBox(Messages.getString("QEDIT.SETCONNERR")
					+ System.getProperty("line.separator") + e.getMessage());
			CommonTool.debugPrint(e);
		}
	}
}
