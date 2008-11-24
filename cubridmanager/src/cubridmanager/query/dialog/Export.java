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

package cubridmanager.query.dialog;

import java.io.File;
import java.io.FileNotFoundException;
import java.io.FileOutputStream;
import java.io.IOException;
import java.math.BigDecimal;
import java.sql.Connection;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.sql.Statement;

import jxl.Workbook;
import jxl.write.Label;
import jxl.write.Number;
import jxl.write.WritableSheet;
import jxl.write.WritableWorkbook;
import jxl.write.WriteException;
import jxl.write.biff.RowsExceededException;

import org.eclipse.swt.SWT;
import org.eclipse.swt.widgets.FileDialog;
import org.eclipse.swt.widgets.Table;
import org.eclipse.swt.widgets.TableItem;

import cubrid.sql.CUBRIDOID;
import cubrid.jdbc.driver.CUBRIDConnection;
import cubrid.jdbc.driver.CUBRIDResultSet;
import cubrid.jdbc.driver.CUBRIDResultSetMetaData;
import cubridmanager.Application;
import cubridmanager.CommonTool;
import cubridmanager.MainRegistry;
import cubridmanager.Messages;
import cubridmanager.WaitingMsgBox;
import cubridmanager.query.view.QueryEditor;

public class Export {
	private static final String NEW_LINE = System.getProperty("line.separator");
	private String fileName;
	private File file;
	private WaitingMsgBox waitDlg = null;
	private String sql = null;
	private Statement stmt = null;
	private String thistableName = null;

	public Export(Table tbl, boolean isSelection) {
		fileName = getFileName(new String[] { "*.xls", "*.csv" }, new String[] {
				"Excel(xls)", "Comma separated value(csv)" });
		if (fileName == null)
			return;

		if (fileName.toLowerCase().endsWith(".xls"))
			exportXls(tbl, isSelection);
		else
			exportCsv(tbl, isSelection);
	}

	public Export(Connection con, String tableName) throws SQLException {
		fileName = getFileName(new String[] { "*.xls", "*.csv", "*.sql",
				"*.obs" }, new String[] { "Excel(xls)",
				"Comma separated value(csv)", "Insert query script file(sql)",
				"CUBRID load format file(obs)" });
		if (fileName == null)
			return;

		sql = "select * from " + tableName;
		stmt = con.createStatement(ResultSet.TYPE_SCROLL_INSENSITIVE,
				ResultSet.CONCUR_UPDATABLE, ResultSet.HOLD_CURSORS_OVER_COMMIT);

		waitDlg = new WaitingMsgBox(Application.mainwindow.getShell());
		waitDlg.setJobEndState(false);
		thistableName = tableName;
		Thread execThread = new Thread() {
			public void run() {
				CUBRIDResultSet rs = null;
				try {
					rs = (CUBRIDResultSet) stmt.executeQuery(sql);
					try {
						if (fileName.toLowerCase().endsWith(".xls"))
							exportXls(rs);
						else if (fileName.toLowerCase().endsWith(".csv"))
							exportCsv(rs);
						else if (fileName.toLowerCase().endsWith(".sql"))
							exportSql(rs, thistableName);
						else if (fileName.toLowerCase().endsWith(".obs"))
							exportLoad(rs, thistableName);
						else
							exportCsv(rs);
					} catch (Exception e) {
						CommonTool.ErrorBox(e.getMessage());
						CommonTool.debugPrint(e);
					}
					waitDlg.setJobEndState(true);
				} catch (Exception e) {
					waitDlg.setJobEndState(true);
				}
			}
		};

		execThread.start();

		try {
			execThread.join(1000);
			if (!waitDlg.getJobEndState()) {
				MainRegistry.WaitDlg = true;
				waitDlg.run(Messages.getString("WAITING.EXPORT"));
			}

			if (execThread.isAlive())
				execThread.join();
		} catch (Exception e) {
			CommonTool.debugPrint("Exception!!!" + e.toString());
			CommonTool.debugPrint(e);
		}
		if (MainRegistry.isProtegoBuild()) {
			((CUBRIDConnection) con).Logout();
		}
		con.close();
	}

