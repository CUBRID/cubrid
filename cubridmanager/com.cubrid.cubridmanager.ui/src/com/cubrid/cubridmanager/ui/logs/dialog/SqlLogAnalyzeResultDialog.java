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

package com.cubrid.cubridmanager.ui.logs.dialog;

import java.io.BufferedWriter;
import java.io.File;
import java.io.FileNotFoundException;
import java.io.FileWriter;
import java.io.IOException;
import java.util.Arrays;
import java.util.Comparator;
import java.util.List;

import org.apache.log4j.Logger;
import org.eclipse.core.runtime.IProgressMonitor;
import org.eclipse.jface.dialogs.Dialog;
import org.eclipse.jface.dialogs.IDialogConstants;
import org.eclipse.jface.viewers.ColumnWeightData;
import org.eclipse.jface.viewers.TableLayout;
import org.eclipse.swt.SWT;
import org.eclipse.swt.events.SelectionAdapter;
import org.eclipse.swt.events.SelectionEvent;
import org.eclipse.swt.events.SelectionListener;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Control;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.FileDialog;
import org.eclipse.swt.widgets.Group;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.Table;
import org.eclipse.swt.widgets.TableColumn;
import org.eclipse.swt.widgets.TableItem;
import org.eclipse.swt.widgets.Text;
import org.eclipse.ui.PlatformUI;

import com.cubrid.cubridmanager.core.broker.model.BrokerInfos;
import com.cubrid.cubridmanager.core.common.ITask;
import com.cubrid.cubridmanager.core.common.log.LogUtil;
import com.cubrid.cubridmanager.core.logs.model.AnalyzeCasLogResultInfo;
import com.cubrid.cubridmanager.core.logs.model.AnalyzeCasLogResultList;
import com.cubrid.cubridmanager.core.logs.model.AnalyzeCasLogTopResultInfo;
import com.cubrid.cubridmanager.core.logs.model.GetExecuteCasRunnerResultInfo;
import com.cubrid.cubridmanager.core.logs.model.LogInfo;
import com.cubrid.cubridmanager.core.logs.task.GetCasLogTopResultTask;
import com.cubrid.cubridmanager.core.logs.task.GetExecuteCasRunnerContentResultTask;
import com.cubrid.cubridmanager.help.CubridManagerHelpContextIDs;
import com.cubrid.cubridmanager.ui.logs.Messages;
import com.cubrid.cubridmanager.ui.logs.action.LogViewAction;
import com.cubrid.cubridmanager.ui.spi.CommonTool;
import com.cubrid.cubridmanager.ui.spi.dialog.CMTitleAreaDialog;
import com.cubrid.cubridmanager.ui.spi.model.CubridDatabase;
import com.cubrid.cubridmanager.ui.spi.model.DefaultCubridNode;
import com.cubrid.cubridmanager.ui.spi.progress.ExecTaskWithProgress;
import com.cubrid.cubridmanager.ui.spi.progress.TaskExecutor;

/**
 * 
 * The dialog is used to Analyze sql log.
 * 
 * @author wuyingshi
 * @version 1.0 - 2009-3-18 created by wuyingshi
 */
