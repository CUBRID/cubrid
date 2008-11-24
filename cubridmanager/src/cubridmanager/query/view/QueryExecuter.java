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

import java.sql.SQLException;

import org.eclipse.swt.SWT;
import org.eclipse.swt.custom.TableEditor;
import org.eclipse.swt.dnd.Clipboard;
import org.eclipse.swt.dnd.TextTransfer;
import org.eclipse.swt.dnd.Transfer;
import org.eclipse.swt.events.KeyEvent;
import org.eclipse.swt.events.MenuEvent;
import org.eclipse.swt.events.SelectionAdapter;
import org.eclipse.swt.events.SelectionEvent;
import org.eclipse.swt.graphics.Font;
import org.eclipse.swt.graphics.Point;
import org.eclipse.swt.graphics.RGB;
import org.eclipse.swt.graphics.Rectangle;
import org.eclipse.swt.widgets.Event;
import org.eclipse.swt.widgets.Listener;
import org.eclipse.swt.widgets.Menu;
import org.eclipse.swt.widgets.MenuItem;
import org.eclipse.swt.widgets.MessageBox;
import org.eclipse.swt.widgets.Table;
import org.eclipse.swt.widgets.TableColumn;
import org.eclipse.swt.widgets.TableItem;
import org.eclipse.swt.widgets.Text;

import cubrid.sql.CUBRIDOID;

import cubrid.jdbc.driver.CUBRIDResultSet;
import cubrid.jdbc.driver.CUBRIDResultSetMetaData;
import cubrid.sql.CUBRIDOID;
import cubridmanager.Application;
import cubridmanager.CommonTool;
import cubridmanager.MainConstants;
import cubridmanager.MainRegistry;
import cubridmanager.Messages;
import cubridmanager.OnlyQueryEditor;
import cubridmanager.query.ColorManager;
import cubridmanager.query.dialog.Export;
import cubridmanager.query.dialog.OIDNavigator;

public class QueryExecuter {
	private final int recordLimit;
	public CUBRIDResultSet rs;
	public String query = new String();
	public int idx;
	public int cntRecord = 0;
	public boolean isNotEndOfTuple = false;
	private final boolean doesGetOidInfo;
	final static Clipboard cb = new Clipboard(Application.mainwindow.getShell()
			.getDisplay());
	private CUBRIDResultSetMetaData rsmt;
	private Table tblResult = null;
	private int cntColumn;
	private QueryEditor qe;
	private int curIndex = -1;
	private int newIndex = -1;

	public QueryExecuter(QueryEditor qe, int idx, String query,
			CUBRIDResultSet rs) {
		this.qe = qe;
		this.idx = idx;
		this.query = query;
		this.rs = rs;

		recordLimit = MainRegistry.queryEditorOption.recordlimit;
		doesGetOidInfo = MainRegistry.queryEditorOption.oidinfo;
	}

	public QueryExecuter(CUBRIDResultSet rs) {
		this(null, 0, null, rs);
	}

	public Table getTable() {
		return tblResult;
	}

