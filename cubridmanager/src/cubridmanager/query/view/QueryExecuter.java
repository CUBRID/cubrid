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

package cubridmanager.query.view;

import java.sql.Connection;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.text.NumberFormat;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

import org.eclipse.jface.action.Action;
import org.eclipse.jface.action.ToolBarManager;
import org.eclipse.swt.SWT;
import org.eclipse.swt.custom.StyledText;
import org.eclipse.swt.custom.TableEditor;
import org.eclipse.swt.dnd.Clipboard;
import org.eclipse.swt.dnd.TextTransfer;
import org.eclipse.swt.dnd.Transfer;
import org.eclipse.swt.events.KeyEvent;
import org.eclipse.swt.events.MenuEvent;
import org.eclipse.swt.events.SelectionAdapter;
import org.eclipse.swt.events.SelectionEvent;
import org.eclipse.swt.events.SelectionListener;
import org.eclipse.swt.graphics.Point;
import org.eclipse.swt.graphics.RGB;
import org.eclipse.swt.graphics.Rectangle;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.Event;
import org.eclipse.swt.widgets.Listener;
import org.eclipse.swt.widgets.Menu;
import org.eclipse.swt.widgets.MenuItem;
import org.eclipse.swt.widgets.MessageBox;
import org.eclipse.swt.widgets.Table;
import org.eclipse.swt.widgets.TableColumn;
import org.eclipse.swt.widgets.TableItem;
import org.eclipse.swt.widgets.Text;

import cubrid.jdbc.driver.CUBRIDPreparedStatement;
import cubrid.jdbc.driver.CUBRIDResultSet;
import cubrid.jdbc.driver.CUBRIDResultSetMetaData;
import cubrid.sql.CUBRIDOID;
import cubridmanager.Application;
import cubridmanager.CommonTool;
import cubridmanager.MainConstants;
import cubridmanager.MainRegistry;
import cubridmanager.Messages;
import cubridmanager.query.QueryUtil;
import cubridmanager.query.action.FirstAction;
import cubridmanager.query.action.LastAction;
import cubridmanager.query.action.NextAction;
import cubridmanager.query.action.PreviousAction;
import cubridmanager.query.dialog.Export;
import cubridmanager.query.dialog.OIDNavigator;

public class QueryExecuter {
	private int recordLimit;
	public String query = new String();
	public int idx;
	public int cntRecord = 0;
	private boolean doesGetOidInfo;
	public Table tblResult = null;
	private QueryEditor qe;
	private int curIndex = -1;
	private int newIndex = -1;
	private QueryInfo queryInfo = null;
	private Action firstPageAction = null;
	private Action nextPageAction = null;
	private Action previousPageAction = null;
	private Action lastPageAction = null;
	private List<Map<String, String>> allDataList = null;
	private List<ColumnInfo> allColumnList = null;
	private boolean isEnd = false;
	public int pageLimit; // each page size
	private String queryMsg;
	private String multiQuerySql = null;
	private Map<String, ColumnComparator> colComparatorMap = null;

	public QueryExecuter(QueryEditor qe, int idx, String query) {
		this.qe = qe;
		this.idx = idx;
		this.query = query;

		pageLimit = MainRegistry.queryEditorOption.pagelimit;
		recordLimit = MainRegistry.queryEditorOption.recordlimit;
		doesGetOidInfo = MainRegistry.queryEditorOption.oidinfo;
		allDataList = new ArrayList<Map<String, String>>();
		allColumnList = new ArrayList<ColumnInfo>();
		colComparatorMap = new HashMap<String, ColumnComparator>();
	}

	public QueryExecuter() {
		this(null, 0, null);
	}

	/**
	 * make table by rs,including column data information and item data
	 * information
	 * 
	 * @param rs
	 * 
	 */
	public void makeTable(CUBRIDResultSet rs) throws SQLException {
		fillColumnData(rs);
		fillTableItemData(rs);
	}

