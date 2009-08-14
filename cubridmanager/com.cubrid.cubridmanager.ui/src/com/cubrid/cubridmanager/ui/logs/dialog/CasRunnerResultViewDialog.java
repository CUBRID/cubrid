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

import org.eclipse.core.runtime.IProgressMonitor;
import org.eclipse.jface.dialogs.IDialogConstants;
import org.eclipse.swt.SWT;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Control;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.Text;

import com.cubrid.cubridmanager.core.common.ITask;
import com.cubrid.cubridmanager.core.logs.model.LogContentInfo;
import com.cubrid.cubridmanager.core.logs.task.GetLogListTask;
import com.cubrid.cubridmanager.core.logs.task.RemoveCasRunnerTmpFileTask;
import com.cubrid.cubridmanager.help.CubridManagerHelpContextIDs;
import com.cubrid.cubridmanager.ui.logs.Messages;
import com.cubrid.cubridmanager.ui.spi.CommonTool;
import com.cubrid.cubridmanager.ui.spi.dialog.CMTitleAreaDialog;
import com.cubrid.cubridmanager.ui.spi.model.DefaultCubridNode;
import com.cubrid.cubridmanager.ui.spi.progress.ExecTaskWithProgress;
import com.cubrid.cubridmanager.ui.spi.progress.TaskExecutor;

/**
 * 
 * The dialog is used to show CasRunnerResult.
 * 
 * @author wuyingshi
 * @version 1.0 - 2009-3-18 created by wuyingshi
 */
