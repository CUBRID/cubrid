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

package com.cubrid.cubridmanager.ui.logs.editor;

import java.text.ParseException;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Calendar;
import java.util.Collections;
import java.util.Comparator;
import java.util.Date;
import java.util.List;

import org.apache.log4j.Logger;
import org.eclipse.core.runtime.IProgressMonitor;
import org.eclipse.core.runtime.IStatus;
import org.eclipse.core.runtime.Status;
import org.eclipse.swt.SWT;
import org.eclipse.swt.dnd.Clipboard;
import org.eclipse.swt.dnd.TextTransfer;
import org.eclipse.swt.dnd.Transfer;
import org.eclipse.swt.events.SelectionAdapter;
import org.eclipse.swt.events.SelectionEvent;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.Event;
import org.eclipse.swt.widgets.Listener;
import org.eclipse.swt.widgets.Menu;
import org.eclipse.swt.widgets.MenuItem;
import org.eclipse.swt.widgets.Table;
import org.eclipse.swt.widgets.TableColumn;
import org.eclipse.swt.widgets.TableItem;
import org.eclipse.swt.widgets.Text;
import org.eclipse.ui.IEditorInput;
import org.eclipse.ui.IEditorSite;
import org.eclipse.ui.PartInitException;
import org.eclipse.ui.PlatformUI;
import org.eclipse.ui.help.IWorkbenchHelpSystem;

import com.cubrid.cubridmanager.core.common.ITask;
import com.cubrid.cubridmanager.core.common.log.LogUtil;
import com.cubrid.cubridmanager.core.logs.model.LogContentInfo;
import com.cubrid.cubridmanager.core.logs.model.ManagerLogInfo;
import com.cubrid.cubridmanager.core.logs.model.ManagerLogInfos;
import com.cubrid.cubridmanager.core.logs.task.GetLogListTask;
import com.cubrid.cubridmanager.help.CubridManagerHelpContextIDs;
import com.cubrid.cubridmanager.ui.logs.Messages;
import com.cubrid.cubridmanager.ui.logs.PageUtil;
import com.cubrid.cubridmanager.ui.spi.CommonTool;
import com.cubrid.cubridmanager.ui.spi.CubridEditorPart;
import com.cubrid.cubridmanager.ui.spi.event.CubridNodeChangedEvent;
import com.cubrid.cubridmanager.ui.spi.model.CubridNodeType;
import com.cubrid.cubridmanager.ui.spi.progress.TaskJob;
import com.cubrid.cubridmanager.ui.spi.progress.TaskJobExecutor;

/**
 * 
 * This query editor part is used to view log.
 * 
 * @author wuyingshi
 * @version 1.0 - 2009-3-10 created by wuyingshi
 */