	/**
	 * fill table column data by rs,all data is saved to allColumnList
	 * 
	 * @param rs
	 * 
	 */
	private void fillColumnData(CUBRIDResultSet rs) throws SQLException {
		CUBRIDResultSetMetaData rsmt = (CUBRIDResultSetMetaData) rs
				.getMetaData();
		int cntColumn = rsmt.getColumnCount();
		if (doesGetOidInfo) {
			allColumnList.add(0, new ColumnInfo("OID", "OID"));
		}
		for (int i = 1; i <= cntColumn; i++) {
			allColumnList.add(new ColumnInfo(rsmt.getColumnName(i), rsmt
					.getColumnTypeName(i)));
		}
	}

	/**
	 * fill table item data by rs,all data is saved to allDataList
	 * 
	 * @param rs
	 * 
	 */
	private void fillTableItemData(CUBRIDResultSet rs) throws SQLException {
		cntRecord = 0;
		int limit = recordLimit;
		isEnd = false;
		while (rs.next()) {
			cntRecord++;
			Map<String, String> map = new HashMap<String, String>();
			int columnPos = 0, columnCount = allColumnList.size();
			if (doesGetOidInfo) {
				CUBRIDOID oid = ((CUBRIDResultSet) rs).getOID();
				if (oid != null && oid.getOidString() != null)
					map.put("OID", oid.getOidString());
				else {
					map.put("OID", "NONE");
				}
				columnPos++;
				columnCount--;
			}
			for (int j = 1; j <= columnCount; j++) {
				String data = QueryEditor.STR_NULL;
				String columnType = ((ColumnInfo) allColumnList.get(columnPos))
						.getType();
				String columnName = ((ColumnInfo) allColumnList.get(columnPos))
						.getName();
				if (rs.getObject(j) != null) {
					if (columnType.equals("SET")
							|| columnType.equals("MULTISET")
							|| columnType.equals("SEQUENCE")) {
						Object[] set = (Object[]) rs.getCollection(j);
						data = "{";
						for (int i = 0; i < set.length; i++) {
							if (set[i] instanceof CUBRIDOID)
								data += ((CUBRIDOID) set[i]).getOidString();
							else
								data += set[i];
							if (i < set.length - 1)
								data += ", ";
						}
						data += "}";
					} else {
						data = rs.getString(j);
					}
				}
				map.put(columnName, data);
				columnPos++;
			}
			allDataList.add(map);
			if (recordLimit > 0 && cntRecord >= limit && multiQuerySql == null) {
				final String msg = Messages.getString("QEDIT.TOOMANYRECORD1")
						+ " " + limit + " "
						+ Messages.getString("QEDIT.TOOMANYRECORD2");
				Application.mainwindow.getShell().getDisplay().syncExec(
						new Runnable() {
							public void run() {
								MessageBox mb = new MessageBox(
										Application.mainwindow.getShell(),
										SWT.ICON_WARNING | SWT.YES | SWT.NO);
								mb.setText(Messages.getString("QEDIT.WARNING"));
								mb.setMessage(msg);
								int state = mb.open();
								if (state == SWT.NO) {
									isEnd = true;
								}
							}
						});
				limit += recordLimit;
			}
			if (isEnd) {
				break;
			}
		}
		if (multiQuerySql == null) {
			queryInfo = new QueryInfo(cntRecord, pageLimit);
			queryInfo.setCurrentPage(1);
		}
	}

	/**
	 * fill paged action into query editor result toolbar
	 * 
	 * @param toolBarManager
	 * 
	 */
	public void makeActions(ToolBarManager toolBarManager) {
		firstPageAction = new FirstAction(this);
		lastPageAction = new LastAction(this);
		previousPageAction = new PreviousAction(this);
		nextPageAction = new NextAction(this);
		toolBarManager.add(firstPageAction);
		toolBarManager.add(previousPageAction);
		toolBarManager.add(nextPageAction);
		toolBarManager.add(lastPageAction);
		updateActions();
		toolBarManager.update(true);
	}

