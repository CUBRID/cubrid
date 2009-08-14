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
package com.cubrid.cubridmanager.ui.cubrid.table;

import java.io.BufferedWriter;
import java.io.File;
import java.io.FileNotFoundException;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.OutputStreamWriter;
import java.io.UnsupportedEncodingException;
import java.lang.reflect.InvocationTargetException;
import java.sql.Connection;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.sql.Statement;
import java.sql.Timestamp;
import java.text.ParseException;

import jxl.Workbook;
import jxl.write.Label;
import jxl.write.Number;
import jxl.write.WritableSheet;
import jxl.write.WritableWorkbook;
import jxl.write.WriteException;
import jxl.write.biff.RowsExceededException;

import org.apache.log4j.Logger;
import org.eclipse.core.runtime.IProgressMonitor;
import org.eclipse.jface.dialogs.ProgressMonitorDialog;
import org.eclipse.swt.SWT;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.FileDialog;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.ui.PlatformUI;

import com.cubrid.cubridmanager.core.common.log.LogUtil;
import com.cubrid.cubridmanager.core.cubrid.table.model.DBAttribute;
import com.cubrid.cubridmanager.core.cubrid.table.model.DataType;
import com.cubrid.cubridmanager.core.query.QueryOptions;
import com.cubrid.cubridmanager.ui.CubridManagerUIPlugin;
import com.cubrid.cubridmanager.ui.spi.CommonTool;
import com.cubrid.cubridmanager.ui.spi.model.CubridDatabase;
import com.cubrid.cubridmanager.ui.spi.progress.ExecTaskWithProgress;
import com.cubrid.cubridmanager.ui.spi.progress.TaskExecutor;

import cubrid.jdbc.driver.CUBRIDResultSet;
import cubrid.jdbc.driver.CUBRIDResultSetMetaData;
import cubrid.sql.CUBRIDOID;

public class ExportDataUtil {
	private static final Logger logger = LogUtil.getLogger(ExportDataUtil.class);
	private String fileName;
	private File file;
	private String sql = null;
	private Statement stmt = null;
	private String thistableName = null;
	private CubridDatabase database;
	private String returnMSG = null;
	boolean returnErrorFlag = false;
	static private String sessionExportKey = "Export-ExportFilePath";
	boolean exit = false;

	public ExportDataUtil(Connection con, String tableName,
			CubridDatabase database) throws SQLException {
		this.database = database;
		fileName = getFileName(new String[] { "*.xls", "*.csv", "*.sql",
				"*.obs" }, new String[] { "Excel(xls)",
				"Comma separated value(csv)", "Insert query script file(sql)",
				"CUBRID load format file(obs)" });
		if (fileName == null)
			return;
		CubridManagerUIPlugin.setPreference(sessionExportKey, file.getParent());

		sql = "select * from \"" + tableName + "\"";
		stmt = con.createStatement(ResultSet.TYPE_SCROLL_INSENSITIVE,
				ResultSet.CONCUR_UPDATABLE, ResultSet.HOLD_CURSORS_OVER_COMMIT);

		thistableName = tableName;
		final Shell shell = PlatformUI.getWorkbench().getActiveWorkbenchWindow().getShell();
		TaskExecutor taskExcutor = new TaskExecutor() {
			private void openErrorBox() {
				Display.getDefault().syncExec(new Runnable() {
					public void run() {
						CommonTool.openErrorBox(shell, returnMSG);
					}
				});
			}

			public boolean exec(final IProgressMonitor monitor) {
				if (monitor.isCanceled()) {
					return false;
				}
				try {
					monitor.beginTask(Messages.exportMonitorMsg, 100);
					monitor.worked(30);
					CUBRIDResultSet rs = null;
					rs = (CUBRIDResultSet) stmt.executeQuery(sql);
					
					if (fileName.toLowerCase().endsWith(".xls"))
						exportXls(rs);
					else if (fileName.toLowerCase().endsWith(".csv"))
						exportCsv(rs);
					else if (fileName.toLowerCase().endsWith(".sql"))
						exportSql(rs, thistableName);
					else if (fileName.toLowerCase().endsWith(".obs"))
						exportLoad(rs, thistableName);
					else {
						exportCsv(rs);
					}					
					
					if (returnErrorFlag) {
						openErrorBox();
					} else {
						if (returnMSG != null) {
							Display.getDefault().syncExec(new Runnable() {
								public void run() {
									CommonTool.openInformationBox(shell,
											Messages.exportResultTitle,
											returnMSG);
								}
							});
						}
					}
					monitor.worked(100);
				} catch (SQLException e) {
					returnMSG = e.getMessage();
					openErrorBox();
					logger.error(e);
				} catch (NumberFormatException e) {
					returnMSG = e.getMessage();
					openErrorBox();
					logger.error(e);
				} catch (ParseException e) {
					returnMSG = e.getMessage();
					openErrorBox();
					logger.error(e);
				} finally {
					monitor.done();
				}
				return true;
			}
		};
		try {
			new ProgressMonitorDialog(shell).run(true, true,
					new ExecTaskWithProgress(taskExcutor));
		} catch (InvocationTargetException e) {
			logger.error(e.getMessage(), e);
		} catch (InterruptedException e) {
			logger.error(e.getMessage(), e);
		}
	}