public class LogEditorPart extends
		CubridEditorPart {

	private static final Logger logger = LogUtil.getLogger(LogEditorPart.class);
	public static final String ID = "com.cubrid.cubridmanager.ui.logs.editor.LogEditorPart";
	private boolean isDirty = false;
	private Text pageText = null;
	private Button buttonFirst = null;
	private Button buttonNext = null;
	private Button buttonPrev = null;
	private Button buttonEnd = null;

	private Table table = null;

	private long line_start = 1;
	private long line_end = 100;
	private long line_tot = 0;
	// start page info
	private int pageSize = 100;
	private int cntRecord = 0;
	private PageUtil pageInfo = null;
	private String path = "";
	// end page info
	private static String BLANK = "  ";
	private LogComparator comparator = new LogComparator();
	private List<ManagerLogInfo> accesslogList = new ArrayList<ManagerLogInfo>();
	private List<ManagerLogInfo> errorlogList = new ArrayList<ManagerLogInfo>();

	/**
	 * The constructor
	 */
	public LogEditorPart() {
	}

	@Override
	public void init(IEditorSite site, IEditorInput input) throws PartInitException {
		super.init(site, input);
		if (this.cubridNode != null) {
			CubridNodeType nodeType = this.cubridNode.getType();
			switch (nodeType) {
			case BROKER_SQL_LOG:
				this.setPartName(cubridNode.getParent().getLabel() + "--"
						+ input.getName());
				break;
			case LOGS_BROKER_ACCESS_LOG:
			case LOGS_BROKER_ERROR_LOG:
			case LOGS_BROKER_ADMIN_LOG:
				this.setPartName(cubridNode.getParent().getParent().getLabel()
						+ "--" + input.getName());
				break;
			case LOGS_MANAGER_ACCESS_LOG:
			case LOGS_MANAGER_ERROR_LOG:
			case LOGS_SERVER_DATABASE_LOG:
				this.setPartName(cubridNode.getParent().getLabel() + "--"
						+ input.getName());
				break;
			}
		}
		if (input.getImageDescriptor() != null)
			this.setTitleImage(input.getImageDescriptor().createImage());
	}

	@Override
	public void createPartControl(Composite parent) {
		final Composite composite_log = new Composite(parent, SWT.NONE);
		GridLayout gridLayout_log = new GridLayout();
		gridLayout_log.verticalSpacing = 0;
		gridLayout_log.marginWidth = 0;
		gridLayout_log.marginHeight = 0;
		gridLayout_log.horizontalSpacing = 0;
		gridLayout_log.numColumns = 7;
		composite_log.setLayout(gridLayout_log);

		//dynamicHelp start		
		IWorkbenchHelpSystem whs = PlatformUI.getWorkbench().getHelpSystem();		
		if (this.cubridNode.getType() == CubridNodeType.BROKER_SQL_LOG) {
			whs.setHelp(parent, CubridManagerHelpContextIDs.brokerSqlLog);
		} else {
			whs.setHelp(parent, CubridManagerHelpContextIDs.logEditor);
		}
		//dynamicHelp end
		
		table = new Table(composite_log, SWT.MULTI | SWT.FULL_SELECTION
				| SWT.BORDER);
		table.setHeaderVisible(true);
		GridData gridData = new GridData(SWT.FILL, SWT.FILL, true, true);
		gridData.horizontalSpan = 7;
		gridData.verticalSpan = 10;
		gridData.heightHint = 600;
		table.setLayoutData(gridData);
		table.setLinesVisible(true);

		/*
		 * // fill in context menu
		 */
		CubridNodeType nodeType = this.cubridNode.getType();
		if (nodeType == CubridNodeType.BROKER_SQL_LOG
				|| nodeType == CubridNodeType.LOGS_BROKER_ERROR_LOG
				|| nodeType == CubridNodeType.LOGS_MANAGER_ERROR_LOG
				|| nodeType == CubridNodeType.LOGS_SERVER_DATABASE_LOG) {
			Menu menu = new Menu(parent.getShell(), SWT.POP_UP);
			table.setMenu(menu);
			MenuItem copy = new MenuItem(menu, SWT.PUSH);
			copy.setText(Messages.bind(Messages.context_copy, "Ctrl+C"));
			copy.setAccelerator(SWT.CTRL + 'C');
			copy.addListener(SWT.Selection, new Listener() {
				public void handleEvent(Event event) {
					TextTransfer textTransfer = TextTransfer.getInstance();
					Clipboard clipboard = CommonTool.getClipboard();
					StringBuilder content = new StringBuilder();
					TableItem[] items = table.getSelection();
					for (int i = 0; i < items.length; i++) {
						if (cubridNode.getType() == CubridNodeType.BROKER_SQL_LOG) {
							content.append(items[i].getText(1)
									+ System.getProperty("line.separator"));
						}
						if (cubridNode.getType() == CubridNodeType.LOGS_BROKER_ERROR_LOG) {
							content.append(items[i].getText(1) + BLANK
									+ items[i].getText(2) + BLANK
									+ items[i].getText(3) + BLANK
									+ items[i].getText(4) + BLANK
									+ items[i].getText(5) + BLANK
									+ items[i].getText(6) + BLANK
									+ System.getProperty("line.separator"));
						}
						if (cubridNode.getType() == CubridNodeType.LOGS_MANAGER_ERROR_LOG) {
							content.append(items[i].getText(1) + BLANK
									+ items[i].getText(2) + BLANK
									+ items[i].getText(3) + BLANK
									+ items[i].getText(4) + BLANK
									+ System.getProperty("line.separator"));
						}
						if (cubridNode.getType() == CubridNodeType.LOGS_SERVER_DATABASE_LOG) {
							content.append(items[i].getText(1) + BLANK
									+ items[i].getText(2) + BLANK
									+ items[i].getText(3) + BLANK
									+ items[i].getText(4) + BLANK
									+ items[i].getText(5) + BLANK
									+ items[i].getText(6) + BLANK
									+ System.getProperty("line.separator"));
						}
					}
					String data = content.toString();
					if (data != null && !data.equals("")) {
						clipboard.setContents(new Object[] { data },
								new Transfer[] { textTransfer });
					}
				}
			});
			table.addKeyListener(new org.eclipse.swt.events.KeyAdapter() {
				public void keyPressed(org.eclipse.swt.events.KeyEvent e) {
					if ((e.stateMask & SWT.CTRL) != 0
							&& (e.stateMask & SWT.SHIFT) == 0) {
						if (e.keyCode == 'c') // Run
						{
							TextTransfer textTransfer = TextTransfer.getInstance();
							Clipboard clipboard = CommonTool.getClipboard();
							StringBuilder content = new StringBuilder();

							TableItem[] items = table.getSelection();
							for (int i = 0; i < items.length; i++) {
								if (cubridNode.getType() == CubridNodeType.BROKER_SQL_LOG) {
									content.append(items[i].getText(1)
											+ System.getProperty("line.separator"));
								}
								if (cubridNode.getType() == CubridNodeType.LOGS_BROKER_ERROR_LOG) {
									content.append(items[i].getText(1)
											+ BLANK
											+ items[i].getText(2)
											+ BLANK
											+ items[i].getText(3)
											+ BLANK
											+ items[i].getText(4)
											+ BLANK
											+ items[i].getText(5)
											+ BLANK
											+ items[i].getText(6)
											+ BLANK
											+ System.getProperty("line.separator"));
								}
								if (cubridNode.getType() == CubridNodeType.LOGS_MANAGER_ERROR_LOG) {
									content.append(items[i].getText(1)
											+ BLANK
											+ items[i].getText(2)
											+ BLANK
											+ items[i].getText(3)
											+ BLANK
											+ items[i].getText(4)
											+ BLANK
											+ System.getProperty("line.separator"));
								}
								if (cubridNode.getType() == CubridNodeType.LOGS_SERVER_DATABASE_LOG) {
									content.append(items[i].getText(1)
											+ BLANK
											+ items[i].getText(2)
											+ BLANK
											+ items[i].getText(3)
											+ BLANK
											+ items[i].getText(4)
											+ BLANK
											+ items[i].getText(5)
											+ BLANK
											+ items[i].getText(6)
											+ BLANK
											+ System.getProperty("line.separator"));
								}
							}

							String data = content.toString();
							if (data != null && !data.equals("")) {
								clipboard.setContents(new Object[] { data },
										new Transfer[] { textTransfer });
							}
						}
					}
				}
			});
		}
		// page button
		if (nodeType != CubridNodeType.LOGS_MANAGER_ERROR_LOG
				&& nodeType != CubridNodeType.LOGS_MANAGER_ACCESS_LOG) {
			GridData gridData5 = CommonTool.createGridData(1, 1, 60, -1);
			gridData5.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
			gridData5.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
			GridData gridData4 = CommonTool.createGridData(1, 1, 60, -1);
			gridData4.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
			gridData4.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
			GridData gridData3 = CommonTool.createGridData(1, 1, 60, -1);
			gridData3.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
			gridData3.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
			GridData gridData2 = CommonTool.createGridData(1, 1, 60, -1);
			gridData2.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
			gridData2.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
			GridData gridData1 = CommonTool.createGridData(1, 1, 150, -1);
			gridData1.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
			gridData1.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
			gridData1.grabExcessHorizontalSpace = true;

			pageText = new Text(composite_log, SWT.BORDER);
			pageText.setVisible(true);
			pageText.setEditable(false);
			pageText.setLayoutData(gridData1);

			buttonFirst = new Button(composite_log, SWT.NONE);
			buttonFirst.setVisible(true);
			buttonFirst.setText("|<");
			buttonFirst.setLayoutData(gridData2);
			buttonFirst.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
				public void widgetSelected(
						org.eclipse.swt.events.SelectionEvent e) {
					line_start = 1;
					line_end = 100;
					connect();
				}
			});
			buttonPrev = new Button(composite_log, SWT.NONE);
			buttonPrev.setVisible(true);
			buttonPrev.setText("<");
			buttonPrev.setLayoutData(gridData3);
			buttonPrev.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
				public void widgetSelected(
						org.eclipse.swt.events.SelectionEvent e) {
					line_start -= 100;
					if (line_start < 1)
						line_start = 1;
					line_end = line_start + 99;
					connect();
				}
			});
			buttonNext = new Button(composite_log, SWT.NONE);
			buttonNext.setVisible(true);
			buttonNext.setText(">");
			buttonNext.setLayoutData(gridData4);
			buttonNext.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
				public void widgetSelected(
						org.eclipse.swt.events.SelectionEvent e) {
					line_start += 100;
					if (line_start > line_tot)
						line_start = line_tot;
					line_end = line_start + 99;
					if (line_end > line_tot)
						line_end = line_tot;
					connect();
				}
			});
			buttonEnd = new Button(composite_log, SWT.NONE);
			buttonEnd.setVisible(true);
			buttonEnd.setText(">|");
			buttonEnd.setLayoutData(gridData5);
			buttonEnd.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
				public void widgetSelected(
						org.eclipse.swt.events.SelectionEvent e) {
					line_end = line_tot;
					line_start = line_end - line_tot % 100 + 1;
					connect();
				}
			});
		}
		// manager log page button
		if (nodeType == CubridNodeType.LOGS_MANAGER_ERROR_LOG
				|| nodeType == CubridNodeType.LOGS_MANAGER_ACCESS_LOG) {
			GridData gridData5 = CommonTool.createGridData(1, 1, 60, -1);
			gridData5.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
			gridData5.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
			GridData gridData4 = CommonTool.createGridData(1, 1, 60, -1);
			gridData4.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
			gridData4.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
			GridData gridData3 = CommonTool.createGridData(1, 1, 60, -1);
			gridData3.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
			gridData3.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
			GridData gridData2 = CommonTool.createGridData(1, 1, 60, -1);
			gridData2.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
			gridData2.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
			GridData gridData1 = CommonTool.createGridData(1, 1, 150, -1);
			gridData1.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
			gridData1.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
			gridData1.grabExcessHorizontalSpace = true;

			pageText = new Text(composite_log, SWT.BORDER);
			pageText.setVisible(true);
			pageText.setEditable(false);
			pageText.setLayoutData(gridData1);

			buttonFirst = new Button(composite_log, SWT.NONE);
			buttonFirst.setVisible(true);
			buttonFirst.setText("|<");
			buttonFirst.setLayoutData(gridData2);
			buttonFirst.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
				public void widgetSelected(
						org.eclipse.swt.events.SelectionEvent e) {
					pageInfo.setCurrentPage(1);
					updateManagerLogTable();
				}
			});
			buttonPrev = new Button(composite_log, SWT.NONE);
			buttonPrev.setVisible(true);
			buttonPrev.setText("<");
			buttonPrev.setLayoutData(gridData3);
			buttonPrev.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
				public void widgetSelected(
						org.eclipse.swt.events.SelectionEvent e) {
					pageInfo.setCurrentPage(pageInfo.getCurrentPage() - 1);
					updateManagerLogTable();
				}
			});
			buttonNext = new Button(composite_log, SWT.NONE);
			buttonNext.setVisible(true);
			buttonNext.setText(">");
			buttonNext.setLayoutData(gridData4);
			buttonNext.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
				public void widgetSelected(
						org.eclipse.swt.events.SelectionEvent e) {
					pageInfo.setCurrentPage(pageInfo.getCurrentPage() + 1);
					updateManagerLogTable();

				}
			});
			buttonEnd = new Button(composite_log, SWT.NONE);
			buttonEnd.setVisible(true);
			buttonEnd.setText(">|");
			buttonEnd.setLayoutData(gridData5);
			buttonEnd.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
				public void widgetSelected(
						org.eclipse.swt.events.SelectionEvent e) {
					pageInfo.setCurrentPage(pageInfo.getPages());
					updateManagerLogTable();

				}
			});
		}
		composite_log.pack();
	}

	@Override
	public void setFocus() {
	}

	@Override
	public void doSave(IProgressMonitor monitor) {
		logger.info("do Save " + this.getEditorInput().getName());
		isDirty = false;
		firePropertyChange(PROP_DIRTY);
	}

	@Override
	public void doSaveAs() {
	}

	@Override
	public boolean isDirty() {
		return isDirty;
	}

	@Override
	public boolean isSaveAsAllowed() {
		return false;
	}

	/**
	 * each page of log connect
	 * 
	 */
	public void connect() {
		GetLogListTask task = new GetLogListTask(
				this.cubridNode.getServer().getServerInfo());
		task.setPath(path);
		task.setStart(Long.toString(line_start));
		task.setEnd(Long.toString(line_end));

		TaskJobExecutor taskJobExecutor = new TaskJobExecutor() {
			public IStatus exec(IProgressMonitor monitor) {
				if (monitor.isCanceled()) {
					return Status.CANCEL_STATUS;
				}
				for (final ITask task : taskList) {
					task.execute();
					final String msg = task.getErrorMsg();
					if (monitor.isCanceled()) {
						return Status.CANCEL_STATUS;
					}
					if (msg != null && msg.length() > 0
							&& !monitor.isCanceled()) {
						Display.getDefault().syncExec(new Runnable() {
							public void run() {
								CommonTool.openErrorBox(msg);
							}
						});
						return Status.CANCEL_STATUS;
					} else {
						Display.getDefault().asyncExec(new Runnable() {
							public void run() {
								if (task instanceof GetLogListTask) {
									GetLogListTask getLogListTask = (GetLogListTask) task;
									LogContentInfo logContentInfo = (LogContentInfo) getLogListTask.getLogContent();
									setinfo(logContentInfo);
								}
							}
						});
					}
					if (monitor.isCanceled()) {
						return Status.CANCEL_STATUS;
					}
				}
				return Status.OK_STATUS;
			}
		};
		taskJobExecutor.addTask(task);
		TaskJob job = new TaskJob(Messages.viewLogJobName, taskJobExecutor);
		job.setUser(true);
		job.schedule();
	}

	/**
	 * initialize some values of common logs.
	 * 
	 * @param logContentInfo
	 */
	public void setinfo(LogContentInfo logContentInfo) {
		if (table == null || table.isDisposed()) {
			return;
		}
		table.setRedraw(false);
		while (table.getColumnCount() > 0) {
			if (!table.getColumn(0).isDisposed())
				table.getColumn(0).dispose();
		}
		table.removeAll();
		if (logContentInfo == null) {
			buttonFirst.setEnabled(false);
			buttonPrev.setEnabled(false);
			buttonEnd.setEnabled(false);
			buttonNext.setEnabled(false);

			TableColumn tblColumn = new TableColumn(table, SWT.LEFT);
			tblColumn.setText(Messages.msg_nullLogFile);
			tblColumn.setWidth(500);
		} else {

			path = logContentInfo.getPath();
			line_start = Integer.parseInt(logContentInfo.getStart());
			line_end = Integer.parseInt(logContentInfo.getEnd());
			line_tot = Integer.parseInt(logContentInfo.getTotal());
			pageText.setText(line_start + "-" + line_end + " (" + line_tot
					+ ")");
			if (line_start <= 100) {
				buttonFirst.setEnabled(false);
				buttonPrev.setEnabled(false);
				buttonEnd.setEnabled(false);
				buttonNext.setEnabled(false);
			} else {
				buttonFirst.setEnabled(true);
				buttonPrev.setEnabled(true);
			}
			if (line_end >= line_tot) {
				buttonEnd.setEnabled(false);
				buttonNext.setEnabled(false);
			} else {
				buttonEnd.setEnabled(true);
				buttonNext.setEnabled(true);
			}
			TableItem item;
			if (line_start <= 0 && line_end <= 0) {
				TableColumn tblColumn = new TableColumn(table, SWT.LEFT);
				tblColumn.setText(Messages.msg_nullLogFile);
				tblColumn.setWidth(300);
				item = new TableItem(table, SWT.NONE);
				item.setText(Messages.msg_nullLogFile);
			} else {
				int j = 0;
				if (line_start >= 1) {
					j = (int) line_start;
				}
				if (this.cubridNode.getType() == CubridNodeType.LOGS_BROKER_ACCESS_LOG) {
					TableColumn tblColumn = new TableColumn(table, SWT.LEFT);
					tblColumn.setText(Messages.table_number);
					tblColumn.setWidth(50);
					tblColumn = new TableColumn(table, SWT.LEFT);
					tblColumn.setText(Messages.table_casId);
					tblColumn.setWidth(60);
					tblColumn = new TableColumn(table, SWT.LEFT);
					tblColumn.setText(Messages.table_ip);
					tblColumn.setWidth(150);
					tblColumn = new TableColumn(table, SWT.LEFT);
					tblColumn.setText(Messages.table_startTime);
					tblColumn.setWidth(150);
					tblColumn = new TableColumn(table, SWT.LEFT);
					tblColumn.setText(Messages.table_endTime);
					tblColumn.setWidth(150);
					tblColumn = new TableColumn(table, SWT.LEFT);
					tblColumn.setText(Messages.table_elapsedTime);
					tblColumn.setWidth(150);
					tblColumn = new TableColumn(table, SWT.LEFT);
					tblColumn.setText(Messages.table_processId);
					tblColumn.setWidth(150);
					tblColumn = new TableColumn(table, SWT.LEFT);
					tblColumn.setText(Messages.table_errorInfo);
					tblColumn.setWidth(150);
					String str = "";
					for (int i = 0, n = logContentInfo.getLine().size(); i < n; i++) {
						str = (String) logContentInfo.getLine().get(i);
						item = new TableItem(table, SWT.NONE);
						item.setText(0, Integer.toString(j + i));
						item.setText(1, str.substring(0, 1));
						item.setText(2, str.substring(2, str.indexOf(" - -")));
						item.setText(3, str.substring(str.indexOf(" ", 40) + 1,
								str.indexOf(" ~")));
						item.setText(4, str.substring(str.indexOf(" ~") + 3,
								str.indexOf(" ", str.indexOf(" ~") + 15)));
						item.setText(5, calDate(str.substring(str.indexOf(" ",
								40) + 1, str.indexOf(" ~")), str.substring(
								str.indexOf(" ~") + 3, str.indexOf(" ",
										str.indexOf(" ~") + 15))));
						item.setText(6, str.substring(str.indexOf(" ",
								str.indexOf(" ~") + 15) + 1, str.indexOf(" ",
								str.indexOf(" ", str.indexOf(" ~") + 15) + 1)));
						if (("- -1".equals(str.substring(str.indexOf(" ",
								str.indexOf(" ", str.indexOf(" ~") + 15) + 1) + 1)))) {
							item.setText(7, " ");
						} else {
							item.setText(
									7,
									str.substring(str.indexOf(" ", str.indexOf(
											" ", str.indexOf(" ~") + 15) + 1) + 1));
						}
					}
				} else if (this.cubridNode.getType() == CubridNodeType.LOGS_BROKER_ERROR_LOG
						|| this.cubridNode.getType() == CubridNodeType.LOGS_SERVER_DATABASE_LOG) {

					TableColumn tblColumn = new TableColumn(table, SWT.LEFT);
					tblColumn.setText(Messages.table_number);
					tblColumn.setWidth(50);
					tblColumn = new TableColumn(table, SWT.LEFT);
					tblColumn.setText(Messages.table_time);
					tblColumn.setWidth(150);
					tblColumn = new TableColumn(table, SWT.LEFT);
					tblColumn.setText(Messages.table_errorType);
					tblColumn.setWidth(100);
					tblColumn = new TableColumn(table, SWT.LEFT);
					tblColumn.setText(Messages.table_errorCode);
					tblColumn.setWidth(75);
					tblColumn = new TableColumn(table, SWT.LEFT);
					tblColumn.setText(Messages.table_tranId);
					tblColumn.setWidth(60);
					tblColumn = new TableColumn(table, SWT.LEFT);
					tblColumn.setText(Messages.table_errorId);
					tblColumn.setWidth(70);
					tblColumn = new TableColumn(table, SWT.LEFT);
					tblColumn.setText(Messages.table_errorMsg);
					tblColumn.setWidth(430);
					String str = "";
					int errorNo = 1; // aim at errorlog NO
					for (int i = 0, n = logContentInfo.getLine().size(); i < n; i++) {
						str = (String) logContentInfo.getLine().get(i);
						if (str.trim().length() > 0) {
							if (!"***".equals(str.substring(0, 3))) {

								// if ("Time".equals(str.substring(0, 4))) {
								if (str.toLowerCase().startsWith("time:")) {
									item = new TableItem(table, SWT.NONE);
									item.setText(0, Integer.toString(errorNo));
									item.setText(1, str.substring(6,
											str.indexOf(" - ")));
									item.setText(2, str.substring(
											str.indexOf(" - ") + 3,
											str.indexOf(" *** ")));
									item.setText(
											3,
											str.substring(
													str.indexOf("*** ERROR CODE = ") + 17,
													str.indexOf(", Tran = ")));
									if (str.indexOf(", EID = ") != -1) {
										item.setText(
												4,
												str.substring(
														str.indexOf(", Tran = ") + 9,
														str.indexOf(
																", ",
																str.indexOf(", Tran = ") + 9)));
										item.setText(
												5,
												str.substring(str.indexOf(", EID = ") + 8));
									} else {
										item.setText(
												4,
												str.substring(str.indexOf(", Tran = ") + 9));
										item.setText(5, "");
									}
									if (i + 1 < n
											&& !(((String) logContentInfo.getLine().get(
													i + 1)).toLowerCase().startsWith("time:"))) {
										item.setText(
												6,
												(String) logContentInfo.getLine().get(
														i + 1));
										i++;
									}
									errorNo++;
								} else {
									item = new TableItem(table, SWT.NONE);
									item.setText(
											6,
											(String) logContentInfo.getLine().get(
													i));
								}
							}
						}
					}

				} else if (this.cubridNode.getType() == CubridNodeType.LOGS_BROKER_ADMIN_LOG) {
					TableColumn tblColumn = new TableColumn(table, SWT.LEFT);
					tblColumn.setText(Messages.table_number);
					tblColumn.setWidth(50);
					tblColumn = new TableColumn(table, SWT.CENTER);
					tblColumn.setText(Messages.table_time);
					tblColumn.setWidth(150);
					tblColumn = new TableColumn(table, SWT.CENTER);
					tblColumn.setText(Messages.table_status);
					tblColumn.setWidth(100);
					String str = "";
					for (int i = 0, n = logContentInfo.getLine().size(); i < n; i++) {
						str = (String) logContentInfo.getLine().get(i);
						item = new TableItem(table, SWT.NONE);
						item.setText(0, Integer.toString(j + i));
						item.setText(1, str.substring(0, 20));
						item.setText(2, str.substring(20));
					}
				} else {
					TableColumn tblColumn = new TableColumn(table, SWT.LEFT);
					tblColumn.setText(Messages.table_number);
					tblColumn.setWidth(50);
					tblColumn = new TableColumn(table, SWT.LEFT);
					tblColumn.setText(Messages.table_content);
					tblColumn.setWidth(900);
					for (int i = 0, n = logContentInfo.getLine().size(); i < n; i++) {
						item = new TableItem(table, SWT.NONE);
						item.setText(0, Integer.toString(j + i));
						item.setText(1,
								(String) logContentInfo.getLine().get(i));
					}
				}
			}
		}
		table.setRedraw(true);
		for (int i = 0, n = table.getColumnCount(); i < n; i++) {
			table.getColumn(i).pack();
		}
	}

	/**
	 * initialize some values of manager logs.
	 * 
	 * @param managerLogInfos
	 * @param label
	 */
	public void setManagerLogInfo(ManagerLogInfos managerLogInfos, String label) {
		table.setRedraw(false);
		while (table.getColumnCount() > 0) {
			if (!table.getColumn(0).isDisposed())
				table.getColumn(0).dispose();
		}
		table.removeAll();
		if (managerLogInfos == null) {
			TableColumn tblColumn = new TableColumn(table, SWT.LEFT);
			tblColumn.setText(Messages.msg_nullLogFile);
			tblColumn.setWidth(500);
		} else {

			accesslogList = managerLogInfos.getAccessLog().getManagerLogInfoList();
			errorlogList = managerLogInfos.getErrorLog().getManagerLogInfoList();
			if (this.cubridNode.getType() == CubridNodeType.LOGS_MANAGER_ACCESS_LOG) {
				pageInfo = new PageUtil(accesslogList.size(), pageSize);
				pageInfo.setCurrentPage(1);

			} else if (this.cubridNode.getType() == CubridNodeType.LOGS_MANAGER_ERROR_LOG) {
				pageInfo = new PageUtil(errorlogList.size(), pageSize);
				pageInfo.setCurrentPage(1);

			}
			TableColumn col = new TableColumn(table, SWT.LEFT);

			col.setText(Messages.table_number);
			col.setWidth(200);

			col = new TableColumn(table, SWT.LEFT);

			col.setText(Messages.table_user);
			col.setWidth(200);
			col.addSelectionListener(new SelectionAdapter() {
				public void widgetSelected(SelectionEvent event) {
					TableColumn column = (TableColumn) event.widget;
					comparator.setColumn(0);
					comparator.reverseDirection();
					table.setSortColumn(column);
					table.setSortDirection(comparator.getDirection() == 0 ? SWT.UP
							: SWT.DOWN);
					updateManagerLogTable();
				}
			});

			col = new TableColumn(table, SWT.LEFT);
			col.setText(Messages.table_taskName); //$NON-NLS-1$
			col.setWidth(300);
			col.addSelectionListener(new SelectionAdapter() {
				public void widgetSelected(SelectionEvent event) {
					TableColumn column = (TableColumn) event.widget;
					comparator.setColumn(1);
					comparator.reverseDirection();
					table.setSortColumn(column);
					table.setSortDirection(comparator.getDirection() == 0 ? SWT.UP
							: SWT.DOWN);
					updateManagerLogTable();
				}
			});

			col = new TableColumn(table, SWT.LEFT);
			col.setText(Messages.table_time); //$NON-NLS-1$
			col.setWidth(300);
			col.addSelectionListener(new SelectionAdapter() {
				public void widgetSelected(SelectionEvent event) {
					TableColumn column = (TableColumn) event.widget;
					comparator.setColumn(2);
					comparator.reverseDirection();
					table.setSortColumn(column);
					table.setSortDirection(comparator.getDirection() == 0 ? SWT.UP
							: SWT.DOWN);
					updateManagerLogTable();
				}
			});
			if (this.cubridNode.getType() == CubridNodeType.LOGS_MANAGER_ERROR_LOG) {
				col = new TableColumn(table, SWT.LEFT);
				col.setText(Messages.table_description);
				col.setWidth(400);
				col.addSelectionListener(new SelectionAdapter() {
					public void widgetSelected(SelectionEvent event) {
						TableColumn column = (TableColumn) event.widget;
						comparator.setColumn(3);
						comparator.reverseDirection();
						table.setSortColumn(column);
						table.setSortDirection(comparator.getDirection() == 0 ? SWT.UP
								: SWT.DOWN);
						updateManagerLogTable();
					}
				});
			}
			updateManagerLogTable();
		}
		table.setRedraw(true);
		for (int i = 0, n = table.getColumnCount(); i < n; i++) {
			table.getColumn(i).pack();
		}
	}

	/**
	 * update manager log to table.s
	 * 
	 */
	public void updateManagerLogTable() {
		table.setRedraw(false);
		table.removeAll();

		if (this.cubridNode.getType() == CubridNodeType.LOGS_MANAGER_ERROR_LOG) {
			if (errorlogList.size() > 1) {
				Collections.sort(errorlogList, comparator);
				int begin = (pageInfo.getCurrentPage() - 1)
						* pageInfo.getPageSize();
				int last = begin + pageInfo.getPageSize();
				int index = (pageInfo.getCurrentPage() - 1)
						* pageInfo.getPageSize() + 1;

				if (begin + 1 <= 1) {
					buttonFirst.setEnabled(false);
					buttonPrev.setEnabled(false);
				} else {
					buttonFirst.setEnabled(true);
					buttonPrev.setEnabled(true);
				}
				if ((pageInfo.getTotalRs() < last ? pageInfo.getTotalRs()
						: last) >= pageInfo.getTotalRs()) {
					buttonEnd.setEnabled(false);
					buttonNext.setEnabled(false);
				} else {
					buttonEnd.setEnabled(true);
					buttonNext.setEnabled(true);
				}

				pageText.setText((begin + 1)
						+ "-"
						+ (pageInfo.getTotalRs() < last ? pageInfo.getTotalRs()
								: last) + " (" + pageInfo.getTotalRs() + ")");
				for (int i = begin; i < last && i < pageInfo.getTotalRs(); i++) {
					ManagerLogInfo lfi = (ManagerLogInfo) errorlogList.get(i);
					TableItem item = new TableItem(table, SWT.NONE);
					item.setText(0, Integer.toString(index + i - begin));
					item.setText(1, lfi.getUser());
					item.setText(2, lfi.getTaskName());
					item.setText(3, lfi.getTime());
					item.setText(4, lfi.getErrorNote());
				}
			} else {
				buttonFirst.setEnabled(false);
				buttonPrev.setEnabled(false);
				buttonEnd.setEnabled(false);
				buttonNext.setEnabled(false);
				pageInfo = new PageUtil(cntRecord, pageSize);
				pageInfo.setCurrentPage(1);
				TableItem item = new TableItem(table, SWT.NONE);
				item.setText(0, Messages.msg_nullLogFile);

			}
		} else {
			if (accesslogList.size() > 1) {
				Collections.sort(accesslogList, comparator);

				int begin = (pageInfo.getCurrentPage() - 1)
						* pageInfo.getPageSize();
				int last = begin + pageInfo.getPageSize();
				int index = (pageInfo.getCurrentPage() - 1)
						* pageInfo.getPageSize() + 1;

				if (begin + 1 <= 1) {
					buttonFirst.setEnabled(false);
					buttonPrev.setEnabled(false);
				} else {
					buttonFirst.setEnabled(true);
					buttonPrev.setEnabled(true);
				}
				if ((pageInfo.getTotalRs() < last ? pageInfo.getTotalRs()
						: last) >= pageInfo.getTotalRs()) {
					buttonEnd.setEnabled(false);
					buttonNext.setEnabled(false);
				} else {
					buttonEnd.setEnabled(true);
					buttonNext.setEnabled(true);
				}

				pageText.setText((begin + 1)
						+ "-"
						+ (pageInfo.getTotalRs() < last ? pageInfo.getTotalRs()
								: last) + " (" + pageInfo.getTotalRs() + ")");
				for (int i = begin; i < last && i < pageInfo.getTotalRs(); i++) {
					ManagerLogInfo lfi = (ManagerLogInfo) accesslogList.get(i);
					TableItem item = new TableItem(table, SWT.NONE);
					item.setText(0, Integer.toString(index + i - begin));
					item.setText(1, lfi.getUser());
					item.setText(2, lfi.getTaskName());
					item.setText(3, lfi.getTime());

				}
			} else {
				buttonFirst.setEnabled(false);
				buttonPrev.setEnabled(false);
				buttonEnd.setEnabled(false);
				buttonNext.setEnabled(false);
				pageInfo = new PageUtil(cntRecord, pageSize);
				pageInfo.setCurrentPage(1);
				TableItem item = new TableItem(table, SWT.NONE);
				item.setText(0, Messages.msg_nullLogFile);

			}

		}
		table.setRedraw(true);
		for (int i = 0, n = table.getColumnCount(); i < n; i++) {
			table.getColumn(i).pack();
		}
	}

	/**
	 * calculate dates difference
	 * 
	 * @param beginstr
	 * @param endstr
	 * @return
	 */
	public String calDate(String beginstr, String endstr) {
		String result = "";
		Calendar c1 = Calendar.getInstance();
		Calendar c2 = Calendar.getInstance();
		SimpleDateFormat my_time = new SimpleDateFormat("yyyy/MM/dd HH:mm:ss");

		Date temp1, temp2;
		long l1 = 0, l2 = 0, l = 0;
		int h = 0, m = 0, s = 0;
		try {
			temp1 = my_time.parse(beginstr);
			temp2 = my_time.parse(endstr);
			c1.setTime(temp1);
			c2.setTime(temp2);
			l1 = c1.getTimeInMillis();
			l2 = c2.getTimeInMillis();
			l = l2 - l1;
			h = (int) (l / (60 * 60 * 1000));
			m = (int) ((l % (60 * 60 * 1000)) / (60 * 1000));
			s = (int) (((l % (60 * 60 * 1000)) % (60 * 1000)) / 1000);
			result = String.valueOf(h) + ":" + String.valueOf(m) + ":"
					+ String.valueOf(s);
		} catch (ParseException e) {
			logger.error(e.getMessage(), e);
			result = "";
		}
		return result;
	}

	/**
	 * send when CUBRID node object
	 * 
	 * @param e
	 */
	public void nodeChanged(CubridNodeChangedEvent e) {

	}

	/**
	 * log of compare class
	 * 
	 * @author wuyingshi 2009-3-10
	 */

	static class LogComparator implements
			Comparator<ManagerLogInfo> {
		private int column = 2; // time
		private int direction = 0; // asc

		/**
		 * compare two object.
		 * 
		 * @param obj1
		 * @param obj2
		 * @return
		 */
		public int compare(ManagerLogInfo obj1, ManagerLogInfo obj2) {
			int rc = 0;
			switch (column) {
			case 0:
				rc = obj1.getUser().compareTo(obj2.getUser());
				break;
			case 1:
				rc = obj1.getTaskName().compareTo(obj2.getTaskName());
				break;
			case 2:
				SimpleDateFormat dateFormat = new SimpleDateFormat(
						"yyyy/MM/dd HH:mm:ss");
				try {
					Date date1 = dateFormat.parse(obj1.getTime());
					Date date2 = dateFormat.parse(obj2.getTime());
					rc = date1.compareTo(date2);
				} catch (ParseException e) {
					rc = 0;
				}
				break;
			case 3:
				rc = obj1.getErrorNote().compareTo(obj2.getErrorNote());
				break;
			}

			if (direction == 1) {
				rc = -rc;
			}
			return rc;
		}

		/**
		 * set the column.
		 * 
		 * @param column
		 */
		public void setColumn(int column) {
			this.column = column;
		}

		/**
		 * set the direction.
		 * 
		 * @param direction
		 */
		public void setDirection(int direction) {
			this.direction = direction;
		}

		/**
		 * get the direction.
		 * 
		 * @return
		 */
		public int getDirection() {
			return this.direction;
		}

		/**
		 * reverse the direction
		 */
		public void reverseDirection() {
			direction = 1 - direction;
		}
	}

}
