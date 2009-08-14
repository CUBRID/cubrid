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

package com.cubrid.cubridmanager.ui.query.control;

import java.sql.Connection;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.text.NumberFormat;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

import org.apache.log4j.Logger;
import org.eclipse.jface.action.Action;
import org.eclipse.jface.action.IAction;
import org.eclipse.jface.action.ToolBarManager;
import org.eclipse.jface.dialogs.MessageDialog;
import org.eclipse.swt.SWT;
import org.eclipse.swt.custom.StyledText;
import org.eclipse.swt.custom.TableEditor;
import org.eclipse.swt.dnd.Clipboard;
import org.eclipse.swt.dnd.TextTransfer;
import org.eclipse.swt.dnd.Transfer;
import org.eclipse.swt.events.KeyEvent;
import org.eclipse.swt.events.MenuEvent;
import org.eclipse.swt.events.MouseAdapter;
import org.eclipse.swt.events.MouseEvent;
import org.eclipse.swt.events.SelectionAdapter;
import org.eclipse.swt.events.SelectionEvent;
import org.eclipse.swt.events.SelectionListener;
import org.eclipse.swt.graphics.Color;
import org.eclipse.swt.graphics.Font;
import org.eclipse.swt.graphics.Point;
import org.eclipse.swt.graphics.RGB;
import org.eclipse.swt.graphics.Rectangle;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Control;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.Event;
import org.eclipse.swt.widgets.Listener;
import org.eclipse.swt.widgets.Menu;
import org.eclipse.swt.widgets.MenuItem;
import org.eclipse.swt.widgets.MessageBox;
import org.eclipse.swt.widgets.Table;
import org.eclipse.swt.widgets.TableColumn;
import org.eclipse.swt.widgets.TableItem;

import com.cubrid.cubridmanager.core.common.log.LogUtil;
import com.cubrid.cubridmanager.core.common.model.ServerInfo;
import com.cubrid.cubridmanager.core.query.QueryOptions;
import com.cubrid.cubridmanager.ui.common.dialog.OIDNavigatorDialog;
import com.cubrid.cubridmanager.ui.query.Messages;
import com.cubrid.cubridmanager.ui.query.action.FirstAction;
import com.cubrid.cubridmanager.ui.query.action.LastAction;
import com.cubrid.cubridmanager.ui.query.action.NextAction;
import com.cubrid.cubridmanager.ui.query.action.PasteAction;
import com.cubrid.cubridmanager.ui.query.action.PreviousAction;
import com.cubrid.cubridmanager.ui.query.dialog.RowDetailDialog;
import com.cubrid.cubridmanager.ui.query.editor.QueryEditorPart;
import com.cubrid.cubridmanager.ui.query.editor.QueryUtil;
import com.cubrid.cubridmanager.ui.spi.ActionManager;
import com.cubrid.cubridmanager.ui.spi.CommonTool;
import com.cubrid.cubridmanager.ui.spi.SWTResourceManager;
import com.cubrid.cubridmanager.ui.spi.model.CubridDatabase;
import com.cubrid.cubridmanager.ui.spi.progress.ExecTaskWithProgress;

import cubrid.jdbc.driver.CUBRIDPreparedStatement;
import cubrid.jdbc.driver.CUBRIDResultSet;
import cubrid.jdbc.driver.CUBRIDResultSetMetaData;
import cubrid.sql.CUBRIDOID;

/**
 * execute sql script , and return the result table view
 * 
 * @author wangsl 2009-6-4
 */
public class QueryExecuter {

	private static final Logger logger = LogUtil.getLogger(QueryExecuter.class);

	private int recordLimit;
	public String query = "";
	public int idx;
	public int cntRecord = 0;
	private boolean doesGetOidInfo;
	public Table tblResult = null;
	private QueryEditorPart qe;
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
	private boolean dontTipNext = false;
	public int pageLimit; // each page size
	private String queryMsg;
	private String multiQuerySql = null;
	private Map<String, ColumnComparator> colComparatorMap = null;
	private CubridDatabase database;

	private CUBRIDPreparedStatement stmt;