	public void makeResult(final Table tblResult) {
		this.tblResult = tblResult;
		if (qe != null) {
			Menu menu = new Menu(Application.mainwindow.getShell(), SWT.POP_UP);
			MenuItem itemCopy = new MenuItem(menu, SWT.PUSH);
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
			MenuItem itemExportAll = new MenuItem(subItemExport, SWT.PUSH);
			itemExportAll.setText(Messages.getString("QEDIT.ALL"));
			MenuItem itemExportSelection = new MenuItem(subItemExport, SWT.PUSH);
			itemExportSelection.setText(Messages.getString("QEDIT.SELECTION"));

			menu.addMenuListener(new org.eclipse.swt.events.MenuAdapter() {
				public void menuShown(MenuEvent e) {
					TableItem[] tblItems = tblResult.getSelection();
					for (int i = 0; i < tblItems.length; i++) {
						if (tblItems[i].getText(0).equals("NONE"))
							itemDelete.setEnabled(false);
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
					new Export(tblResult, false);
				}
			});
			itemExportSelection.addSelectionListener(new SelectionAdapter() {
				public void widgetSelected(SelectionEvent event) {
					new Export(tblResult, true);
				}
			});

			final TableEditor editor = new TableEditor(tblResult);
			editor.horizontalAlignment = SWT.LEFT;
			editor.grabHorizontal = true;

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
						for (int i = 1; i < tblResult.getColumnCount(); i++) {
							Rectangle rect = item.getBounds(i);
							if (rect.contains(pt)) {
								final int row = index;
								final int column = i;
								final Text text = new Text(tblResult, SWT.MULTI);
								if (qe.font != null)
									text.setFont(qe.font);

								String type = tblResult.getColumn(i).getData()
										.toString();
								if (type.equals("CLASS") || type.equals("SET")
										|| type.equals("MULTISET")
										|| type.equals("SEQUENCE")
										|| item.getText(0).equals("NONE"))
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
													item.setText(column, text.getText());
													updateValue(row, column);
												} catch (SQLException e1) {
													CommonTool
															.ErrorBox(e1
																	.getErrorCode()
																	+ MainConstants.NEW_LINE
																	+ e1.getMessage());
													tblResult.getItem(oldRow)
															.setText(oldCol, old);
													e.doit = false;
												}
											}
											text.dispose();
											break;
										case SWT.Traverse:
											switch (e.detail) {
											case SWT.TRAVERSE_RETURN:
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
																		+ e1.getMessage());
														tblResult.getItem(
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
								text.addListener(SWT.FocusOut, textListener);
								text.addListener(SWT.Traverse, textListener);
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
			tblResult.addListener(SWT.MouseDoubleClick, new Listener() {
				public void handleEvent(Event event) {
					Rectangle clientArea = tblResult.getClientArea();
					Point pt = new Point(event.x, event.y);
					int index = tblResult.getTopIndex();
					while (index < tblResult.getItemCount()) {
						boolean visible = false;
						final TableItem item = tblResult.getItem(index);
						for (int i = 1; i < tblResult.getColumnCount(); i++) {
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
			tblResult.addKeyListener(new org.eclipse.swt.events.KeyAdapter() {
				public void keyReleased(KeyEvent e) {
					if (e.keyCode == SWT.DEL) {
						deleteRecord(tblResult.getSelection());
					}
				}

				public void keyPressed(org.eclipse.swt.events.KeyEvent e) {
					// Doesn't work.
					if (e.keyCode == 'c' && (e.stateMask & SWT.CTRL) != 0) {
						// if (e.keyCode == ('C' + SWT.CTRL)) {
						// System.out.println("Copy!!!!");
					}
				}
			});
		}

		try {
			makeColumn();
			makeTuple(0);
		} catch (SQLException e) {
			CommonTool.ErrorBox(e.getErrorCode() + MainConstants.NEW_LINE
					+ Messages.getString("QEDIT.ERRORHEAD") + e.getMessage());
			CommonTool.debugPrint(e);
		}
		tblResult.pack();
	}

	public void makeColumn() throws SQLException {
		TableColumn tblColumn[];

		rsmt = (CUBRIDResultSetMetaData) rs.getMetaData();
		cntColumn = rsmt.getColumnCount();

		tblColumn = new TableColumn[cntColumn + 1];

		tblColumn[0] = new TableColumn(tblResult, SWT.NONE);
		tblColumn[0].setText("OID");
		if (!doesGetOidInfo)
			tblColumn[0].setWidth(0);
		tblColumn[0].setResizable(doesGetOidInfo);
		tblColumn[0].setData("OID");

		for (int j = 1; j <= cntColumn; j++) {
			tblColumn[j] = new TableColumn(tblResult, SWT.NONE);
			tblColumn[j].setText(rsmt.getColumnName(j));
			tblColumn[j].setWidth(80);
			tblColumn[j].setData(rsmt.getColumnTypeName(j));
		}
	}

	public void makeTuple(int position) throws SQLException {
		cntRecord = 0;
		int limit = recordLimit;
		TableItem item = null;

		if (!rs.absolute(position + 1))
			return;

		isNotEndOfTuple = false;

		do {
			// while (rs.next()) {
			cntRecord++;

			if (qe != null && recordLimit > 0 && cntRecord > limit) {
				MessageBox mb = new MessageBox(Application.mainwindow
						.getShell(), SWT.ICON_WARNING | SWT.YES | SWT.NO);
				mb.setText(Messages.getString("QEDIT.WARNING"));
				mb.setMessage(Messages.getString("QEDIT.TOOMANYRECORD1")
						+ limit + Messages.getString("QEDIT.TOOMANYRECORD2"));
				int state = mb.open();
				if (state == SWT.NO) {
					isNotEndOfTuple = true;
					break;
				}
				limit += recordLimit;
			}

			item = new TableItem(tblResult, SWT.MULTI);
			CUBRIDOID oid = ((CUBRIDResultSet) rs).getOID();
			if (oid == null)
				item.setText(0, "NONE");
			else
				item.setText(0, oid.getOidString());

			for (int j = 1; j <= cntColumn; j++) {
				String itemText = QueryEditor.STR_NULL;
				String columnType = tblResult.getColumn(j).getData().toString();
				if (rs.getObject(j) != null) {
					if (columnType.equals("SET")
							|| columnType.equals("MULTISET")
							|| columnType.equals("SEQUENCE")) {
						Object[] set = (Object[]) rs.getCollection(j);
						itemText = "{";
						for (int i = 0; i < set.length; i++) {
							if (set[i] instanceof CUBRIDOID)
								itemText += ((CUBRIDOID) set[i]).getOidString();
							else
								itemText += set[i];
							if (i < set.length - 1)
								itemText += ", ";
						}
						itemText += "}";
					} else {
						itemText = rs.getString(j);
					}
				}
				if (qe.font != null)
					item.setForeground(qe.getColorManager().getColor(
							new RGB(qe.fontColorRed, qe.fontColorGreen,
									qe.fontColorBlue)));

				item.setText(j, itemText);
			}
		} while (rs.next());

		if (doesGetOidInfo)
			tblResult.getColumn(0).pack();
	}

	private void updateValue(int index, int column) throws SQLException {
		String oid = tblResult.getItem(index).getText(0);
		String value = tblResult.getItem(index).getText(column);
		String colName = tblResult.getColumn(column).getText();

		qe.updateResult(oid, colName, value);
	}

	protected void deleteRecord(TableItem[] selection) {
		MessageBox mb = new MessageBox(Application.mainwindow.getShell(),
				SWT.ICON_QUESTION | SWT.YES | SWT.NO);
		mb.setText(Messages.getString("QEDIT.DELETE"));
		mb.setMessage(tblResult.getSelectionCount()
				+ Messages.getString("QEDIT.CONFIRMDELMSG"));
		int state = mb.open();
		if (state == SWT.YES) {
			String[] oid = new String[selection.length];
			for (int i = 0; i < selection.length; i++) {
				oid[i] = selection[i].getText(0);
				selection[i].dispose();
			}

			try {
				qe.deleteResult(oid);
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
		cb.setContents(new Object[] { data }, new Transfer[] { textTransfer });
	}
}
