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
package com.cubrid.cubridmanager.ui.cubrid.dbspace.dialog;

import java.math.BigDecimal;

import org.eclipse.jface.dialogs.IDialogConstants;
import org.eclipse.swt.SWT;
import org.eclipse.swt.events.FocusEvent;
import org.eclipse.swt.events.FocusListener;
import org.eclipse.swt.events.ModifyEvent;
import org.eclipse.swt.events.ModifyListener;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.widgets.Combo;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Control;
import org.eclipse.swt.widgets.Group;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.Text;

import com.cubrid.cubridmanager.core.common.model.ServerInfo;
import com.cubrid.cubridmanager.core.cubrid.database.task.CheckDirTask;
import com.cubrid.cubridmanager.core.cubrid.dbspace.model.AddVolumeDbInfo;
import com.cubrid.cubridmanager.core.cubrid.dbspace.model.GetAddVolumeStatusInfo;
import com.cubrid.cubridmanager.help.CubridManagerHelpContextIDs;
import com.cubrid.cubridmanager.ui.cubrid.dbspace.Messages;
import com.cubrid.cubridmanager.ui.spi.CommonTool;
import com.cubrid.cubridmanager.ui.spi.ValidateUtil;
import com.cubrid.cubridmanager.ui.spi.dialog.CMTitleAreaDialog;
import com.cubrid.cubridmanager.ui.spi.model.DefaultSchemaNode;
import com.cubrid.cubridmanager.ui.spi.progress.CommonTaskExec;
import com.cubrid.cubridmanager.ui.spi.progress.ExecTaskWithProgress;
import com.cubrid.cubridmanager.ui.spi.progress.TaskExecutor;

/**
 * A dialog that show up when a user click the add volume context menu.
 * 
 * @author lizhiqiang 2009-4-17
 */
