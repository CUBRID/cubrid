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
package com.cubrid.cubridmanager.ui.cubrid.database.control;

import java.text.NumberFormat;

import org.eclipse.jface.wizard.WizardPage;
import org.eclipse.swt.SWT;
import org.eclipse.swt.events.FocusEvent;
import org.eclipse.swt.events.FocusListener;
import org.eclipse.swt.events.ModifyEvent;
import org.eclipse.swt.events.ModifyListener;
import org.eclipse.swt.events.SelectionAdapter;
import org.eclipse.swt.events.SelectionEvent;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.widgets.Combo;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.DirectoryDialog;
import org.eclipse.swt.widgets.Group;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Text;
import org.eclipse.ui.PlatformUI;
import org.eclipse.ui.help.IWorkbenchHelpSystem;

import com.cubrid.cubridmanager.core.common.model.EnvInfo;
import com.cubrid.cubridmanager.core.common.model.ServerInfo;
import com.cubrid.cubridmanager.core.cubrid.database.model.DatabaseInfo;
import com.cubrid.cubridmanager.core.cubrid.table.task.ModelUtil.OsInfoType;
import com.cubrid.cubridmanager.help.CubridManagerHelpContextIDs;
import com.cubrid.cubridmanager.ui.cubrid.database.Messages;
import com.cubrid.cubridmanager.ui.spi.CommonTool;
import com.cubrid.cubridmanager.ui.spi.FileNameUtils;
import com.cubrid.cubridmanager.ui.spi.ValidateUtil;
import com.cubrid.cubridmanager.ui.spi.model.CubridServer;

/**
 * 
 * Database general information for creating database
 * 
 * @author pangqiren
 * @version 1.0 - 2009-6-4 created by pangqiren
 */
