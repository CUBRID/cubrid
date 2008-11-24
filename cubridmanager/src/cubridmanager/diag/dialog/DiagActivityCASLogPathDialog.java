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

package cubridmanager.diag.dialog;

import java.io.BufferedWriter;
import java.io.File;
import java.io.FileNotFoundException;
import java.io.FileWriter;
import java.io.IOException;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Comparator;

import org.eclipse.jface.viewers.ColumnWeightData;
import org.eclipse.jface.viewers.TableLayout;
import org.eclipse.jface.window.Window;
import org.eclipse.swt.events.SelectionEvent;
import org.eclipse.swt.events.SelectionListener;
import org.eclipse.swt.widgets.Dialog;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.SWT;
import org.eclipse.swt.widgets.FileDialog;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.widgets.Group;
import org.eclipse.swt.widgets.Table;
import org.eclipse.swt.widgets.TableColumn;
import org.eclipse.swt.widgets.TableItem;
import org.eclipse.swt.widgets.Text;

import cubridmanager.ClientSocket;
import cubridmanager.CommonTool;
import cubridmanager.MainRegistry;
import cubridmanager.Messages;
import cubridmanager.diag.DiagAnalyzeCasLogResult;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.layout.GridData;

public class DiagActivityCASLogPathDialog extends Dialog {
	public Object self = this;
	private int DiagEndCode = Window.CANCEL;
	private Shell sShell = null; // @jve:decl-index=0:visual-constraint="4,8"
	private Button buttonClose = null;
	private Group groupAnalyzeResult = null;
	private Group groupBroker = null;
	private Label label = null;
	private Table table = null;
	private Group groupQuery = null;
	private Text textQuery = null;
	private Text textQueryResult = null;
	private Button buttonRunOriginalQuery = null;
	private Button buttonSaveToFile = null;
	private Label label1 = null;
	private Label label2 = null;
	public String filename = null;
	public String resultFile = null;
	public boolean option_t = false;
	public int currentResultIndex;

	public DiagActivityCASLogPathDialog(Shell parent) {
		super(parent);
	}

	public DiagActivityCASLogPathDialog(Shell parent, int style) {
		super(parent, style);
	}

	public int doModal() {
		createSShell();
		CommonTool.centerShell(sShell);
		sShell.open();
		Display display = sShell.getDisplay();
		label.setText(filename);
		table.removeAll();

		textQuery.setText("");
		textQueryResult.setText("");
		buttonRunOriginalQuery.setEnabled(false);
		buttonSaveToFile.setEnabled(false);
		ArrayList resultList = MainRegistry.tmpAnalyzeCasLogResult;
		int itemcount = resultList.size();
		for (int i = 0; i < itemcount; i++) {
			DiagAnalyzeCasLogResult logResult = (DiagAnalyzeCasLogResult) (resultList
					.get(i));
			TableItem item = new TableItem(table, SWT.NONE);
			item.setText(0, logResult.qindex);
			if (!option_t) {
				item.setText(1, logResult.max);
				item.setText(2, logResult.min);
				item.setText(3, logResult.avg);
				item.setText(4, logResult.cnt);
				item.setText(5, logResult.err_cnt);
			} else {
				item.setText(1, logResult.exec_time);
			}
		}

		while (!sShell.isDisposed()) {
			if (!display.readAndDispatch())
				display.sleep();
		}

		return DiagEndCode;
	}