	private String getFileName(String[] filterExts, String[] filterNames) {

		FileDialog dialog = new FileDialog(
				PlatformUI.getWorkbench().getActiveWorkbenchWindow().getShell(),
				SWT.SAVE | SWT.APPLICATION_MODAL);
		String filepath = CubridManagerUIPlugin.getPreference(sessionExportKey);
		if (null != filepath) {
			dialog.setFilterPath(filepath);
		}
		dialog.setFilterExtensions(filterExts);
		dialog.setFilterNames(filterNames);

		boolean doExit = false;
		while (!doExit) {
			String result = dialog.open();
			if (result != null) {
				if (!result.toLowerCase().endsWith(".xls")
						&& !result.toLowerCase().endsWith(".csv")
						&& !result.toLowerCase().endsWith(".sql")
						&& !result.toLowerCase().endsWith(".obs")) {
					result = result.concat(".csv");
				}
				File tmpFile = new File(result);
				if (tmpFile.exists()) {
					boolean state = CommonTool.openConfirmBox(Messages.bind(
							Messages.exportFileOverwriteQuestionMSG,
							tmpFile.getAbsolutePath()));
					if (state) {
						String bakfile = result + ".bak";
						boolean delSuccess = new File(bakfile).delete();
						boolean renameSuccess = tmpFile.renameTo(new File(
								bakfile));
						String msg = "";
						if (!delSuccess) {
							msg += Messages.bind(Messages.errFileCannotDelete,
									bakfile);
						}
						if (!renameSuccess) {
							msg += Messages.bind(Messages.errFileCannotRename,
									result, bakfile);
							boolean state2 = CommonTool.openConfirmBox(msg);
							if (state2) {
								//continue
							} else {
								doExit = false;
								continue;
							}
						}

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

	private void exportXls(CUBRIDResultSet rs) {
		WritableWorkbook workbook = null;
		try {
			int sheetNum = 0;
			workbook = Workbook.createWorkbook(file);

			WritableSheet sheet = workbook.createSheet("Sheet " + sheetNum,
					sheetNum);
			int rowLimit = 65536; // 65536: limit xls row number.
			int columnLimit = 257; // 256: limit xls column number.
			// it set 257. Because Tbl's first column is oid value that doesn't
			// export.
			int cellCharacterLimit = 32767;
			StringBuffer addMsg = new StringBuffer("");
			CUBRIDResultSetMetaData rsmt = (CUBRIDResultSetMetaData) rs.getMetaData();

			int colCount = rsmt.getColumnCount() + 1;
			exit = false;
			for (int j = 1; j < colCount; j++) {
				String columnName = rsmt.getColumnName(j);
				int precision = rsmt.getPrecision(j);
				if (precision > cellCharacterLimit) {
					final String confirmMSG = Messages.bind(
							Messages.exportCharacterCountExceedWarnInfo,
							columnName);

					Display.getDefault().syncExec(new Runnable() {
						public void run() {
							if (!CommonTool.openConfirmBox(confirmMSG)) {
								exit = true;
							}
						}
					});
					if (exit) {
						return;
					} else {
						break;
					}
				}
			}
			if (colCount > columnLimit) {
				Display.getDefault().syncExec(new Runnable() {
					public void run() {
						if (!CommonTool.openConfirmBox(Messages.exportColumnCountOverWarnInfo)) {
							exit = true;
						}
					}
				});
				if (exit) {
					return;
				}
				colCount = columnLimit;
				addMsg.append(Messages.exportColumnCountOver);
			}

			int xlsRecordNum = 0, i = 0;
			while (rs.next()) {
				for (int j = 1; j < colCount; j++) {
					String colType = rsmt.getColumnTypeName(j);

					if (rs.getObject(j) == null) {
						//no action, so response cell will be set null
					} else if (colType.equals("INTEGER")
							|| colType.equals("TINYINT")
							|| colType.equals("SMALLINT")
							|| colType.equals("BIGINT")) {
						sheet.addCell(new Number(j - 1, xlsRecordNum,
								rs.getLong(j)));

					} else if (colType.equals("BIT")
							|| colType.equals("BIT VARYING")) {
						byte[] bytes = (byte[]) rs.getObject(j);
						String bitString = getBitString(bytes);
						if (bitString.length() > cellCharacterLimit) {
							bitString = bitString.substring(0,
									cellCharacterLimit);
						}
						sheet.addCell(new Label(j - 1, xlsRecordNum, bitString));
					} else if (colType.equals("DOUBLE")
							|| colType.equals("FLOAT")
							|| colType.equals("REAL")) {
						sheet.addCell(new Number(j - 1, xlsRecordNum,
								rs.getDouble(j)));
					} else if (colType.equals("NUMERIC")
							|| colType.equals("DECIMAL")
							|| colType.equals("MONETORY")) {
						sheet.addCell(new Label(j - 1, xlsRecordNum,
								rs.getBigDecimal(j).toString()));
					} else if (colType.equals("DATETIME")) {
						String datetime = formatDateTime(rs.getTimestamp(j));
						sheet.addCell(new Label(j - 1, xlsRecordNum, datetime));
					} else if (colType.equals("CLASS")) {
						//no action, so response cell will be set null
					} else if (colType.equals("SET")
							|| colType.equals("MULTISET")
							|| colType.equals("SEQUENCE")) {
						Object[] set = (Object[]) rs.getCollection(j);
						StringBuffer value = new StringBuffer("{");
						for (int k = 0; k < set.length; k++) {
							String elemType = rsmt.getElementTypeName(j);
							if (k > 0)
								value.append(",");
							if (set[k] == null) {
								value.append(QueryOptions.STR_NULL);
							} else if (set[k] instanceof CUBRIDOID)
								//oid value will be set NULL
								value.append(QueryOptions.STR_NULL);
							else {
								if (elemType.equals("DATETIME")) {
									Timestamp datetime = (Timestamp) set[k];
									String datetimeStr = formatDateTime(datetime);
									value.append(getCVSValueInSet(elemType,
											datetimeStr));
								} else {
									value.append(getCVSValueInSet(elemType,
											set[k].toString()));
								}
							}
						}
						value.append("}");
						if (value.length() > cellCharacterLimit) {
							sheet.addCell(new Label(j - 1, xlsRecordNum,
									value.substring(0, cellCharacterLimit)));
						} else {
							sheet.addCell(new Label(j - 1, xlsRecordNum,
									value.toString()));
						}
					} else {
						String value = rs.getString(j);
						if (value.length() > cellCharacterLimit) {
							sheet.addCell(new Label(j - 1, xlsRecordNum,
									value.substring(0, cellCharacterLimit)));
						} else {
							sheet.addCell(new Label(j - 1, xlsRecordNum,
									value.toString()));
						}
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
			if (sheetNum > 0) {
				addMsg.append(Messages.bind(Messages.exportLimit, rowLimit,
						sheetNum + 1));
			}
			int size = i;
			String msg;
			if (size <= 1) {
				msg = Messages.bind(Messages.exportResultOK1, size);
			} else {
				msg = Messages.bind(Messages.exportResultOK2, size);
			}
			returnMSG = msg + addMsg;
			returnErrorFlag = false;

		} catch (IOException e) {
			returnMSG = e.getMessage();
			returnErrorFlag = true;
			logger.error(e);
		} catch (RowsExceededException e) {
			returnMSG = e.getMessage();
			returnErrorFlag = true;
			logger.error(e);
		} catch (NumberFormatException e) {
			returnMSG = e.getMessage();
			returnErrorFlag = true;
			logger.error(e);
		} catch (WriteException e) {
			returnMSG = e.getMessage();
			returnErrorFlag = true;
			logger.error(e);
		} catch (SQLException e) {
			returnMSG = e.getMessage();
			returnErrorFlag = true;
			logger.error(e);
		} catch (OutOfMemoryError e) {
			returnMSG = e.getMessage();
			returnErrorFlag = true;
			logger.error(e);
		} finally {
			try {
				if (workbook != null)
					workbook.close();
			} catch (WriteException e) {
				logger.error(e);
			} catch (IOException e) {
				logger.error(e);
			}
		}

	}

	private BufferedWriter getBufferedWriter() throws UnsupportedEncodingException,
			FileNotFoundException {
		BufferedWriter fs = null;
		String charset = QueryOptions.getCharset(database.getDatabaseInfo());
		if (charset != null && charset.trim().length() > 0) {
			fs = new BufferedWriter(new OutputStreamWriter(
					new FileOutputStream(file), charset.trim()));
		} else {
			fs = new BufferedWriter(new OutputStreamWriter(
					new FileOutputStream(file)));
		}
		return fs;
	}

	private void exportCsv(CUBRIDResultSet rs) {
		BufferedWriter fs = null;
		try {
			fs = getBufferedWriter();
			CUBRIDResultSetMetaData rsmt = (CUBRIDResultSetMetaData) rs.getMetaData();

			int i = 0;
			while (rs.next()) {
				writeCSVNextLine(fs, rs, rsmt);

				i++;
			}
			int size = i;
			String msg;
			if (size <= 1) {
				msg = Messages.bind(Messages.exportResultOK1, size);
			} else {
				msg = Messages.bind(Messages.exportResultOK2, size);
			}
			returnMSG = msg;
			returnErrorFlag = false;
		} catch (FileNotFoundException e) {
			returnMSG = e.getMessage();
			returnErrorFlag = true;
			logger.error(e);
		} catch (IOException e) {
			returnMSG = e.getMessage();
			returnErrorFlag = true;
			logger.error(e);
		} catch (SQLException e) {
			returnMSG = e.getMessage();
			returnErrorFlag = true;
			logger.error(e);
		} finally {
			try {
				if (fs != null)
					fs.close();
			} catch (IOException e) {
				logger.error(e);
			}
		}
	}

	/**
	 * write a line to a csv file
	 * 
	 * @param fs
	 * @param rs
	 * @param rsmt
	 * @throws SQLException
	 * @throws IOException
	 */
	private void writeCSVNextLine(BufferedWriter fs, CUBRIDResultSet rs,
			CUBRIDResultSetMetaData rsmt) throws SQLException, IOException {
		for (int j = 1; j < rsmt.getColumnCount() + 1; j++) {
			String colType = rsmt.getColumnTypeName(j);
			if (rs.getObject(j) != null) {
				if (colType.equals("SET") || colType.equals("MULTISET")
						|| colType.equals("SEQUENCE")) {
					String elemType = rsmt.getElementTypeName(j);
					Object[] set = (Object[]) rs.getCollection(j);
					StringBuffer value = new StringBuffer("{");
					for (int k = 0; k < set.length; k++) {
						if (k > 0) {
							value.append(",");
						}
						if (set[k] == null) {
							value.append(QueryOptions.STR_NULL);
						} else if (set[k] instanceof CUBRIDOID) {
							value.append(QueryOptions.STR_NULL);
						} else {
							if (elemType.equals("DATETIME")) {
								Timestamp datetime = (Timestamp) set[k];
								String datetimeStr = formatDateTime(datetime);
								value.append(getCVSValueInSet(elemType,
										datetimeStr));
							} else {
								value.append(getCVSValueInSet(elemType,
										set[k].toString()));
							}
						}
					}
					value.append("}");
					fs.write(("\"".concat(value.toString().replaceAll("\"",
							"\"\"")).concat("\"")));
				} else if (colType.equals("MONETARY")
						|| colType.equals("INTEGER")
						|| colType.equals("TINYINT")
						|| colType.equals("SMALLINT")
						|| colType.equals("BIGINT") || colType.equals("DOUBLE")
						|| colType.equals("FLOAT") || colType.equals("REAL")
						|| colType.equals("NUMERIC")
						|| colType.equals("DECIMAL")) {
					fs.write(rs.getString(j));
				} else if (colType.equals("DATETIME")) {
					String datetime = formatDateTime(rs.getTimestamp(j));
					fs.write("\"" + datetime + "\"");
				} else if (colType.equals("BIT")
						|| colType.equals("BIT VARYING")) {
					byte[] bytes = (byte[]) rs.getObject(j);
					String bitString = getBitString(bytes);
					fs.write(bitString);
				} else if (colType.equals("CLASS")) {
					fs.write(QueryOptions.STR_NULL);
				} else {
					fs.write(("\"" + rs.getString(j).replaceAll("\"", "\"\"") + "\""));
				}
			} else {
				fs.write(QueryOptions.STR_NULL);
			}

			if (j != rsmt.getColumnCount()) {
				fs.write(',');
			}
		}
		fs.write('\n');
		fs.flush();
	}

	private String getCVSValueInSet(String colType, String value) throws SQLException {
		if (colType.equals("CHAR") || colType.equals("VARCHAR")
				|| colType.equals("NCHAR") || colType.equals("NCHAR VARYING")) {
			return "\"" + value.replaceAll("\"", "\"\"") + "\"";
		} else {
			return value;
		}
	}

	/**
	 * use only for exporting data to a file
	 * 
	 * @param datetime
	 * @return
	 */
	public static String formatDateTime(java.sql.Timestamp datetime) {
		long time = datetime.getTime();
		return com.cubrid.cubridmanager.core.CommonTool.getDatetimeString(time,
				"yyyy-MM-dd HH:mm:ss.SSS");
	}

	private void exportSql(CUBRIDResultSet rs, String tableName) throws NumberFormatException,
			ParseException {
		BufferedWriter fs = null;
		try {
			fs = getBufferedWriter();
			CUBRIDResultSetMetaData rsmt = (CUBRIDResultSetMetaData) rs.getMetaData();

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
					} else if (colType.equals("BIT")
							|| colType.equals("BIT VARYING")) {
						byte[] bytes = (byte[]) rs.getObject(j);
						String bitString = getBitString(bytes);
						values.append("X'");
						values.append(bitString);
						values.append("'");

					} else if (colType.startsWith("NCHAR")) {
						values.append("N'");
						values.append(rs.getString(j).replaceAll("'", "''"));
						values.append("'");
					} else if (colType.equalsIgnoreCase("DATETIME")) {
						String datetimeStr = formatDateTime(rs.getTimestamp(j));
						String formatValue = DBAttribute.formatValue(
								"DATETIME", datetimeStr);
						values.append(formatValue);
					} else if (colType.equalsIgnoreCase("TIMESTAMP")) {
						String datetime = rs.getString(j);
						String formatValue = DBAttribute.formatValue(
								"TIMESTAMP", datetime);
						values.append(formatValue);
					} else if (colType.equalsIgnoreCase("DATE")) {
						String datetime = rs.getString(j);
						String formatValue = DBAttribute.formatValue("DATE",
								datetime);
						values.append(formatValue);
					} else if (colType.equalsIgnoreCase("TIME")) {
						String datetime = rs.getString(j);
						String formatValue = DBAttribute.formatValue("TIME",
								datetime);
						values.append(formatValue);
					} else if (colType.startsWith("CHAR")
							|| colType.startsWith("VARCHAR")) {
						values.append("'");
						values.append(rs.getString(j).replaceAll("'", "''"));
						values.append("'");
					} else if (colType.equals("CLASS")) {
						values.append(QueryOptions.STR_NULL);
					} else if (colType.equals("SET")
							|| colType.equals("MULTISET")
							|| colType.equals("SEQUENCE")) {
						Object[] set = (Object[]) rs.getCollection(j);
						StringBuffer value = new StringBuffer("{");
						String elemType = rsmt.getElementTypeName(j);
						int s = rsmt.getScale(j);
						int p = rsmt.getPrecision(j);
						elemType = DataType.makeType(elemType, null, p, s);
						elemType = DataType.getType(elemType);
						for (int k = 0; k < set.length; k++) {
							if (k > 0)
								value.append(", ");
							if (set[k] == null) {
								value.append(QueryOptions.STR_NULL);
							} else if (set[k] instanceof CUBRIDOID) {
								value.append(QueryOptions.STR_NULL);
							} else {
								String elem = set[k].toString();
								if (elemType.equalsIgnoreCase("DATETIME")) {
									Timestamp datetime = (Timestamp) set[k];
									elem = formatDateTime(datetime);
								}
								elem = DBAttribute.formatValue(elemType, elem);
								value.append(elem);
							}
						}
						value.append("}");
						values.append(value);
					} else
						values.append("null");
				}
				values.append(");\n");
				i++;
				fs.write(insert.toString());
				fs.write(values.toString());
				fs.flush();
			}

			int size = i;
			String msg;
			if (size <= 1) {
				msg = Messages.bind(Messages.exportResultOK1, size);
			} else {
				msg = Messages.bind(Messages.exportResultOK2, size);
			}
			returnMSG = msg;
			returnErrorFlag = false;
		} catch (FileNotFoundException e) {
			CommonTool.openErrorBox(e.getMessage());
			logger.error(e);
		} catch (IOException e) {
			CommonTool.openErrorBox(e.getMessage());
			logger.error(e);
		} catch (SQLException e) {
			CommonTool.openErrorBox(e.getMessage());
			logger.error(e);
		} finally {
			try {
				if (fs != null)
					fs.close();
			} catch (IOException e) {
				logger.error(e);
			}
		}
	}

	/**
	 * reture bit String
	 * 
	 * @param bytes
	 * @return
	 */
	private String getBitString(byte[] bytes) {
		StringBuffer bf = new StringBuffer();
		for (byte b : bytes) {
			int value = b & 0x00ff;
			bf.append(Integer.toHexString(value));
		}
		return bf.toString();
	}

	private void exportLoad(CUBRIDResultSet rs, String tableName) {
		BufferedWriter fs = null;
		try {
			fs = getBufferedWriter();
			CUBRIDResultSetMetaData rsmt = (CUBRIDResultSetMetaData) rs.getMetaData();

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
			fs.write(header.toString());

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
					} else if (colType.equals("BIT")
							|| colType.equals("BIT VARYING")) {
						byte[] bytes = (byte[]) rs.getObject(j);						
						String bitString = getBitString(bytes);
						values.append(" X'");
						values.append(bitString);
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
					} else if (colType.equals("DATETIME")) {
						String datetime = formatDateTime(rs.getTimestamp(j));
						values.append(" datetime '");
						values.append(datetime);
						values.append("'");
					} else if (colType.equals("CLASS")) {
						values.append(" NULL");
					} else if (colType.equals("SET")
							|| colType.equals("MULTISET")
							|| colType.equals("SEQUENCE")) {
						Object[] set = (Object[]) rs.getCollection(j);
						StringBuffer value = new StringBuffer(" {");
						for (int k = 0; k < set.length; k++) {
							if (k > 0)
								value.append(", ");
							if (set[k] == null) {
								value.append(QueryOptions.STR_NULL);
							} else if (set[k] instanceof CUBRIDOID)
								value.append(QueryOptions.STR_NULL);
							else {
								String elemType = rsmt.getElementTypeName(j);
								String elem = null;
								if (elemType.equals("DATETIME")) {
									Timestamp datetime = (Timestamp) set[k];
									elem = formatDateTime(datetime);
								} else {
									elem = set[k].toString();
								}
								value.append("\"").append(
										elem.replaceAll("'", "''")).append("\"");
							}
						}
						value.append("}");
						values.append(value);
					} else {
						values.append(" NULL");
					}
				}
				values.append("\n");

				fs.write(values.toString());
				fs.flush();
			}
			int size = i;
			String msg;
			if (size <= 1) {
				msg = Messages.bind(Messages.exportResultOK1, size);
			} else {
				msg = Messages.bind(Messages.exportResultOK2, size);
			}
			returnMSG = msg;
			returnErrorFlag = false;
		} catch (FileNotFoundException e) {
			returnMSG = e.getMessage();
			returnErrorFlag = true;
			logger.error(e);
		} catch (IOException e) {
			returnMSG = e.getMessage();
			returnErrorFlag = true;
			logger.error(e);
		} catch (SQLException e) {
			returnMSG = e.getMessage();
			returnErrorFlag = true;
			logger.error(e);
		} finally {
			try {
				if (fs != null)
					fs.close();
			} catch (IOException e) {
				logger.error(e);
			}
		}
	}
}