	private String getFileName(String[] filterExts, String[] filterNames) {
		FileDialog dialog = new FileDialog(Application.mainwindow.getShell(),
				SWT.SAVE | SWT.APPLICATION_MODAL);
		dialog.setFilterExtensions(filterExts);
		dialog.setFilterNames(filterNames);
		File curdir = new File(".");
		try {
			dialog.setFilterPath(curdir.getCanonicalPath());
		} catch (Exception e) {
			dialog.setFilterPath(".");
		}

		boolean doExit = false;
		while (!doExit) {
			String result = dialog.open();
			if (result != null) {
				if (!result.toLowerCase().endsWith(".xls")
						&& !result.toLowerCase().endsWith(".csv")
						&& !result.toLowerCase().endsWith(".sql")
						&& !result.toLowerCase().endsWith(".obs"))
					result = result.concat(".csv");
				File tmpFile = new File(result);
				if (tmpFile.exists()) {
					int state = CommonTool.MsgBox(Application.mainwindow
							.getShell(), SWT.ICON_QUESTION | SWT.YES | SWT.NO,
							Messages.getString("QEDIT.FILESAVE"), "\""
									+ tmpFile.getAbsolutePath() + "\""
									+ NEW_LINE
									+ Messages.getString("QEDIT.FILESAVEMSG"));
					if (state == SWT.YES) {
						new File(result + ".bak").delete();
						tmpFile.renameTo(new File(result + ".bak"));
					} else {
						doExit = false;
						continue;
					}
				}
				file = new File(result);
				return file.getName();
			} else
				doExit = true;
		}
		return null;
	}

	private void exportXls(Table tbl, boolean isSelection) {
		try {
			int sheetNum = 0;
			WritableWorkbook workbook = Workbook.createWorkbook(file);
			WritableSheet sheet = workbook.createSheet("Sheet " + sheetNum,
					sheetNum);
			int rowLimit = 65536; // 65536: limit xls row number.
			int columnLimit = 257; // 256: limit xls column number.
			// it set 257. Because Tbl's first column is oid value that doesn't export.
			StringBuffer addMsg = new StringBuffer("");

			int colCount, itemCount;
			colCount = tbl.getColumnCount();

			TableItem[] items;
			if (isSelection) {
				itemCount = tbl.getSelectionCount();
				items = tbl.getSelection();
			} else {
				itemCount = tbl.getItemCount();
				items = tbl.getItems();
			}

			if (colCount > columnLimit) {
				if (CommonTool.MsgBox(Application.mainwindow.getShell(),
						SWT.ICON_WARNING | SWT.APPLICATION_MODAL | SWT.YES
								| SWT.NO, Messages.getString("MSG.WARNING"),
						Messages.getString("WARNING.COLUMNCOUNTOVER")) == SWT.NO)
					return;
				colCount = columnLimit;
				addMsg.append(NEW_LINE);
				addMsg.append(NEW_LINE);
				addMsg.append(Messages.getString("MESSAGE.COLUMNCOUNTOVER"));
			}

			for (int i = 0, xlsRecordNum = 0; i < itemCount; i++) {
				for (int j = 1; j < colCount; j++) {
					String colType = tbl.getColumns()[j].getData().toString();
					String value = items[i].getText(j);
					if (colType.equals("INTEGER") || colType.equals("TINYINT")
							|| colType.equals("SMALLINT")
							|| colType.equals("BIGINT")) {
						if (!value
								.equals(cubridmanager.query.view.QueryEditor.STR_NULL))
							sheet.addCell(new Number(j - 1, xlsRecordNum,
									Integer.parseInt(value)));
					} else if (colType.equals("DOUBLE")
							|| colType.equals("FLOAT")
							|| colType.equals("REAL")) {
						if (!value
								.equals(cubridmanager.query.view.QueryEditor.STR_NULL))
							sheet.addCell(new Number(j - 1, xlsRecordNum,
									Double.parseDouble(value)));
					} else if (colType.equals("NUMERIC")
							|| colType.equals("DECIMAL")
							|| colType.equals("MONETORY")) {
						if (!value
								.equals(cubridmanager.query.view.QueryEditor.STR_NULL))
							sheet.addCell(new Number(j - 1, xlsRecordNum,
									(new BigDecimal(value)).doubleValue()));
					} else {
						if (!value
								.equals(cubridmanager.query.view.QueryEditor.STR_NULL))
							sheet.addCell(new Label(j - 1, xlsRecordNum, value
									.toString()));
					}
				}
				xlsRecordNum++;

				if (((i + 1) % rowLimit) == 0) {
					sheetNum++;
					xlsRecordNum -= rowLimit;
					sheet = workbook.createSheet("Sheet " + sheetNum, sheetNum);
				}
			}
			workbook.write();
			workbook.close();

			if (sheetNum > 0) {
				addMsg.append(NEW_LINE);
				addMsg.append(NEW_LINE);
				addMsg.append(Messages.getString("QEDIT.EXPORTLIMIT1"));
				addMsg.append(rowLimit);
				addMsg.append(Messages.getString("QEDIT.EXPORTLIMIT2"));
				addMsg.append(NEW_LINE);
				addMsg.append(sheetNum + 1);
				addMsg.append(Messages.getString("QEDIT.EXPORTLIMIT3"));
			}

			CommonTool.InformationBox(Messages.getString("QEDIT.EXPORT"),
					itemCount + Messages.getString("QEDIT.EXPORTOK") + addMsg);
		} catch (IOException e) {
			CommonTool.ErrorBox(e.getMessage());
			CommonTool.debugPrint(e);
		} catch (RowsExceededException e) {
			CommonTool.ErrorBox(e.getMessage());
			CommonTool.debugPrint(e);
		} catch (NumberFormatException e) {
			CommonTool.ErrorBox(e.getMessage());
			CommonTool.debugPrint(e);
		} catch (WriteException e) {
			CommonTool.ErrorBox(e.getMessage());
			CommonTool.debugPrint(e);
		} catch (OutOfMemoryError e) {
			CommonTool.ErrorBox(e.toString());
			CommonTool.debugPrint(e);
		}
	}