	/**
	 * update paged action state
	 * 
	 */
	public void updateActions() {
		if (queryInfo.getCurrentPage() <= 1) {
			firstPageAction.setEnabled(false);
			previousPageAction.setEnabled(false);
		} else {
			firstPageAction.setEnabled(true);
			previousPageAction.setEnabled(true);
		}
		if (queryInfo.getCurrentPage() >= queryInfo.getPages()) {
			lastPageAction.setEnabled(false);
			nextPageAction.setEnabled(false);
		} else {
			lastPageAction.setEnabled(true);
			nextPageAction.setEnabled(true);
		}
	}

	/**
	 * make query editor result panel,including table panel and sql text and
	 * message text
	 * 
	 * @param tblResult,
	 *            the Table.
	 * @param sqlText,
	 *            the Text.
	 * @param messageText,
	 *            the StyledText.
	 * 
	 */
	public void makeResult(final Table tblResult, Text sqlText,
			StyledText messageText) {
		this.tblResult = tblResult;
		if (qe != null) {
			Menu menu = new Menu(Application.mainwindow.getShell(), SWT.POP_UP);
			final MenuItem itemCopy = new MenuItem(menu, SWT.PUSH);
			itemCopy.setText(Messages.getString("QEDIT.COPYCLIPBOARD"));

			final MenuItem itemDelete = new MenuItem(menu, SWT.PUSH);
			itemDelete.setText(Messages.getString("QEDIT.DELETE"));

			new MenuItem(menu, SWT.SEPARATOR);

			MenuItem itemExport = new MenuItem(menu, SWT.CASCADE);
			itemExport.setText(Messages.getString("QEDIT.EXPORT"));
			tblResult.setMenu(menu);
			Menu subItemExport = new Menu(Application.mainwindow.getShell(),
					SWT.DROP_DOWN);
			itemExport.setMenu(subItemExport);
			final MenuItem itemExportAll = new MenuItem(subItemExport, SWT.PUSH);
			itemExportAll.setText(Messages.getString("QEDIT.ALL"));
			final MenuItem itemExportSelection = new MenuItem(subItemExport,
					SWT.PUSH);
			itemExportSelection.setText(Messages.getString("QEDIT.SELECTION"));

			menu.addMenuListener(new org.eclipse.swt.events.MenuAdapter() {
				public void menuShown(MenuEvent e) {
					TableItem[] tblItems = tblResult.getSelection();
					itemDelete.setEnabled(true);
					itemCopy.setEnabled(true);
					itemExportAll.setEnabled(true);
					if (tblItems.length == 0) {
						itemDelete.setEnabled(false);
						itemCopy.setEnabled(false);
						itemExportSelection.setEnabled(false);
					}
					if (allDataList.size() <= 0) {
						itemExportAll.setEnabled(false);
					}
					for (int i = 0; i < tblItems.length; i++) {
						if (!doesGetOidInfo) {
							itemDelete.setEnabled(false);
						} else {
							if (tblItems[i].getText(1).equals("NONE"))
								itemDelete.setEnabled(false);
						}
					}
				}
			});

			itemCopy.addSelectionListener(new SelectionAdapter() {
				public void widgetSelected(SelectionEvent event) {
					tblItemCopy(tblResult.getSelection());
				}
			});
			itemCopy.setAccelerator(SWT.CTRL + 'c');

			itemDelete.addSelectionListener(new SelectionAdapter() {
				public void widgetSelected(SelectionEvent event) {
					deleteRecord(tblResult.getSelection());
				}
			});
			itemExportAll.addSelectionListener(new SelectionAdapter() {
				public void widgetSelected(SelectionEvent event) {
					new Export(allColumnList, allDataList, doesGetOidInfo);
				}
			});
			itemExportSelection.addSelectionListener(new SelectionAdapter() {
				public void widgetSelected(SelectionEvent event) {
					new Export(tblResult, true, doesGetOidInfo);
				}
			});

			final TableEditor editor = new TableEditor(tblResult);
			editor.horizontalAlignment = SWT.LEFT;
			editor.grabHorizontal = true;
			if (doesGetOidInfo) {
				tblResult.addListener(SWT.MouseUp, new Listener() {
					public void handleEvent(Event event) {
						Rectangle clientArea = tblResult.getClientArea();
						Point pt = new Point(event.x, event.y);
						int index = tblResult.getTopIndex();
						curIndex = newIndex;
						newIndex = tblResult.getSelectionIndex();
						if (curIndex < 0 || curIndex != newIndex)
							return;
						while (index < tblResult.getItemCount()) {
							boolean visible = false;
							final TableItem item = tblResult.getItem(index);
							for (int i = 2; i < tblResult.getColumnCount(); i++) {
								Rectangle rect = item.getBounds(i);
								if (rect.contains(pt)) {
									final int row = index;
									final int column = i;
									final Text text = new Text(tblResult,
											SWT.MULTI);
									if (qe.font != null)
										text.setFont(qe.font);

									String type = tblResult.getColumn(i)
											.getData().toString();
									if (type.equals("CLASS")
											|| type.equals("SET")
											|| type.equals("MULTISET")
											|| type.equals("SEQUENCE")
											|| item.getText(1).equals("NONE"))
										text.setEditable(false);
									else
										text.setEditable(true);
									Listener textListener = new Listener() {
										public void handleEvent(final Event e) {
											String old = item.getText(column);
											int oldRow = row;
											int oldCol = column;
											switch (e.type) {
											case SWT.FocusOut:
												if (!text.getText().equals(
														item.getText(column))) {
													try {
														item.setText(column,
																text.getText());
														updateValue(row, column);
													} catch (SQLException e1) {
														CommonTool
																.ErrorBox(e1
																		.getErrorCode()
																		+ MainConstants.NEW_LINE
																		+ e1
																				.getMessage());
														tblResult.getItem(
																oldRow)
																.setText(
																		oldCol,
																		old);
														e.doit = false;
													}
												}
												text.dispose();
												break;
											case SWT.Traverse:
												switch (e.detail) {
												case SWT.TRAVERSE_RETURN:
													if (!text
															.getText()
															.equals(
																	item
																			.getText(column))) {
														try {
															item
																	.setText(
																			column,
																			text
																					.getText());
															updateValue(row,
																	column);
														} catch (SQLException e1) {
															CommonTool
																	.ErrorBox(e1
																			.getErrorCode()
																			+ MainConstants.NEW_LINE
																			+ e1
																					.getMessage());
															tblResult
																	.getItem(
																			oldRow)
																	.setText(
																			oldCol,
																			old);
															e.doit = false;
														}
													}
												case SWT.TRAVERSE_ESCAPE:
													text.dispose();
													e.doit = false;
												}
												break;
											}
										}
									};
									text
											.addListener(SWT.FocusOut,
													textListener);
									text
											.addListener(SWT.Traverse,
													textListener);
									editor.setEditor(text, item, i);
									// if
									// (item.getText(i).equals(QueryEditor.STR_NULL))
									// text.setText("");
									// else
									text.setText(item.getText(i));
									text.selectAll();
									text.setFocus();
									return;
								}
								if (!visible && rect.intersects(clientArea)) {
									visible = true;
								}
							}
							if (!visible)
								return;
							index++;
						}
					}
				});
			}
			tblResult.addListener(SWT.MouseDoubleClick, new Listener() {
				public void handleEvent(Event event) {
					Rectangle clientArea = tblResult.getClientArea();
					Point pt = new Point(event.x, event.y);
					int index = tblResult.getTopIndex();
					while (index < tblResult.getItemCount()) {
						boolean visible = false;
						final TableItem item = tblResult.getItem(index);
						int i = 1;
						if (doesGetOidInfo) {
							i++;
						}
						for (; i < tblResult.getColumnCount(); i++) {
							Rectangle rect = item.getBounds(i);
							if (rect.contains(pt)) {
								final int column = i;
								String type = tblResult.getColumn(i).getData()
										.toString();
								if (type.equals("CLASS")
										&& !item.getText(column).equals("NULL")) {
									OIDNavigator oidnavi = new OIDNavigator(
											Application.mainwindow.getShell(),
											qe.getConnection());
									oidnavi.doModal(item.getText(column));
								}
								return;
							}
							if (!visible && rect.intersects(clientArea)) {
								visible = true;
							}
						}
						index++;
					}
				}
			});
			if (doesGetOidInfo) {
				tblResult
						.addKeyListener(new org.eclipse.swt.events.KeyAdapter() {
							public void keyReleased(KeyEvent e) {
								if (e.keyCode == SWT.DEL) {
									deleteRecord(tblResult.getSelection());
								}
							}

							public void keyPressed(
									org.eclipse.swt.events.KeyEvent e) {
								// Doesn't work.
								if (e.keyCode == 'c'
										&& (e.stateMask & SWT.CTRL) != 0) {
									// if (e.keyCode == ('C' + SWT.CTRL)) {
									// System.out.println("Copy!!!!");
								}
							}
						});
			}
		}
		makeColumn();
		makeItem();
		sqlText.setText(query);
		sqlText.setTopIndex(sqlText.getLineCount() - 1);
		messageText.setText(getQueryMsg());
		messageText.setTopIndex(messageText.getLineCount() - 1);
		tblResult.pack();
	}

