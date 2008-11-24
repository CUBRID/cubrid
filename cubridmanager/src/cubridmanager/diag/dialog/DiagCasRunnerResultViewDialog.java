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

import org.eclipse.swt.widgets.Dialog;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.SWT;
import org.eclipse.swt.widgets.Text;
import org.eclipse.swt.widgets.Button;

import cubridmanager.ClientSocket;
import cubridmanager.CommonTool;
import cubridmanager.Messages;
import cubridmanager.MainRegistry;
import cubridmanager.cubrid.view.CubridView;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.widgets.Label;

public class DiagCasRunnerResultViewDialog extends Dialog {

	private Shell sShell = null; // @jve:decl-index=0:visual-constraint="8,11"
	private Text textArea = null;
	private Text textFiles = null;
	private Button buttonFirst = null;
	private Button buttonNext = null;
	private Button buttonPrev = null;
	private Button buttonEnd = null;
	private Button buttonClose = null;
	private String result_filepath = null;
	private static long line_num_to_display = 1000;
	public static long line_start = 1;
	public static long line_end = line_num_to_display;
	public static long line_tot = 0;
	private boolean ret = true;
	private Button buttonFileBefore = null;
	private Button buttonFileNext = null;
	public int total_result_num = 1;
	public int current_resultfile_index = 1;

	public DiagCasRunnerResultViewDialog(Shell parent) {
		super(parent);
	}