	private void exportXls(CUBRIDResultSet rs) {
		try {
			int sheetNum = 0;
			WritableWorkbook workbook = Workbook.createWorkbook(file);
			WritableSheet sheet = workbook.createSheet("Sheet " + sheetNum,
					sheetNum);
			int rowLimit = 65536; // 65536: limit xls row number.
			int columnLimit = 257; // 256: limit xls column number.
			// it set 257. Because Tbl's first column is oid value that doesn't export.
			StringBuffer addMsg = new StringBuffer("");
			CUBRIDResultSetMetaData rsmt = (CUBRIDResultSetMetaData) rs
					.getMetaData();

			int colCount = rsmt.getColumnCount() + 1;
			if (colCount > columnLimit) {
				if (CommonTool.MsgBox(Application.mainwindow.getShell(),
						SWT.ICON_WARNING | SWT.APPLICATION_MODAL | SWT.YES
								| SWT.NO, Messages.getString("MSG.WARNING"),
						Messages.getString("WARNING.COLUMNCOUNTOVER")) == SWT.NO)
					return;
				colCount = columnLimit;
				addMsg.append(NEW_LINE);
				addMsg.append(NEW_LINE);
				addMsg.append(Messages.getString("MESSAGE.COLUMNCOUNTOVER"));
			}

			int xlsRecordNum = 0, i = 0;
			while (rs.next()) {
				for (int j = 1; j < colCount; j++) {
					String colType = rsmt.getColumnTypeName(j);

					if (rs.getObject(j) == null) {
						sheet.addCell(new Label(j - 1, xlsRecordNum,
								QueryEditor.STR_NULL));
					} else if (colType.equals("INTEGER")
							|| colType.equals("TINYINT")
							|| colType.equals("SMALLINT")
							|| colType.equals("BIGINT")) {
						sheet.addCell(new Number(j - 1, xlsRecordNum, rs
								.getLong(j)));
					} else if (colType.equals("DOUBLE")
							|| colType.equals("FLOAT")
							|| colType.equals("REAL")) {
						sheet.addCell(new Number(j - 1, xlsRecordNum, rs
								.getDouble(j)));
					} else if (colType.equals("NUMERIC")
							|| colType.equals("DECIMAL")
							|| colType.equals("MONETORY")) {
						sheet.addCell(new Label(j - 1, xlsRecordNum, rs
								.getBigDecimal(j).toString()));
					} else if (colType.equals("SET")
							|| colType.equals("MULTISET")
							|| colType.equals("SEQUENCE")) {
						Object[] set = (Object[]) rs.getCollection(j);
						StringBuffer value = new StringBuffer("{");
						for (int k = 0; k < set.length; k++) {
							if (k > 0)
								value.append(", ");
							if (set[k] instanceof CUBRIDOID)
								value.append(((CUBRIDOID) set[k])
										.getOidString());
							else
								value.append(set[k]);
						}
						value.append("}");
						sheet.addCell(new Label(j - 1, xlsRecordNum, value
								.toString()));
					} else {
						sheet.addCell(new Label(j - 1, xlsRecordNum, rs
								.getString(j)));
					}
				}
				xlsRecordNum++;

				if (((i + 1) % rowLimit) == 0) {
					sheetNum++;
					xlsRecordNum -= rowLimit;
					sheet = workbook.createSheet("Sheet " + sheetNum, sheetNum);
				}
				i++;
			}
			workbook.write();
			workbook.close();

			if (sheetNum > 0) {
				addMsg.append(NEW_LINE);
				addMsg.append(NEW_LINE);
				addMsg.append(Messages.getString("QEDIT.EXPORTLIMIT1"));
				addMsg.append(rowLimit);
				addMsg.append(Messages.getString("QEDIT.EXPORTLIMIT2"));
				addMsg.append(NEW_LINE);
				addMsg.append(sheetNum + 1);
				addMsg.append(Messages.getString("QEDIT.EXPORTLIMIT3"));
			}

			CommonTool.InformationBox(Messages.getString("QEDIT.EXPORT"), i
					+ Messages.getString("QEDIT.EXPORTOK") + addMsg);
		} catch (IOException e) {
			CommonTool.ErrorBox(e.getMessage());
			CommonTool.debugPrint(e);
		} catch (RowsExceededException e) {
			CommonTool.ErrorBox(e.getMessage());
			CommonTool.debugPrint(e);
		} catch (NumberFormatException e) {
			CommonTool.ErrorBox(e.getMessage());
			CommonTool.debugPrint(e);
		} catch (WriteException e) {
			CommonTool.ErrorBox(e.getMessage());
			CommonTool.debugPrint(e);
		} catch (SQLException e) {
			CommonTool.ErrorBox(e.getMessage());
			CommonTool.debugPrint(e);
		} catch (OutOfMemoryError e) {
			CommonTool.ErrorBox(e.toString());
			CommonTool.debugPrint(e);
		}

	}