	/**
	 * make query editor result table column
	 * 
	 */
	public void makeColumn() {
		TableColumn tblColumn[];
		tblColumn = new TableColumn[allColumnList.size() + 1];
		tblColumn[0] = new TableColumn(tblResult, SWT.NONE);
		tblColumn[0].setText("NO");
		tblColumn[0].setWidth(40);
		for (int j = 0; j < allColumnList.size(); j++) {
			tblColumn[j + 1] = new TableColumn(tblResult, SWT.NONE);
			String name = ((ColumnInfo) allColumnList.get(j)).getName();
			String type = ((ColumnInfo) allColumnList.get(j)).getType();
			tblColumn[j + 1].setText(name);
			tblColumn[j + 1].setToolTipText(type);
			tblColumn[j + 1].setData(type);
			tblColumn[j + 1].pack();
			ColumnComparator comparator = new ColumnComparator(name, type, true);
			colComparatorMap.put(name, comparator);
			tblColumn[j + 1].addSelectionListener(new SelectionListener() {
				public void widgetSelected(SelectionEvent e) {
					TableColumn column = (TableColumn) e.widget;
					if (column == null || column.getText() == null
							|| column.getText().trim().length() == 0) {
						return;
					}
					ColumnComparator comparator = colComparatorMap.get(column
							.getText());
					tblResult.setSortColumn(column);
					tblResult.setSortDirection(comparator.isAsc() ? SWT.UP
							: SWT.DOWN);
					Collections.sort(allDataList, comparator);
					comparator.setAsc(!comparator.isAsc());
					makeItem();
					updateActions();
				}

				public void widgetDefaultSelected(SelectionEvent e) {
				}
			});
		}
	}

