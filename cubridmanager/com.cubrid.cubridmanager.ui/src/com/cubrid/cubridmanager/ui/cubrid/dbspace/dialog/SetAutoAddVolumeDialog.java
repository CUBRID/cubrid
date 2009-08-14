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
import java.text.NumberFormat;

import org.eclipse.jface.dialogs.IDialogConstants;
import org.eclipse.swt.SWT;
import org.eclipse.swt.events.FocusEvent;
import org.eclipse.swt.events.FocusListener;
import org.eclipse.swt.events.ModifyEvent;
import org.eclipse.swt.events.ModifyListener;
import org.eclipse.swt.events.SelectionAdapter;
import org.eclipse.swt.events.SelectionEvent;
import org.eclipse.swt.events.VerifyEvent;
import org.eclipse.swt.events.VerifyListener;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.widgets.Combo;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Control;
import org.eclipse.swt.widgets.Group;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.Text;

import com.cubrid.cubridmanager.core.common.model.OnOffType;
import com.cubrid.cubridmanager.core.cubrid.dbspace.model.GetAutoAddVolumeInfo;
import com.cubrid.cubridmanager.help.CubridManagerHelpContextIDs;
import com.cubrid.cubridmanager.ui.cubrid.dbspace.Messages;
import com.cubrid.cubridmanager.ui.spi.CommonTool;
import com.cubrid.cubridmanager.ui.spi.ValidateUtil;
import com.cubrid.cubridmanager.ui.spi.dialog.CMTitleAreaDialog;

/**
 * A dialog that show up when a user click the Database space context menu.
 * 
 * @author lizhiqiang 2009-3-16
 */
