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
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

import org.eclipse.jface.dialogs.IPageChangedListener;
import org.eclipse.jface.dialogs.PageChangedEvent;
import org.eclipse.jface.viewers.StructuredSelection;
import org.eclipse.jface.viewers.TableViewer;
import org.eclipse.jface.wizard.IWizardPage;
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
import org.eclipse.swt.layout.RowLayout;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.widgets.Combo;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.DirectoryDialog;
import org.eclipse.swt.widgets.Group;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Table;
import org.eclipse.swt.widgets.TableItem;
import org.eclipse.swt.widgets.Text;
import org.eclipse.ui.PlatformUI;
import org.eclipse.ui.help.IWorkbenchHelpSystem;

import com.cubrid.cubridmanager.core.common.model.EnvInfo;
import com.cubrid.cubridmanager.core.common.model.ServerInfo;
import com.cubrid.cubridmanager.core.cubrid.table.task.ModelUtil.OsInfoType;
import com.cubrid.cubridmanager.help.CubridManagerHelpContextIDs;
import com.cubrid.cubridmanager.ui.cubrid.database.Messages;
import com.cubrid.cubridmanager.ui.spi.CommonTool;
import com.cubrid.cubridmanager.ui.spi.FileNameUtils;
import com.cubrid.cubridmanager.ui.spi.TableViewerSorter;
import com.cubrid.cubridmanager.ui.spi.ValidateUtil;
import com.cubrid.cubridmanager.ui.spi.model.CubridServer;

/**
 * 
 * Database volume information for creating database
 * 
 * @author pangqiren
 * @version 1.0 - 2009-6-4 created by pangqiren
 */