	private void exportCsv(Table tbl, boolean isSelection) {
		try {
			FileOutputStream fs = new FileOutputStream(file);
			TableItem[] tblItem;

			if (isSelection)
				tblItem = tbl.getSelection();
			else
				tblItem = tbl.getItems();

			for (int i = 0; i < tblItem.length; i++) {
				for (int j = 1; j < tbl.getColumnCount(); j++) {
					String colType = tbl.getColumns()[j].getData().toString();
					if (colType.equals("MONETARY") || colType.equals("INTEGER")
							|| colType.equals("TINYINT")
							|| colType.equals("SMALLINT")
							|| colType.equals("BIGINT")
							|| colType.equals("DOUBLE")
							|| colType.equals("FLOAT")
							|| colType.equals("REAL")
							|| colType.equals("NUMERIC")
							|| colType.equals("DECIMAL")) {
						fs.write(tblItem[i].getText(j).getBytes());
					}

					else {
						fs.write(("\""
								+ tblItem[i].getText(j)
										.replaceAll("\"", "\"\"") + "\"")
								.getBytes());
					}
					if (j != tbl.getColumnCount() - 1) {
						fs.write(',');
					}
				}
				fs.write('\n');
			}
			fs.flush();
			fs.close();

			CommonTool.InformationBox(Messages.getString("QEDIT.EXPORT"),
					tblItem.length + Messages.getString("QEDIT.EXPORTOK"));
		} catch (FileNotFoundException e) {
			CommonTool.ErrorBox(e.getMessage());
			CommonTool.debugPrint(e);
		} catch (IOException e) {
			CommonTool.ErrorBox(e.getMessage());
			CommonTool.debugPrint(e);
		}
	}