	/**
	 * This method initializes sShell
	 * 
	 */
	private void createSShell() {
		GridData gridData1 = new org.eclipse.swt.layout.GridData();
		gridData1.heightHint = 24;
		gridData1.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		gridData1.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		gridData1.grabExcessHorizontalSpace = false;
		gridData1.widthHint = 84;
		GridLayout gridLayout = new GridLayout();
		gridLayout.numColumns = 2;
		sShell = new Shell(getParent(), SWT.DIALOG_TRIM | SWT.APPLICATION_MODAL);
		// sShell = new Shell(SWT.DIALOG_TRIM | SWT.APPLICATION_MODAL);
		sShell.setText(Messages.getString("TITLE.DIAGANALIZECASSCRIPTLOG"));
		sShell.setLayout(gridLayout);
		createGroup2();
		createGroupQuery();
		createGroup();
		sShell.addDisposeListener(new org.eclipse.swt.events.DisposeListener() {
			public void widgetDisposed(org.eclipse.swt.events.DisposeEvent e) {
				StringBuffer msg = new StringBuffer();
				ClientSocket cs = new ClientSocket();
				msg.append("filename:").append(resultFile).append("\n");
				cs.SendClientMessage(sShell, msg.toString(),
						"removecasrunnertmpfile");
			}
		});
		Label dummy = new Label(sShell, SWT.NONE);
		buttonClose = new Button(sShell, SWT.NONE);
		buttonClose.setText(Messages.getString("BUTTON.CLOSE"));
		buttonClose.setLayoutData(gridData1);
		buttonClose
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						sShell.dispose();
					}
				});
		sShell.pack(true);
	}

	/**
	 * This method initializes group
	 * 
	 */
	private void createGroup() {
		GridData gridData5 = new org.eclipse.swt.layout.GridData();
		gridData5.heightHint = -1;
		gridData5.widthHint = -1;
		groupAnalyzeResult = new Group(sShell, SWT.NONE);
		groupAnalyzeResult.setText(Messages.getString("LABEL.ANALYSISRESULT"));
		groupAnalyzeResult.setLayout(new GridLayout());
		groupAnalyzeResult.setLayoutData(gridData5);
		createTable();
	}

	/**
	 * This method initializes group2
	 * 
	 */
	private void createGroup2() {
		GridData gridData4 = new org.eclipse.swt.layout.GridData();
		gridData4.heightHint = 25;
		gridData4.widthHint = 87;
		GridData gridData2 = new org.eclipse.swt.layout.GridData();
		gridData2.heightHint = 18;
		gridData2.horizontalSpan = 3;
		gridData2.widthHint = 370;
		GridLayout gridLayout1 = new GridLayout();
		gridLayout1.numColumns = 3;
		groupBroker = new Group(sShell, SWT.NONE);
		groupBroker.setText(Messages.getString("LABEL.LOGFILE"));
		groupBroker.setLayout(gridLayout1);
		label = new Label(groupBroker, SWT.NONE);
		label.setText(Messages.getString("LABEL.CASLOGFILE"));
		label.setLayoutData(gridData2);
	}

	/**
	 * This method initializes table
	 * 
	 */
	private void createTable() {
		GridData gridData6 = new org.eclipse.swt.layout.GridData();
		gridData6.heightHint = 263;
		gridData6.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData6.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData6.grabExcessVerticalSpace = false;
		gridData6.grabExcessHorizontalSpace = false;
		gridData6.widthHint = 355;
		table = new Table(groupAnalyzeResult, SWT.FULL_SELECTION | SWT.MULTI);
		table.setHeaderVisible(true);
		table.setLayoutData(gridData6);
		table.setLinesVisible(true);

		TableLayout tlayout = new TableLayout();

		if (option_t) {
			tlayout.addColumnData(new ColumnWeightData(20, 60, true));
			tlayout.addColumnData(new ColumnWeightData(20, 60, true));

			table.setLayout(tlayout);
			table.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
						public void widgetSelected(
								org.eclipse.swt.events.SelectionEvent e) {
							int selectioncount = table.getSelectionCount();
							int ResultCount = MainRegistry.tmpAnalyzeCasLogResult.size();
							StringBuffer queryString = new StringBuffer("");
							DiagAnalyzeCasLogResult logResult;
							for (int j = 0; j < selectioncount; j++) {
								String qindex = table.getSelection()[j].getText(0);

								for (int i = 0; i < ResultCount; i++) {
									logResult = (DiagAnalyzeCasLogResult) (MainRegistry.tmpAnalyzeCasLogResult
											.get(i));
									if (qindex.equals(logResult.qindex)) {
										currentResultIndex = i;

										if (logResult.queryString.equals("")) {
											/* send getanalyzeresult message */
											String msg = new String();
											msg = "filename:" + resultFile + "\n";
											msg += "qindex:" + logResult.qindex + "\n";
											ClientSocket cs = new ClientSocket();
											cs.socketOwner = self;
											if (!cs.SendBackGround(sShell, msg,
													"getcaslogtopresult",
													"getting log " + logResult.qindex)) {
											}
										}

										queryString.append(logResult.queryString);
										queryString.append("\r\n");
										buttonRunOriginalQuery.setEnabled(true);
										buttonSaveToFile.setEnabled(true);
										break;
									}
								}
							}
							textQuery.setText(queryString.toString());
						}
					});

			TableColumn qindex = new TableColumn(table, SWT.LEFT);
			qindex.addSelectionListener(new SelectionListener() {
				boolean desc = false;

				public void widgetSelected(SelectionEvent e) {
					// provide sorting function when column clicked.

					// Memory selected row's index value.
					String selected_row_index = new String("");
					if (table.getSelectionCount() > 0) {
						TableItem item = table.getSelection()[0];
						selected_row_index = item.getText(0);
					}

					// Convert to array and using array.sort fuction.
					Object[] obj = MainRegistry.tmpAnalyzeCasLogResult
							.toArray();
					Arrays.sort(obj, new Comparator() {
						public int compare(Object o1, Object o2) {
							int ret_val;
							int it1, it2;
							String t1 = (String) ((DiagAnalyzeCasLogResult) o1).qindex;
							String t2 = (String) ((DiagAnalyzeCasLogResult) o2).qindex;
							t1 = t1.substring(2, t1.indexOf(']'));
							t2 = t2.substring(2, t2.indexOf(']'));

							try {
								it1 = Integer.parseInt(t1);
								it2 = Integer.parseInt(t2);
								if (desc)
									ret_val = it1 - it2;
								else
									ret_val = it2 - it1;
							} catch (Exception ee) {
								if (desc)
									ret_val = t1.compareTo(t2);
								else
									ret_val = t2.compareTo(t1);
							}

							return ret_val;
						}
					});
					// input table through complete sorted array. 
					table.removeAll();
					InsertArrayToTable(obj);

					// Select row that before sorting progress.
					if (!selected_row_index.equals("")) {
						for (int i = 0; i < table.getItemCount(); i++) {
							if (table.getItem(i).getText(0).equals(
									selected_row_index)) {
								table.setSelection(i);
								break;
							}
						}
					}

					desc = !desc;
				}

				public void widgetDefaultSelected(SelectionEvent e) {
				}
			});

			TableColumn exec_time = new TableColumn(table, SWT.LEFT);
			exec_time.addSelectionListener(new SelectionListener() {
				boolean desc = false;

				public void widgetSelected(SelectionEvent e) {
					// provide sorting function when column clicked.

					// Memory selected row's index value.
					String selected_row_index = new String("");
					if (table.getSelectionCount() > 0) {
						TableItem item = table.getSelection()[0];
						selected_row_index = item.getText(0);
					}

					// Convert to array and using array.sort fuction.
					Object[] obj = MainRegistry.tmpAnalyzeCasLogResult
							.toArray();
					Arrays.sort(obj, new Comparator() {
						public int compare(Object o1, Object o2) {
							int ret_val;
							float ft1, ft2;
							String t1 = getTimeSec((String) ((DiagAnalyzeCasLogResult) o1).exec_time);
							String t2 = getTimeSec((String) ((DiagAnalyzeCasLogResult) o2).exec_time);

							try {
								ft1 = Float.parseFloat(t1);
								ft2 = Float.parseFloat(t2);

								if (desc) {
									if (ft1 > ft2)
										return 1;
									else if (ft1 == ft2)
										return 0;
									else
										return -1;
								} else {
									if (ft1 > ft2)
										return -1;
									else if (ft1 == ft2)
										return 0;
									else
										return 1;
								}
							} catch (Exception ee) {
								if (desc)
									ret_val = t1.compareTo(t2);
								else
									ret_val = t2.compareTo(t1);
							}

							return ret_val;
						}
					});
					// input table through complete sorted array 
					table.removeAll();
					InsertArrayToTable(obj);

					// Select row that before sorting progress.
					if (!selected_row_index.equals("")) {
						for (int i = 0; i < table.getItemCount(); i++) {
							if (table.getItem(i).getText(0).equals(
									selected_row_index)) {
								table.setSelection(i);
								break;
							}
						}
					}

					desc = !desc;
				}

				public void widgetDefaultSelected(SelectionEvent e) {
				}
			});

			qindex.setText(Messages.getString("TABLE.INDEX"));
			exec_time.setText(Messages.getString("TABLE.TRANSACTIONEXETIME"));
			qindex.setWidth(100);
			exec_time.setWidth(100);
		} else {
			tlayout.addColumnData(new ColumnWeightData(20, 60, true));
			tlayout.addColumnData(new ColumnWeightData(20, 60, true));
			tlayout.addColumnData(new ColumnWeightData(20, 60, true));
			tlayout.addColumnData(new ColumnWeightData(20, 60, true));
			tlayout.addColumnData(new ColumnWeightData(10, 30, true));
			tlayout.addColumnData(new ColumnWeightData(10, 30, true));

			table.setLayout(tlayout);
			table
					.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
						public void widgetSelected(
								org.eclipse.swt.events.SelectionEvent e) {
							int selectioncount = table.getSelectionCount();
							int ResultCount = MainRegistry.tmpAnalyzeCasLogResult.size();
							StringBuffer queryString = new StringBuffer("");
							DiagAnalyzeCasLogResult logResult;
							for (int j = 0; j < selectioncount; j++) {
								String qindex = table.getSelection()[j].getText(0);

								for (int i = 0; i < ResultCount; i++) {
									logResult = (DiagAnalyzeCasLogResult) (MainRegistry.tmpAnalyzeCasLogResult
											.get(i));
									if (qindex.equals(logResult.qindex)) {
										currentResultIndex = i;

										if (logResult.queryString.equals("")) {
											/* send getanalyzeresult message */
											String msg = new String();
											msg = "filename:" + resultFile + "\n";
											msg += "qindex:" + logResult.qindex + "\n";
											ClientSocket cs = new ClientSocket();
											cs.socketOwner = self;
											if (!cs.SendBackGround(sShell, msg,
													"getcaslogtopresult",
													"getting log "
															+ logResult.qindex)) {
											}
										}
										queryString.append(logResult.queryString);
										queryString.append("\r\n");
										buttonRunOriginalQuery.setEnabled(true);
										buttonSaveToFile.setEnabled(true);
										break;
									}
								}
							}
							textQuery.setText(queryString.toString());
						}
					});

			TableColumn qindex = new TableColumn(table, SWT.LEFT);
			qindex.addSelectionListener(new SelectionListener() {
				boolean desc = false;

				public void widgetSelected(SelectionEvent e) {
					// sorting method when column click

					// Memory value that selected index
					String selected_row_index = new String("");
					if (table.getSelectionCount() > 0) {
						TableItem item = table.getSelection()[0];
						selected_row_index = item.getText(0);
					}

					// Convert to array, and use "array.sort" function
					Object[] obj = MainRegistry.tmpAnalyzeCasLogResult
							.toArray();
					Arrays.sort(obj, new Comparator() {
						public int compare(Object o1, Object o2) {
							int ret_val;
							int it1, it2;
							String t1 = (String) ((DiagAnalyzeCasLogResult) o1).qindex;
							String t2 = (String) ((DiagAnalyzeCasLogResult) o2).qindex;
							t1 = t1.substring(2, t1.indexOf(']'));
							t2 = t2.substring(2, t2.indexOf(']'));

							try {
								it1 = Integer.parseInt(t1);
								it2 = Integer.parseInt(t2);
								if (desc)
									ret_val = it1 - it2;
								else
									ret_val = it2 - it1;
							} catch (Exception ee) {
								if (desc)
									ret_val = t1.compareTo(t2);
								else
									ret_val = t2.compareTo(t1);
							}

							return ret_val;
						}
					});
					table.removeAll();
					InsertArrayToTable(obj);

					if (!selected_row_index.equals("")) {
						for (int i = 0; i < table.getItemCount(); i++) {
							if (table.getItem(i).getText(0).equals(
									selected_row_index)) {
								table.setSelection(i);
								break;
							}
						}
					}

					desc = !desc;
				}

				public void widgetDefaultSelected(SelectionEvent e) {
				}
			});

			TableColumn max = new TableColumn(table, SWT.LEFT);
			max.addSelectionListener(new SelectionListener() {
				boolean desc = false;

				public void widgetSelected(SelectionEvent e) {
					String selected_row_index = new String("");
					if (table.getSelectionCount() > 0) {
						TableItem item = table.getSelection()[0];
						selected_row_index = item.getText(0);
					}

					Object[] obj = MainRegistry.tmpAnalyzeCasLogResult.toArray();
					Arrays.sort(obj, new Comparator() {
						public int compare(Object o1, Object o2) {
							int ret_val;
							float ft1, ft2;
							String t1 = getTimeSec((String) ((DiagAnalyzeCasLogResult) o1).max);
							String t2 = getTimeSec((String) ((DiagAnalyzeCasLogResult) o2).max);

							try {
								ft1 = Float.parseFloat(t1);
								ft2 = Float.parseFloat(t2);

								if (desc) {
									if (ft1 > ft2)
										return 1;
									else if (ft1 == ft2)
										return 0;
									else
										return -1;
								} else {
									if (ft1 > ft2)
										return -1;
									else if (ft1 == ft2)
										return 0;
									else
										return 1;
								}
							} catch (Exception ee) {
								if (desc)
									ret_val = t1.compareTo(t2);
								else
									ret_val = t2.compareTo(t1);
							}

							return ret_val;
						}
					});
					table.removeAll();
					InsertArrayToTable(obj);

					if (!selected_row_index.equals("")) {
						for (int i = 0; i < table.getItemCount(); i++) {
							if (table.getItem(i).getText(0).equals(
									selected_row_index)) {
								table.setSelection(i);
								break;
							}
						}
					}

					desc = !desc;
				}

				public void widgetDefaultSelected(SelectionEvent e) {
				}
			});

			TableColumn min = new TableColumn(table, SWT.LEFT);
			min.addSelectionListener(new SelectionListener() {
				boolean desc = false;

				public void widgetSelected(SelectionEvent e) {
					String selected_row_index = new String("");
					if (table.getSelectionCount() > 0) {
						TableItem item = table.getSelection()[0];
						selected_row_index = item.getText(0);
					}

					Object[] obj = MainRegistry.tmpAnalyzeCasLogResult.toArray();
					Arrays.sort(obj, new Comparator() {
						public int compare(Object o1, Object o2) {
							int ret_val;
							float ft1, ft2;
							String t1 = getTimeSec((String) ((DiagAnalyzeCasLogResult) o1).min);
							String t2 = getTimeSec((String) ((DiagAnalyzeCasLogResult) o2).min);

							try {
								ft1 = Float.parseFloat(t1);
								ft2 = Float.parseFloat(t2);

								if (desc) {
									if (ft1 > ft2)
										return 1;
									else if (ft1 == ft2)
										return 0;
									else
										return -1;
								} else {
									if (ft1 > ft2)
										return -1;
									else if (ft1 == ft2)
										return 0;
									else
										return 1;
								}
							} catch (Exception ee) {
								if (desc)
									ret_val = t1.compareTo(t2);
								else
									ret_val = t2.compareTo(t1);
							}

							return ret_val;
						}
					});
					table.removeAll();
					InsertArrayToTable(obj);

					if (!selected_row_index.equals("")) {
						for (int i = 0; i < table.getItemCount(); i++) {
							if (table.getItem(i).getText(0).equals(
									selected_row_index)) {
								table.setSelection(i);
								break;
							}
						}
					}

					desc = !desc;
				}

				public void widgetDefaultSelected(SelectionEvent e) {
				}
			});

			TableColumn avg = new TableColumn(table, SWT.LEFT);
			avg.addSelectionListener(new SelectionListener() {
				boolean desc = false;

				public void widgetSelected(SelectionEvent e) {
					String selected_row_index = new String("");
					if (table.getSelectionCount() > 0) {
						TableItem item = table.getSelection()[0];
						selected_row_index = item.getText(1);
					}

					Object[] obj = MainRegistry.tmpAnalyzeCasLogResult.toArray();
					Arrays.sort(obj, new Comparator() {
						public int compare(Object o1, Object o2) {
							int ret_val;
							float ft1, ft2;
							String t1 = getTimeSec((String) ((DiagAnalyzeCasLogResult) o1).avg);
							String t2 = getTimeSec((String) ((DiagAnalyzeCasLogResult) o2).avg);

							try {
								ft1 = Float.parseFloat(t1);
								ft2 = Float.parseFloat(t2);

								if (desc) {
									if (ft1 > ft2)
										return 1;
									else if (ft1 == ft2)
										return 0;
									else
										return -1;
								} else {
									if (ft1 > ft2)
										return -1;
									else if (ft1 == ft2)
										return 0;
									else
										return 1;
								}
							} catch (Exception ee) {
								if (desc)
									ret_val = t1.compareTo(t2);
								else
									ret_val = t2.compareTo(t1);
							}

							return ret_val;
						}
					});
					table.removeAll();
					InsertArrayToTable(obj);

					if (!selected_row_index.equals("")) {
						for (int i = 0; i < table.getItemCount(); i++) {
							if (table.getItem(i).getText(1).equals(
									selected_row_index)) {
								table.setSelection(i);
								break;
							}
						}
					}

					desc = !desc;
				}

				public void widgetDefaultSelected(SelectionEvent e) {
				}
			});

			TableColumn cnt = new TableColumn(table, SWT.LEFT);
			cnt.addSelectionListener(new SelectionListener() {
				boolean desc = false;

				public void widgetSelected(SelectionEvent e) {
					String selected_row_index = new String("");
					if (table.getSelectionCount() > 0) {
						TableItem item = table.getSelection()[0];
						selected_row_index = item.getText(1);
					}

					Object[] obj = MainRegistry.tmpAnalyzeCasLogResult.toArray();
					Arrays.sort(obj, new Comparator() {
						public int compare(Object o1, Object o2) {
							int ret_val;
							float ft1, ft2;
							String t1 = (String) ((DiagAnalyzeCasLogResult) o1).cnt;
							String t2 = (String) ((DiagAnalyzeCasLogResult) o2).cnt;

							try {
								ft1 = Float.parseFloat(t1);
								ft2 = Float.parseFloat(t2);

								if (desc) {
									if (ft1 > ft2)
										return 1;
									else if (ft1 == ft2)
										return 0;
									else
										return -1;
								} else {
									if (ft1 > ft2)
										return -1;
									else if (ft1 == ft2)
										return 0;
									else
										return 1;
								}
							} catch (Exception ee) {
								if (desc)
									ret_val = t1.compareTo(t2);
								else
									ret_val = t2.compareTo(t1);
							}

							return ret_val;
						}
					});
					table.removeAll();
					InsertArrayToTable(obj);

					if (!selected_row_index.equals("")) {
						for (int i = 0; i < table.getItemCount(); i++) {
							if (table.getItem(i).getText(1).equals(
									selected_row_index)) {
								table.setSelection(i);
								break;
							}
						}
					}

					desc = !desc;
				}

				public void widgetDefaultSelected(SelectionEvent e) {
				}
			});
			TableColumn err = new TableColumn(table, SWT.LEFT);
			err.addSelectionListener(new SelectionListener() {
				boolean desc = false;

				public void widgetSelected(SelectionEvent e) {
					String selected_row_index = new String("");
					if (table.getSelectionCount() > 0) {
						TableItem item = table.getSelection()[0];
						selected_row_index = item.getText(1);
					}

					Object[] obj = MainRegistry.tmpAnalyzeCasLogResult.toArray();
					Arrays.sort(obj, new Comparator() {
						public int compare(Object o1, Object o2) {
							int ret_val;
							float ft1, ft2;
							String t1 = (String) ((DiagAnalyzeCasLogResult) o1).err_cnt;
							String t2 = (String) ((DiagAnalyzeCasLogResult) o2).err_cnt;

							try {
								ft1 = Float.parseFloat(t1);
								ft2 = Float.parseFloat(t2);

								if (desc) {
									if (ft1 < ft2)
										return -1;
									else if (ft1 == ft2)
										return 0;
									else
										return 1;
								} else {
									if (ft1 < ft2)
										return 1;
									else if (ft1 == ft2)
										return 0;
									else
										return -1;
								}
							} catch (Exception ee) {
								if (desc)
									ret_val = t1.compareTo(t2);
								else
									ret_val = t2.compareTo(t1);
							}

							return ret_val;
						}
					});
					table.removeAll();
					InsertArrayToTable(obj);

					if (!selected_row_index.equals("")) {
						for (int i = 0; i < table.getItemCount(); i++) {
							if (table.getItem(i).getText(1).equals(
									selected_row_index)) {
								table.setSelection(i);
								break;
							}
						}
					}

					desc = !desc;
				}

				public void widgetDefaultSelected(SelectionEvent e) {
				}
			});

			qindex.setText(Messages.getString("TABLE.INDEX"));
			max.setText(Messages.getString("TABLE.MAX"));
			min.setText(Messages.getString("TABLE.MIN"));
			avg.setText(Messages.getString("TABLE.AVG"));
			cnt.setText(Messages.getString("TABLE.TOTALCOUNT"));
			err.setText(Messages.getString("TABLE.ERRCOUNT"));

			qindex.setWidth(60);
			max.setWidth(60);
			min.setWidth(60);
			avg.setWidth(60);
			cnt.setWidth(50);
			err.setWidth(50);
		}
	}

	/**
	 * This method initializes groupQuery
	 * 
	 */
	private void createGroupQuery() {
		GridData gridData22 = new org.eclipse.swt.layout.GridData();
		gridData22.heightHint = 26;
		gridData22.widthHint = 122;
		GridData gridData23 = new org.eclipse.swt.layout.GridData();
		gridData23.heightHint = 26;
		gridData23.widthHint = 122;
		GridData gridData12 = new org.eclipse.swt.layout.GridData();
		gridData12.heightHint = 26;
		gridData12.widthHint = 122;
		GridData gridData21 = new org.eclipse.swt.layout.GridData();
		gridData21.horizontalSpan = 3;
		gridData21.widthHint = 432;
		gridData21.heightHint = 84;
		GridData gridData11 = new org.eclipse.swt.layout.GridData();
		gridData11.horizontalSpan = 3;
		gridData11.widthHint = 432;
		gridData11.heightHint = 152;
		GridLayout gridLayout2 = new GridLayout();
		gridLayout2.numColumns = 3;
		gridLayout2.horizontalSpacing = 45;
		GridData gridData = new org.eclipse.swt.layout.GridData();
		gridData.verticalSpan = 2;
		groupQuery = new Group(sShell, SWT.NONE);
		groupQuery.setText("");
		groupQuery.setLayout(gridLayout2);
		groupQuery.setLayoutData(gridData);
		label1 = new Label(groupQuery, SWT.NONE);
		label1.setText(Messages.getString("LABEL.LOGCONTENTS"));
		Label dummy1 = new Label(groupQuery, SWT.NONE);
		Label dummy2 = new Label(groupQuery, SWT.NONE);
		textQuery = new Text(groupQuery, SWT.BORDER | SWT.READ_ONLY
				| SWT.V_SCROLL | SWT.H_SCROLL);
		textQuery.setEditable(true);
		textQuery.setLayoutData(gridData11);
		label2 = new Label(groupQuery, SWT.NONE);
		label2.setText(Messages.getString("LABEL.EXECUTERESULT"));
		Label dummy3 = new Label(groupQuery, SWT.NONE);
		Label dummy4 = new Label(groupQuery, SWT.NONE);
		textQueryResult = new Text(groupQuery, SWT.BORDER | SWT.READ_ONLY
				| SWT.V_SCROLL);
		textQueryResult.setLayoutData(gridData21);
		buttonRunOriginalQuery = new Button(groupQuery, SWT.NONE);
		buttonRunOriginalQuery.setEnabled(false);
		buttonRunOriginalQuery.setLayoutData(gridData12);
		buttonRunOriginalQuery.setText(Messages
				.getString("BUTTON.EXECUTEORIGINALQUERY"));

		Label dummy5 = new Label(groupQuery, SWT.NONE);
		dummy5.setLayoutData(gridData22);
		buttonRunOriginalQuery
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						// executecasrunner command execute using query in querystring window.
						String queryString = textQuery.getText();

						if (queryString.length() > 0) {
							DiagCasRunnerConfigDialog casrunnerconfigdialog = new DiagCasRunnerConfigDialog(
									sShell);
							casrunnerconfigdialog.logfile = filename;
							casrunnerconfigdialog.logstring = textQuery
									.getText();

							if (casrunnerconfigdialog.doModal() == Window.CANCEL)
								return;

							StringBuffer msg = new StringBuffer();
							msg.append("dbname:").append(
									DiagCasRunnerConfigDialog.dbname).append(
									"\n");
							msg.append("brokername:").append(
									DiagCasRunnerConfigDialog.brokerName)
									.append("\n");
							msg.append("username:").append(
									DiagCasRunnerConfigDialog.userName).append(
									"\n");
							msg.append("passwd:").append(
									DiagCasRunnerConfigDialog.password).append(
									"\n");
							msg.append("num_thread:").append(
									DiagCasRunnerConfigDialog.numThread)
									.append("\n");
							msg.append("repeat_count:").append(
									DiagCasRunnerConfigDialog.numRepeatCount)
									.append("\n");
							msg.append("executelogfile:no\n");
							msg.append("open:logstring\n");
							msg.append("logstring:").append(
									queryString.replaceAll("\r\n",
											"\nlogstring:")).append("\n");
							msg.append("close:logstring\n");
							msg.append("show_queryresult:");
							if (DiagCasRunnerConfigDialog.showqueryresult) {
								msg.append("yes\n");
								if (DiagCasRunnerConfigDialog.showqueryplan)
									msg.append("show_queryplan:yes\n");
								else
									msg.append("show_queryplan:no\n");
							} else {
								msg.append("no\n");
								msg.append("show_queryplan:no\n");
							}

							ClientSocket cs = new ClientSocket();
							if (cs.SendBackGround(sShell, msg.toString(),
									"executecasrunner", "Executing cas runner") == false) {
								CommonTool.ErrorBox(sShell, cs.ErrorMsg);
								return;
							}

							// Display 
							// MainRegistry.tmpDiagExecuteCasRunnerResult.resultString
							// in result window 
							textQueryResult
									.setText(MainRegistry.tmpDiagExecuteCasRunnerResult.resultString);

							if (casrunnerconfigdialog.showqueryresult) {
								String file_path = MainRegistry.tmpDiagExecuteCasRunnerResult.queryResultFile;
								DiagCasRunnerResultViewDialog casRunnerResultDialog = new DiagCasRunnerResultViewDialog(
										sShell);
								casRunnerResultDialog.total_result_num = MainRegistry.tmpDiagExecuteCasRunnerResult.resultFileNum;
								casRunnerResultDialog.doModal(file_path);
							}
						}
					}
				});
		buttonSaveToFile = new Button(groupQuery, SWT.NONE);
		buttonSaveToFile.setEnabled(false);
		buttonSaveToFile.setLayoutData(gridData23);
		buttonSaveToFile.setText(Messages.getString("BUTTON.SAVELOGSTRING"));
		buttonSaveToFile
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						String savedFilename = saveAsFile();
						if (savedFilename.equals(""))
							return;
						DiagAnalyzeCasLogResult diagAnalyzeCasLogResult = (DiagAnalyzeCasLogResult) (MainRegistry.tmpAnalyzeCasLogResult
								.get(currentResultIndex));
						if (savedFilename.length() > 0) {
							diagAnalyzeCasLogResult.savedFileName = savedFilename;
						}
					}
				});
	}

	private String saveAsFile() {
		String fileName = new String();
		FileDialog dialog = new FileDialog(sShell, SWT.SAVE
				| SWT.APPLICATION_MODAL);
		dialog.setFilterExtensions(new String[] { "*.clg", "*.txt", "*.*" });
		dialog.setFilterNames(new String[] { "CAS Log File(*.clg)",
				"Text File(*.txt)", "All" });
		String result = dialog.open();
		if (result != null) {
			File file = new File(result);
			if (file.exists()) {
				if (CommonTool.MsgBox(sShell, SWT.ICON_WARNING | SWT.YES
						| SWT.NO, Messages.getString("MSG.WARNING"), result
						+ Messages.getString("MSG.OVERWRITEFILE")) == SWT.NO) {
					return "";
				}
			}
			try {
				BufferedWriter bw = new BufferedWriter(new FileWriter(file));
				String text = textQuery.getText();
				bw.write(text);
				bw.close();
				fileName = result;
			} catch (FileNotFoundException e1) {
				CommonTool.ErrorBox(sShell, e1.getMessage());
				return "";
			} catch (IOException e1) {
				CommonTool.ErrorBox(sShell, e1.getMessage());
				return "";
			}
		}

		return fileName;
	}

	public void InsertArrayToTable(Object[] obj) {
		int itemcount = obj.length;
		for (int i = 0; i < itemcount; i++) {
			DiagAnalyzeCasLogResult logResult = (DiagAnalyzeCasLogResult) (obj[i]);
			TableItem item = new TableItem(table, SWT.NONE);
			item.setText(0, logResult.qindex);
			if (!option_t) {
				item.setText(1, logResult.max);
				item.setText(2, logResult.min);
				item.setText(3, logResult.avg);
				item.setText(4, logResult.cnt);
				item.setText(5, logResult.err_cnt);
			} else {
				item.setText(1, logResult.exec_time);
			}
		}
	}

	private String getTimeSec(String sTime) {
		String sec = "";
		String min = "";
		String hour = "";

		float result_sec = (float) 0.0;
		String ret_val = new String();

		String[] arrayTime = sTime.split(":");

		if (arrayTime.length == 1) {
			sec = arrayTime[0];
		} else if (arrayTime.length == 2) {
			min = arrayTime[0];
			sec = arrayTime[1];
		} else {
			hour = arrayTime[0];
			min = arrayTime[1];
			sec = arrayTime[2];
		}

		if (!min.equals("")) {
			if (!hour.equals("")) {
				result_sec = Float.parseFloat(hour) * 3600;
			}
			result_sec += (Float.parseFloat(min) * 60);
		}

		result_sec += Float.parseFloat(sec);

		ret_val = String.valueOf(result_sec);

		return ret_val;
	}
}