public class SqlLogAnalyzeResultDialog extends
		CMTitleAreaDialog {
	private static final Logger logger = LogUtil.getLogger(LogViewAction.class);

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
	private String resultFile = null;
	private boolean option = false;
	private int currentResultIndex;
	private Composite composite;
	private CubridDatabase database = null;
	private AnalyzeCasLogResultList analyzeCasLogResultList = null;
	private DefaultCubridNode node = null;
	private boolean isSuccess;// If the operation is succeed,it is true,it is
	private String nextLine = "\r\n";

	/**
	 * The constructor
	 * 
	 * @param parentShell
	 */
	public SqlLogAnalyzeResultDialog(Shell parentShell) {
		super(parentShell);
	}

	@Override
	protected Control createDialogArea(Composite parent) {
		Composite parentComp = (Composite) super.createDialogArea(parent);
		composite = new Composite(parentComp, SWT.NONE);
		composite.setLayoutData(new GridData(GridData.FILL_BOTH));
		GridLayout layout = new GridLayout();
		layout.marginHeight = convertVerticalDLUsToPixels(IDialogConstants.VERTICAL_MARGIN);
		layout.marginWidth = convertHorizontalDLUsToPixels(IDialogConstants.HORIZONTAL_MARGIN);
		layout.verticalSpacing = convertVerticalDLUsToPixels(IDialogConstants.VERTICAL_SPACING);
		layout.horizontalSpacing = convertHorizontalDLUsToPixels(IDialogConstants.HORIZONTAL_SPACING);
		layout.numColumns = 2;
		composite.setLayout(layout);

		//dynamicHelp start
		getHelpSystem().setHelp(parentComp,
				CubridManagerHelpContextIDs.brokerSqlLog);
		//dynamicHelp end		

		createLogFileGroup();
		createGroupQuery();
		createAnalyzeResultGroup();

		setTitle(Messages.title_sqlLogAnalyzeResultDialog);
		setMessage(Messages.msg_sqlLogAnalyzeResultDialog);
		return parentComp;
	}

	@Override
	protected void constrainShellSize() {
		super.constrainShellSize();
		CommonTool.centerShell(getShell());
		getShell().setText(Messages.title_sqlLogAnalyzeResultDialog);
	}

	@Override
	protected void createButtonsForButtonBar(Composite parent) {
		createButton(parent, IDialogConstants.CANCEL_ID, Messages.button_close,
				true);
	}

	/**
	 * This method initializes analyze result group
	 * 
	 */
	private void createAnalyzeResultGroup() {
		GridData gridData5 = new GridData(GridData.FILL_BOTH);
		gridData5.heightHint = -1;
		gridData5.widthHint = -1;
		groupAnalyzeResult = new Group(composite, SWT.NONE);
		groupAnalyzeResult.setText(Messages.label_analysisResult);
		groupAnalyzeResult.setLayout(new GridLayout());
		groupAnalyzeResult.setLayoutData(gridData5);
		createAnalyzeResultTable();
	}

	/**
	 * This method initializes log file group
	 * 
	 */
	private void createLogFileGroup() {

		GridData gridData2 = new GridData(GridData.FILL_BOTH);
		gridData2.heightHint = 18;
		gridData2.horizontalSpan = 3;
		gridData2.widthHint = 467;

		GridLayout gridLayout1 = new GridLayout();
		gridLayout1.numColumns = 3;

		groupBroker = new Group(composite, SWT.NONE);
		groupBroker.setText(Messages.label_logFile);
		groupBroker.setLayout(gridLayout1);
		groupBroker.setLayoutData(new GridData(GridData.FILL_HORIZONTAL));

		label = new Label(groupBroker, SWT.NONE);
		label.setText(Messages.label_casLogFile);
		label.setLayoutData(gridData2);
	}

	/**
	 * This method initializes table
	 * 
	 */
	private void createAnalyzeResultTable() {
		GridData gridData6 = new GridData(GridData.FILL_BOTH);
		gridData6.heightHint = 432;
		gridData6.widthHint = 450;
		table = new Table(groupAnalyzeResult, SWT.FULL_SELECTION | SWT.MULTI);
		table.setHeaderVisible(true);
		table.setLayoutData(gridData6);
		table.setLinesVisible(true);
		TableLayout tlayout = new TableLayout();

		if (option) {
			tlayout.addColumnData(new ColumnWeightData(20, 60, true));
			tlayout.addColumnData(new ColumnWeightData(20, 60, true));
			table.setLayout(tlayout);
			table.addSelectionListener(new SelectionAdapter() {
				public void widgetSelected(SelectionEvent e) {
					int selectioncount = table.getSelectionCount();
					int ResultCount = analyzeCasLogResultList.getLogFileInfoList().size();
					StringBuffer queryString = new StringBuffer("");
					AnalyzeCasLogResultInfo logResult;
					for (int j = 0; j < selectioncount; j++) {
						String qindex = table.getSelection()[j].getText(0);
						for (int i = 0; i < ResultCount; i++) {
							logResult = (AnalyzeCasLogResultInfo) (analyzeCasLogResultList.getLogFileInfoList().get(i));
							if (qindex.equals(logResult.getQindex())) {
								currentResultIndex = i;
								queryString = connect(logResult.getQindex(),
										queryString);
								queryString.append(logResult.getQueryString());
								queryString.append(nextLine);
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
					TableColumn column = (TableColumn) e.widget;
					String selected_row_index = "";
					if (table.getSelectionCount() > 0) {
						TableItem item = table.getSelection()[0];
						selected_row_index = item.getText(0);
					}
					Object[] obj = analyzeCasLogResultList.getLogFileInfoList().toArray();
					Arrays.sort(obj, new Comparator<Object>() {
						public int compare(Object o1, Object o2) {
							int ret_val;
							int it1, it2;
							String t1 = (String) ((AnalyzeCasLogResultInfo) o1).getQindex();
							String t2 = (String) ((AnalyzeCasLogResultInfo) o2).getQindex();
							t1 = t1.substring(2, t1.indexOf(']'));
							t2 = t2.substring(2, t2.indexOf(']'));

							try {
								it1 = Integer.parseInt(t1);
								it2 = Integer.parseInt(t2);
								if (desc)
									ret_val = it1 - it2;
								else
									ret_val = it2 - it1;
							} catch (NumberFormatException ee) {
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
					insertArrayToTableSort(obj);

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
					table.setSortColumn(column);
					table.setSortDirection(desc ? SWT.DOWN : SWT.UP);
				}

				public void widgetDefaultSelected(SelectionEvent e) {
				}
			});

			TableColumn exec_time = new TableColumn(table, SWT.LEFT);
			exec_time.addSelectionListener(new SelectionListener() {
				boolean desc = false;

				public void widgetSelected(SelectionEvent e) {
					// provide sorting function when column clicked.
					TableColumn column = (TableColumn) e.widget;
					// Memory selected row's index value.
					String selected_row_index = "";
					if (table.getSelectionCount() > 0) {
						TableItem item = table.getSelection()[0];
						selected_row_index = item.getText(0);
					}

					// Convert to array and using array.sort fuction.
					Object[] obj = analyzeCasLogResultList.getLogFileInfoList().toArray();
					Arrays.sort(obj, new Comparator<Object>() {
						public int compare(Object o1, Object o2) {
							int ret_val;
							float ft1, ft2;
							String t1 = getTimeSec((String) ((AnalyzeCasLogResultInfo) o1).getExecTime());
							String t2 = getTimeSec((String) ((AnalyzeCasLogResultInfo) o2).getExecTime());

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
							} catch (NumberFormatException ee) {
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
					insertArrayToTableSort(obj);

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
					table.setSortColumn(column);
					table.setSortDirection(desc ? SWT.DOWN : SWT.UP);
				}

				public void widgetDefaultSelected(SelectionEvent e) {
				}
			});

			qindex.setText(Messages.table_index);
			exec_time.setText(Messages.table_transactionExeTime);
			qindex.setWidth(100);
			exec_time.setWidth(100);
		} else {
			tlayout.addColumnData(new ColumnWeightData(13, 60, true));
			tlayout.addColumnData(new ColumnWeightData(14, 60, true));
			tlayout.addColumnData(new ColumnWeightData(14, 60, true));
			tlayout.addColumnData(new ColumnWeightData(14, 60, true));
			tlayout.addColumnData(new ColumnWeightData(20, 60, true));
			tlayout.addColumnData(new ColumnWeightData(20, 60, true));

			table.setLayout(tlayout);
			table.addSelectionListener(new SelectionAdapter() {
				public void widgetSelected(SelectionEvent e) {
					int selectioncount = table.getSelectionCount();
					int ResultCount = analyzeCasLogResultList.getLogFileInfoList().size();
					StringBuffer queryString = new StringBuffer("");
					AnalyzeCasLogResultInfo logResult;
					for (int j = 0; j < selectioncount; j++) {
						String qindex = table.getSelection()[j].getText(0);

						for (int i = 0; i < ResultCount; i++) {
							logResult = (AnalyzeCasLogResultInfo) (analyzeCasLogResultList.getLogFileInfoList().get(i));
							if (qindex.equals(logResult.getQindex())) {
								currentResultIndex = i;

								queryString = connect(logResult.getQindex(),
										queryString);

								queryString.append(logResult.getQueryString());
								queryString.append(nextLine);
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
					TableColumn column = (TableColumn) e.widget;
					// Memory value that selected index
					String selected_row_index = "";
					if (table.getSelectionCount() > 0) {
						TableItem item = table.getSelection()[0];
						selected_row_index = item.getText(0);
					}

					// Convert to array, and use "array.sort" function
					Object[] obj = analyzeCasLogResultList.getLogFileInfoList().toArray();
					Arrays.sort(obj, new Comparator<Object>() {
						public int compare(Object o1, Object o2) {
							int ret_val;
							int it1, it2;
							String t1 = (String) ((AnalyzeCasLogResultInfo) o1).getQindex();
							String t2 = (String) ((AnalyzeCasLogResultInfo) o2).getQindex();
							t1 = t1.substring(2, t1.indexOf(']'));
							t2 = t2.substring(2, t2.indexOf(']'));

							try {
								it1 = Integer.parseInt(t1);
								it2 = Integer.parseInt(t2);
								if (desc)
									ret_val = it1 - it2;
								else
									ret_val = it2 - it1;
							} catch (NumberFormatException ee) {
								if (desc)
									ret_val = t1.compareTo(t2);
								else
									ret_val = t2.compareTo(t1);
							}

							return ret_val;
						}
					});
					table.removeAll();
					insertArrayToTableSort(obj);

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
					table.setSortColumn(column);
					table.setSortDirection(desc ? SWT.DOWN : SWT.UP);
				}

				public void widgetDefaultSelected(SelectionEvent e) {
				}
			});

			TableColumn max = new TableColumn(table, SWT.LEFT);
			max.addSelectionListener(new SelectionListener() {
				boolean desc = false;

				public void widgetSelected(SelectionEvent e) {
					TableColumn column = (TableColumn) e.widget;
					String selected_row_index = "";
					if (table.getSelectionCount() > 0) {
						TableItem item = table.getSelection()[0];
						selected_row_index = item.getText(0);
					}

					Object[] obj = analyzeCasLogResultList.getLogFileInfoList().toArray();
					Arrays.sort(obj, new Comparator<Object>() {
						public int compare(Object o1, Object o2) {
							int ret_val;
							float ft1, ft2;
							String t1 = getTimeSec((String) ((AnalyzeCasLogResultInfo) o1).getMax());
							String t2 = getTimeSec((String) ((AnalyzeCasLogResultInfo) o2).getMax());

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
							} catch (NumberFormatException ee) {
								if (desc)
									ret_val = t1.compareTo(t2);
								else
									ret_val = t2.compareTo(t1);
							}

							return ret_val;
						}
					});
					table.removeAll();
					insertArrayToTableSort(obj);

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
					table.setSortColumn(column);
					table.setSortDirection(desc ? SWT.DOWN : SWT.UP);
				}

				public void widgetDefaultSelected(SelectionEvent e) {
				}
			});

			TableColumn min = new TableColumn(table, SWT.LEFT);
			min.addSelectionListener(new SelectionListener() {
				boolean desc = false;

				public void widgetSelected(SelectionEvent e) {
					TableColumn column = (TableColumn) e.widget;
					String selected_row_index = "";
					if (table.getSelectionCount() > 0) {
						TableItem item = table.getSelection()[0];
						selected_row_index = item.getText(0);
					}

					Object[] obj = analyzeCasLogResultList.getLogFileInfoList().toArray();
					Arrays.sort(obj, new Comparator<Object>() {
						public int compare(Object o1, Object o2) {
							int ret_val;
							float ft1, ft2;
							String t1 = getTimeSec((String) ((AnalyzeCasLogResultInfo) o1).getMin());
							String t2 = getTimeSec((String) ((AnalyzeCasLogResultInfo) o2).getMin());

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
							} catch (NumberFormatException ee) {
								if (desc)
									ret_val = t1.compareTo(t2);
								else
									ret_val = t2.compareTo(t1);
							}

							return ret_val;
						}
					});
					table.removeAll();
					insertArrayToTableSort(obj);

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
					table.setSortColumn(column);
					table.setSortDirection(desc ? SWT.DOWN : SWT.UP);
				}

				public void widgetDefaultSelected(SelectionEvent e) {
				}
			});

			TableColumn avg = new TableColumn(table, SWT.LEFT);
			avg.addSelectionListener(new SelectionListener() {
				boolean desc = false;

				public void widgetSelected(SelectionEvent e) {
					TableColumn column = (TableColumn) e.widget;
					String selected_row_index = "";
					if (table.getSelectionCount() > 0) {
						TableItem item = table.getSelection()[0];
						selected_row_index = item.getText(1);
					}

					Object[] obj = analyzeCasLogResultList.getLogFileInfoList().toArray();
					Arrays.sort(obj, new Comparator<Object>() {
						public int compare(Object o1, Object o2) {
							int ret_val;
							float ft1, ft2;
							String t1 = getTimeSec((String) ((AnalyzeCasLogResultInfo) o1).getAvg());
							String t2 = getTimeSec((String) ((AnalyzeCasLogResultInfo) o2).getAvg());

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
							} catch (NumberFormatException ee) {
								if (desc)
									ret_val = t1.compareTo(t2);
								else
									ret_val = t2.compareTo(t1);
							}

							return ret_val;
						}
					});
					table.removeAll();
					insertArrayToTableSort(obj);

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
					table.setSortColumn(column);
					table.setSortDirection(desc ? SWT.DOWN : SWT.UP);
				}

				public void widgetDefaultSelected(SelectionEvent e) {
				}
			});

			TableColumn cnt = new TableColumn(table, SWT.LEFT);
			cnt.addSelectionListener(new SelectionListener() {
				boolean desc = false;

				public void widgetSelected(SelectionEvent e) {
					TableColumn column = (TableColumn) e.widget;
					String selected_row_index = "";
					if (table.getSelectionCount() > 0) {
						TableItem item = table.getSelection()[0];
						selected_row_index = item.getText(1);
					}

					Object[] obj = analyzeCasLogResultList.getLogFileInfoList().toArray();
					Arrays.sort(obj, new Comparator<Object>() {
						public int compare(Object o1, Object o2) {
							int ret_val;
							float ft1, ft2;
							String t1 = (String) ((AnalyzeCasLogResultInfo) o1).getCnt();
							String t2 = (String) ((AnalyzeCasLogResultInfo) o2).getCnt();

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
							} catch (NumberFormatException ee) {
								if (desc)
									ret_val = t1.compareTo(t2);
								else
									ret_val = t2.compareTo(t1);
							}

							return ret_val;
						}
					});
					table.removeAll();
					insertArrayToTableSort(obj);

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
					table.setSortColumn(column);
					table.setSortDirection(desc ? SWT.DOWN : SWT.UP);
				}

				public void widgetDefaultSelected(SelectionEvent e) {
				}
			});
			TableColumn err = new TableColumn(table, SWT.LEFT);
			err.addSelectionListener(new SelectionListener() {
				boolean desc = false;

				public void widgetSelected(SelectionEvent e) {
					TableColumn column = (TableColumn) e.widget;
					String selected_row_index = "";
					if (table.getSelectionCount() > 0) {
						TableItem item = table.getSelection()[0];
						selected_row_index = item.getText(1);
					}

					Object[] obj = analyzeCasLogResultList.getLogFileInfoList().toArray();
					Arrays.sort(obj, new Comparator<Object>() {
						public int compare(Object o1, Object o2) {
							int ret_val;
							float ft1, ft2;
							String t1 = (String) ((AnalyzeCasLogResultInfo) o1).getErr();
							String t2 = (String) ((AnalyzeCasLogResultInfo) o2).getErr();

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
							} catch (NumberFormatException ee) {
								if (desc)
									ret_val = t1.compareTo(t2);
								else
									ret_val = t2.compareTo(t1);
							}

							return ret_val;
						}
					});
					table.removeAll();
					insertArrayToTableSort(obj);

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
					table.setSortColumn(column);
					table.setSortDirection(desc ? SWT.DOWN : SWT.UP);
				}

				public void widgetDefaultSelected(SelectionEvent e) {
				}
			});

			qindex.setText(Messages.table_index);
			max.setText(Messages.table_max);
			max.setAlignment(SWT.RIGHT);
			min.setText(Messages.table_min);
			min.setAlignment(SWT.RIGHT);
			avg.setText(Messages.table_avg);
			avg.setAlignment(SWT.RIGHT);
			cnt.setText(Messages.table_totalCount);
			cnt.setAlignment(SWT.RIGHT);
			err.setText(Messages.table_errCount);
			err.setAlignment(SWT.RIGHT);

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
		GridData gridData22 = new GridData(GridData.FILL_HORIZONTAL);
		gridData22.heightHint = 26;
		gridData22.widthHint = 122;

		GridData gridData21 = new GridData(GridData.FILL_BOTH);
		gridData21.horizontalSpan = 3;
		gridData21.widthHint = 432;
		gridData21.heightHint = 150;

		GridData gridData11 = new GridData(GridData.FILL_BOTH);
		gridData11.horizontalSpan = 3;
		gridData11.widthHint = 432;
		gridData11.heightHint = 250;

		GridLayout gridLayout2 = new GridLayout();
		gridLayout2.numColumns = 3;
		gridLayout2.horizontalSpacing = 45;

		GridData gridData = new GridData(GridData.FILL_BOTH);
		gridData.verticalSpan = 2;

		groupQuery = new Group(composite, SWT.NONE);
		groupQuery.setText("");
		groupQuery.setLayout(gridLayout2);
		groupQuery.setLayoutData(gridData);

		label1 = new Label(groupQuery, SWT.NONE);
		label1.setText(Messages.label_logContents);

		textQuery = new Text(groupQuery, SWT.BORDER | SWT.READ_ONLY
				| SWT.V_SCROLL | SWT.H_SCROLL);
		textQuery.setEditable(true);
		textQuery.setLayoutData(gridData11);

		label2 = new Label(groupQuery, SWT.NONE);
		label2.setText(Messages.label_executeResult);

		textQueryResult = new Text(groupQuery, SWT.BORDER | SWT.READ_ONLY
				| SWT.V_SCROLL | SWT.H_SCROLL);
		textQueryResult.setLayoutData(gridData21);

		buttonRunOriginalQuery = new Button(groupQuery, SWT.NONE);
		buttonRunOriginalQuery.setEnabled(false);
		buttonRunOriginalQuery.setText(Messages.button_executeOriginalQuery);

		Label dummy5 = new Label(groupQuery, SWT.NONE);
		dummy5.setLayoutData(gridData22);

		buttonRunOriginalQuery.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				String queryString = textQuery.getText();
				LogInfo logInfo = null;

				String targetBroker = null;
				if (node.getId().indexOf("Sql log/") < 0) {
					targetBroker = node.getParent().getLabel();
				} else {
					targetBroker = node.getParent().getParent().getLabel();
				}
				logInfo = (LogInfo) node.getAdapter(LogInfo.class);
				List<String> allDatabaseList = node.getServer().getServerInfo().getAllDatabaseList();
				BrokerInfos brokerInfos = node.getServer().getServerInfo().getBrokerInfos();
				CasRunnerConfigDialog casRunnerConfigDialog = new CasRunnerConfigDialog(
						getShell());
				casRunnerConfigDialog.setBrokerInfos(brokerInfos);
				casRunnerConfigDialog.setAllDatabaseList(allDatabaseList);
				casRunnerConfigDialog.setLogInfo(logInfo);
				casRunnerConfigDialog.setTargetBroker(targetBroker);
				casRunnerConfigDialog.create();
				if (casRunnerConfigDialog.open() == Dialog.OK) {
					final GetExecuteCasRunnerContentResultTask task = new GetExecuteCasRunnerContentResultTask(
							node.getServer().getServerInfo());
					task.setBrokerName(CasRunnerConfigDialog.getBrokerName());
					task.setUserName(CasRunnerConfigDialog.getUserName());
					task.setPasswd(CasRunnerConfigDialog.getPassword());
					task.setNumThread(CasRunnerConfigDialog.getNumThread());
					task.setRepeatCount(casRunnerConfigDialog.getNumRepeatCount());
					String isShowqueryresult = "";
					String isShowqueryplan = "";
					if (casRunnerConfigDialog.isShowqueryresult()) {
						isShowqueryresult = "yes";
						isShowqueryplan = casRunnerConfigDialog.isShowqueryresult() ? "yes"
								: "no";
					} else {
						isShowqueryresult = "no";
						isShowqueryplan = "no";
					}
					task.setShowQueryResult(isShowqueryresult);
					task.setShowQueryResult(isShowqueryplan);
					task.setDbName(CasRunnerConfigDialog.getDbname());
					task.setExecuteLogFile("no");
					String[] queryStringArr = queryString.split("\\r\\n");
					task.setLogstring(queryStringArr);
					TaskExecutor taskExecutor = new TaskExec();
					taskExecutor.addTask(task);
					new ExecTaskWithProgress(taskExecutor).exec();
					if (isSuccess) {
						GetExecuteCasRunnerResultInfo getExecuteCasRunnerResultInfo = (GetExecuteCasRunnerResultInfo) task.getContent();
						StringBuffer result = new StringBuffer("");
						for (int i = 0, n = getExecuteCasRunnerResultInfo.getResult().size(); i < n; i++) {
							result.append(getExecuteCasRunnerResultInfo.getResult().get(
									i)
									+ "\n");
						}
						String logPath = null;
						logPath = getExecuteCasRunnerResultInfo.getQueryResultFile();

						if (queryString.length() > 0) {
							textQueryResult.setText(result.toString());
							if (casRunnerConfigDialog.isShowqueryresult()) {
								CasRunnerResultViewDialog casRunnerResultViewDialog = new CasRunnerResultViewDialog(
										getShell());
								casRunnerResultViewDialog.create();
								casRunnerResultViewDialog.connectInit(logPath,
										CasRunnerConfigDialog.getDbname(), node);
								casRunnerResultViewDialog.getShell().setSize(
										665, 555);
								casRunnerResultViewDialog.open();
							}
						}
					}
				}
			}
		});
		buttonSaveToFile = new Button(groupQuery, SWT.NONE);
		buttonSaveToFile.setEnabled(false);
		buttonSaveToFile.setText(Messages.button_saveLogString);
		buttonSaveToFile.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				String savedFilename = saveAsFile();
				if (savedFilename.equals(""))
					return;
				AnalyzeCasLogResultInfo analyzeCasLogResultInfo = (AnalyzeCasLogResultInfo) (analyzeCasLogResultList.getLogFileInfoList().get(currentResultIndex));
				if (savedFilename.length() > 0) {
					analyzeCasLogResultInfo.setSavedFileName(savedFilename);
				}
			}
		});
	}

	/**
	 * 
	 * save as file
	 * 
	 * 
	 */
	private String saveAsFile() {
		String fileName = "";
		Shell sShell = null;
		FileDialog dialog = new FileDialog(
				PlatformUI.getWorkbench().getActiveWorkbenchWindow().getShell(),
				SWT.SAVE | SWT.APPLICATION_MODAL);
		dialog.setFilterExtensions(new String[] { "*.clg", "*.txt", "*.*" });
		dialog.setFilterNames(new String[] { "CAS Log File(*.clg)",
				"Text File(*.txt)", "All" });
		String result = dialog.open();
		if (result != null) {
			File file = new File(result);
			if (file.exists()) {
				if (!CommonTool.openConfirmBox(result
						+ Messages.msg_overwriteFile)) {
					return "";
				}
			}
			BufferedWriter bw = null;
			try {
				bw = new BufferedWriter(new FileWriter(file));
				String text = textQuery.getText();
				bw.write(text);
				fileName = result;
			} catch (FileNotFoundException e1) {
				CommonTool.openErrorBox(sShell, e1.getMessage());
				return "";
			} catch (IOException e1) {
				CommonTool.openErrorBox(sShell, e1.getMessage());
				return "";
			} finally {
				if (bw != null) {
					try {
						bw.close();
					} catch (IOException e) {
						logger.error(e.getMessage(), e);
					}
				}
			}
		}
		return fileName;
	}

	/**
	 * open a dialog to set the save file
	 * 
	 * @return
	 */
	public static FileDialog openFileSaveDialog() {
		FileDialog dialog = new FileDialog(
				PlatformUI.getWorkbench().getActiveWorkbenchWindow().getShell(),
				SWT.SAVE | SWT.APPLICATION_MODAL);
		dialog.setFilterExtensions(new String[] { "*.sql", "*.txt", "*.*" });
		dialog.setFilterNames(new String[] { "SQL File", "Text File", "All" });
		File curdir = new File(".");
		try {
			dialog.setFilterPath(curdir.getCanonicalPath());
		} catch (IOException e) {
			dialog.setFilterPath(".");
		}
		return dialog;
	}

	/**
	 * insert AnalyzeCasLogResult information to table.
	 * 
	 * @param analyzeCasLogResultList
	 */
	public void insertArrayToTable(
			AnalyzeCasLogResultList analyzeCasLogResultList) {
		if (analyzeCasLogResultList.getLogFileInfoList() != null) {
			int itemcount = analyzeCasLogResultList.getLogFileInfoList().size();
			for (int i = 0; i < itemcount; i++) {
				AnalyzeCasLogResultInfo logResult = (AnalyzeCasLogResultInfo) (analyzeCasLogResultList.getLogFileInfoList().get(i));
				TableItem item = new TableItem(table, SWT.NONE);
				item.setText(0, logResult.getQindex());
				if (!option) {
					item.setText(1, logResult.getMax());
					item.setText(2, logResult.getMin());
					item.setText(3, logResult.getAvg());
					item.setText(4, logResult.getCnt());
					item.setText(5, logResult.getErr());
				} else {
					item.setText(1, logResult.getExecTime());
				}
			}
		}
	}

	/**
	 * insert sorted AnalyzeCasLogResult information to table.
	 * 
	 * @param obj
	 */
	public void insertArrayToTableSort(Object[] obj) {
		int itemcount = obj.length;
		for (int i = 0; i < itemcount; i++) {
			AnalyzeCasLogResultInfo logResult = (AnalyzeCasLogResultInfo) (obj[i]);
			TableItem item = new TableItem(table, SWT.NONE);
			item.setText(0, logResult.getQindex());
			if (!option) {
				item.setText(1, logResult.getMax());
				item.setText(2, logResult.getMin());
				item.setText(3, logResult.getAvg());
				item.setText(4, logResult.getCnt());
				item.setText(5, logResult.getErr());
			} else {
				item.setText(1, logResult.getExecTime());
			}
		}
	}

	/**
	 * get time of time_format:"%H:%M:%S"}
	 * 
	 * @param sTime
	 * @return
	 */
	private String getTimeSec(String sTime) {
		String sec = "";
		String min = "";
		String hour = "";

		float result_sec = (float) 0.0;
		String ret_val = "";

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

	/**
	 * each page of log connect
	 * 
	 * @param qindex
	 * @param queryString
	 * @return
	 */
	public StringBuffer connect(String qindex, StringBuffer queryString) {

		final GetCasLogTopResultTask task = new GetCasLogTopResultTask(
				node.getServer().getServerInfo());

		task.setFileName(resultFile);
		task.setQindex(qindex);
		TaskExecutor taskExecutor = new TaskExec();
		taskExecutor.addTask(task);
		new ExecTaskWithProgress(taskExecutor).exec();
		if (isSuccess) {
			AnalyzeCasLogTopResultInfo analyzeCasLogTopResultInfo = (AnalyzeCasLogTopResultInfo) task.getAnalyzeCasLogTopResultList();

			for (int i = 0, n = analyzeCasLogTopResultInfo.getLogString().size(); i < n; i++) {
				queryString.append(analyzeCasLogTopResultInfo.getLogString().get(
						i)
						+ "\n");
			}
		}
		return queryString;
	}

	/**
	 * 
	 * Set label.
	 * 
	 * @param selectedStringList
	 */
	public void setLabel(List<String> selectedStringList) {
		if (selectedStringList.size() > 1) {
			String str = (String) selectedStringList.get(0);
			int i = str.lastIndexOf("_");
			str = str.substring(0, i);
			label.setText(str);
		} else {
			label.setText((String) selectedStringList.get(0));
		}

	}

	/**
	 * get database.
	 * 
	 * @return
	 */
	public CubridDatabase getDatabase() {
		return database;
	}

	/**
	 * set database.
	 * 
	 * @param database
	 */
	public void setDatabase(CubridDatabase database) {
		this.database = database;
	}

	/**
	 * get the analyzeCasLogResultList.
	 * 
	 * @return
	 */
	public AnalyzeCasLogResultList getAnalyzeCasLogResultList() {
		return analyzeCasLogResultList;
	}

	/**
	 * set the analyzeCasLogResultList.
	 * 
	 * @param analyzeCasLogResultList
	 */
	public void setAnalyzeCasLogResultList(
			AnalyzeCasLogResultList analyzeCasLogResultList) {
		this.analyzeCasLogResultList = analyzeCasLogResultList;
	}

	/**
	 * get the resultFile.
	 * 
	 * @return
	 */
	public String getResultFile() {
		return resultFile;
	}

	/**
	 * set the resultFile.
	 * 
	 * @param resultFile
	 */
	public void setResultFile(String resultFile) {
		this.resultFile = resultFile;
	}

	/**
	 * get the node.
	 * 
	 * @return
	 */
	public DefaultCubridNode getNode() {
		return node;
	}

	/**
	 * set the node.
	 * 
	 * @param node
	 */
	public void setNode(DefaultCubridNode node) {
		this.node = node;
	}

	/**
	 * A common type which extends the type TaakExecutor and overrides the
	 * method exec.Generally ,it can be used in an action or other type of which
	 * there is no dialog
	 */
	private class TaskExec extends
			TaskExecutor {

		/**
		 * Override method
		 * 
		 * @param monitor
		 * @return
		 */

		public boolean exec(final IProgressMonitor monitor) {
			isSuccess = true;
			Display display = Display.getDefault();

			if (monitor.isCanceled()) {
				isSuccess = false;
				return isSuccess;
			}

			for (ITask task : taskList) {
				task.execute();
				final String msg = task.getErrorMsg();
				if (monitor.isCanceled()) {
					return false;
				}
				if (msg != null && msg.length() > 0 && !monitor.isCanceled()) {
					isSuccess = false;
					display.syncExec(new Runnable() {
						public void run() {
							CommonTool.openErrorBox(msg);
						}
					});
					isSuccess = false;
					return isSuccess;
				}
				if (monitor.isCanceled()) {
					isSuccess = false;
					return isSuccess;
				}
			}

			return isSuccess;
		}
	}

	/**
	 * 
	 * Return whether is option
	 * 
	 * @return
	 */
	public boolean isOption() {
		return option;
	}

	/**
	 * 
	 * Set whether is option
	 * 
	 * @param option
	 */
	public void setOption(boolean option) {
		this.option = option;
	}
}