	private void exportCsv(CUBRIDResultSet rs) {
		try {
			FileOutputStream fs = new FileOutputStream(file);
			CUBRIDResultSetMetaData rsmt = (CUBRIDResultSetMetaData) rs
					.getMetaData();

			int i = 0;
			while (rs.next()) {
				for (int j = 1; j < rsmt.getColumnCount() + 1; j++) {
					String colType = rsmt.getColumnTypeName(j);
					if (rs.getObject(j) != null) {
						if (colType.equals("SET") || colType.equals("MULTISET")
								|| colType.equals("SEQUENCE")) {
							Object[] set = (Object[]) rs.getCollection(j);
							StringBuffer value = new StringBuffer("{");
							for (int k = 0; k < set.length; k++) {
								if (k > 0)
									value.append(", ");
								if (set[k] instanceof CUBRIDOID)
									value.append(((CUBRIDOID) set[k])
											.getOidString());
								else
									value.append(set[k]);
							}
							value.append("}");
							fs.write(("\"".concat(value.toString().replaceAll(
									"\"", "\"\"")).concat("\"")).getBytes());
						} else if (colType.equals("MONETARY")
								|| colType.equals("INTEGER")
								|| colType.equals("TINYINT")
								|| colType.equals("SMALLINT")
								|| colType.equals("BIGINT")
								|| colType.equals("DOUBLE")
								|| colType.equals("FLOAT")
								|| colType.equals("REAL")
								|| colType.equals("NUMERIC")
								|| colType.equals("DECIMAL")) {
							fs.write(rs.getString(j).getBytes());
						} else {
							fs
									.write(("\""
											+ rs.getString(j).replaceAll("\"",
													"\"\"") + "\"").getBytes());
						}
					} else
						fs.write(QueryEditor.STR_NULL.getBytes());

					if (j != rsmt.getColumnCount()) {
						fs.write(',');
					}
				}
				fs.write('\n');
				i++;
			}
			fs.flush();
			fs.close();

			CommonTool.InformationBox(Messages.getString("QEDIT.EXPORT"), i
					+ Messages.getString("QEDIT.EXPORTOK"));
		} catch (FileNotFoundException e) {
			CommonTool.ErrorBox(e.getMessage());
			CommonTool.debugPrint(e);
		} catch (IOException e) {
			CommonTool.ErrorBox(e.getMessage());
			CommonTool.debugPrint(e);
		} catch (SQLException e) {
			CommonTool.ErrorBox(e.getMessage());
			CommonTool.debugPrint(e);
		}
	}

	private void exportSql(CUBRIDResultSet rs, String tableName) {
		try {
			FileOutputStream fs = new FileOutputStream(file);
			CUBRIDResultSetMetaData rsmt = (CUBRIDResultSetMetaData) rs
					.getMetaData();

			StringBuffer insert = new StringBuffer("insert into \"");
			insert.append(tableName);
			insert.append("\" (");
			for (int i = 1; i < rsmt.getColumnCount() + 1; i++) {
				if (i > 1)
					insert.append(", ");
				insert.append("\"");
				insert.append(rsmt.getColumnName(i));
				insert.append("\"");
			}
			insert.append(") ");

			int i = 0;
			while (rs.next()) {
				StringBuffer values = new StringBuffer("values (");
				for (int j = 1; j < rsmt.getColumnCount() + 1; j++) {
					if (j > 1)
						values.append(", ");
					String colType = rsmt.getColumnTypeName(j);
					if (rs.getObject(j) == null)
						values.append("null");
					else if (colType.equals("MONETARY")
							|| colType.equals("INTEGER")
							|| colType.equals("TINYINT")
							|| colType.equals("SMALLINT")
							|| colType.equals("BIGINT")
							|| colType.equals("DOUBLE")
							|| colType.equals("FLOAT")
							|| colType.equals("REAL")
							|| colType.equals("NUMERIC")
							|| colType.equals("DECIMAL")) {
						values.append(rs.getString(j));
					} else if (colType.startsWith("BIT")) {
						values.append("X'");
						values.append(rs.getString(j));
						values.append("'");
					} else if (colType.startsWith("NCHAR")) {
						values.append("N'");
						values.append(rs.getString(j).replaceAll("'", "''"));
						values.append("'");
					} else if (colType.startsWith("CHAR")
							|| colType.startsWith("VARCHAR")
							|| colType.startsWith("TIME")
							|| colType.startsWith("DATE")) {
						values.append("'");
						values.append(rs.getString(j).replaceAll("'", "''"));
						values.append("'");
					} else if (colType.equals("SET")
							|| colType.equals("MULTISET")
							|| colType.equals("SEQUENCE")) {
						Object[] set = (Object[]) rs.getCollection(j);
						StringBuffer value = new StringBuffer("{");
						for (int k = 0; k < set.length; k++) {
							if (k > 0)
								value.append(", ");
							if (set[k] instanceof CUBRIDOID)
								value.append(((CUBRIDOID) set[k])
										.getOidString());
							else
								value.append(set[k].toString().replaceAll("'",
										"''"));
						}
						value.append("}");
						values.append(value);
					} else
						values.append("null");
				}
				values.append(");\n");
				i++;
				fs.write(insert.toString().getBytes());
				fs.write(values.toString().getBytes());
			}
			fs.flush();
			fs.close();

			CommonTool.InformationBox(Messages.getString("QEDIT.EXPORT"), i
					+ Messages.getString("QEDIT.EXPORTOK"));
		} catch (FileNotFoundException e) {
			CommonTool.ErrorBox(e.getMessage());
			CommonTool.debugPrint(e);
		} catch (IOException e) {
			CommonTool.ErrorBox(e.getMessage());
			CommonTool.debugPrint(e);
		} catch (SQLException e) {
			CommonTool.ErrorBox(e.getMessage());
			CommonTool.debugPrint(e);
		}
	}