public class SetAutoAddVolumeDialog extends
		CMTitleAreaDialog {

	private String dialogTitle = Messages.setDialogTitle;
	private String dialogMsg = Messages.setDialogMsg;
	private String dataGroupTitle = Messages.dataGroupTitle;
	private String dataUseAutoVolBtnText = Messages.dataUseAutoVolBtnText;
	private String dataOutOfSpaceRateLbl = Messages.dataOutOfSpaceRateLbl;
	private String dataExtPageLbl = Messages.dataExtPageLbl;
	private String indexGroupTitle = Messages.indexGroupTitle;
	private String indexUseAutoVolBtnText = Messages.indexUseAutoVolBtnText;
	private String indexOutOfSpaceRateLbl = Messages.indexOutOfSpaceRateLbl;
	private String indexExtPageLbl = Messages.indexExtPageLbl;
	private String datavolumeLbl = Messages.datavolumeLbl;
	private String indexvolumeLbl = Messages.indexvolumeLbl;

	private Button dataUsingAutoVolButton;
	private Button indexUsingAutoVolButton;
	private Combo dataOutRateCombo;
	private Combo indexOutRateCombo;
	private Text indexExtPageText;
	private Text dataExtPageText;
	private static final int INITPAGE = 10240;

	// private String errExtPageText = "Error extent page";

	private GetAutoAddVolumeInfo getAutoAddVolumeInfo;
	private Text dataVolumeText;
	private Text indexVolumeText;
	private BigDecimal pageSize;

	private int rateMin = 5;
	private int rateMax = 30;

	private final static BigDecimal MEGABYTES = new BigDecimal(1024 * 1024);
	private static final String ERROR_PAGE = Messages.errorPage;
	private String ERROR_RATE = Messages.bind(Messages.errorRate, rateMin,
			rateMax);
	private String ERROR_VOLUME = Messages.errorVolume;
	private String[] itemsOfOutRate;
	private boolean[] isOkenable;

	private static BigDecimal initDataVol;
	private static BigDecimal initIndexVol;

	private VolumeModifyListener dataVolumeModifyListener;
	private VolumeModifyListener indexVolumeModifyListener;
	private PageModifyListener dataPageModifyListener;
	private PageModifyListener indexPageModifyListener;

	private String initDataExt;
	private String initIndexExt;

	/**
	 * The constructor
	 * 
	 * @param parentShell
	 */
	public SetAutoAddVolumeDialog(Shell parentShell) {
		super(parentShell);
		isOkenable = new boolean[6];
		for (int i = 0; i < isOkenable.length; i++) {
			isOkenable[i] = true;
		}

	}

	@Override
	protected Control createDialogArea(Composite parent) {
		Composite parentComp = (Composite) super.createDialogArea(parent);
		getHelpSystem().setHelp(parentComp, CubridManagerHelpContextIDs.databaseSpace);		
		setTitle(dialogTitle);
		setMessage(dialogMsg);
		final Composite composite = new Composite(parentComp, SWT.RESIZE);
		final GridData gdComposite = new GridData(SWT.FILL, SWT.CENTER, true,
				false);
		gdComposite.widthHint = 500;
		composite.setLayoutData(gdComposite);
		final GridLayout gridLayout = new GridLayout();
		gridLayout.marginHeight = convertVerticalDLUsToPixels(IDialogConstants.VERTICAL_MARGIN);
		gridLayout.marginWidth = convertHorizontalDLUsToPixels(IDialogConstants.HORIZONTAL_MARGIN);
		gridLayout.verticalSpacing = convertVerticalDLUsToPixels(IDialogConstants.VERTICAL_SPACING);
		gridLayout.horizontalSpacing = convertHorizontalDLUsToPixels(IDialogConstants.HORIZONTAL_SPACING);
		composite.setLayout(gridLayout);

		createDataParaGroup(composite);
		createIndexParaGroup(composite);
		init();
		return parentComp;

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
		dataParaGroup.setText(dataGroupTitle);
		GridLayout groupLayout = new GridLayout(4, false);
		dataParaGroup.setLayout(groupLayout);

		dataUsingAutoVolButton = new Button(dataParaGroup, SWT.CHECK);
		final GridData gd_usingAutoVolButton = new GridData(SWT.LEFT,
				SWT.CENTER, true, false, 4, 1);
		dataUsingAutoVolButton.setLayoutData(gd_usingAutoVolButton);
		dataUsingAutoVolButton.setText(dataUseAutoVolBtnText);

		final Label outOfSpaceLabel = new Label(dataParaGroup, SWT.NONE);
		outOfSpaceLabel.setLayoutData(new GridData(SWT.LEFT, SWT.CENTER, false,
				false));
		outOfSpaceLabel.setText(dataOutOfSpaceRateLbl);

		dataOutRateCombo = new Combo(dataParaGroup, SWT.BORDER|SWT.RIGHT);
		dataOutRateCombo.setLayoutData(new GridData(SWT.FILL, SWT.CENTER, true,
				false, 3, 1));

		final Label volumeLabel = new Label(dataParaGroup, SWT.NONE);
		volumeLabel.setText(datavolumeLbl);
		volumeLabel.setLayoutData(new GridData(SWT.FILL, SWT.CENTER, false,
				false, 1, 1));

		dataVolumeText = new Text(dataParaGroup, SWT.BORDER|SWT.RIGHT);
		final GridData gd_dataVolumeText = new GridData(SWT.FILL, SWT.CENTER,
				true, false, 1, 1);
		dataVolumeText.setLayoutData(gd_dataVolumeText);

		final Label extPageLabel = new Label(dataParaGroup, SWT.NONE);
		extPageLabel.setText(dataExtPageLbl);
		extPageLabel.setLayoutData(new GridData(SWT.FILL, SWT.CENTER, false,
				false, 1, 1));

		dataExtPageText = new Text(dataParaGroup, SWT.BORDER|SWT.RIGHT);
		final GridData gd_dataPageText = new GridData(SWT.FILL, SWT.CENTER,
				true, false, 1, 1);
		dataExtPageText.setLayoutData(gd_dataPageText);
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
		indexParaGroup.setText(indexGroupTitle);

		indexUsingAutoVolButton = new Button(indexParaGroup, SWT.CHECK);
		indexUsingAutoVolButton.setLayoutData(new GridData(SWT.LEFT,
				SWT.CENTER, true, false, 4, 1));
		indexUsingAutoVolButton.setText(indexUseAutoVolBtnText);

		final Label outOfSpaceLabel = new Label(indexParaGroup, SWT.NONE);
		outOfSpaceLabel.setLayoutData(new GridData(SWT.LEFT, SWT.CENTER, false,
				false, 1, 1));
		outOfSpaceLabel.setText(indexOutOfSpaceRateLbl);

		indexOutRateCombo = new Combo(indexParaGroup, SWT.BORDER|SWT.RIGHT);
		indexOutRateCombo.setLayoutData(new GridData(SWT.FILL, SWT.CENTER,
				true, false, 3, 1));

		final Label volumeLabel = new Label(indexParaGroup, SWT.NONE);
		volumeLabel.setText(indexvolumeLbl);
		volumeLabel.setLayoutData(new GridData(SWT.FILL, SWT.CENTER, false,
				false, 1, 1));

		indexVolumeText = new Text(indexParaGroup, SWT.BORDER|SWT.RIGHT);
		final GridData gd_dataVolumeText = new GridData(SWT.FILL, SWT.CENTER,
				true, false, 1, 1);
		indexVolumeText.setLayoutData(gd_dataVolumeText);

		final Label extPageLabel = new Label(indexParaGroup, SWT.NONE);
		extPageLabel.setLayoutData(new GridData(SWT.FILL, SWT.CENTER, false,
				false, 1, 1));
		extPageLabel.setText(indexExtPageLbl);

		indexExtPageText = new Text(indexParaGroup, SWT.BORDER|SWT.RIGHT);
		final GridData gd_dataText = new GridData(SWT.FILL, SWT.CENTER, true,
				false, 1, 1);
		indexExtPageText.setLayoutData(gd_dataText);
	}

	/**
	 * 
	 */
	public void okPressed() {
		buildGetAutoAddVolumeInfo();
		super.okPressed();
	}

	/**
	 * Gets the instance of GetAutoAddVolumeInfo
	 * 
	 * @return
	 */
	public GetAutoAddVolumeInfo getGetAutoAddVolumeInfo() {
		return getAutoAddVolumeInfo;
	}

	/**
	 * Sets the instance of GetAutoAddVolumeInfo
	 * 
	 * @param getAutoAddVolumeInfo
	 */
	public void setGetAutoAddVolumeInfo(
			GetAutoAddVolumeInfo getAutoAddVolumeInfo) {
		this.getAutoAddVolumeInfo = getAutoAddVolumeInfo;
	}

	/*
	 * Initials some values
	 * 
	 */
	private void init() {
		int itemsSize = rateMax - rateMin + 1;
		itemsOfOutRate = new String[itemsSize];
		for (int i = 0; i < itemsSize; i++) {
			itemsOfOutRate[i] = Integer.toString(rateMin + i);
		}
		// Sets the initial value
		if (getAutoAddVolumeInfo.getData().equals(OnOffType.ON.getText())) {
			dataUsingAutoVolButton.setSelection(true);
			dataOutRateCombo.setEnabled(true);
			dataVolumeText.setEnabled(true);
			dataExtPageText.setEnabled(true);
		} else {
			dataUsingAutoVolButton.setSelection(false);
			dataOutRateCombo.setEnabled(false);
			dataVolumeText.setEnabled(false);
			dataExtPageText.setEnabled(false);
		}

		if (getAutoAddVolumeInfo.getIndex().equals(OnOffType.ON.getText())) {
			indexUsingAutoVolButton.setSelection(true);
			indexOutRateCombo.setEnabled(true);
			indexVolumeText.setEnabled(true);
			indexExtPageText.setEnabled(true);
		} else {
			indexUsingAutoVolButton.setSelection(false);
			indexOutRateCombo.setEnabled(false);
			indexVolumeText.setEnabled(false);
			indexExtPageText.setEnabled(false);
		}

		int dataWarnOutofSpace = (int) ((Double.parseDouble(getAutoAddVolumeInfo.getData_warn_outofspace()) * 100) + 0.5);
		int indexWarnOutofSpace = (int) ((Double.parseDouble(getAutoAddVolumeInfo.getIndex_warn_outofspace()) * 100) + 0.5);
		if (dataWarnOutofSpace < rateMin) {
			dataWarnOutofSpace = rateMin;
		} else if (dataWarnOutofSpace > rateMax) {
			dataWarnOutofSpace = rateMax;
		}
		if (indexWarnOutofSpace < rateMin) {
			indexWarnOutofSpace = rateMin;
		} else if (dataWarnOutofSpace > rateMax) {
			indexWarnOutofSpace = rateMax;
		}
		dataOutRateCombo.setItems(itemsOfOutRate);
		dataOutRateCombo.setText(Integer.toString(dataWarnOutofSpace));
		indexOutRateCombo.setItems(itemsOfOutRate);
		indexOutRateCombo.setText(Integer.toString(indexWarnOutofSpace));

		BigDecimal dataExtPage = new BigDecimal(
				getAutoAddVolumeInfo.getData_ext_page());
		BigDecimal indexExtPage = new BigDecimal(
				getAutoAddVolumeInfo.getIndex_ext_page());

		if (dataExtPage.compareTo(BigDecimal.ZERO) <= 0) {
			dataExtPage = new BigDecimal(INITPAGE);
		}
		if (indexExtPage.compareTo(BigDecimal.ZERO) <= 0) {
			indexExtPage = new BigDecimal(INITPAGE);
		}
		initDataExt = dataExtPage.setScale(0).toString();
		initIndexExt = indexExtPage.setScale(0).toString();

		dataExtPageText.setText(initDataExt);
		initDataVol = dataExtPage.multiply(pageSize).divide(MEGABYTES, 3,
				BigDecimal.ROUND_HALF_UP);

		dataVolumeText.setText(initDataVol.toString());
		indexExtPageText.setText(initIndexExt);

		initIndexVol = indexExtPage.multiply(pageSize).divide(MEGABYTES, 3,
				BigDecimal.ROUND_HALF_UP);
		indexVolumeText.setText(initIndexVol.toString());
		dataOutRateCombo.addVerifyListener(new NumberVerifyListener());
		dataOutRateCombo.addModifyListener(new DataRateModifyListener());
		dataVolumeModifyListener = new VolumeModifyListener();
		dataPageModifyListener = new PageModifyListener();
		dataVolumeText.addFocusListener(new FocusListener() {

			public void focusGained(FocusEvent e) {
				dataVolumeText.addModifyListener(dataVolumeModifyListener);
			}

			public void focusLost(FocusEvent e) {
				dataVolumeText.removeModifyListener(dataVolumeModifyListener);

			}
		});
		dataExtPageText.addFocusListener(new FocusListener() {

			public void focusGained(FocusEvent e) {
				dataExtPageText.addModifyListener(dataPageModifyListener);
			}

			public void focusLost(FocusEvent e) {
				dataExtPageText.removeModifyListener(dataPageModifyListener);
			}

		});

		indexOutRateCombo.addVerifyListener(new NumberVerifyListener());
		indexOutRateCombo.addModifyListener(new IndexRateModifyListener());
		indexVolumeModifyListener = new VolumeModifyListener();
		indexPageModifyListener = new PageModifyListener();
		indexVolumeText.addFocusListener(new FocusListener() {

			public void focusGained(FocusEvent e) {
				indexVolumeText.addModifyListener(indexVolumeModifyListener);
			}

			public void focusLost(FocusEvent e) {
				indexVolumeText.removeModifyListener(indexVolumeModifyListener);
			}

		});
		indexExtPageText.addFocusListener(new FocusListener() {

			public void focusGained(FocusEvent e) {
				indexExtPageText.addModifyListener(indexPageModifyListener);
			}

			public void focusLost(FocusEvent e) {
				indexExtPageText.removeModifyListener(indexPageModifyListener);
			}

		});

		dataUsingAutoVolButton.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				if (dataUsingAutoVolButton.getSelection()) {
					dataOutRateCombo.setEnabled(true);
					dataVolumeText.setEnabled(true);
					dataExtPageText.setEnabled(true);
				} else {
					dataOutRateCombo.setEnabled(false);
					dataOutRateCombo.setText(Integer.toString(rateMin));
					dataVolumeText.setText(initDataVol.toString());
					dataExtPageText.setText(initDataExt);
					enableOk();
					dataVolumeText.setEnabled(false);
					dataExtPageText.setEnabled(false);
				}
			}
		});
		indexUsingAutoVolButton.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				if (indexUsingAutoVolButton.getSelection()) {
					indexOutRateCombo.setEnabled(true);
					indexVolumeText.setEnabled(true);
					indexExtPageText.setEnabled(true);
				} else {
					indexOutRateCombo.setEnabled(false);
					indexOutRateCombo.setText(Integer.toString(rateMin));
					indexVolumeText.setText(initIndexVol.toString());
					indexExtPageText.setText(initIndexExt);
					enableOk();
					indexVolumeText.setEnabled(false);
					indexExtPageText.setEnabled(false);

				}
			}
		});
	}

	/**
	 * Builds the instance of GetAutoAddVolumeInfo
	 * 
	 */
	private void buildGetAutoAddVolumeInfo() {
		NumberFormat nf = NumberFormat.getInstance();
		nf.setMinimumFractionDigits(2);
		nf.setMaximumFractionDigits(2);
		nf.setGroupingUsed(false);
		if (dataUsingAutoVolButton.getSelection()) {
			getAutoAddVolumeInfo.setData(OnOffType.ON.getText());
			double dataOutRate = Double.valueOf(dataOutRateCombo.getText()) / 100.0;
			getAutoAddVolumeInfo.setData_warn_outofspace(nf.format(dataOutRate));
			getAutoAddVolumeInfo.setData_ext_page(dataExtPageText.getText().trim());
		} else {
			getAutoAddVolumeInfo.setData(OnOffType.OFF.getText());
			getAutoAddVolumeInfo.setData_warn_outofspace("0");
			getAutoAddVolumeInfo.setData_ext_page("0");
		}
		if (indexUsingAutoVolButton.getSelection()) {
			getAutoAddVolumeInfo.setIndex(OnOffType.ON.getText());
			double indexOutRate = Double.valueOf(indexOutRateCombo.getText()) / 100.0;
			getAutoAddVolumeInfo.setIndex_warn_outofspace(nf.format(indexOutRate));
			getAutoAddVolumeInfo.setIndex_ext_page(indexExtPageText.getText().trim());

		} else {
			getAutoAddVolumeInfo.setIndex(OnOffType.OFF.getText());
			getAutoAddVolumeInfo.setIndex_warn_outofspace("0");
			getAutoAddVolumeInfo.setIndex_ext_page("0");

		}

	}

	@Override
	protected void constrainShellSize() {
		super.constrainShellSize();
		CommonTool.centerShell(getShell());
		getShell().setText(dialogTitle);
	}

	public void setPageSize(int pageSize) {
		this.pageSize = new BigDecimal(pageSize);

	}

	/*
	 * A class that verify the entering of rate Spinner
	 */

	private class DataRateModifyListener implements
			ModifyListener {

		public void modifyText(ModifyEvent e) {
			String sRate = ((Combo) e.widget).getText().trim();
			if (sRate.length() == 0) {
				isOkenable[0] = false;
				enableOk();
				return;
			}
			int rate = Integer.valueOf(sRate);
			if (rate > rateMax || rate < rateMin) {
				isOkenable[0] = false;
			} else {
				isOkenable[0] = true;
			}
			enableOk();
		}
	}

	/*
	 * A class that verify the entering of rate Spinner
	 */

	private class IndexRateModifyListener implements
			ModifyListener {

		public void modifyText(ModifyEvent e) {
			String sRate = ((Combo) e.widget).getText().trim();
			if (sRate.length() == 0) {
				isOkenable[2] = false;
				enableOk();
				return;
			}
			int rate = Integer.valueOf(sRate);
			if (rate > rateMax || rate < rateMin) {
				isOkenable[2] = false;
			} else {
				isOkenable[2] = true;
			}
			enableOk();
		}
	}

	/*
	 * A class that verify the entering of volumeText
	 */
	static private class NumberVerifyListener implements
			VerifyListener {

		public void verifyText(VerifyEvent e) {
			if (e.text.equals("")) {
				return;
			}
			if (!ValidateUtil.isNumber(e.text)) {
				e.doit = false;
			} else {
				e.doit = true;
			}
		}
	}

	/*
	 * A class that response to the modify of volumeText
	 */
	private class VolumeModifyListener implements
			ModifyListener {

		public void modifyText(ModifyEvent e) {
			if (e.widget == dataVolumeText) {
				String sVolume = dataVolumeText.getText();

				if (!ValidateUtil.isPositiveDouble(sVolume)) {
					isOkenable[1] = false;
					enableOk();
					return;
				}
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
				dataExtPageText.setText(page.toString());

			} else if (e.widget == indexVolumeText) {
				String sVolume = indexVolumeText.getText();

				if (!ValidateUtil.isPositiveDouble(sVolume)) {
					isOkenable[3] = false;
					enableOk();
					return;
				}
				isOkenable[3] = true;
				enableOk();

				BigDecimal volume = new BigDecimal(sVolume);
				BigDecimal page = volume.multiply(MEGABYTES.divide(pageSize));

				if (page.compareTo(BigDecimal.ONE) < 0) {
					isOkenable[3] = false;
					enableOk();
					return;
				}
				isOkenable[3] = true;
				enableOk();
				page = page.setScale(0, BigDecimal.ROUND_HALF_UP);
				indexExtPageText.setText(page.toString());
			}
		}
	}

	/*
	 * A class that response to the modify of volumeText
	 */
	private class PageModifyListener implements
			ModifyListener {

		public void modifyText(ModifyEvent e) {
			if (e.widget == dataExtPageText) {
				String sPage = dataExtPageText.getText();

				if (!ValidateUtil.isInteger(sPage)) {
					isOkenable[4] = false;
					enableOk();
					return;
				}

				BigDecimal page = new BigDecimal(sPage);
				if (page.compareTo(BigDecimal.ONE) < 0) {
					isOkenable[4] = false;
					enableOk();
				}
				isOkenable[4] = true;
				enableOk();

				BigDecimal volume = page.multiply(pageSize).divide(MEGABYTES,
						3, BigDecimal.ROUND_HALF_UP);
				dataVolumeText.setText(volume.toString());
			} else if (e.widget == indexExtPageText) {
				String sPage = indexExtPageText.getText();

				if (!ValidateUtil.isInteger(sPage)) {
					isOkenable[5] = false;
					enableOk();
					return;
				}

				BigDecimal page = new BigDecimal(sPage);
				if (page.compareTo(BigDecimal.ONE) < 0) {
					isOkenable[5] = false;
					enableOk();
				}
				isOkenable[5] = true;
				enableOk();

				BigDecimal volume = page.multiply(pageSize).divide(MEGABYTES,
						3, BigDecimal.ROUND_HALF_UP);
				indexVolumeText.setText(volume.toString());
			}
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
		if (!isOkenable[0] || !isOkenable[2]) {
			setErrorMessage(ERROR_RATE);
		} else if (!isOkenable[1] || !isOkenable[3]) {
			setErrorMessage(ERROR_VOLUME);
		} else if (!isOkenable[4] || !isOkenable[5]) {
			setErrorMessage(ERROR_PAGE);
		}
		if (is) {
			setErrorMessage(null);
		}

	}

}