public class VolumeInfoPage extends
		WizardPage implements
		ModifyListener,
		IPageChangedListener {

	public static String PAGENAME = "CreateDatabaseWizard/VolumeInfoPage";
	private Text volumeNameText;
	private Text volumePathText;
	private Combo volumeTypeCombo;
	private Text numberPagesText;
	private Button addVolumeButton;
	private Table volumeTable;
	private TableViewer volumeTableViewer;
	private Button deleteVolumeButton;
	private CubridServer server = null;
	private String databasePath = "";
	private String databaseName = "";
	private List<Map<String, String>> volumeTableList = new ArrayList<Map<String, String>>();
	private Text volumeSizeText;
	private String pageSize = null;

	/**
	 * The constructor
	 */
	public VolumeInfoPage(CubridServer server) {
		super(PAGENAME);
		this.server = server;
	}

	/**
	 * Creates the controls for this page
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

		createVolumeGroup(composite);
		createTable(composite);
		initial();

		setTitle(Messages.titleWizardPageAdditional);
		setMessage(Messages.msgWizardPageAdditional);
		setControl(composite);

	}

	/**
	 * 
	 * Create volume group area
	 * 
	 * @param parent
	 */
	private void createVolumeGroup(Composite parent) {
		Group volumeGroup = new Group(parent, SWT.NONE);
		volumeGroup.setText(Messages.grpAddtionalVolInfo);
		GridData gridData = new GridData(GridData.FILL_HORIZONTAL);
		volumeGroup.setLayoutData(gridData);
		GridLayout layout = new GridLayout();
		layout.numColumns = 4;
		volumeGroup.setLayout(layout);

		Label volumeNameLabel = new Label(volumeGroup, SWT.LEFT | SWT.WRAP);
		volumeNameLabel.setText(Messages.lblVolName);
		volumeNameLabel.setLayoutData(CommonTool.createGridData(1, 1, -1, -1));

		volumeNameText = new Text(volumeGroup, SWT.BORDER);
		gridData = new GridData(GridData.FILL_HORIZONTAL);
		gridData.horizontalSpan = 3;
		volumeNameText.setLayoutData(gridData);
		volumeNameText.setEditable(false);

		Label volumePathLabel = new Label(volumeGroup, SWT.LEFT | SWT.WRAP);
		volumePathLabel.setText(Messages.lblVolPath);
		volumePathLabel.setLayoutData(CommonTool.createGridData(1, 1, -1, -1));

		volumePathText = new Text(volumeGroup, SWT.BORDER);
		gridData = new GridData(GridData.FILL_HORIZONTAL);
		gridData.horizontalSpan = 2;
		volumePathText.setLayoutData(gridData);

		Button selectDirectoryButton = new Button(volumeGroup, SWT.NONE);
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
					volumePathText.setText(dir);
				}
			}
		});
		ServerInfo serverInfo = server.getServerInfo();
		if (serverInfo != null && !serverInfo.isLocalServer()) {
			selectDirectoryButton.setEnabled(false);
		}

		Label volumeTypeLabel = new Label(volumeGroup, SWT.LEFT | SWT.WRAP);
		volumeTypeLabel.setText(Messages.lblVolType);
		volumeTypeLabel.setLayoutData(CommonTool.createGridData(1, 1, -1, -1));

		volumeTypeCombo = new Combo(volumeGroup, SWT.DROP_DOWN | SWT.READ_ONLY);
		gridData = new GridData(GridData.FILL_HORIZONTAL);
		gridData.horizontalSpan = 3;
		volumeTypeCombo.setLayoutData(gridData);
		volumeTypeCombo.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				changeVolumeName();
			}
		});

		Label sizeOfPageLabel = new Label(volumeGroup, SWT.LEFT | SWT.WRAP);
		sizeOfPageLabel.setText(Messages.lblVolSize);
		sizeOfPageLabel.setLayoutData(CommonTool.createGridData(1, 1, -1, -1));

		volumeSizeText = new Text(volumeGroup, SWT.BORDER);
		volumeSizeText.setTextLimit(20);
		gridData = new GridData(GridData.FILL_HORIZONTAL);
		gridData.horizontalSpan = 3;
		volumeSizeText.setLayoutData(gridData);
		volumeSizeText.addFocusListener(new FocusListener() {
			public void focusGained(FocusEvent e) {
				volumeSizeText.addModifyListener(VolumeInfoPage.this);
			}

			public void focusLost(FocusEvent e) {
				volumeSizeText.removeModifyListener(VolumeInfoPage.this);
			}
		});

		Label numberPageLabel = new Label(volumeGroup, SWT.LEFT | SWT.WRAP);
		numberPageLabel.setText(Messages.lblNumOfPages);
		numberPageLabel.setLayoutData(CommonTool.createGridData(1, 1, -1, -1));

		numberPagesText = new Text(volumeGroup, SWT.BORDER);
		volumeSizeText.setTextLimit(20);
		gridData = new GridData(GridData.FILL_HORIZONTAL);
		gridData.horizontalSpan = 3;
		numberPagesText.setLayoutData(gridData);
		numberPagesText.addFocusListener(new FocusListener() {
			public void focusGained(FocusEvent e) {
				numberPagesText.addModifyListener(VolumeInfoPage.this);
			}

			public void focusLost(FocusEvent e) {
				numberPagesText.removeModifyListener(VolumeInfoPage.this);
			}
		});

		Composite composite = new Composite(parent, SWT.NONE);
		RowLayout rowLayout = new RowLayout();
		rowLayout.spacing = 5;
		composite.setLayout(rowLayout);
		gridData = new GridData(GridData.FILL_HORIZONTAL);
		gridData.horizontalAlignment = GridData.END;
		composite.setLayoutData(gridData);

		addVolumeButton = new Button(composite, SWT.NONE);
		addVolumeButton.setText(Messages.btnAddVolume);
		addVolumeButton.setEnabled(false);
		addVolumeButton.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				String volumeName = volumeNameText.getText();
				String volumeType = volumeTypeCombo.getText();
				String pageNumber = numberPagesText.getText();
				String volumePath = volumePathText.getText();
				Map<String, String> map = new HashMap<String, String>();
				map.put("0", volumeName);
				map.put("1", volumeType);
				map.put("2", pageNumber);
				map.put("3", volumePath);
				volumeTableList.add(map);
				volumeTableViewer.refresh();
				for (int i = 0; i < volumeTable.getColumnCount(); i++) {
					volumeTable.getColumn(i).pack();
				}
				changeVolumeName();
				changeAutoVolumeButton();
			}
		});
	}

	public void changeAutoVolumeButton() {
		boolean isHasDataVolume = false;
		boolean isHasIndexVolume = false;
		if (volumeTableList != null) {
			for (int i = 0; i < volumeTableList.size(); i++) {
				Map<String, String> map = volumeTableList.get(i);
				String type = map.get("1");
				if (type.equals("data")) {
					isHasDataVolume = true;
				}
				if (type.equals("index")) {
					isHasIndexVolume = true;
				}
			}
		}
		SetAutoAddVolumeInfoPage setAutoAddVolumeInfoPage = (SetAutoAddVolumeInfoPage) getWizard().getPage(
				SetAutoAddVolumeInfoPage.PAGENAME);
		if (setAutoAddVolumeInfoPage != null) {
			setAutoAddVolumeInfoPage.setUsingAutoDataVolume(isHasDataVolume);
			setAutoAddVolumeInfoPage.setUsingAutoIndexVolume(isHasIndexVolume);
		}
	}

	/**
	 * 
	 * Change volume table
	 * 
	 */
	public void changeVolumeTable() {
		if (volumeTableViewer != null) {
			volumeTableList.clear();
			volumeTableViewer.refresh();
		}
	}

	/**
	 * 
	 * Create volume table area
	 * 
	 * @param parent
	 */
	private void createTable(Composite parent) {

		Label tipLabel = new Label(parent, SWT.LEFT | SWT.WRAP);
		tipLabel.setText(Messages.msgVolumeList);
		GridData gridData = new GridData(GridData.FILL_HORIZONTAL);
		gridData.horizontalSpan = 4;
		tipLabel.setLayoutData(gridData);

		final String[] columnNameArr = new String[] {
				Messages.tblColumnVolName, Messages.tblColumnVolType,
				Messages.tblColumnNumOfPages, Messages.tblColumnVolPath };
		volumeTableViewer = CommonTool.createCommonTableViewer(parent,
				new TableViewerSorter(), columnNameArr,
				CommonTool.createGridData(GridData.FILL_BOTH, 4, 1, -1, 300));
		volumeTableViewer.setInput(volumeTableList);
		volumeTable = volumeTableViewer.getTable();
		for (int i = 0; i < volumeTable.getColumnCount(); i++) {
			volumeTable.getColumn(i).pack();
		}

		volumeTable.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				deleteVolumeButton.setEnabled(volumeTable.getSelectionCount() > 0);
			}
		});

		Composite composite = new Composite(parent, SWT.NONE);
		RowLayout rowLayout = new RowLayout();
		rowLayout.spacing = 5;
		composite.setLayout(rowLayout);
		gridData = new GridData(GridData.FILL_HORIZONTAL);
		gridData.horizontalAlignment = GridData.END;
		composite.setLayoutData(gridData);

		deleteVolumeButton = new Button(composite, SWT.NONE);
		deleteVolumeButton.setText(Messages.btnDelVolume);
		deleteVolumeButton.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				StructuredSelection selection = (StructuredSelection) volumeTableViewer.getSelection();
				if (selection != null && !selection.isEmpty()) {
					volumeTableList.removeAll(selection.toList());
				}
				volumeTableViewer.refresh();
				deleteVolumeButton.setEnabled(volumeTable.getSelectionCount() > 0);
				changeAutoVolumeButton();
			}
		});
		deleteVolumeButton.setEnabled(false);
	}

	public void modifyText(ModifyEvent e) {
		if (e.widget == volumeSizeText) {
			this.calcVolumePageNum();
		}
		if (e.widget == numberPagesText) {
			this.calcVolumeSize();
		}
		String volumePath = volumePathText.getText();
		boolean isValidVolumePath = ValidateUtil.isValidPathName(volumePath);
		String volumeSize = volumeSizeText.getText();
		boolean isValidVolmueSize = (ValidateUtil.isNumber(volumeSize) || ValidateUtil.isPositiveDouble(volumeSize))
				&& Double.parseDouble(volumeSize) > 0;
		String pageNum = numberPagesText.getText();
		boolean isValidPageNum = ValidateUtil.isNumber(pageNum)
				&& Double.parseDouble(pageNum) > 0;

		if (!isValidVolumePath) {
			setErrorMessage(Messages.errVolumePath);
		} else if (!isValidVolmueSize) {
			setErrorMessage(Messages.errVolumeSize);
		} else if (!isValidPageNum) {
			setErrorMessage(Messages.errVolumePageNum);
		}
		boolean isEnabled = isValidVolumePath && isValidVolmueSize
				&& isValidPageNum;
		if (isEnabled) {
			setErrorMessage(null);
		}
		addVolumeButton.setEnabled(isEnabled);
	}

	public void pageChanged(PageChangedEvent event) {
		IWizardPage page = (IWizardPage) event.getSelectedPage();
		if (page.getName().equals(PAGENAME)) {
			GeneralInfoPage generalInfoPage = (GeneralInfoPage) getWizard().getPage(
					GeneralInfoPage.PAGENAME);
			pageSize = generalInfoPage.getPageSize();
			String volumeSize = volumeSizeText.getText();
			String volumePageNum = numberPagesText.getText();
			if (volumeSize.trim().equals("")) {
				volumeSizeText.setText(generalInfoPage.getGenericVolumeSize());
			}
			if (volumePageNum.trim().equals("")) {
				numberPagesText.setText(generalInfoPage.getGenericPageNum());
			}
			changeVolumeName();
		}
	}

	/**
	 * 
	 * Initial data
	 * 
	 */
	private void initial() {
		volumeTypeCombo.setItems(new String[] { "data", "index", "temp",
				"generic" });
		volumeTypeCombo.select(0);
		EnvInfo envInfo = server.getServerInfo().getEnvInfo();
		if (envInfo != null) {
			databasePath = envInfo.getDatabaseDir();
			ServerInfo serverInfo = server.getServerInfo();
			if (serverInfo != null
					&& serverInfo.getServerOsInfo() == OsInfoType.NT) {
				databasePath = FileNameUtils.separatorsToWindows(databasePath);
			}
		}
		volumeNameText.addModifyListener(this);
		volumePathText.addModifyListener(this);
	}

	/**
	 * 
	 * Change volume path
	 * 
	 */
	public void changeVolumePath() {
		GeneralInfoPage generalInfoPage = (GeneralInfoPage) getWizard().getPage(
				GeneralInfoPage.PAGENAME);
		databaseName = generalInfoPage.getDatabaseName();
		volumePathText.setText(databasePath
				+ server.getServerInfo().getPathSeparator() + databaseName);
	}

	/**
	 * 
	 * Change volume name
	 * 
	 */
	private void changeVolumeName() {
		String type = volumeTypeCombo.getText();
		int count = 1;
		TableItem[] items = volumeTable.getItems();
		while (true) {
			NumberFormat nf = NumberFormat.getInstance();
			nf.setMinimumIntegerDigits(3);
			String volumeName = databaseName + "_" + type + "_x"
					+ nf.format(count);
			boolean isExist = false;
			for (int i = 0; i < items.length; i++) {
				String str = items[i].getText(0);
				if (str.trim().equals(volumeName)) {
					isExist = true;
				}
			}
			if (!isExist) {
				volumeNameText.setText(volumeName);
				break;
			}
			count++;
		}

	}

	/**
	 * 
	 * Calculate volume size
	 * 
	 */
	private void calcVolumeSize() {
		String pageNumStr = numberPagesText.getText();
		boolean isValidPageNum = ValidateUtil.isNumber(pageNumStr);
		if (pageSize != null && pageSize.trim().length() > 0) {
			int size = Integer.parseInt(pageSize);
			if (isValidPageNum) {
				double pageNum = Double.parseDouble(pageNumStr);
				double vSize = size * pageNum / (1024 * 1024);
				NumberFormat nf = NumberFormat.getInstance();
				nf.setGroupingUsed(false);
				nf.setMaximumFractionDigits(3);
				nf.setMinimumFractionDigits(3);
				volumeSizeText.setText(nf.format(vSize));
			} else {
				volumeSizeText.setText("");
			}
		}
	}

	/**
	 * 
	 * Calculate volume page number
	 * 
	 */
	private void calcVolumePageNum() {
		String volumeSizeStr = volumeSizeText.getText();
		boolean isValidVolumeSize = ValidateUtil.isNumber(volumeSizeStr)
				|| ValidateUtil.isPositiveDouble(volumeSizeStr);
		if (pageSize != null && pageSize.trim().length() > 0) {
			int size = Integer.parseInt(pageSize);
			if (isValidVolumeSize) {
				double volumeSize = Double.parseDouble(volumeSizeStr);
				double pageNumber = (1024 * 1024 / size) * volumeSize;
				numberPagesText.setText(String.valueOf(GeneralInfoPage.getPageNum(pageNumber)));
			} else {
				numberPagesText.setText("");
			}
		}
	}

	/**
	 * 
	 * Get all volume information
	 * 
	 * @return
	 */
	public List<Map<String, String>> getVolumeList() {
		return volumeTableList;
	}

	/**
	 * 
	 * Set volume size
	 * 
	 * @param size
	 */
	public void setVolumeSize(String size) {
		if (volumeSizeText != null && !volumeSizeText.isDisposed()) {
			volumeSizeText.setText(size);
		}
	}

	/**
	 * 
	 * Set page number
	 * 
	 * @param pageNum
	 */
	public void setVolumePagNum(String pageNum) {
		if (numberPagesText != null && !numberPagesText.isDisposed()) {
			numberPagesText.setText(pageNum);
		}
	}
}