	/**
	 * This method initializes sShell
	 * 
	 */
	private void createSShell() {
		GridData gridData41 = new org.eclipse.swt.layout.GridData();
		gridData41.horizontalSpan = 2;
		gridData41.widthHint = 135;
		GridData gridData31 = new org.eclipse.swt.layout.GridData();
		gridData31.verticalSpan = 1;
		gridData31.widthHint = 135;
		gridData31.horizontalSpan = 2;
		GridData gridData6 = new org.eclipse.swt.layout.GridData();
		gridData6.widthHint = 65;
		GridData gridData5 = new org.eclipse.swt.layout.GridData();
		gridData5.widthHint = 65;
		GridData gridData4 = new org.eclipse.swt.layout.GridData();
		gridData4.widthHint = 65;
		GridData gridData3 = new GridData();
		gridData3.widthHint = 65;
		GridData gridData2 = new GridData();
		gridData2.widthHint = 65;
		gridData2.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		gridData2.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		gridData2.grabExcessHorizontalSpace = true;
		GridData gridData1 = new GridData();
		gridData1.widthHint = 85;
		gridData1.grabExcessHorizontalSpace = true;
		GridData gridData = new GridData();
		gridData.heightHint = -1;
		gridData.widthHint = -1;
		gridData.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData.grabExcessVerticalSpace = true;
		gridData.grabExcessHorizontalSpace = true;
		gridData.horizontalSpan = 6;
		GridLayout gridLayout = new GridLayout();
		gridLayout.numColumns = 6;
		sShell = new Shell(SWT.APPLICATION_MODAL | SWT.SHELL_TRIM);
		sShell.setSize(new org.eclipse.swt.graphics.Point(504, 409));
		sShell.setLayout(gridLayout);
		sShell.addDisposeListener(new org.eclipse.swt.events.DisposeListener() {
			public void widgetDisposed(org.eclipse.swt.events.DisposeEvent e) {
				String msg = new String(); // Remove left result file.
				ClientSocket cs = new ClientSocket();
				msg = "filename:" + result_filepath + "\n";
				cs.SendClientMessage(sShell, msg, "removecasrunnertmpfile");
			}
		});

		sShell.addControlListener(new org.eclipse.swt.events.ControlAdapter() {
			public void controlResized(org.eclipse.swt.events.ControlEvent e) {
				if (sShell.getSize().x < 504)
					sShell.setSize(504, sShell.getSize().y);
				if (sShell.getSize().y < 409)
					sShell.setSize(sShell.getSize().x, 409);
			}
		});
		textArea = new Text(sShell, SWT.MULTI | SWT.V_SCROLL | SWT.H_SCROLL
				| SWT.BORDER | SWT.WRAP);
		textArea.setLayoutData(gridData);
		textFiles = new Text(sShell, SWT.BORDER);
		textFiles.setEditable(false);
		textFiles.setLayoutData(gridData1);
		buttonFirst = new Button(sShell, SWT.NONE);
		buttonFirst.setText("|<");
		buttonFirst.setLayoutData(gridData2);
		buttonFirst.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						line_start = 1;
						line_end = line_num_to_display;
						setinfo();
					}
				});
		buttonPrev = new Button(sShell, SWT.NONE);
		buttonPrev.setText("<");
		buttonPrev.setLayoutData(gridData3);
		buttonPrev.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						line_start -= line_num_to_display;
						if (line_start < 1)
							line_start = 1;
						line_end = line_start + (line_num_to_display - 1);
						setinfo();
					}
				});
		buttonNext = new Button(sShell, SWT.NONE);
		buttonNext.setText(">");
		buttonNext.setLayoutData(gridData4);
		buttonNext.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						line_start = line_end + 1;
						line_end += line_num_to_display;
						setinfo();
					}
				});
		buttonEnd = new Button(sShell, SWT.NONE);
		buttonEnd.setText(">|");
		buttonEnd.setLayoutData(gridData5);
		buttonEnd.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						line_end = line_tot;
						line_start = ((line_tot - 1) / 1000) * 1000 + 1;
						if (line_start < 1)
							line_start = 1;
						setinfo();
					}
				});
		buttonClose = new Button(sShell, SWT.NONE);
		buttonClose.setText(Messages.getString("BUTTON.CLOSE"));
		buttonClose.setLayoutData(gridData6);
		buttonClose.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						ret = true;
						sShell.dispose();
					}
				});
		Label label = new Label(sShell, SWT.NONE);
		Label label1 = new Label(sShell, SWT.NONE);
		buttonFileBefore = new Button(sShell, SWT.NONE);
		buttonFileBefore.setText(Messages.getString("BUTTON.BEFORERESULTFILE"));
		buttonFileBefore.setLayoutData(gridData31);
		buttonFileBefore.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						if (current_resultfile_index == 1)
							return;
						if (current_resultfile_index == 2)
							buttonFileBefore.setEnabled(false);
						String title = new String();
						buttonFileNext.setEnabled(true);
						current_resultfile_index--;
						line_start = 1;
						line_end = line_num_to_display;
						title = Messages.getString("TITLE.CASRUNNERRESULT")
								+ current_resultfile_index + "/"
								+ total_result_num;
						sShell.setText(title);
						setinfo();
					}
				});
		buttonFileNext = new Button(sShell, SWT.NONE);
		buttonFileNext.setText(Messages.getString("BUTTON.NEXTRESULTFILE"));
		buttonFileNext.setLayoutData(gridData41);
		buttonFileNext.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						if (current_resultfile_index == total_result_num)
							return;
						if (current_resultfile_index == total_result_num - 1)
							buttonFileNext.setEnabled(false);
						String title = new String();
						buttonFileBefore.setEnabled(true);
						current_resultfile_index++;
						line_start = 1;
						line_end = line_num_to_display;
						title = Messages.getString("TITLE.CASRUNNERRESULT")
								+ current_resultfile_index + "/"
								+ total_result_num;
						sShell.setText(title);
						setinfo();
					}
				});
		sShell.pack();
	}

	public boolean doModal(String filepath) {
		if (filepath == null) {
			return false;
		}
		String title = new String();
		title = Messages.getString("TITLE.CASRUNNERRESULT")
				+ current_resultfile_index + "/" + total_result_num;
		createSShell();
		CommonTool.centerShell(sShell);
		if (total_result_num > 1)
			buttonFileNext.setEnabled(true);
		else
			buttonFileNext.setEnabled(false);

		buttonFileBefore.setEnabled(false);

		sShell.open();
		result_filepath = filepath;
		line_start = 1;
		line_end = line_num_to_display;
		sShell.setText(title);

		setinfo();

		Display display = sShell.getDisplay();
		while (!sShell.isDisposed()) {
			if (!display.readAndDispatch())
				display.sleep();
		}
		return ret;
	}

	private void setinfo() {
		String strMsg = "";
		strMsg += "dbname:" + CubridView.Current_db + "\n";
		strMsg += "path:" + result_filepath + ".";
		strMsg += String.valueOf(current_resultfile_index - 1);
		strMsg += "\n";
		strMsg += "start:" + line_start + "\n";
		strMsg += "end:" + line_end + "\n";

		ClientSocket cs = new ClientSocket();
		if (!cs.SendBackGround(sShell, strMsg, "viewlog", Messages
				.getString("WAITING.GETTINGLOGINFORM"))) {
			CommonTool.ErrorBox(sShell, cs.ErrorMsg);
			ret = false;
			sShell.dispose();
		}

		if (line_start <= 0 && line_end <= 0)
			textArea.setText(Messages.getString("MSG.NULLLOGFILE"));
		else {
			StringBuffer lines = new StringBuffer("");
			for (int i = 0, n = MainRegistry.Tmpchkrst.size(); i < n; i++) {
				lines.append((String) MainRegistry.Tmpchkrst.get(i)).append(
						"\n");
			}
			textArea.setText(lines.toString());
		}
		textFiles.setText(line_start + "-" + line_end + " (" + line_tot + ")");

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
	}
}