	/**
	 * make query editor result table
	 * 
	 * @param start
	 * @param end
	 * 
	 */
	public void makeTable(int start) throws SQLException {
		int end = start + recordLimit - 1;
		String sql = this.multiQuerySql.replace("${start}",
				String.valueOf(start)).replace("${end}", String.valueOf(end));
		Connection conn = qe.getConnection();
		long beginTimestamp = 0;
		long endTimestamp = 0;
		double elapsedTime = 0.0;
		NumberFormat nf = NumberFormat.getInstance();
		nf.setMaximumFractionDigits(3);
		CUBRIDPreparedStatement stmt = null;
		CUBRIDResultSet rs = null;
		boolean isHasError = false;
		try {
			beginTimestamp = System.currentTimeMillis();
			stmt = (CUBRIDPreparedStatement) conn
					.prepareStatement(
							sql,
							ResultSet.TYPE_FORWARD_ONLY,
							(MainRegistry.queryEditorOption.oidinfo) ? ResultSet.CONCUR_UPDATABLE
									: ResultSet.CONCUR_READ_ONLY,
							ResultSet.HOLD_CURSORS_OVER_COMMIT);
			stmt.setQueryInfo(false);
			stmt.setOnlyQueryPlan(false);
			try {
				stmt.executeQuery();
				endTimestamp = System.currentTimeMillis();
				rs = (CUBRIDResultSet) stmt.getResultSet();
			} catch (SQLException ee) {
				throw ee;
			}
			elapsedTime = (endTimestamp - beginTimestamp) * 0.001;
			String elapsedTimeStr = nf.format(elapsedTime);
			String[] timeArr = elapsedTimeStr.split("\\.");
			if (timeArr != null && timeArr.length > 0) {
				if (timeArr[0].length() == 1)
					elapsedTimeStr = " " + timeArr[0];
				if (timeArr.length == 1) {
					elapsedTimeStr += ".000";
				} else {
					elapsedTimeStr += "." + timeArr[1];
					int i = timeArr[1].length();
					while (i < 3) {
						elapsedTimeStr += "0";
						i++;
					}
				}
			}
			if (start == 1) {
				makeTable(rs);
			} else {
				fillTableItemData(rs);
			}
			queryMsg += "[ " + elapsedTimeStr + " "
					+ Messages.getString("QEDIT.SECOND") + " , "
					+ Messages.getString("MSG.TOTALROWS") + " : " + cntRecord
					+ " ]" + MainConstants.NEW_LINE;
			query += sql + MainConstants.NEW_LINE;
		} catch (SQLException e) {
			queryMsg += Messages.getString("QEDIT.RUNERR") + e.getErrorCode()
					+ MainConstants.NEW_LINE
					+ Messages.getString("QEDIT.ERRORHEAD") + e.getMessage()
					+ MainConstants.NEW_LINE;
			CommonTool.debugPrint(e);
			query += sql + MainConstants.NEW_LINE;
			isHasError = true;
			throw e;
		} finally {
			queryInfo = new QueryInfo(allDataList.size(), pageLimit);
			queryInfo.setCurrentPage(1);
			QueryUtil.freeQuery(stmt, rs);
			if (!isHasError && cntRecord == recordLimit && recordLimit > 0) {
				isEnd = false;
				final String msg = Messages.getString("QEDIT.TOOMANYRECORD1")
						+ " " + end + " "
						+ Messages.getString("QEDIT.TOOMANYRECORD2");
				Application.mainwindow.getShell().getDisplay().syncExec(
						new Runnable() {
							public void run() {
								MessageBox mb = new MessageBox(
										Application.mainwindow.getShell(),
										SWT.ICON_WARNING | SWT.YES | SWT.NO);
								mb.setText(Messages.getString("QEDIT.WARNING"));
								mb.setMessage(msg);
								int state = mb.open();
								if (state == SWT.NO) {
									isEnd = true;
								}
							}
						});
				if (!isEnd) {
					makeTable(end + 1);
				}
			}
		}
	}