	private void exportLoad(CUBRIDResultSet rs, String tableName) {
		try {
			FileOutputStream fs = new FileOutputStream(file);
			CUBRIDResultSetMetaData rsmt = (CUBRIDResultSetMetaData) rs
					.getMetaData();

			StringBuffer header = new StringBuffer("%class \"");
			header.append(tableName);
			header.append("\" (");
			for (int i = 1; i < rsmt.getColumnCount() + 1; i++) {
				if (i > 1)
					header.append(" ");
				header.append("\"");
				header.append(rsmt.getColumnName(i));
				header.append("\"");
			}
			header.append(")\n");
			fs.write(header.toString().getBytes());

			// for (int i = 0; i < tblItem.length; i++) {
			int i = 0;
			while (rs.next()) {
				StringBuffer values = new StringBuffer();
				values.append(++i);
				values.append(":");
				for (int j = 1; j < rsmt.getColumnCount() + 1; j++) {
					String colType = rsmt.getColumnTypeName(j);
					if (rs.getObject(j) == null) {
						values.append(" NULL");
					} else if (colType.equals("MONETARY")
							|| colType.equals("INTEGER")
							|| colType.equals("TINYINT")
							|| colType.equals("SMALLINT")
							|| colType.equals("BIGINT")
							|| colType.equals("DOUBLE")
							|| colType.equals("FLOAT")
							|| colType.equals("REAL")
							|| colType.equals("NUMERIC")
							|| colType.equals("DECIMAL")) {
						values.append(" ");
						values.append(rs.getString(j));
					} else if (colType.startsWith("BIT")) {
						values.append(" X'");
						values.append(rs.getString(j));
						values.append("'");
					} else if (colType.startsWith("NCHAR")) {
						values.append(" N'");
						values.append(rs.getString(j).replaceAll("'", "''"));
						values.append("'");
					} else if (colType.startsWith("CHAR")
							|| colType.startsWith("VARCHAR")) {
						values.append(" '");
						values.append(rs.getString(j).replaceAll("'", "''"));
						values.append("'");
					} else if (colType.equals("TIME")) {
						values.append(" time '");
						values.append(rs.getString(j));
						values.append("'");
					} else if (colType.equals("DATE")) {
						values.append(" date '");
						values.append(rs.getString(j));
						values.append("'");
					} else if (colType.equals("TIMESTAMP")) {
						values.append(" timestamp '");
						values.append(rs.getString(j));
						values.append("'");
					} else if (colType.equals("SET")
							|| colType.equals("MULTISET")
							|| colType.equals("SEQUENCE")) {
						Object[] set = (Object[]) rs.getCollection(j);
						StringBuffer value = new StringBuffer(" {");
						for (int k = 0; k < set.length; k++) {
							if (k > 0)
								value.append(", ");
							if (set[k] instanceof CUBRIDOID)
								value.append(((CUBRIDOID) set[k])
										.getOidString());
							else
								value.append(set[k].toString().replaceAll("'",
										"''"));
						}
						value.append("}");
						values.append(value);
					} else
						values.append(" NULL");
				}
				values.append("\n");

				fs.write(values.toString().getBytes());
			}
			fs.flush();
			fs.close();

			CommonTool.InformationBox(Messages.getString("QEDIT.EXPORT"), i
					+ Messages.getString("QEDIT.EXPORTOK"));
		} catch (FileNotFoundException e) {
			CommonTool.ErrorBox(e.getMessage());
			CommonTool.debugPrint(e);
		} catch (IOException e) {
			CommonTool.ErrorBox(e.getMessage());
			CommonTool.debugPrint(e);
		} catch (SQLException e) {
			CommonTool.ErrorBox(e.getMessage());
			CommonTool.debugPrint(e);
		}
	}
}