public class CasRunnerResultViewDialog extends
		CMTitleAreaDialog {

	private Text textArea = null;
	private Text textFiles = null;
	private Button buttonFirst = null;
	private Button buttonNext = null;
	private Button buttonPrev = null;
	private Button buttonEnd = null;
	private long line_num_to_display = 1000;
	private long line_start = 1;
	private long line_end = line_num_to_display;
	private long line_tot = 0;
	private Button buttonFileBefore = null;
	private Button buttonFileNext = null;
	private int total_result_num = 1;
	private int current_resultfile_index = 1;
	private Composite composite;
	private String path = "";
	private String dbName = "";
	private boolean isSuccess;// If the operation is succeed,it is true,it is
	private DefaultCubridNode selection;

	/**
	 * The constructor
	 * 
	 * @param parentShell
	 */
	public CasRunnerResultViewDialog(Shell parentShell) {
		super(parentShell);
	}

	@Override
	protected Control createDialogArea(Composite parent) {
		Composite parentComp = (Composite) super.createDialogArea(parent);

		composite = new Composite(parentComp, SWT.NONE);
		GridLayout layout = new GridLayout();
		layout.marginHeight = convertVerticalDLUsToPixels(IDialogConstants.VERTICAL_MARGIN);
		layout.marginWidth = convertHorizontalDLUsToPixels(IDialogConstants.HORIZONTAL_MARGIN);
		layout.verticalSpacing = convertVerticalDLUsToPixels(IDialogConstants.VERTICAL_SPACING);
		layout.horizontalSpacing = 0;
		layout.numColumns = 7;
		composite.setLayout(layout);
		composite.setLayoutData(new GridData(GridData.FILL_BOTH));

		//dynamicHelp start
		getHelpSystem().setHelp(parentComp,
				CubridManagerHelpContextIDs.brokerSqlLog);
		//dynamicHelp end		

		GridData gridData41 = CommonTool.createGridData(1, 1, 110, -1);
		gridData41.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		gridData41.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		GridData gridData31 = CommonTool.createGridData(1, 1, 130, -1);
		gridData31.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		gridData31.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		GridData gridData6 = CommonTool.createGridData(1, 1, 60, -1);
		gridData6.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		gridData6.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
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
		GridData gridData1 = CommonTool.createGridData(1, 1, 145, -1);
		gridData1.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		gridData1.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		gridData1.grabExcessHorizontalSpace = true;
		GridData gridData = new GridData();
		gridData.heightHint = -1;
		gridData.widthHint = -1;
		gridData.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData.grabExcessVerticalSpace = true;
		gridData.grabExcessHorizontalSpace = true;
		gridData.horizontalSpan = 7;

		composite.addDisposeListener(new org.eclipse.swt.events.DisposeListener() {
			public void widgetDisposed(org.eclipse.swt.events.DisposeEvent e) {
				final RemoveCasRunnerTmpFileTask task = new RemoveCasRunnerTmpFileTask(
						selection.getServer().getServerInfo());
				task.setFileName(path + "."
						+ String.valueOf(current_resultfile_index - 1));
				task.execute();
			}
		});

		composite.addControlListener(new org.eclipse.swt.events.ControlAdapter() {
			public void controlResized(org.eclipse.swt.events.ControlEvent e) {
				if (composite.getSize().x < 504)
					composite.setSize(504, composite.getSize().y);
				if (composite.getSize().y < 409)
					composite.setSize(composite.getSize().x, 409);
			}
		});
		textArea = new Text(composite, SWT.MULTI | SWT.V_SCROLL | SWT.H_SCROLL
				| SWT.BORDER | SWT.WRAP);
		textArea.setLayoutData(gridData);
		textFiles = new Text(composite, SWT.BORDER);
		textFiles.setEditable(false);
		textFiles.setLayoutData(gridData1);
		buttonFirst = new Button(composite, SWT.NONE);
		buttonFirst.setText("|<");
		buttonFirst.setLayoutData(gridData2);
		buttonFirst.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
			public void widgetSelected(org.eclipse.swt.events.SelectionEvent e) {
				line_start = 1;
				line_end = line_num_to_display;
				connect();

			}
		});
		buttonPrev = new Button(composite, SWT.NONE);
		buttonPrev.setText("<");
		buttonPrev.setLayoutData(gridData3);
		buttonPrev.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
			public void widgetSelected(org.eclipse.swt.events.SelectionEvent e) {
				line_start -= line_num_to_display;
				if (line_start < 1)
					line_start = 1;
				line_end = line_start + (line_num_to_display - 1);
				connect();

			}
		});
		buttonNext = new Button(composite, SWT.NONE);
		buttonNext.setText(">");
		buttonNext.setLayoutData(gridData4);
		buttonNext.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
			public void widgetSelected(org.eclipse.swt.events.SelectionEvent e) {
				line_start = line_end + 1;
				line_end += line_num_to_display;
				connect();

			}
		});
		buttonEnd = new Button(composite, SWT.NONE);
		buttonEnd.setText(">|");
		buttonEnd.setLayoutData(gridData5);
		buttonEnd.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
			public void widgetSelected(org.eclipse.swt.events.SelectionEvent e) {
				line_end = line_tot;
				line_start = ((line_tot - 1) / 1000) * 1000 + 1;
				if (line_start < 1)
					line_start = 1;
				connect();

			}
		});

		buttonFileBefore = new Button(composite, SWT.NONE);
		buttonFileBefore.setText(Messages.button_beforeResultFile);
		buttonFileBefore.setLayoutData(gridData31);
		buttonFileBefore.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
			public void widgetSelected(org.eclipse.swt.events.SelectionEvent e) {
				if (current_resultfile_index == 1)
					return;
				if (current_resultfile_index == 2)
					buttonFileBefore.setEnabled(false);
				String title = "";
				buttonFileNext.setEnabled(true);
				current_resultfile_index--;
				line_start = 1;
				line_end = line_num_to_display;
				title = Messages.title_casRunnerResult
						+ current_resultfile_index + "/" + total_result_num;
				setTitle(title);
				connect();

			}
		});
		buttonFileNext = new Button(composite, SWT.NONE);
		buttonFileNext.setText(Messages.button_nextResultFile);
		buttonFileNext.setLayoutData(gridData41);
		buttonFileNext.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
			public void widgetSelected(org.eclipse.swt.events.SelectionEvent e) {
				if (current_resultfile_index == total_result_num)
					return;
				if (current_resultfile_index == total_result_num - 1)
					buttonFileNext.setEnabled(false);
				String title = "";
				buttonFileBefore.setEnabled(true);
				current_resultfile_index++;
				line_start = 1;
				line_end = line_num_to_display;
				title = Messages.title_casRunnerResult
						+ current_resultfile_index + "/" + total_result_num;
				setTitle(title);
				connect();

			}
		});
		composite.pack();

		setTitle(Messages.title_casRunnerResultDialog);
		setMessage(Messages.msg_casRunnerResultDialog);
		return parentComp;
	}

	/**
	 * each page of log connect
	 * 
	 */
	public void connect() {

		final GetLogListTask task = new GetLogListTask(
				selection.getServer().getServerInfo());

		task.setPath(path + "." + String.valueOf(current_resultfile_index - 1));
		task.setDbName(dbName);
		task.setStart(Long.toString(line_start));
		task.setEnd(Long.toString(line_end));
		task.execute();
		TaskExecutor taskExecutor = new TaskExec();
		taskExecutor.addTask(task);
		new ExecTaskWithProgress(taskExecutor).exec();
		if (isSuccess) {
			LogContentInfo logContentInfo = (LogContentInfo) task.getLogContent();
			this.setinfo(logContentInfo, selection);
		}

	}

	/**
	 * initialize log view table.
	 * 
	 * @param logPath
	 * @param dbName
	 * @param node
	 */
	public void connectInit(String logPath, String dbName,
			DefaultCubridNode node) {
		this.selection = node;
		final GetLogListTask taskGetLog = new GetLogListTask(
				node.getServer().getServerInfo());
		path = logPath;
		taskGetLog.setPath(path + "."
				+ String.valueOf(current_resultfile_index - 1));
		taskGetLog.setDbName(dbName);
		taskGetLog.setStart("1");
		taskGetLog.setEnd("1000");
		TaskExecutor taskExecutor = new TaskExec();
		taskExecutor.addTask(taskGetLog);
		new ExecTaskWithProgress(taskExecutor).exec();
		if (isSuccess) {
			LogContentInfo logContentInfo = (LogContentInfo) taskGetLog.getLogContent();
			this.setinfo(logContentInfo, selection);
		}
	}

	/**
	 * initialize some values
	 * 
	 * @param logPath
	 * @param dbName
	 * @param node
	 */
	public void setinfo(LogContentInfo logContentInfo, DefaultCubridNode node) {
		if (logContentInfo == null) {
			textArea.setText(Messages.msg_nullLogFile);
		} else {
			line_start = Integer.parseInt(logContentInfo.getStart());
			line_end = Integer.parseInt(logContentInfo.getEnd());
			line_tot = Integer.parseInt(logContentInfo.getTotal());

			if (line_start <= 0 && line_end <= 0)
				textArea.setText(Messages.msg_nullLogFile);
			else {
				String lines = "";
				for (int i = 0, n = logContentInfo.getLine().size(); i < n; i++) {
					lines += (String) logContentInfo.getLine().get(i) + "\n";
				}
				textArea.setText(lines);
			}

			textFiles.setText(line_start + "-" + line_end + " (" + line_tot
					+ ")");

			if (line_start <= 1) {
				buttonFirst.setEnabled(false);
				buttonPrev.setEnabled(false);
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
			if (total_result_num > 1)
				buttonFileNext.setEnabled(true);
			else
				buttonFileNext.setEnabled(false);
			buttonFileBefore.setEnabled(false);
		}
	}

	@Override
	protected void constrainShellSize() {
		super.constrainShellSize();
		CommonTool.centerShell(getShell());
		getShell().setText(Messages.title_casRunnerResultDialog);

	}

	@Override
	protected void createButtonsForButtonBar(Composite parent) {
		createButton(parent, IDialogConstants.CANCEL_ID, Messages.button_close,
				true);
	}

	@Override
	protected void buttonPressed(int buttonId) {
		if (buttonId == IDialogConstants.CANCEL_ID) {
		}
		super.buttonPressed(buttonId);
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
}