	/**
	 * make table item by the data in allDataList
	 * 
	 */
	public void makeItem() {
		tblResult.removeAll();
		int begin = (queryInfo.getCurrentPage() - 1) * queryInfo.getPageSize();
		int last = begin + queryInfo.getPageSize();

		int index = (queryInfo.getCurrentPage() - 1) * queryInfo.getPageSize()
				+ 1;
		for (int i = begin; i < last && i < queryInfo.getTotalRs(); i++) {
			TableItem item = new TableItem(tblResult, SWT.MULTI);
			Map<String, String> map = (Map<String, String>) allDataList.get(i);
			item.setText(0, String.valueOf(index + i - begin));
			for (int j = 0; j < allColumnList.size(); j++) {
				String columnName = allColumnList.get(j).getName();
				String data = (String) map.get(columnName);
				if (qe.font != null)
					item.setForeground(qe.getColorManager().getColor(
							new RGB(qe.fontColorRed, qe.fontColorGreen,
									qe.fontColorBlue)));
				if (data != null) {
					item.setText(j + 1, data);
				}
			}
			item.setBackground(0, Display.getCurrent().getSystemColor(
					SWT.COLOR_GRAY));
			if (doesGetOidInfo) {
				item.setBackground(1, Display.getCurrent().getSystemColor(
						SWT.COLOR_GRAY));
			}
		}
		for (int i = 0; i < tblResult.getColumnCount(); i++) {
			tblResult.getColumns()[i].pack();
			if (tblResult.getColumns()[i].getWidth() > 300)
				tblResult.getColumns()[i].setWidth(300);
		}
	}

