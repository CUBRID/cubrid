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
import java.util.List;
import java.util.Map;

import org.eclipse.jface.dialogs.IPageChangedListener;
import org.eclipse.jface.dialogs.PageChangedEvent;
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
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.widgets.Combo;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Group;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Text;
import org.eclipse.ui.PlatformUI;
import org.eclipse.ui.help.IWorkbenchHelpSystem;

import com.cubrid.cubridmanager.core.common.model.OnOffType;
import com.cubrid.cubridmanager.core.cubrid.dbspace.model.GetAutoAddVolumeInfo;
import com.cubrid.cubridmanager.help.CubridManagerHelpContextIDs;
import com.cubrid.cubridmanager.ui.cubrid.database.Messages;
import com.cubrid.cubridmanager.ui.spi.CommonTool;
import com.cubrid.cubridmanager.ui.spi.ValidateUtil;

/**
 * 
 * Set auto adding volume information page for creating database
 * 
 * @author pangqiren
 * @version 1.0 - 2009-6-4 created by pangqiren
 */
public class SetAutoAddVolumeInfoPage extends
		WizardPage implements
		IPageChangedListener,
		ModifyListener {

	public static String PAGENAME = "CreateDatabaseWizard/SetAutoAddVolumeInfoPage";

	private Button dataUsingAutoVolButton;
	private Button indexUsingAutoVolButton;
	private Combo dataOutRateCombo;
	private Combo indexOutRateCombo;
	private Text indexExtPageText;
	private Text dataExtPageText;
	private Text dataVolumeText;
	private Text indexVolumeText;
	List<Map<String, String>> volumeList = null;

	private final static int MEGABYTES = 1024 * 1024;
	final private static int rateMin = 5;
	final private static int rateMax = 30;
	private String pageSize = null;

	private boolean isSelectedUsingAutoDataVolume = false;
	private boolean isSelectedUsingAutoIndexVolume = false;

	/**
	 * The constructor
	 */
	public SetAutoAddVolumeInfoPage() {
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

		createDataParaGroup(composite);
		createIndexParaGroup(composite);
		init();
		setTitle(Messages.titleWizardPageAuto);
		setMessage(Messages.msgWizardPageAuto);
		setControl(composite);

	}

	/*
	 * Creates dataParaGroup which is the part of Dialog area
	 * 
	 * @param composite
	 */
	private void createDataParaGroup(Composite composite) {
		final Group dataParaGroup = new Group(composite, SWT.RESIZE);
		final GridData gdDataParaGroup = new GridData(SWT.FILL, SWT.CENTER,
				true, false);
		dataParaGroup.setLayoutData(gdDataParaGroup);
		dataParaGroup.setText(Messages.grpVolPurposeData);
		GridLayout groupLayout = new GridLayout(4, false);
		dataParaGroup.setLayout(groupLayout);

		dataUsingAutoVolButton = new Button(dataParaGroup, SWT.CHECK);
		final GridData gd_usingAutoVolButton = new GridData(SWT.LEFT,
				SWT.CENTER, true, false, 4, 1);
		dataUsingAutoVolButton.setLayoutData(gd_usingAutoVolButton);
		dataUsingAutoVolButton.setText(Messages.btnUsingAuto);

		final Label outOfSpaceLabel = new Label(dataParaGroup, SWT.NONE);
		outOfSpaceLabel.setLayoutData(CommonTool.createGridData(1, 1, -1, -1));
		outOfSpaceLabel.setText(Messages.lblOutOfSpaceWarning);

		dataOutRateCombo = new Combo(dataParaGroup, SWT.BORDER);
		dataOutRateCombo.setTextLimit(2);
		dataOutRateCombo.setLayoutData(new GridData(SWT.FILL, SWT.CENTER, true,
				false, 3, 1));

		final Label volumeLabel = new Label(dataParaGroup, SWT.NONE);
		volumeLabel.setText(Messages.lblVolSize);
		volumeLabel.setLayoutData(CommonTool.createGridData(1, 1, -1, -1));

		dataVolumeText = new Text(dataParaGroup, SWT.BORDER);
		dataVolumeText.setTextLimit(20);
		final GridData gd_dataVolumeText = new GridData(SWT.FILL, SWT.CENTER,
				true, false, 3, 1);
		dataVolumeText.setLayoutData(gd_dataVolumeText);
		dataVolumeText.addFocusListener(new FocusListener() {
			public void focusGained(FocusEvent e) {
				dataVolumeText.addModifyListener(SetAutoAddVolumeInfoPage.this);
			}

			public void focusLost(FocusEvent e) {
				dataVolumeText.removeModifyListener(SetAutoAddVolumeInfoPage.this);
			}
		});

		final Label extPageLabel = new Label(dataParaGroup, SWT.NONE);
		extPageLabel.setText(Messages.lblExtensionPage);
		extPageLabel.setLayoutData(CommonTool.createGridData(1, 1, -1, -1));

		dataExtPageText = new Text(dataParaGroup, SWT.BORDER);
		dataExtPageText.setTextLimit(20);
		final GridData gd_dataPageText = new GridData(SWT.FILL, SWT.CENTER,
				true, false, 3, 1);
		dataExtPageText.setLayoutData(gd_dataPageText);
		dataExtPageText.addFocusListener(new FocusListener() {
			public void focusGained(FocusEvent e) {
				dataExtPageText.addModifyListener(SetAutoAddVolumeInfoPage.this);
			}

			public void focusLost(FocusEvent e) {
				dataExtPageText.removeModifyListener(SetAutoAddVolumeInfoPage.this);
			}
		});
	}

	/*
	 * Creates indexParaGroup which is the part of Dialog area
	 * 
	 * @param composite
	 */
	private void createIndexParaGroup(Composite composite) {
		final Group indexParaGroup = new Group(composite, SWT.RESIZE);
		final GridData gdIndexParaGroup = new GridData(SWT.FILL, SWT.CENTER,
				true, false);
		final GridLayout gridLayout = new GridLayout(4, false);
		indexParaGroup.setLayout(gridLayout);
		indexParaGroup.setLayoutData(gdIndexParaGroup);
		indexParaGroup.setText(Messages.grpVolPurposeIndex);

		indexUsingAutoVolButton = new Button(indexParaGroup, SWT.CHECK);
		indexUsingAutoVolButton.setLayoutData(new GridData(SWT.LEFT,
				SWT.CENTER, true, false, 4, 1));
		indexUsingAutoVolButton.setText(Messages.btnUsingAuto);

		final Label outOfSpaceLabel = new Label(indexParaGroup, SWT.NONE);
		outOfSpaceLabel.setText(Messages.lblOutOfSpaceWarning);
		outOfSpaceLabel.setLayoutData(CommonTool.createGridData(1, 1, -1, -1));

		indexOutRateCombo = new Combo(indexParaGroup, SWT.BORDER);
		indexOutRateCombo.setTextLimit(2);
		indexOutRateCombo.setLayoutData(new GridData(SWT.FILL, SWT.CENTER,
				true, false, 3, 1));

		final Label volumeLabel = new Label(indexParaGroup, SWT.NONE);
		volumeLabel.setText(Messages.lblVolSize);
		volumeLabel.setLayoutData(CommonTool.createGridData(1, 1, -1, -1));

		indexVolumeText = new Text(indexParaGroup, SWT.BORDER);
		indexVolumeText.setTextLimit(20);
		final GridData gd_dataVolumeText = new GridData(SWT.FILL, SWT.CENTER,
				true, false, 3, 1);
		indexVolumeText.setLayoutData(gd_dataVolumeText);
		indexVolumeText.addFocusListener(new FocusListener() {
			public void focusGained(FocusEvent e) {
				indexVolumeText.addModifyListener(SetAutoAddVolumeInfoPage.this);
			}

			public void focusLost(FocusEvent e) {
				indexVolumeText.removeModifyListener(SetAutoAddVolumeInfoPage.this);
			}
		});

		final Label extPageLabel = new Label(indexParaGroup, SWT.NONE);
		extPageLabel.setLayoutData(CommonTool.createGridData(1, 1, -1, -1));
		extPageLabel.setText(Messages.lblExtensionPage);

		indexExtPageText = new Text(indexParaGroup, SWT.BORDER);
		indexExtPageText.setTextLimit(20);
		final GridData gd_dataText = new GridData(SWT.FILL, SWT.CENTER, true,
				false, 3, 1);
		indexExtPageText.setLayoutData(gd_dataText);
		indexExtPageText.addFocusListener(new FocusListener() {
			public void focusGained(FocusEvent e) {
				indexExtPageText.addModifyListener(SetAutoAddVolumeInfoPage.this);
			}

			public void focusLost(FocusEvent e) {
				indexExtPageText.removeModifyListener(SetAutoAddVolumeInfoPage.this);
			}
		});
	}

	/*
	 * Initials some values
	 * 
	 */
	private void init() {
		for (int i = rateMin; i <= rateMax; i++) {
			dataOutRateCombo.add(String.valueOf(i));
			dataOutRateCombo.select(0);
			indexOutRateCombo.add(String.valueOf(i));
			indexOutRateCombo.select(0);
		}
		dataUsingAutoVolButton.setSelection(false);
		indexUsingAutoVolButton.setSelection(false);
		dataOutRateCombo.setEnabled(false);
		dataVolumeText.setEnabled(false);
		dataExtPageText.setEnabled(false);
		indexOutRateCombo.setEnabled(false);
		indexVolumeText.setEnabled(false);
		indexExtPageText.setEnabled(false);
		dataOutRateCombo.addModifyListener(this);
		indexOutRateCombo.addModifyListener(this);
		dataUsingAutoVolButton.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				isSelectedUsingAutoDataVolume = true;
				changeButtonStatus(false);
			}
		});
		indexUsingAutoVolButton.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				isSelectedUsingAutoIndexVolume = true;
				changeButtonStatus(false);
			}
		});
	}

	public void modifyText(ModifyEvent e) {
		if (e.widget == dataVolumeText) {
			String sVolume = dataVolumeText.getText();
			boolean isValidDataVolumeSize = ValidateUtil.isNumber(sVolume)
					|| ValidateUtil.isPositiveDouble(sVolume);
			if (isValidDataVolumeSize && pageSize != null
					&& pageSize.trim().length() > 0) {
				double volume = Double.valueOf(sVolume);
				double pageNum = volume
						* (MEGABYTES / Integer.parseInt(pageSize));
				dataExtPageText.setText(String.valueOf(GeneralInfoPage.getPageNum(pageNum)));
			}
		} else if (e.widget == indexVolumeText) {
			String sVolume = indexVolumeText.getText();
			boolean isValidIndexVolumeSize = ValidateUtil.isNumber(sVolume)
					|| ValidateUtil.isPositiveDouble(sVolume);
			if (isValidIndexVolumeSize && pageSize != null
					&& pageSize.trim().length() > 0) {
				double volume = Double.valueOf(sVolume);
				double pageNum = volume
						* (MEGABYTES / Integer.parseInt(pageSize));
				indexExtPageText.setText(String.valueOf(GeneralInfoPage.getPageNum(pageNum)));
			}
		} else if (e.widget == dataExtPageText) {
			String sPageNum = dataExtPageText.getText();
			boolean isValidPageNum = ValidateUtil.isNumber(sPageNum);
			if (isValidPageNum && pageSize != null
					&& pageSize.trim().length() > 0) {
				double pageNum = Double.valueOf(sPageNum);
				double volumeSize = pageNum * Integer.parseInt(pageSize)
						/ MEGABYTES;
				NumberFormat nf = NumberFormat.getInstance();
				nf.setGroupingUsed(false);
				nf.setMaximumFractionDigits(3);
				nf.setMinimumFractionDigits(3);
				dataVolumeText.setText(nf.format(volumeSize));
			}
		} else if (e.widget == indexExtPageText) {
			String sPageNum = indexExtPageText.getText();
			boolean isValidPageNum = ValidateUtil.isNumber(sPageNum);
			if (isValidPageNum && pageSize != null
					&& pageSize.trim().length() > 0) {
				double pageNum = Double.valueOf(sPageNum);
				double volumeSize = pageNum * Integer.parseInt(pageSize)
						/ MEGABYTES;
				NumberFormat nf = NumberFormat.getInstance();
				nf.setGroupingUsed(false);
				nf.setMaximumFractionDigits(3);
				nf.setMinimumFractionDigits(3);
				indexVolumeText.setText(nf.format(volumeSize));
			}
		}
		valid();
	}

	private void valid() {
		String dataOutRate = dataOutRateCombo.getText();
		String dataVolumeSize = dataVolumeText.getText();
		String dataPageNum = dataExtPageText.getText();
		String indexOutRate = indexOutRateCombo.getText();
		String indexVolumeSize = indexVolumeText.getText();
		String indexPageNum = indexExtPageText.getText();
		boolean isValidDataOutRate = true;
		boolean isValidDataVolumeSize = true;
		boolean isValidDataPageNum = true;
		boolean isValidIndexOutRate = true;
		boolean isValidIndexVolumeSize = true;
		boolean isValidIndexPageNum = true;
		if (dataUsingAutoVolButton.getSelection()) {
			isValidDataOutRate = ValidateUtil.isNumber(dataOutRate);
			if (isValidDataOutRate) {
				isValidDataOutRate = Integer.parseInt(dataOutRate) >= 5
						&& Integer.parseInt(dataOutRate) <= 30;
			}
			isValidDataVolumeSize = ValidateUtil.isNumber(dataVolumeSize)
					|| ValidateUtil.isPositiveDouble(dataVolumeSize);
			if (isValidDataVolumeSize) {
				isValidDataVolumeSize = Double.parseDouble(dataVolumeSize) > 0;
			}
			isValidDataPageNum = ValidateUtil.isNumber(dataPageNum);
			if (isValidDataPageNum) {
				isValidDataPageNum = Double.parseDouble(dataPageNum) > 0;
			}
		}
		if (indexUsingAutoVolButton.getSelection()) {
			isValidIndexOutRate = ValidateUtil.isNumber(indexOutRate);
			if (isValidIndexOutRate) {
				isValidIndexOutRate = Integer.parseInt(indexOutRate) >= 5
						&& Integer.parseInt(indexOutRate) <= 30;
			}
			isValidIndexVolumeSize = ValidateUtil.isNumber(indexVolumeSize)
					|| ValidateUtil.isPositiveDouble(dataVolumeSize);
			if (isValidIndexVolumeSize) {
				isValidIndexVolumeSize = Double.parseDouble(indexVolumeSize) > 0;
			}
			isValidIndexPageNum = ValidateUtil.isNumber(indexPageNum);
			if (isValidIndexPageNum) {
				isValidIndexPageNum = Double.parseDouble(indexPageNum) > 0;
			}
		}
		if (!isValidDataOutRate) {
			setErrorMessage(Messages.errDataOutOfSpace);
		} else if (!isValidDataVolumeSize) {
			setErrorMessage(Messages.errDataVolumeSize);
		} else if (!isValidDataPageNum) {
			setErrorMessage(Messages.errDataVolumePageNum);
		} else if (!isValidIndexOutRate) {
			setErrorMessage(Messages.errIndexOutOfSpace);
		} else if (!isValidIndexVolumeSize) {
			setErrorMessage(Messages.errIndexVolumeSize);
		} else if (!isValidIndexPageNum) {
			setErrorMessage(Messages.errIndexVolumePageNum);
		}
		boolean isEnabled = isValidDataOutRate && isValidDataVolumeSize
				&& isValidDataPageNum && isValidIndexOutRate
				&& isValidIndexVolumeSize && isValidIndexPageNum;
		if (isEnabled) {
			setErrorMessage(null);
		}
		setPageComplete(isEnabled);
	}

	public void pageChanged(PageChangedEvent event) {
		IWizardPage page = (IWizardPage) event.getSelectedPage();
		if (page.getName().equals(PAGENAME)) {
			GeneralInfoPage generalInfoPage = (GeneralInfoPage) getWizard().getPage(
					GeneralInfoPage.PAGENAME);
			pageSize = generalInfoPage.getPageSize();
			String genericVolumeSize = generalInfoPage.getGenericVolumeSize();
			if (dataVolumeText.getText().trim().length() <= 0)
				dataVolumeText.setText(genericVolumeSize);
			else
				dataVolumeText.setText(dataVolumeText.getText());
			if (indexVolumeText.getText().trim().length() <= 0)
				indexVolumeText.setText(genericVolumeSize);
			else
				indexVolumeText.setText(indexVolumeText.getText());

			String genericVolumePageNum = generalInfoPage.getGenericPageNum();
			if (dataExtPageText.getText().trim().length() <= 0)
				dataExtPageText.setText(genericVolumePageNum);
			else
				dataExtPageText.setText(dataExtPageText.getText());

			if (indexExtPageText.getText().trim().length() <= 0)
				indexExtPageText.setText(genericVolumePageNum);
			else
				indexExtPageText.setText(indexExtPageText.getText());

			VolumeInfoPage volumeInfoPage = (VolumeInfoPage) getWizard().getPage(
					VolumeInfoPage.PAGENAME);
			volumeList = volumeInfoPage.getVolumeList();
			changeButtonStatus(true);
		}
	}

	/**
	 * 
	 * Change button status
	 * 
	 */
	public void changeButtonStatus(boolean isTestSelection) {
		boolean isHasDataVolume = false;
		boolean isHasIndexVolume = false;
		if (volumeList != null) {
			for (int i = 0; i < volumeList.size(); i++) {
				Map<String, String> map = volumeList.get(i);
				String type = map.get("1");
				if (type.equals("data")) {
					isHasDataVolume = true;
				}
				if (type.equals("index")) {
					isHasIndexVolume = true;
				}
			}
		}
		dataUsingAutoVolButton.setEnabled(isHasDataVolume);
		if (!isHasDataVolume && isTestSelection) {
			dataUsingAutoVolButton.setSelection(false);
		}
		if (!isHasDataVolume || !dataUsingAutoVolButton.getSelection()) {
			dataOutRateCombo.setEnabled(false);
			dataVolumeText.setEnabled(false);
			dataExtPageText.setEnabled(false);
		} else if (isHasDataVolume && dataUsingAutoVolButton.getSelection()) {
			dataOutRateCombo.setEnabled(true);
			dataVolumeText.setEnabled(true);
			dataExtPageText.setEnabled(true);
		}
		indexUsingAutoVolButton.setEnabled(isHasIndexVolume);
		if (!isHasIndexVolume && isTestSelection) {
			indexUsingAutoVolButton.setSelection(false);
		}
		if (!isHasIndexVolume || !indexUsingAutoVolButton.getSelection()) {
			indexOutRateCombo.setEnabled(false);
			indexVolumeText.setEnabled(false);
			indexExtPageText.setEnabled(false);
		} else if (isHasIndexVolume && indexUsingAutoVolButton.getSelection()) {
			indexOutRateCombo.setEnabled(true);
			indexVolumeText.setEnabled(true);
			indexExtPageText.setEnabled(true);
		}
		if (!isHasDataVolume && !isHasIndexVolume) {
			setMessage(Messages.errNoIndexAndDataVolume);
		} else {
			setMessage(Messages.msgWizardPageAuto);
		}
		valid();
	}

	/**
	 * 
	 * Get auto adding volume information
	 * 
	 * @return
	 */
	public GetAutoAddVolumeInfo getAutoAddVolumeInfo() {
		if (!dataUsingAutoVolButton.getSelection()
				&& !indexUsingAutoVolButton.getSelection()) {
			return null;
		}
		GetAutoAddVolumeInfo autoAddVolumeInfo = new GetAutoAddVolumeInfo();
		if (dataUsingAutoVolButton.getSelection()) {
			autoAddVolumeInfo.setData(OnOffType.ON.getText());
			String pageNum = dataExtPageText.getText();
			double rate = Double.parseDouble(dataOutRateCombo.getText()) / 100;
			autoAddVolumeInfo.setData_ext_page(pageNum);
			autoAddVolumeInfo.setData_warn_outofspace(String.valueOf(rate));
		} else {
			autoAddVolumeInfo.setData(OnOffType.OFF.getText());
			autoAddVolumeInfo.setData_ext_page("0.0");
			autoAddVolumeInfo.setData_warn_outofspace("0.0");
		}
		if (indexUsingAutoVolButton.getSelection()) {
			autoAddVolumeInfo.setIndex(OnOffType.ON.getText());
			String pageNum = indexExtPageText.getText();
			double rate = Double.parseDouble(indexOutRateCombo.getText()) / 100;
			autoAddVolumeInfo.setIndex_ext_page(pageNum);
			autoAddVolumeInfo.setIndex_warn_outofspace(String.valueOf(rate));
		} else {
			autoAddVolumeInfo.setIndex(OnOffType.OFF.getText());
			autoAddVolumeInfo.setIndex_ext_page("0.0");
			autoAddVolumeInfo.setIndex_warn_outofspace("0.0");
		}
		return autoAddVolumeInfo;
	}

	/**
	 * 
	 * Set data volume size
	 * 
	 * @param size
	 */
	public void setDataVolumeSize(String size) {
		if (dataVolumeText != null && !dataVolumeText.isDisposed()) {
			dataVolumeText.setText(size);
		}
	}

	/**
	 * 
	 * Set data page number
	 * 
	 * @param pageNum
	 */
	public void setDataPageNum(String pageNum) {
		if (dataExtPageText != null && !dataExtPageText.isDisposed()) {
			dataExtPageText.setText(pageNum);
		}
	}

	/**
	 * 
	 * Set index volume size
	 * 
	 * @param size
	 */
	public void setIndexVolumeSize(String size) {
		if (indexVolumeText != null && !indexVolumeText.isDisposed()) {
			indexVolumeText.setText(size);
		}
	}

	/**
	 * 
	 * Set index page number
	 * 
	 * @param pageNum
	 */
	public void setIndexPageNum(String pageNum) {
		if (indexExtPageText != null && !indexExtPageText.isDisposed()) {
			indexExtPageText.setText(pageNum);
		}
	}

	/**
	 * 
	 * Set using auto data volume
	 * 
	 * @param isUsingAutoDataVolume
	 */
	public void setUsingAutoDataVolume(boolean isUsingAutoDataVolume) {
		if (dataUsingAutoVolButton != null
				&& !dataUsingAutoVolButton.isDisposed()
				&& !isSelectedUsingAutoDataVolume) {
			dataUsingAutoVolButton.setSelection(isUsingAutoDataVolume);
		}
	}

	/**
	 * 
	 * Set using auto index volume
	 * 
	 * @param isUsingAutoIndexVolume
	 */
	public void setUsingAutoIndexVolume(boolean isUsingAutoIndexVolume) {
		if (indexUsingAutoVolButton != null
				&& !indexUsingAutoVolButton.isDisposed()
				&& !isSelectedUsingAutoIndexVolume) {
			indexUsingAutoVolButton.setSelection(isUsingAutoIndexVolume);
		}
	}
}
