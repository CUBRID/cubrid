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

import java.util.List;
import java.util.Map;

import org.eclipse.jface.dialogs.IPageChangedListener;
import org.eclipse.jface.dialogs.PageChangedEvent;
import org.eclipse.jface.wizard.IWizardPage;
import org.eclipse.jface.wizard.WizardPage;
import org.eclipse.swt.SWT;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Text;
import org.eclipse.ui.PlatformUI;
import org.eclipse.ui.help.IWorkbenchHelpSystem;

import com.cubrid.cubridmanager.core.common.model.OnOffType;
import com.cubrid.cubridmanager.core.cubrid.dbspace.model.GetAutoAddVolumeInfo;
import com.cubrid.cubridmanager.help.CubridManagerHelpContextIDs;
import com.cubrid.cubridmanager.ui.cubrid.database.Messages;

/**
 * 
 * The last database information for creating database
 * 
 * @author pangqiren
 * @version 1.0 - 2009-6-4 created by pangqiren
 */
public class DatabaseInfoPage extends
		WizardPage implements
		IPageChangedListener {

	public static final String PAGENAME = "CreateDatabaseWizard/DatabaseInfoPage";
	private Text databaseInfoText;

	/**
	 * The constructor
	 */
	public DatabaseInfoPage() {
		super(PAGENAME);
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
		Label tipLabel = new Label(composite, SWT.LEFT | SWT.WRAP);
		tipLabel.setText(Messages.msgDbInfoList);
		gridData = new GridData(GridData.FILL_HORIZONTAL);
		tipLabel.setLayoutData(gridData);

		databaseInfoText = new Text(composite, SWT.LEFT | SWT.WRAP | SWT.BORDER
				| SWT.H_SCROLL | SWT.V_SCROLL);
		databaseInfoText.setEditable(false);
		gridData = new GridData(GridData.FILL_BOTH);
		databaseInfoText.setLayoutData(gridData);
		setTitle(Messages.titleWizardPageDbInfo);
		setMessage(Messages.msgWizardPageDbInfo);
		setControl(composite);
	}

	public void pageChanged(PageChangedEvent event) {
		IWizardPage page = (IWizardPage) event.getSelectedPage();
		if (page.getName().equals(PAGENAME)) {
			fillData();
		}
	}

	/**
	 * 
	 * Fill in the database information
	 * 
	 */
	public void fillData() {
		databaseInfoText.setText("");
		GeneralInfoPage generalInfoPage = (GeneralInfoPage) getWizard().getPage(
				GeneralInfoPage.PAGENAME);
		int pageSize = Integer.parseInt(generalInfoPage.getPageSize());
		databaseInfoText.append("General Information:\r\n");
		databaseInfoText.append("The database name will be "
				+ generalInfoPage.getDatabaseName() + ".\r\n");
		databaseInfoText.append("The size of a page will be " + pageSize
				+ " bytes." + "\r\n");
		databaseInfoText.append("\r\n");
		databaseInfoText.append("Generic volume information:\r\n");
		double genericVolumeSize = Double.parseDouble(generalInfoPage.getGenericPageNum())
				* pageSize / (1024 * 1024);
		databaseInfoText.append("The volume size will be " + genericVolumeSize
				+ "MB.\r\n");
		databaseInfoText.append("The volume path will be "
				+ generalInfoPage.getGenericVolumePath() + ".\r\n");
		databaseInfoText.append("\r\n");
		databaseInfoText.append("Log Volume Information:\r\n");
		double logVolumeSize = Double.parseDouble(generalInfoPage.getLogPageNum())
				* pageSize / (1024 * 1024);
		databaseInfoText.append("The volume size will be " + logVolumeSize
				+ "MB." + ".\r\n");
		databaseInfoText.append("The volume path will be "
				+ generalInfoPage.getLogVolumePath() + ".\r\n");
		databaseInfoText.append("\r\n");
		VolumeInfoPage volumeInfoPage = (VolumeInfoPage) getWizard().getPage(
				VolumeInfoPage.PAGENAME);
		List<Map<String, String>> volumeList = volumeInfoPage.getVolumeList();
		if (volumeList.size() > 0) {
			databaseInfoText.append("Addtional volume information:\r\n");
		}
		for (int i = 0; i < volumeList.size(); i++) {
			Map<String, String> map = volumeList.get(i);
			String volumeName = map.get("0");
			String volumeType = map.get("1");
			String pageNumber = map.get("2");
			String volumePath = map.get("3");
			double volumeSize = Double.parseDouble(pageNumber) * pageSize
					/ (1024 * 1024);
			databaseInfoText.append("The " + (i + 1)
					+ "'th volume information:\r\n");
			databaseInfoText.append("Volume name will be " + volumeName
					+ ",Volume path will be " + volumePath
					+ ", Volume type will be " + volumeType
					+ " volume,Volume size will be " + volumeSize + "MB.\r\n");
			databaseInfoText.append("\r\n");
		}
		SetAutoAddVolumeInfoPage autoAddVolumeInfoPage = (SetAutoAddVolumeInfoPage) getWizard().getPage(
				SetAutoAddVolumeInfoPage.PAGENAME);
		GetAutoAddVolumeInfo autoAddVolumeInfo = autoAddVolumeInfoPage.getAutoAddVolumeInfo();
		if (autoAddVolumeInfo != null) {
			databaseInfoText.append("Auto added volume information:\r\n");
			if (autoAddVolumeInfo.getData().equals(OnOffType.ON.getText())) {
				double pageNum = Double.parseDouble(autoAddVolumeInfo.getData_ext_page());
				double size = pageSize * pageNum / (1024 * 1024);
				double rate = Double.parseDouble(autoAddVolumeInfo.getData_warn_outofspace()) * 100;
				databaseInfoText.append("The size of auto added data volume will be "
						+ size
						+ "MB;Out of space of warning rate(%) will be "
						+ rate + ".\r\n");
			}
			if (autoAddVolumeInfo.getIndex().equals(OnOffType.ON.getText())) {
				double pageNum = Double.parseDouble(autoAddVolumeInfo.getIndex_ext_page());
				double size = pageSize * pageNum / (1024 * 1024);
				double rate = Double.parseDouble(autoAddVolumeInfo.getIndex_warn_outofspace()) * 100;
				databaseInfoText.append("The size of auto added index volume will be "
						+ size
						+ "MB;Out of space of warning rate(%) will be "
						+ rate + ".\r\n");
			}
		}
	}
}