	private void updateValue(int index, int column) throws SQLException {
		String oid = tblResult.getItem(index).getText(1);
		String value = tblResult.getItem(index).getText(column);
		String colName = tblResult.getColumn(column).getText();

		qe.updateResult(oid, colName, value);
		for (int i = 0; i < allDataList.size(); i++) {
			Map<String, String> updatedRecordMap = allDataList.get(i);
			if (updatedRecordMap.get("OID").equals(oid)) {
				updatedRecordMap.put(colName, value);
				break;
			}
		}
	}

	protected void deleteRecord(TableItem[] selection) {
		MessageBox mb = new MessageBox(Application.mainwindow.getShell(),
				SWT.ICON_QUESTION | SWT.YES | SWT.NO);
		mb.setText(Messages.getString("QEDIT.DELETE"));
		mb.setMessage(tblResult.getSelectionCount() + " "
				+ Messages.getString("QEDIT.CONFIRMDELMSG"));
		int state = mb.open();
		if (state == SWT.YES) {
			String[] oid = new String[selection.length];
			for (int i = 0; i < selection.length; i++) {
				oid[i] = selection[i].getText(1);
				selection[i].dispose();
			}

			try {
				qe.deleteResult(oid);
				for (int i = 0; i < oid.length; i++) {
					for (int j = 0; j < allDataList.size(); j++) {
						Map<String, String> deletedRecordMap = allDataList
								.get(j);
						if (deletedRecordMap.get("OID").equals(oid[i])) {
							allDataList.remove(deletedRecordMap);
							queryInfo.setTotalRs(queryInfo.getTotalRs() - 1);
							break;
						}
					}
				}
				makeItem();
				updateActions();
			} catch (SQLException e) {
				CommonTool.ErrorBox(e.getErrorCode() + MainConstants.NEW_LINE
						+ Messages.getString("QEDIT.ERRORHEAD")
						+ e.getMessage());
				CommonTool.debugPrint(e);
			}
		}
	}

	public static void tblItemCopy(TableItem[] selection) {
		String data = "";
		for (int i = 0; i < selection.length; i++) {
			for (int j = 1; j < selection[i].getParent().getColumnCount(); j++) {
				if (j > 1)
					data += "\t";
				data += selection[i].getText(j).replaceFirst(
						QueryEditor.STR_NULL, "");
			}
			data += "\n";
		}

		TextTransfer textTransfer = TextTransfer.getInstance();
		if (MainRegistry.cb == null)
			MainRegistry.cb = new Clipboard(Application.mainwindow.getShell()
					.getDisplay());
		MainRegistry.cb.setContents(new Object[] { data },
				new Transfer[] { textTransfer });
	}

	public QueryInfo getQueryInfo() {
		return queryInfo;
	}

	public void setQueryInfo(QueryInfo queryInfo) {
		this.queryInfo = queryInfo;
	}

	public String getQueryMsg() {
		return queryMsg;
	}

	public void setQueryMsg(String queryMsg) {
		this.queryMsg = queryMsg;
	}

	public String getQuerySql() {
		return query;
	}

	public String getMultiQuerySql() {
		return multiQuerySql;
	}

	public void setMultiQuerySql(String multiQuerySql) {
		this.multiQuerySql = multiQuerySql;
	}

}