	private CUBRIDResultSet rs;
	private String selColumnName;
	private QueryExecuter executer = this;
	private final static String dataNullValueFlag = "NULL";
	// this variable to save the mouse right click position
	protected Point button2Position;

	public static Color FONT_COLOR = SWTResourceManager.getColor(new RGB(
			QueryOptions.fontColorRed, QueryOptions.fontColorGreen,
			QueryOptions.fontColorBlue));;

	/**
	 * 
	 * @param qe query editor
	 * @param idx the index of sql in query editor
	 * @param query sql script
	 * @param cubridDatabase selected database
	 */
	public QueryExecuter(QueryEditorPart qe, int idx, String query,
			CubridDatabase cubridDatabase) {
		if (cubridDatabase == null)
			throw new IllegalArgumentException(Messages.errMsgServerNull);
		this.database = cubridDatabase;
		this.qe = qe;
		this.idx = idx;
		this.query = query;
		ServerInfo serverInfo = cubridDatabase.getServer() != null ? cubridDatabase.getServer().getServerInfo()
				: null;
		pageLimit = QueryOptions.getPageLimit(serverInfo);
		recordLimit = QueryOptions.getSearchLimit(serverInfo);
		doesGetOidInfo = QueryOptions.getOidInfo(serverInfo);
		allDataList = new ArrayList<Map<String, String>>();
		allColumnList = new ArrayList<ColumnInfo>();
		colComparatorMap = new HashMap<String, ColumnComparator>();
		boolean enableSearchUnit = QueryOptions.getEnableSearchUnit(serverInfo);
		if (!enableSearchUnit) {
			dontTipNext = true;
		}
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
		CUBRIDResultSetMetaData rsmt = (CUBRIDResultSetMetaData) rs.getMetaData();
		int cntColumn = rsmt.getColumnCount();
		if (doesGetOidInfo && allColumnList != null) {
			allColumnList.add(0, new ColumnInfo("0", "OID", "OID"));
		}
		for (int i = 1; i <= cntColumn && allColumnList != null; i++) {
			allColumnList.add(new ColumnInfo(i + "", rsmt.getColumnName(i),
					rsmt.getColumnTypeName(i)));
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
			int columnPos = 0, columnCount = allColumnList != null ? allColumnList.size()
					: 0;
			if (doesGetOidInfo) {
				CUBRIDOID oid = ((CUBRIDResultSet) rs).getOID();
				if (oid != null && oid.getOidString() != null)
					map.put("0", oid.getOidString());
				else {
					map.put("0", "NONE");
				}
				columnPos++;
				columnCount--;
			}
			for (int j = 1; j <= columnCount && allColumnList != null; j++) {
				String data = null;
				String columnType = ((ColumnInfo) allColumnList.get(columnPos)).getType();
				String index = ((ColumnInfo) allColumnList.get(columnPos)).getIndex();
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
					} else if (columnType.equalsIgnoreCase("DATETIME")) {
						data = CommonTool.formatDate(rs.getTimestamp(j), "yyyy-MM-dd HH:mm:ss.SSS");
					} 
					else if (columnType.equalsIgnoreCase("BIT VARYING")
							|| columnType.equalsIgnoreCase("BIT")) {

						final short aByteSize = 256;
						byte[] dataTmp = (byte[]) rs.getObject(j);

						int temp = 0;
						StringBuffer strBuf = new StringBuffer();
						if (dataTmp != null) {
							for (int i = 0; i < dataTmp.length; i++) {
								if (dataTmp[i] < 0)
									temp = (short) dataTmp[i] + aByteSize;
								else
									temp = (short) dataTmp[i];
								String res = Integer.toHexString(temp);
								strBuf.append(res.length() == 1 ? ("0" + res) : res);
							}
							data = strBuf.toString();
						}
					} 
					else {
						data = rs.getString(j);
					}
				}
				map.put(index, data);
				columnPos++;
			}
			if (allDataList != null)
				allDataList.add(map);
			if (recordLimit > 0 && cntRecord >= limit && multiQuerySql == null) {
				final String msg = Messages.bind(Messages.tooManyRecord, limit);
				showQueryTip(msg);
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

	private void disableActions() {
		if (firstPageAction != null) {
			firstPageAction.setEnabled(false);
		}
		if (lastPageAction != null) {
			lastPageAction.setEnabled(false);
		}
		if (previousPageAction != null) {
			previousPageAction.setEnabled(false);
		}
		if (nextPageAction != null) {
			nextPageAction.setEnabled(false);
		}
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
	 * @param tblResult, the Table.
	 * @param sqlText, the Text.
	 * @param messageText, the StyledText.
	 * 
	 */
	public void makeResult(final Table tblResult, StyledText sqlText,
			StyledText messageText) {
		this.tblResult = tblResult;
		String[] fontData = QueryOptions.getFont(database.getServer() != null ? database.getServer().getServerInfo()
				: null);
		Font font = SWTResourceManager.getFont(fontData[0],
				Integer.valueOf(fontData[1]), Integer.valueOf(fontData[2]));
		this.tblResult.setFont(font);

		if (qe != null) {
			Menu menu = new Menu(qe.getEditorSite().getShell(), SWT.POP_UP);
			final MenuItem itemCopy = new MenuItem(menu, SWT.PUSH);
			itemCopy.setText(Messages.copyClipBoard);

			final MenuItem itemDelete = new MenuItem(menu, SWT.PUSH);
			itemDelete.setText(Messages.delete);

			new MenuItem(menu, SWT.SEPARATOR);

			final MenuItem itemOid = new MenuItem(menu, SWT.PUSH);
			itemOid.setText(Messages.oidNavigator);
			if (!containsOIDs()) {
				itemOid.setEnabled(false);
			}
			MenuItem itemDetail = new MenuItem(menu, SWT.PUSH);
			itemDetail.setText(Messages.detailView);

			itemDetail.addSelectionListener(new SelectionAdapter() {

				@Override
				public void widgetSelected(SelectionEvent e) {
					TableItem[] item = tblResult.getSelection();
					RowDetailDialog dialog = new RowDetailDialog(
							tblResult.getShell(), allColumnList, item[0],
							selColumnName, executer);
					dialog.open();
				}

			});

			tblResult.setMenu(menu);
			tblResult.addListener(SWT.MouseUp, new Listener() {

				public void handleEvent(Event event) {
					Point pt = new Point(event.x, event.y);
					itemOid.setEnabled(false);
					final TableItem item = tblResult.getItem(tblResult.getSelectionIndex());
					for (int i = 0; i < tblResult.getColumnCount(); i++) {
						Rectangle rect = item.getBounds(i);
						if (rect.contains(pt)) {
							selColumnName = tblResult.getColumn(i).getText();
							Object value = item.getData(i + "");
							if (!dataNullValueFlag.equals(value)
									&& containsOIDs(tblResult.getColumn(i).getText())) {
								itemOid.setEnabled(true);
							} else {
								itemOid.setEnabled(false);
							}
						}
					}
				}

			});
			final MenuItem itemExportAll = new MenuItem(menu, SWT.PUSH);
			itemExportAll.setText(Messages.allExport);
			final MenuItem itemExportSelection = new MenuItem(menu, SWT.PUSH);
			itemExportSelection.setText(Messages.selectExport);
			if (isEmpty()) {
				itemExportAll.setEnabled(false);
				itemExportSelection.setEnabled(false);
				itemDetail.setEnabled(false);
			}

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
					ExportTask task = new ExportTask(allColumnList,
							allDataList, doesGetOidInfo, database);
					ExecTaskWithProgress progress = new ExecTaskWithProgress(
							task);
					if (task.isCancel()) {
						return;
					}
					progress.exec();
				}
			});

			itemOid.addSelectionListener(new SelectionAdapter() {

				@Override
				public void widgetSelected(SelectionEvent e) {
					openOidNavigator(button2Position);
				}

			});
			itemExportSelection.addSelectionListener(new SelectionAdapter() {
				public void widgetSelected(SelectionEvent event) {
					ExportTask task = new ExportTask(tblResult, true,
							doesGetOidInfo, database);
					ExecTaskWithProgress progress = new ExecTaskWithProgress(
							task);
					if (task.isCancel()) {
						return;
					}
					progress.exec();
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

							if (item.getText(1).equals("NONE")) {
								index++;
								continue;
							}
							for (int i = 2; i < tblResult.getColumnCount(); i++) {
								Rectangle rect = item.getBounds(i);
								if (rect.contains(pt)) {
									if (dataNullValueFlag.equals(item.getData(i
											+ ""))) {
										item.setText(i, "");
									}
									final int row = index;
									final int column = i;

									final StyledText text = new StyledText(
											tblResult, SWT.MULTI | SWT.WRAP);
									String[] fontData = QueryOptions.getFont(database.getServer() != null ? database.getServer().getServerInfo()
											: null);
									Font font = SWTResourceManager.getFont(
											fontData[0],
											Integer.valueOf(fontData[1]),
											Integer.valueOf(fontData[2]));
									if (font != null)
										text.setFont(font);
									ColumnInfo columnInfo = (ColumnInfo) tblResult.getColumn(
											i).getData();
									String type = columnInfo.getType();
									if (type.equals("CLASS")
											|| type.equals("SET")
											|| type.equals("MULTISET")
											|| type.equals("SEQUENCE")
											|| item.getText(1).equals("NONE"))
										text.setEditable(false);
									else
										text.setEditable(true);
									Listener textListener = new Listener() {
										private boolean isRunning = false;

										public void handleEvent(final Event e) {
											String old = item.getText(column);
											int oldRow = row;
											int oldCol = column;
											switch (e.type) {
											case SWT.FocusOut:
												if (isRunning) {
													return;
												}
												if (!text.getText().equals(
														item.getText(column))) {
													if (CommonTool.openConfirmBox(Messages.cfmUpdateChangedValue)) {
														try {
															item.setText(
																	column,
																	text.getText());

															updateValue(row,
																	column);
															if (dataNullValueFlag.equals(item.getData(oldCol
																	+ ""))) {
																item.setData(
																		oldCol
																				+ "",
																		"");
																item.setBackground(
																		oldCol,
																		null);
															}
														} catch (SQLException e1) {
															CommonTool.openErrorBox(e1.getErrorCode()
																	+ CommonTool.getLineSeparator()
																	+ e1.getMessage());
															tblResult.getItem(
																	oldRow).setText(
																	oldCol, old);
															e.doit = false;
															if (dataNullValueFlag.equals(item.getData(oldCol
																	+ "")))
																item.setText(
																		oldCol,
																		Messages.msgQueryResultNullFlag);
														}
													}
												} else if ("".equals(text.getText())) {
													if (dataNullValueFlag.equals(item.getData(oldCol
															+ ""))) {
														item.setText(
																oldCol,
																Messages.msgQueryResultNullFlag);
													} else {
														item.setText(column, "");
													}
												}
												text.dispose();
												break;
											case SWT.Traverse:
												switch (e.detail) {
												case SWT.TRAVERSE_RETURN:
													isRunning = true;
													if (!text.getText().equals(
															item.getText(column))) {
														if (CommonTool.openConfirmBox(Messages.cfmUpdateChangedValue)) {
															try {
																item.setText(
																		column,
																		text.getText());
																updateValue(
																		row,
																		column);
															} catch (SQLException e1) {
																CommonTool.openErrorBox(e1.getErrorCode()
																		+ CommonTool.getLineSeparator()
																		+ e1.getMessage());
																tblResult.getItem(
																		oldRow).setText(
																		oldCol,
																		old);
																e.doit = false;
															}
														}
													}
												case SWT.TRAVERSE_ESCAPE:
													isRunning = true;
													text.dispose();
													e.doit = false;
													isRunning = false;
												}
												break;
											}
										}
									};
									text.addListener(SWT.FocusOut, textListener);
									text.addListener(SWT.Traverse, textListener);
									editor.setEditor(text, item, i);
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
					Point pt = new Point(event.x, event.y);
					openOidNavigator(pt);
				}
			});
			tblResult.addMouseListener(new MouseAdapter() {

				@Override
				public void mouseDown(MouseEvent e) {
					if (e.button == 3) {
						button2Position = new Point(e.x, e.y);
					}
				}

			});
			if (doesGetOidInfo) {
				tblResult.addKeyListener(new org.eclipse.swt.events.KeyAdapter() {
					public void keyReleased(KeyEvent e) {
						if (e.keyCode == SWT.DEL) {
							deleteRecord(tblResult.getSelection());
						}
					}

					public void keyPressed(org.eclipse.swt.events.KeyEvent e) {
						// Doesn't work.
						if (e.keyCode == 'c' && (e.stateMask & SWT.CTRL) != 0) {
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

	private void openOidNavigator(Point pt) {
		String oid = getContainedOid(pt);
		if (oid != null) {
			OIDNavigatorDialog dialog = new OIDNavigatorDialog(
					qe.getEditorSite().getShell(), qe.getConnection(), oid);
			dialog.create();
			if (!dialog.find()) {
				return;
			}
			dialog.open();
		}
	}

	/**
	 * 
	 * check out whether there is a oid data at the point on the table, if then
	 * ,return it or return null;
	 * 
	 * @param pt
	 * @return
	 */
	private String getContainedOid(Point pt) {
		Rectangle clientArea = tblResult.getClientArea();
		boolean visible = false;
		Table table = tblResult;
		if (table.getSelectionCount() == 1) {
			TableItem[] items = table.getSelection();
			for (int i = 0; i < tblResult.getColumnCount(); i++) {
				Rectangle rect = items[0].getBounds(i);
				if (rect.contains(pt)) {
					final int column = i;
					ColumnInfo columnInfo = (ColumnInfo) tblResult.getColumn(i).getData();
					String type = columnInfo.getType();
					if ((type.equals("OID") || type.equals("CLASS"))
							&& !dataNullValueFlag.equals(items[0].getData(column
									+ ""))) {
						return items[0].getText(column);
					}
				}
				if (!visible && rect.intersects(clientArea)) {
					visible = true;
				}
			}
		}
		return null;
	}

	/**
	 * make query editor result table column
	 * 
	 */
	public void makeColumn() {
		TableColumn tblColumn[];
		tblColumn = new TableColumn[(allColumnList != null ? allColumnList.size()
				: 0) + 1];
		tblColumn[0] = new TableColumn(tblResult, SWT.NONE);
		tblColumn[0].setText("NO");
		tblColumn[0].setWidth(40);
		for (int j = 0; allColumnList != null && j < allColumnList.size(); j++) {
			tblColumn[j + 1] = new TableColumn(tblResult, SWT.NONE);
			ColumnInfo columnInfo = (ColumnInfo) allColumnList.get(j);
			String name = columnInfo.getName();
			String type = columnInfo.getType();
			tblColumn[j + 1].setText(name);
			tblColumn[j + 1].setToolTipText(type);
			tblColumn[j + 1].setData(columnInfo);
			tblColumn[j + 1].pack();
			ColumnComparator comparator = new ColumnComparator(
					columnInfo.getIndex(), type, true);
			if (colComparatorMap != null)
				colComparatorMap.put(columnInfo.getIndex(), comparator);
			tblColumn[j + 1].addSelectionListener(new SelectionListener() {
				@SuppressWarnings("unchecked")
				public void widgetSelected(SelectionEvent e) {
					TableColumn column = (TableColumn) e.widget;
					if (column == null || column.getText() == null
							|| column.getText().trim().length() == 0) {
						return;
					}
					ColumnInfo columnInfo = (ColumnInfo) column.getData();
					ColumnComparator comparator = colComparatorMap.get(columnInfo.getIndex());
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
		String sql = multiQuerySql;
		if (multiQuerySql.indexOf(SqlParser.ROWNUM_CONDITION_MARK) != -1) {
			if (dontTipNext) {
				if (recordLimit > 0) {
					sql = this.multiQuerySql.replace(
							SqlParser.ROWNUM_CONDITION_MARK,
							"\r\nwhere rownum  >= " + String.valueOf(start));
				}
			} else {
				sql = this.multiQuerySql.replace(
						SqlParser.ROWNUM_CONDITION_MARK,
						"\r\nwhere rownum between " + String.valueOf(start)
								+ " and " + String.valueOf(end));
			}
		}
		Connection conn = qe.getConnection();
		long beginTimestamp = 0;
		long endTimestamp = 0;
		double elapsedTime = 0.0;
		NumberFormat nf = NumberFormat.getInstance();
		nf.setMaximumFractionDigits(3);
		stmt = null;
		rs = null;
		boolean isHasError = false;
		try {
			beginTimestamp = System.currentTimeMillis();
			String fileterSql = SqlParser.convertComment(sql);
			stmt = (CUBRIDPreparedStatement) conn.prepareStatement(fileterSql,
					ResultSet.TYPE_SCROLL_SENSITIVE,
					doesGetOidInfo ? ResultSet.CONCUR_UPDATABLE
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
			queryMsg += "[ " + elapsedTimeStr + " " + Messages.second + " , "
					+ Messages.totalRows + " : " + cntRecord + " ]"
					+ CommonTool.getLineSeparator();
			query += sql + CommonTool.getLineSeparator();
		} catch (SQLException e) {
			queryMsg += Messages.runError + e.getErrorCode()
					+ CommonTool.getLineSeparator() + Messages.errorHead
					+ e.getMessage() + CommonTool.getLineSeparator();
			query += sql + CommonTool.getLineSeparator();
			isHasError = true;
			throw e;
		} finally {
			queryInfo = new QueryInfo(allDataList != null ? allDataList.size()
					: 0, pageLimit);
			queryInfo.setCurrentPage(1);
			QueryUtil.freeQuery(stmt, rs);
			if (!isHasError && cntRecord == recordLimit && recordLimit > 0) {
				isEnd = false;
				final String msg = Messages.bind(Messages.tooManyRecord, end);
				showQueryTip(msg);
				if (!isEnd) {
					makeTable(end + 1);
				}
			}
		}
	}

	private void showQueryTip(final String msg) {
		Display.getDefault().syncExec(new Runnable() {
			public void run() {
				if (!dontTipNext) {

					MessageDialog dialog = new MessageDialog(
							qe.getEditorSite().getShell(), Messages.warning,
							null, msg, MessageDialog.QUESTION, new String[] {
									Messages.btnYes, Messages.btnNo }, 1) {
						Button btn = null;

						@Override
						protected Control createCustomArea(Composite parent) {
							btn = new Button(parent, SWT.CHECK);
							btn.setText(Messages.showOneTimeTip);
							return btn;
						}

						@Override
						protected void buttonPressed(int buttonId) {
							dontTipNext = btn.getSelection();
							if (buttonId == 1) {
								isEnd = true;
							}
							close();
						}

					};
					dialog.open();
				}
			}
		});
	}

	/**
	 * make table item by the data in allDataList
	 * 
	 */
	public void makeItem() {
		disableActions();
		tblResult.removeAll();
		int begin = (queryInfo.getCurrentPage() - 1) * queryInfo.getPageSize();
		int last = begin + queryInfo.getPageSize();

		int index = (queryInfo.getCurrentPage() - 1) * queryInfo.getPageSize()
				+ 1;
		String[] fontData = QueryOptions.getFont(database.getServer() != null ? database.getServer().getServerInfo()
				: null);
		Font font = SWTResourceManager.getFont(fontData[0],
				Integer.valueOf(fontData[1]), Integer.valueOf(fontData[2]));
		for (int i = begin; allDataList != null && i < last
				&& i < queryInfo.getTotalRs(); i++) {
			TableItem item = new TableItem(tblResult, SWT.MULTI);

			Map<String, String> map = (Map<String, String>) allDataList.get(i);
			item.setText(0, String.valueOf(index + i - begin));
			for (int j = 0; allColumnList != null && j < allColumnList.size(); j++) {
				String columnIndex = allColumnList.get(j).getIndex();
				String data = (String) map.get(columnIndex);
				if (font != null) {
					item.setForeground(FONT_COLOR);
				}
				if (data != null) {
					item.setText(j + 1, data);
				} else {
					item.setText(j + 1, Messages.msgQueryResultNullFlag);
					item.setBackground(j + 1,
							Display.getCurrent().getSystemColor(SWT.COLOR_GRAY));
					item.setData((j + 1) + "", dataNullValueFlag);
				}
			}
			if (i % 2 == 0)
				item.setBackground(SWTResourceManager.getColor(230, 230, 230));

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

	public void updateValue(int index, int column) throws SQLException {
		String oid = tblResult.getItem(index).getText(1);
		String value = tblResult.getItem(index).getText(column);
		String colName = tblResult.getColumn(column).getText();

		updateValue(oid, new String[] { value }, new String[] { colName });

	}

	/**
	 * update row data
	 * 
	 * @param oid
	 * @param value
	 * @param colName
	 * @throws SQLException
	 */
	public void updateValue(String oid, String[] value, String[] colName) throws SQLException {
		qe.updateResult(oid, colName, value);
		for (int i = 0; allDataList != null && i < allDataList.size(); i++) {
			Map<String, String> updatedRecordMap = allDataList.get(i);
			String o = updatedRecordMap.get("OID");
			if (o != null && o.equals(oid)) {
				for (int j = 0; j < colName.length; j++) {
					updatedRecordMap.put(colName[j], value[j]);
				}
				break;
			}
		}
	}

	protected void deleteRecord(TableItem[] selection) {
		MessageBox mb = new MessageBox(qe.getEditorSite().getShell(),
				SWT.ICON_QUESTION | SWT.YES | SWT.NO);
		mb.setText(Messages.delete);
		mb.setMessage(Messages.bind(Messages.confirmDelMsg,
				tblResult.getSelectionCount()));
		int state = mb.open();
		if (state == SWT.YES) {
			String[] oid = new String[selection.length];
			for (int i = 0; i < selection.length; i++) {
				oid[i] = selection[i].getText(1);
				//selection[i].dispose();
			}

			try {
				qe.deleteResult(oid);
				for (int i = 0; i < oid.length; i++) {
					for (int j = 0; allDataList != null
							&& j < allDataList.size(); j++) {
						Map<String, String> deletedRecordMap = allDataList.get(j);
						if (deletedRecordMap.get("0").equals(oid[i])) {
							allDataList.remove(deletedRecordMap);
							queryInfo.setTotalRs(queryInfo.getTotalRs() - 1);
							break;
						}
					}
				}
				makeItem();
				updateActions();
			} catch (SQLException e) {
				CommonTool.openErrorBox(e.getErrorCode()
						+ CommonTool.getLineSeparator() + Messages.errorHead
						+ e.getMessage());
			}
		}
	}

	public static void tblItemCopy(TableItem[] selection) {
		String data = "";
		for (int i = 0; i < selection.length; i++) {
			for (int j = 1; j < selection[i].getParent().getColumnCount(); j++) {
				if (j > 1)
					data += "\t";
				if (dataNullValueFlag.equals(selection[i].getData(j + "")))
					data += "";
				else
					data += selection[i].getText(j);
			}
			data += "\n";
		}

		TextTransfer textTransfer = TextTransfer.getInstance();
		Clipboard clipboard = CommonTool.getClipboard();
		if (clipboard != null) {
			IAction pasteAction = ActionManager.getInstance().getAction(
					PasteAction.ID);
			pasteAction.setEnabled(true);
			clipboard.setContents(new Object[] { data },
					new Transfer[] { textTransfer });
		}

	}

	private boolean containsOIDs() {
		for (ColumnInfo column : allColumnList) {
			String type = column.getType();
			if ((type.equals("OID") || type.equals("CLASS"))) {
				return true;
			}
		}
		return false;
	}

	private boolean containsOIDs(String columnName) {
		if (allColumnList == null) {
			return false;
		}
		for (ColumnInfo column : allColumnList) {
			String type = column.getType();
			String name = column.getName();
			if (columnName.equals(name)
					&& (type.equals("OID") || type.equals("CLASS"))) {
				return true;
			}
		}
		return false;
	}

	private boolean isEmpty() {
		return allDataList != null ? allDataList.isEmpty() : false;
	}

	public void dispose() {
		try {
			if (stmt != null) {
				stmt.cancel();
			}
		} catch (SQLException e) {
			logger.error(e);
		}
		QueryUtil.freeQuery(stmt, rs);
		allColumnList = null;
		allDataList = null;
		colComparatorMap = null;
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

	public QueryEditorPart getQueryEditor() {
		return qe;
	}

}