public class AddVolumeDialog extends
		CMTitleAreaDialog {

	private String errorPathMsg = Messages.errorPathMsg;
	private String errorVolumeMsg = Messages.errorVolumeMsg;
	private String errorPageMsg = Messages.errorPageMsg;
	private String noPathMsg = Messages.noPathMsg;
	private String pathLblName = Messages.pathLblName;
	private String pagesLblName = Messages.pagesLblName;
	private String volumeSizeLblName = Messages.volumeSizeLblName;
	private static final String TEMP = Messages.tempOfPurpose;
	private static final String INDEX = Messages.indexOfPurpose;
	private static final String DATA = Messages.dataOfPurpose;
	private static final String GENERIC = Messages.genericOfPurpose;
	private Text volumeSizetext;
	private Text pageText;
	private Text pathText;
	private String pathToolTip = Messages.pathToolTip;
	private String pagesToolTip = Messages.pagesToolTip;
	private String volumeSizeToolTip = Messages.volumeSizeToolTip;
	public static final BigDecimal MEGABYTES = new BigDecimal(1024 * 1024);
	private static final BigDecimal INITPAGE = new BigDecimal(10240);
	private GetAddVolumeStatusInfo getAddVolumeStatusInfo;
	private BigDecimal pageSize;
	private boolean isOkenable[];
	private DefaultSchemaNode selection;
	private String dialogTitle = Messages.dialogTitle;
	private String dialogMsg = Messages.dialogMsg;
	private String purposeLbllName = Messages.purposeLbllName;
	private AddVolumeDbInfo addVolumeDbInfo;
	private Combo purposeCombo;
	private String purpose;
	private boolean purposeEnable;
	private String[] itemsOfPurpose;
	private VolumeModifyListener volumeModifyListener;
	private PageModifyListener pageModifyListener;
	

	public AddVolumeDialog(Shell parentShell) {
		super(parentShell);
		isOkenable = new boolean[3];
		for (int i = 0; i < isOkenable.length; i++) {
			isOkenable[i] = true;
		}
		itemsOfPurpose = new String[] { GENERIC, DATA, INDEX, TEMP };
	}

	protected Control createDialogArea(Composite parent) {
		Composite parentComp = (Composite) super.createDialogArea(parent);
		getHelpSystem().setHelp(parentComp, CubridManagerHelpContextIDs.databaseSpace);
		setTitle(dialogTitle);
		setMessage(dialogMsg);

		final Composite composite = new Composite(parentComp, SWT.RESIZE);
		final GridData gd_composite = new GridData(SWT.FILL, SWT.CENTER, true,
				false);
		gd_composite.widthHint = 500;
		composite.setLayoutData(gd_composite);
		final GridLayout gridLayout = new GridLayout();
		gridLayout.marginHeight = convertVerticalDLUsToPixels(IDialogConstants.VERTICAL_MARGIN);
		gridLayout.marginWidth = convertHorizontalDLUsToPixels(IDialogConstants.HORIZONTAL_MARGIN);
		gridLayout.verticalSpacing = convertVerticalDLUsToPixels(IDialogConstants.VERTICAL_SPACING);
		gridLayout.horizontalSpacing = convertHorizontalDLUsToPixels(IDialogConstants.HORIZONTAL_SPACING);
		composite.setLayout(gridLayout);

		final Group group = new Group(composite, SWT.NONE);
		group.setLayoutData(new GridData(SWT.FILL, SWT.DEFAULT, true, false));
		final GridLayout gd_group = new GridLayout(2, false);
		group.setLayout(gd_group);

		final Label pathLbl = new Label(group, SWT.NONE);
		pathLbl.setText(pathLblName);

		pathText = new Text(group, SWT.BORDER);
		final GridData gd_pathText = new GridData(SWT.FILL, SWT.CENTER, true,
				false);
		pathText.setLayoutData(gd_pathText);
		pathText.setToolTipText(pathToolTip);

		final Label purposeLbl = new Label(group, SWT.NONE);
		purposeLbl.setText(purposeLbllName);

		purposeCombo = new Combo(group, SWT.BORDER | SWT.RIGHT | SWT.READ_ONLY);
		if (purposeEnable) {
			purposeCombo.setEnabled(true);
		} else {
			purposeCombo.setEnabled(false);
		}

		final GridData gd_purposeText = new GridData(SWT.FILL, SWT.CENTER,
				true, false);
		purposeCombo.setLayoutData(gd_purposeText);
		purposeCombo.setItems(itemsOfPurpose);

		final Label volumeSizeLbl = new Label(group, SWT.NONE);
		volumeSizeLbl.setText(volumeSizeLblName);

		volumeSizetext = new Text(group, SWT.BORDER | SWT.RIGHT);
		final GridData gd_volumeSizetext = new GridData(SWT.FILL, SWT.CENTER,
				true, false);
		volumeSizetext.setLayoutData(gd_volumeSizetext);

		final Label pagesLbl = new Label(group, SWT.NONE);
		pagesLbl.setText(pagesLblName);

		pageText = new Text(group, SWT.BORDER | SWT.RIGHT);
		final GridData gd_pageText = new GridData(SWT.FILL, SWT.CENTER, true,
				false);
		pageText.setLayoutData(gd_pageText);

		pageText.setToolTipText(pagesToolTip);
		volumeSizetext.setToolTipText(volumeSizeToolTip);
		// Sets the initial value
		pathText.setText(getAddVolumeStatusInfo.getVolpath());
		pageText.setText(INITPAGE.setScale(0).toString());
		BigDecimal volume = INITPAGE.multiply(pageSize).divide(MEGABYTES, 3,
				BigDecimal.ROUND_HALF_UP);
		volumeSizetext.setText(volume.toString());
		purposeCombo.setText(purpose);
		// Sets listener
		pathText.addModifyListener(new ModifyListener() {

			public void modifyText(ModifyEvent e) {
				if (!ValidateUtil.isValidPathName(pathText.getText())) {
					isOkenable[0] = false;
				} else {
					isOkenable[0] = true;
				}
				enableOk();
			}

		});

		volumeModifyListener = new VolumeModifyListener();
		volumeSizetext.addFocusListener(new FocusListener() {

			public void focusGained(FocusEvent e) {
				volumeSizetext.addModifyListener(volumeModifyListener);
			}

			public void focusLost(FocusEvent e) {
				volumeSizetext.removeModifyListener(volumeModifyListener);

			}

		});
		pageModifyListener = new PageModifyListener();
		pageText.addFocusListener(new FocusListener() {

			public void focusGained(FocusEvent e) {
				pageText.addModifyListener(pageModifyListener);

			}

			public void focusLost(FocusEvent e) {
				pageText.removeModifyListener(pageModifyListener);

			}
		});
		return parentComp;
	}

	@Override
	protected void constrainShellSize() {
		super.constrainShellSize();
		CommonTool.centerShell(getShell());
		getShell().setText(dialogTitle);
	}

	@Override
	public void okPressed() {
		String sPathText = pathText.getText().trim();
		// Checks the path
		ServerInfo serverInfo = selection.getServer().getServerInfo();
		CheckDirTask task = new CheckDirTask(serverInfo);
		task.setDirectory(new String[] { sPathText });
		task.execute();

		TaskExecutor taskExecutor = new CommonTaskExec();
		taskExecutor.addTask(task);
		new ExecTaskWithProgress(taskExecutor).exec();
		if (!taskExecutor.isSuccess()) {
			setErrorMessage(noPathMsg);
			return;
		}
		String numberofpage = pageText.getText().trim();
		String size_need_mb = volumeSizetext.getText().trim();
		purpose = purposeCombo.getText().trim();
		addVolumeDbInfo = new AddVolumeDbInfo();
		addVolumeDbInfo.setPath(sPathText);
		addVolumeDbInfo.setNumberofpage(numberofpage);
		addVolumeDbInfo.setSize_need_mb(size_need_mb + "(MB)");
		addVolumeDbInfo.setPurpose(purpose);
		addVolumeDbInfo.setVolname("");
		super.okPressed();

	}

	/**
	 * @param getAddVolumeStatusInfo the getAddVolumeStatusInfo to set
	 */
	public void setGetAddVolumeStatusInfo(
			GetAddVolumeStatusInfo getAddVolumeStatusInfo) {
		this.getAddVolumeStatusInfo = getAddVolumeStatusInfo;
	}

	/**
	 * Sets the queryPlanInfo and selection which is a folder
	 * 
	 * @param selection the selection to set
	 */
	public void initPara(DefaultSchemaNode selection) {
		this.selection = selection;

		switch (selection.getType()) {
		case GENERIC_VOLUME_FOLDER:
			purpose = GENERIC;
			purposeEnable = false;
			break;
		case DATA_VOLUME_FOLDER:
			purpose = DATA;
			purposeEnable = false;
			break;
		case INDEX_VOLUME_FOLDER:
			purpose = INDEX;
			purposeEnable = false;
			break;
		case TEMP_VOLUME_FOLDER:
			purpose = TEMP;
			purposeEnable = false;
			break;
		case DBSPACE_FOLDER:
			purpose = GENERIC;
			purposeEnable = true;
			break;
		}

	}

	/**
	 * Enable the "OK" button
	 * 
	 */
	private void enableOk() {
		boolean is = true;
		for (int i = 0; i < isOkenable.length; i++) {
			is = is && isOkenable[i];
		}
		if (is) {
			getButton(IDialogConstants.OK_ID).setEnabled(true);
		} else {
			getButton(IDialogConstants.OK_ID).setEnabled(false);
		}
		if (!isOkenable[0]) {
			setErrorMessage(errorPathMsg);
		}else if (!isOkenable[1]) {
			setErrorMessage(errorVolumeMsg);
		}else if(!isOkenable[2]){
			setErrorMessage(errorPageMsg);
		}
		if (is) {
			setErrorMessage(null);
		}

	}

	/**
	 * Gets the instance of AddVolumeDbInfo
	 * 
	 * @return the addVolumeDbInfo
	 */
	public AddVolumeDbInfo getAddVolumeDbInfo() {
		return addVolumeDbInfo;
	}

	/**
	 * @param pageSize the pageSize to set
	 */
	public void setPageSize(int pageSize) {
		this.pageSize = new BigDecimal(pageSize);
	}

	/*
	 * A class that response to the modify of volumeText
	 */
	private class VolumeModifyListener implements
			ModifyListener {

		public void modifyText(ModifyEvent e) {

			String sVolume = volumeSizetext.getText();

			if (!ValidateUtil.isPositiveDouble(sVolume)) {
				isOkenable[1] = false;
				enableOk();
				return;
			}
			isOkenable[1] = true;
			enableOk();

			BigDecimal volume = new BigDecimal(sVolume);
			BigDecimal page = volume.multiply(MEGABYTES.divide(pageSize));

			if (page.compareTo(BigDecimal.ONE) < 0) {
				isOkenable[1] = false;
				enableOk();
				return;
			}
			isOkenable[1] = true;
			enableOk();
            page = page.setScale(0, BigDecimal.ROUND_HALF_UP);
			pageText.setText(page.toString());

		}
	}

	/*
	 * A class that response to the modify of volumeText
	 */
	private class PageModifyListener implements
			ModifyListener {

		public void modifyText(ModifyEvent e) {

			String sPage = pageText.getText();

			if (!ValidateUtil.isInteger(sPage)) {
				isOkenable[2] = false;
				enableOk();
				return;
			}

			BigDecimal page = new BigDecimal(sPage);
			if (page.compareTo(BigDecimal.ONE) < 0) {
				isOkenable[2] = false;
				enableOk();
			}
			isOkenable[2] = true;
			enableOk();

			BigDecimal volume = page.multiply(pageSize).divide(MEGABYTES, 3,
					BigDecimal.ROUND_HALF_UP);
			volumeSizetext.setText(volume.toString());
		}

	}

}