public class GeneralInfoPage extends
		WizardPage implements
		ModifyListener {

	public static String PAGENAME = "CreateDatabaseWizard/GeneralInfoPage";
	private Text databaseNameText;
	private Text genericNumberOfPageText;
	private Combo pageSizeCombo;
	private Text genericVolumePathText;
	private Text logNumberOfPageText;
	private Text logVolumePathText;
	private CubridServer server = null;
	private String databasePath = "";
	private Text genericVolumeSizeText;
	private Text logVolumeSizeText;

	/**
	 * The constructor
	 */
	public GeneralInfoPage(CubridServer server) {
		super(PAGENAME);
		this.server = server;
		setPageComplete(false);
	}

	/**
	 * Create the control for this page
	 */
	public void createControl(Composite parent) {
		IWorkbenchHelpSystem whs = PlatformUI.getWorkbench().getHelpSystem();
		whs.setHelp(parent, CubridManagerHelpContextIDs.databaseCreate);

		Composite composite = new Composite(parent, SWT.NONE);
		GridLayout layout = new GridLayout();
		layout.marginHeight = 10;
		layout.marginWidth = 10;
		composite.setLayout(layout);
		GridData gridData = new GridData(GridData.FILL_BOTH);
		composite.setLayoutData(gridData);

		createDatabseNameGroup(composite);
		createGenericVolumeGroup(composite);
		createLogVolumeGroup(composite);
		initial();
		setTitle(Messages.titleWizardPageGeneral);
		setMessage(Messages.msgWizardPageGeneral);

		setControl(composite);
	}

	/**
	 * 
	 * Create database name group
	 * 
	 * @param parent
	 */
	private void createDatabseNameGroup(Composite parent) {
		Group generalGroup = new Group(parent, SWT.NONE);
		generalGroup.setText(Messages.grpGeneralInfo);
		GridData gridData = new GridData(GridData.FILL_HORIZONTAL);
		generalGroup.setLayoutData(gridData);
		GridLayout layout = new GridLayout();
		layout.numColumns = 4;
		generalGroup.setLayout(layout);

		Label databaseNameLabel = new Label(generalGroup, SWT.LEFT | SWT.WRAP);
		databaseNameLabel.setText(Messages.lblDbName);
		gridData = new GridData();
		gridData.widthHint = 150;
		databaseNameLabel.setLayoutData(gridData);

		databaseNameText = new Text(generalGroup, SWT.BORDER);
		databaseNameText.setTextLimit(16);
		gridData = new GridData(GridData.FILL_HORIZONTAL);
		gridData.horizontalSpan = 3;
		databaseNameText.setLayoutData(gridData);

		Label pageSizeLabel = new Label(generalGroup, SWT.LEFT | SWT.WRAP);
		pageSizeLabel.setText(Messages.lblPageSize);
		gridData = new GridData();
		gridData.widthHint = 150;
		pageSizeLabel.setLayoutData(gridData);

		pageSizeCombo = new Combo(generalGroup, SWT.DROP_DOWN | SWT.READ_ONLY);
		gridData = new GridData(GridData.FILL_HORIZONTAL);
		gridData.horizontalSpan = 3;
		pageSizeCombo.setLayoutData(gridData);
		pageSizeCombo.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				calcGenericVolumeSize();
				calcGenericPageNum();
				calcLogVolumeSize();
				calcLogPageNum();
			}
		});
	}

	/**
	 * 
	 * Create generic volume information group
	 * 
	 * @param parent
	 */
	private void createGenericVolumeGroup(Composite parent) {
		Group genericVolumeGroup = new Group(parent, SWT.NONE);
		genericVolumeGroup.setText(Messages.grpGenericVolInfo);
		GridLayout layout = new GridLayout();
		layout.numColumns = 4;
		genericVolumeGroup.setLayout(layout);
		GridData gridData = new GridData(GridData.FILL_HORIZONTAL);
		genericVolumeGroup.setLayoutData(gridData);

		Label genericSizeLabel = new Label(genericVolumeGroup, SWT.LEFT
				| SWT.WRAP);
		genericSizeLabel.setText(Messages.lblVolSize);
		gridData = new GridData();
		gridData.widthHint = 150;
		genericSizeLabel.setLayoutData(gridData);

		genericVolumeSizeText = new Text(genericVolumeGroup, SWT.BORDER);
		genericVolumeSizeText.setTextLimit(20);
		gridData = new GridData(GridData.FILL_HORIZONTAL);
		gridData.horizontalSpan = 3;
		genericVolumeSizeText.setLayoutData(gridData);
		genericVolumeSizeText.addFocusListener(new FocusListener() {
			public void focusGained(FocusEvent e) {
				genericVolumeSizeText.addModifyListener(GeneralInfoPage.this);
			}

			public void focusLost(FocusEvent e) {
				genericVolumeSizeText.removeModifyListener(GeneralInfoPage.this);
			}
		});

		Label genericNumPageLabel = new Label(genericVolumeGroup, SWT.LEFT
				| SWT.WRAP);
		genericNumPageLabel.setText(Messages.lblNumOfPages);
		gridData = new GridData();
		gridData.widthHint = 150;
		genericNumPageLabel.setLayoutData(gridData);

		genericNumberOfPageText = new Text(genericVolumeGroup, SWT.BORDER);
		genericNumberOfPageText.setTextLimit(20);
		gridData = new GridData(GridData.FILL_HORIZONTAL);
		gridData.horizontalSpan = 3;
		genericNumberOfPageText.setLayoutData(gridData);
		genericNumberOfPageText.addFocusListener(new FocusListener() {
			public void focusGained(FocusEvent e) {
				genericNumberOfPageText.addModifyListener(GeneralInfoPage.this);
			}

			public void focusLost(FocusEvent e) {
				genericNumberOfPageText.removeModifyListener(GeneralInfoPage.this);
			}
		});

		Label genericVolumePathLabel = new Label(genericVolumeGroup, SWT.LEFT
				| SWT.WRAP);
		genericVolumePathLabel.setText(Messages.lblGenericVolPath);
		gridData = new GridData();
		gridData.widthHint = 150;
		genericVolumePathLabel.setLayoutData(gridData);

		genericVolumePathText = new Text(genericVolumeGroup, SWT.BORDER);
		gridData = new GridData(GridData.FILL_HORIZONTAL);
		gridData.horizontalSpan = 2;
		genericVolumePathText.setLayoutData(gridData);

		Button selectDirectoryButton = new Button(genericVolumeGroup, SWT.NONE);
		selectDirectoryButton.setText(Messages.btnBrowse);
		selectDirectoryButton.setLayoutData(CommonTool.createGridData(1, 1, 80,
				-1));
		selectDirectoryButton.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				DirectoryDialog dlg = new DirectoryDialog(getShell());
				if (databasePath != null && databasePath.trim().length() > 0)
					dlg.setFilterPath(databasePath);
				dlg.setText(Messages.msgSelectDir);
				dlg.setMessage(Messages.msgSelectDir);
				String dir = dlg.open();
				if (dir != null) {
					genericVolumePathText.setText(dir);
				}
			}
		});
		ServerInfo serverInfo = server.getServerInfo();
		if (serverInfo != null && !serverInfo.isLocalServer()) {
			selectDirectoryButton.setEnabled(false);
		}
	}

	/**
	 * 
	 * Create log volume information group
	 * 
	 * @param parent
	 */
	private void createLogVolumeGroup(Composite parent) {
		Group logVolumeGroup = new Group(parent, SWT.NONE);
		logVolumeGroup.setText(Messages.grpLogVolInfo);
		GridLayout layout = new GridLayout();
		layout.numColumns = 4;
		logVolumeGroup.setLayout(layout);
		GridData gridData = new GridData(GridData.FILL_HORIZONTAL);
		logVolumeGroup.setLayoutData(gridData);

		Label logSizeLabel = new Label(logVolumeGroup, SWT.LEFT | SWT.WRAP);
		logSizeLabel.setText(Messages.lblVolSize);
		gridData = new GridData();
		gridData.widthHint = 150;
		logSizeLabel.setLayoutData(gridData);

		logVolumeSizeText = new Text(logVolumeGroup, SWT.BORDER);
		logVolumeSizeText.setTextLimit(20);
		gridData = new GridData(GridData.FILL_HORIZONTAL);
		gridData.horizontalSpan = 3;
		logVolumeSizeText.setLayoutData(gridData);
		logVolumeSizeText.addFocusListener(new FocusListener() {
			public void focusGained(FocusEvent e) {
				logVolumeSizeText.addModifyListener(GeneralInfoPage.this);
			}

			public void focusLost(FocusEvent e) {
				logVolumeSizeText.removeModifyListener(GeneralInfoPage.this);
			}
		});

		Label logNumPageLabel = new Label(logVolumeGroup, SWT.LEFT | SWT.WRAP);
		logNumPageLabel.setText(Messages.lblNumOfPages);
		gridData = new GridData();
		gridData.widthHint = 150;
		logNumPageLabel.setLayoutData(gridData);

		logNumberOfPageText = new Text(logVolumeGroup, SWT.BORDER);
		logNumberOfPageText.setTextLimit(20);
		gridData = new GridData(GridData.FILL_HORIZONTAL);
		gridData.horizontalSpan = 3;
		logNumberOfPageText.setLayoutData(gridData);
		logNumberOfPageText.addFocusListener(new FocusListener() {
			public void focusGained(FocusEvent e) {
				logNumberOfPageText.addModifyListener(GeneralInfoPage.this);
			}

			public void focusLost(FocusEvent e) {
				logNumberOfPageText.removeModifyListener(GeneralInfoPage.this);
			}
		});

		Label logVolumePathLabel = new Label(logVolumeGroup, SWT.LEFT
				| SWT.WRAP);
		logVolumePathLabel.setText(Messages.lblLogVolPath);
		gridData = new GridData();
		gridData.widthHint = 150;
		logVolumePathLabel.setLayoutData(gridData);

		logVolumePathText = new Text(logVolumeGroup, SWT.BORDER);
		gridData = new GridData(GridData.FILL_HORIZONTAL);
		gridData.horizontalSpan = 2;
		logVolumePathText.setLayoutData(gridData);

		Button selectDirectoryButton = new Button(logVolumeGroup, SWT.NONE);
		selectDirectoryButton.setText(Messages.btnBrowse);
		selectDirectoryButton.setLayoutData(CommonTool.createGridData(1, 1, 80,
				-1));
		selectDirectoryButton.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				DirectoryDialog dlg = new DirectoryDialog(getShell());
				if (databasePath != null && databasePath.trim().length() > 0)
					dlg.setFilterPath(databasePath);
				dlg.setText(Messages.msgSelectDir);
				dlg.setMessage(Messages.msgSelectDir);
				String dir = dlg.open();
				if (dir != null) {
					logVolumePathText.setText(dir);
				}
			}
		});
		ServerInfo serverInfo = server.getServerInfo();
		if (serverInfo != null && !serverInfo.isLocalServer()) {
			selectDirectoryButton.setEnabled(false);
		}
	}

	public void modifyText(ModifyEvent e) {

		if (e.widget == genericVolumeSizeText) {
			calcGenericPageNum();
			VolumeInfoPage volumeInfoPage = (VolumeInfoPage) getWizard().getPage(
					VolumeInfoPage.PAGENAME);
			if (volumeInfoPage != null) {
				volumeInfoPage.setVolumeSize(genericVolumeSizeText.getText());
				volumeInfoPage.setVolumePagNum(genericNumberOfPageText.getText());
			}
			SetAutoAddVolumeInfoPage setAutoAddVolumeInfoPage = (SetAutoAddVolumeInfoPage) getWizard().getPage(
					SetAutoAddVolumeInfoPage.PAGENAME);
			if (setAutoAddVolumeInfoPage != null) {
				setAutoAddVolumeInfoPage.setDataVolumeSize(genericVolumeSizeText.getText());
				setAutoAddVolumeInfoPage.setDataPageNum(genericNumberOfPageText.getText());
				setAutoAddVolumeInfoPage.setIndexVolumeSize(genericVolumeSizeText.getText());
				setAutoAddVolumeInfoPage.setIndexPageNum(genericNumberOfPageText.getText());
			}
			logVolumeSizeText.setText(genericVolumeSizeText.getText());
			calcLogPageNum();
		}
		if (e.widget == genericNumberOfPageText) {
			calcGenericVolumeSize();
			VolumeInfoPage volumeInfoPage = (VolumeInfoPage) getWizard().getPage(
					VolumeInfoPage.PAGENAME);
			if (volumeInfoPage != null) {
				volumeInfoPage.setVolumeSize(genericVolumeSizeText.getText());
				volumeInfoPage.setVolumePagNum(genericNumberOfPageText.getText());
			}
			logNumberOfPageText.setText(genericNumberOfPageText.getText());
			calcLogVolumeSize();
		}
		if (e.widget == logVolumeSizeText) {
			calcLogPageNum();
		}
		if (e.widget == logNumberOfPageText) {
			calcLogVolumeSize();
		}

		boolean isValidDatabaseName = true;
		boolean isValidDatabaseNameLength = true;
		String databaseName = databaseNameText.getText();
		String genericVolumeSize = genericVolumeSizeText.getText();
		String genericVolumePath = genericVolumePathText.getText();
		String logSize = logVolumeSizeText.getText();
		String logVolumePath = logVolumePathText.getText();
		String genericPageNum = genericNumberOfPageText.getText();
		String logPageNum = logNumberOfPageText.getText();

		boolean isValidGenericPathName = true;
		boolean isValidLogPathName = true;

		isValidDatabaseName = ValidateUtil.isValidDBName(databaseName);
		isValidGenericPathName = ValidateUtil.isValidPathName(genericVolumePath);
		isValidLogPathName = ValidateUtil.isValidPathName(logVolumePath);
		isValidDatabaseNameLength = ValidateUtil.isValidDbNameLength(databaseName);

		DatabaseInfo databaseInfo = server.getServerInfo().getLoginedUserInfo().getDatabaseInfo(
				databaseName);
		boolean isDatabaseNameAlrExist = databaseInfo != null;
		boolean isValidGenericiVolumeSize = (ValidateUtil.isNumber(genericVolumeSize) || ValidateUtil.isPositiveDouble(genericVolumeSize))
				&& Double.parseDouble(genericVolumeSize) > 0;
		boolean isValidLogSize = (ValidateUtil.isNumber(logSize) || ValidateUtil.isPositiveDouble(logSize))
				&& Double.parseDouble(logSize) > 0;
		boolean isValidGenericPageNum = ValidateUtil.isNumber(genericPageNum)
				&& Double.parseDouble(genericPageNum) > 0;
		boolean isValidLogPageNum = ValidateUtil.isNumber(logPageNum)
				&& Double.parseDouble(logPageNum) > 0;

		if (!isValidDatabaseName) {
			setErrorMessage(Messages.errDbName);
		} else if (!isValidDatabaseNameLength) {
			setErrorMessage(Messages.bind(
					Messages.errDbNameLength,
					new String[] { String.valueOf(ValidateUtil.MAX_DB_NAME_LENGTH - 1) }));
		} else if (isDatabaseNameAlrExist) {
			setErrorMessage(Messages.errDbExist);
		} else if (!isValidGenericiVolumeSize) {
			setErrorMessage(Messages.errGenericVolSize);
		} else if (!isValidGenericPageNum) {
			setErrorMessage(Messages.errGenericPageNum);
		} else if (!isValidGenericPathName) {
			setErrorMessage(Messages.errGenericVolPath);
		} else if (!isValidLogSize) {
			setErrorMessage(Messages.errLogSize);
		} else if (!isValidLogPageNum) {
			setErrorMessage(Messages.errLogPageNum);
		} else if (!isValidLogPathName) {
			setErrorMessage(Messages.errLogVolPath);
		}
		if (e.widget == databaseNameText && isValidDatabaseName
				&& !isDatabaseNameAlrExist && isValidDatabaseNameLength) {
			genericVolumePathText.setText(databasePath
					+ server.getServerInfo().getPathSeparator()
					+ databaseNameText.getText());
			logVolumePathText.setText(databasePath
					+ server.getServerInfo().getPathSeparator()
					+ databaseNameText.getText());
			VolumeInfoPage volumeInfoPage = (VolumeInfoPage) getWizard().getPage(
					VolumeInfoPage.PAGENAME);
			if (volumeInfoPage != null) {
				volumeInfoPage.changeVolumePath();
				volumeInfoPage.changeVolumeTable();
			}
		} else if (e.widget == databaseNameText
				&& (!isValidDatabaseName || isDatabaseNameAlrExist || !isValidDatabaseNameLength)) {
			genericVolumePathText.setText(databasePath);
			logVolumePathText.setText(databasePath);
		}

		boolean isEnabled = isValidDatabaseName && !isDatabaseNameAlrExist
				&& isValidGenericiVolumeSize && isValidGenericiVolumeSize
				&& isValidGenericPathName && isValidLogSize
				&& isValidLogPageNum && isValidLogPathName;
		if (isEnabled) {
			setErrorMessage(null);
		}
		setPageComplete(isEnabled);
	}

	/**
	 * 
	 * Return page number,for example:(1)pageNum=100.01,return 101 (2)
	 * pageNum=100.00,return 100
	 * 
	 * @param pageNum
	 * @return
	 */
	public static long getPageNum(double pageNum) {
		long lPageNum = (long) pageNum;
		if (lPageNum < pageNum) {
			lPageNum = lPageNum + 1;
		}
		return lPageNum;
	}

	/**
	 * 
	 * Calculate volume size
	 * 
	 */
	private void calcGenericPageNum() {
		String genericVolumeSize = genericVolumeSizeText.getText();
		boolean isValidGenericVolumeSize = ValidateUtil.isPositiveDouble(genericVolumeSize)
				|| ValidateUtil.isNumber(genericVolumeSize);
		String pageSizeStr = pageSizeCombo.getText();
		if (pageSizeStr != null && pageSizeStr.trim().length() > 0) {
			int pageSize = Integer.parseInt(pageSizeStr);
			if (isValidGenericVolumeSize) {
				double volumeSize = Double.parseDouble(genericVolumeSize);
				double pageNumber = (1024 * 1024 / pageSize) * volumeSize;
				genericNumberOfPageText.setText(String.valueOf(getPageNum(pageNumber)));
			} else {
				genericNumberOfPageText.setText("");
			}
		}
	}

	/**
	 * 
	 * Calculate generic page number
	 * 
	 */
	private void calcGenericVolumeSize() {
		String genericPageNum = genericNumberOfPageText.getText();
		boolean isValidGenericPageNum = ValidateUtil.isNumber(genericPageNum);
		String pageSizeStr = pageSizeCombo.getText();
		if (pageSizeStr != null && pageSizeStr.trim().length() > 0) {
			int pageSize = Integer.parseInt(pageSizeStr);
			if (isValidGenericPageNum) {
				double pageNum = Double.parseDouble(genericPageNum);
				double size = pageSize * pageNum / (1024 * 1024);
				NumberFormat nf = NumberFormat.getInstance();
				nf.setGroupingUsed(false);
				nf.setMaximumFractionDigits(3);
				nf.setMinimumFractionDigits(3);
				genericVolumeSizeText.setText(nf.format(size));
			} else {
				genericVolumeSizeText.setText("");
			}
		}
	}

	/**
	 * 
	 * Calculate volume size
	 * 
	 */
	private void calcLogPageNum() {
		String logVolumeSize = logVolumeSizeText.getText();
		boolean isValidLogVolumeSize = ValidateUtil.isPositiveDouble(logVolumeSize)
				|| ValidateUtil.isNumber(logVolumeSize);
		String pageSizeStr = pageSizeCombo.getText();
		if (pageSizeStr != null && pageSizeStr.trim().length() > 0) {
			int pageSize = Integer.parseInt(pageSizeStr);
			if (isValidLogVolumeSize) {
				double volumeSize = Double.parseDouble(logVolumeSize);
				double pageNumber = (1024 * 1024 / pageSize) * volumeSize;
				logNumberOfPageText.setText(String.valueOf(getPageNum(pageNumber)));
			} else {
				logNumberOfPageText.setText("");
			}
		}
	}

	/**
	 * 
	 * Calculate log page number
	 * 
	 */
	private void calcLogVolumeSize() {
		String logPageNum = logNumberOfPageText.getText();
		boolean isValidLogPageNum = ValidateUtil.isNumber(logPageNum);
		String pageSizeStr = pageSizeCombo.getText();
		if (pageSizeStr != null && pageSizeStr.trim().length() > 0) {
			int pageSize = Integer.parseInt(pageSizeStr);
			if (isValidLogPageNum) {
				double pageNum = Double.parseDouble(logPageNum);
				double size = pageSize * pageNum / (1024 * 1024);
				NumberFormat nf = NumberFormat.getInstance();
				nf.setGroupingUsed(false);
				nf.setMaximumFractionDigits(3);
				nf.setMinimumFractionDigits(3);
				logVolumeSizeText.setText(nf.format(size));
			} else {
				logVolumeSizeText.setText("");
			}
		}
	}

	/**
	 * 
	 * Initial data
	 * 
	 */
	private void initial() {
		pageSizeCombo.setItems(new String[] { "1024", "2048", "4096", "8192",
				"16384" });
		pageSizeCombo.setText("4096");
		EnvInfo envInfo = server.getServerInfo().getEnvInfo();
		if (envInfo != null) {
			databasePath = envInfo.getDatabaseDir();
			ServerInfo serverInfo = server.getServerInfo();
			if (serverInfo != null
					&& serverInfo.getServerOsInfo() == OsInfoType.NT) {
				databasePath = FileNameUtils.separatorsToWindows(databasePath);
			}
			genericVolumePathText.setText(databasePath);
			logVolumePathText.setText(databasePath);
		}
		genericVolumeSizeText.setText("40");
		logVolumeSizeText.setText("40");
		calcGenericPageNum();
		calcLogPageNum();
		databaseNameText.addModifyListener(this);
	}

	/**
	 * 
	 * Get database name
	 * 
	 * @return
	 */
	public String getDatabaseName() {
		return databaseNameText.getText();
	}

	/**
	 * 
	 * Get generic volume size
	 * 
	 * @return
	 */
	public String getGenericVolumeSize() {
		return genericVolumeSizeText.getText();
	}

	/**
	 * 
	 * Get generic page number
	 * 
	 * @return
	 */
	public String getGenericPageNum() {
		return genericNumberOfPageText.getText();
	}

	/**
	 * 
	 * Get generic page size
	 * 
	 * @return
	 */
	public String getPageSize() {
		return pageSizeCombo.getText();
	}

	/**
	 * 
	 * Get generic volume path
	 * 
	 * @return
	 */
	public String getGenericVolumePath() {
		return genericVolumePathText.getText();
	}

	/**
	 * 
	 * Get log page number
	 * 
	 * @return
	 */
	public String getLogPageNum() {
		return logNumberOfPageText.getText();
	}

	/**
	 * 
	 * Get log volume path
	 * 
	 * @return
	 */
	public String getLogVolumePath() {
		return logVolumePathText.getText();
	}
}
