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
package com.cubrid.cubridmanager.ui.query.control;

import org.apache.log4j.Logger;
import org.eclipse.core.runtime.Preferences;
import org.eclipse.swt.SWT;
import org.eclipse.swt.events.SelectionAdapter;
import org.eclipse.swt.events.SelectionEvent;
import org.eclipse.swt.graphics.FontData;
import org.eclipse.swt.graphics.RGB;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.widgets.Combo;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.FileDialog;
import org.eclipse.swt.widgets.FontDialog;
import org.eclipse.swt.widgets.Group;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Spinner;
import org.eclipse.swt.widgets.Text;
import org.eclipse.ui.IEditorPart;
import org.eclipse.ui.IEditorReference;
import org.eclipse.ui.IWorkbenchPage;
import org.eclipse.ui.PlatformUI;

import com.cubrid.cubridmanager.core.CubridManagerCorePlugin;
import com.cubrid.cubridmanager.core.common.StringUtil;
import com.cubrid.cubridmanager.core.common.log.LogUtil;
import com.cubrid.cubridmanager.core.query.QueryOptions;
import com.cubrid.cubridmanager.ui.query.Messages;
import com.cubrid.cubridmanager.ui.query.editor.QueryEditorPart;
import com.cubrid.cubridmanager.ui.spi.model.CubridServer;

/**
 * a composite to show query property page
 * 
 * @author wangsl 2009-6-4
 */
public class QueryPropertyComposite extends
		Composite {

	private Button useDefDriverBtn;
	private static final Logger logger = LogUtil.getLogger(QueryPropertyComposite.class);

	private Text driver;
	private Button autocommitBtn;
	private Button searchUnitBtn;
	private Spinner pageUnitCountSpinner;
	private Spinner unitCountSpinner;
	private Button queryPlanBtn;
	private Button oidBtn;
	private Button charSetBtn;
	private Combo charsetText;
	private Text fontText;
	private Text sizeText;
	private Button changeBtn;
	private Button restoreBtn;
	private CubridServer server;
	protected int fontColorBlue;
	protected int fontColorGreen;
	protected int fontColorRed;
	protected String fontString = "";
	protected int fontStyle;
	private Button fileButton;

	public QueryPropertyComposite(Composite parent, CubridServer server) {
		super(parent, SWT.NONE);
		this.server = server;
		createContent();
	}

	/**
	 * load query option from preference store
	 */
	public void loadPreference() {
		Preferences pref = CubridManagerCorePlugin.getDefault().getPluginPreferences();
		String prefix = "";
		if (server != null
				&& pref.contains(server.getServerInfo().getHostAddress()
						+ QueryOptions.PROPERTY)) {
			prefix = server.getServerInfo().getHostAddress();
		}
		boolean autoCommit = QueryOptions.getAutoCommit(server != null ? server.getServerInfo()
				: null);
		boolean unitInstances = pref.getBoolean(prefix
				+ QueryOptions.ENABLE_UNIT_INSTANCES);
		int recordCount = pref.getInt(prefix
				+ QueryOptions.UNIT_INSTANCES_COUNT);
		if (recordCount <= 0) {
			recordCount = QueryOptions.defaultMaxRecordlimit;
		}
		int pageCount = pref.getInt(prefix + QueryOptions.PAGE_INSTANCES_COUNT);
		if (pageCount <= 0) {
			pageCount = QueryOptions.defaultMaxPagelimit;
		}
		boolean enablePlan = pref.getBoolean(prefix
				+ QueryOptions.ENABLE_QUERY_PLAN);
		boolean enableOid = pref.getBoolean(prefix
				+ QueryOptions.ENABLE_GET_OID);

		String fontName = pref.getString(prefix + QueryOptions.FONT_NAME);
		fontColorRed = pref.getInt(prefix + QueryOptions.FONT_RGB_RED);
		fontColorBlue = pref.getInt(prefix + QueryOptions.FONT_RGB_BLUE);
		fontColorGreen = pref.getInt(prefix + QueryOptions.FONT_RGB_GREEN);
		fontString = pref.getString(prefix + QueryOptions.FONT_STRING);
		fontStyle = pref.getInt(prefix + QueryOptions.FONT_STYLE);
		String size = pref.getString(prefix + QueryOptions.SIZE);

		autocommitBtn.setSelection(autoCommit);
		searchUnitBtn.setSelection(unitInstances);
		unitCountSpinner.setEnabled(unitInstances);
		unitCountSpinner.setSelection(recordCount);
		pageUnitCountSpinner.setSelection(pageCount);
		queryPlanBtn.setSelection(enablePlan);
		oidBtn.setSelection(enableOid);
		if (server != null) {
			String driverPath = pref.getString(prefix
					+ QueryOptions.DRIVER_PATH);
			boolean useDefDriver = pref.getBoolean(prefix
					+ QueryOptions.USE_DEFAULT_DRIVER)
					|| driverPath == null || driverPath.equals("");
			useDefDriverBtn.setSelection(useDefDriver);
			if (useDefDriver) {
				driver.setEnabled(false);
				fileButton.setEnabled(false);
			} else {
				driver.setText(driverPath);
			}
		} else {
			String charset = QueryOptions.getCharset(null);
			boolean enableCharset = QueryOptions.getEnableCharset(null);
			charSetBtn.setSelection(enableCharset);
			charsetText.setEnabled(enableCharset);
			charsetText.setText(charset);
		}
		fontText.setText(fontName);
		sizeText.setText(size);
		// set default value
	}

	/**
	 * 
	 * save query options
	 */
	public void save() {
		try {
			String prefix = null;
			if (server != null) {
				prefix = server.getServerInfo().getHostAddress();
			} else {
				prefix = "";
			}

			boolean autocommit = autocommitBtn.getSelection();
			boolean enbaleSearchUnit = searchUnitBtn.getSelection();
			int unitCount = unitCountSpinner.getSelection();
			int pageUnitCount = pageUnitCountSpinner.getSelection();
			boolean queryPlan = queryPlanBtn.getSelection();
			boolean oid = oidBtn.getSelection();
			String font = fontText.getText();
			String size = sizeText.getText();
			Preferences pref = CubridManagerCorePlugin.getDefault().getPluginPreferences();
			pref.setValue(prefix + QueryOptions.AUTO_COMMIT, autocommit);
			pref.setValue(prefix + QueryOptions.PROPERTY, true);
			pref.setValue(prefix + QueryOptions.ENABLE_UNIT_INSTANCES,
					enbaleSearchUnit);
			pref.setValue(prefix + QueryOptions.UNIT_INSTANCES_COUNT, unitCount);
			pref.setValue(prefix + QueryOptions.PAGE_INSTANCES_COUNT,
					pageUnitCount);
			pref.setValue(prefix + QueryOptions.ENABLE_QUERY_PLAN, queryPlan);
			pref.setValue(prefix + QueryOptions.ENABLE_GET_OID, oid);
			if (server != null) {
				pref.setValue(prefix + QueryOptions.USE_DEFAULT_DRIVER,
						useDefDriverBtn.getSelection());
				if (!useDefDriverBtn.getSelection()) {
					String driverPath = driver.getText();
					pref.setValue(prefix + QueryOptions.DRIVER_PATH, driverPath);
				}
			} else {
				boolean charset = charSetBtn.getSelection();
				String charsetStr = charsetText.getText();
				pref.setValue(prefix + QueryOptions.ENABLE_CHAR_SET, charset);
				pref.setValue(prefix + QueryOptions.CHAR_SET, charsetStr);
			}
			pref.setValue(prefix + QueryOptions.FONT_NAME, font);
			pref.setValue(prefix + QueryOptions.FONT_RGB_BLUE, fontColorBlue);
			pref.setValue(prefix + QueryOptions.FONT_RGB_GREEN, fontColorGreen);
			pref.setValue(prefix + QueryOptions.FONT_RGB_RED, fontColorRed);
			pref.setValue(prefix + QueryOptions.FONT_STRING, fontString);
			pref.setValue(prefix + QueryOptions.FONT_STYLE, fontStyle);
			pref.setValue(prefix + QueryOptions.SIZE, size);
			CubridManagerCorePlugin.getDefault().savePluginPreferences();
		} catch (Exception e) {
			logger.error(StringUtil.getStackTrace(e, "\n"));
		}
		IWorkbenchPage page = PlatformUI.getWorkbench().getActiveWorkbenchWindow().getActivePage();
		if (page != null) {
			IEditorReference[] editorRefs = page.getEditorReferences();
			for (IEditorReference ref : editorRefs) {
				IEditorPart editor = ref.getEditor(true);
				if (editor != null && editor instanceof QueryEditorPart) {
					QueryEditorPart part = (QueryEditorPart) editor;
					part.refreshQueryOptions();
				}

			}
		}
	}

	private void createContent() {
		setLayout(new GridLayout());
		setLayoutData(new GridData(SWT.FILL, SWT.FILL, true, true));

		final Group autocommitGroup = new Group(this, SWT.NONE);
		final GridData gd_autocommitGroup = new GridData(SWT.FILL, SWT.CENTER,
				true, false);
		autocommitGroup.setLayoutData(gd_autocommitGroup);
		autocommitGroup.setLayout(new GridLayout());

		autocommitBtn = new Button(autocommitGroup, SWT.CHECK);
		final GridData gd_autocommitbtnButton = new GridData(SWT.FILL,
				SWT.CENTER, true, false);
		autocommitBtn.setLayoutData(gd_autocommitbtnButton);
		autocommitBtn.setText(Messages.autoCommitLabel);

		final Group group_1 = new Group(this, SWT.NONE);
		group_1.setLayoutData(new GridData(SWT.FILL, SWT.CENTER, true, false));
		group_1.setLayout(new GridLayout());

		final Composite composite = new Composite(group_1, SWT.NONE);
		composite.setLayoutData(new GridData(SWT.FILL, SWT.CENTER, true, false));
		final GridLayout gridLayout = new GridLayout();
		gridLayout.numColumns = 2;
		composite.setLayout(gridLayout);

		searchUnitBtn = new Button(composite, SWT.CHECK);
		final GridData gd_searchUnitBtn = new GridData(SWT.LEFT, SWT.CENTER,
				true, false);
		searchUnitBtn.setLayoutData(gd_searchUnitBtn);
		searchUnitBtn.setText(Messages.searchUnitInstances);
		searchUnitBtn.addSelectionListener(new SelectionAdapter() {

			@Override
			public void widgetSelected(SelectionEvent e) {
				unitCountSpinner.setEnabled(searchUnitBtn.getSelection());
			}

		});
		unitCountSpinner = new Spinner(composite, SWT.BORDER);
		unitCountSpinner.setMaximum(2147483647);
		final GridData gd_unitCountSpinner = new GridData(SWT.RIGHT,
				SWT.CENTER, false, false);
		gd_unitCountSpinner.widthHint = 129;
		unitCountSpinner.setLayoutData(gd_unitCountSpinner);

		final Label label = new Label(composite, SWT.NONE);
		label.setText(Messages.pageUnitInstances);

		pageUnitCountSpinner = new Spinner(composite, SWT.BORDER);
		pageUnitCountSpinner.setMaximum(2147483647);
		final GridData gd_pageUnitSpinner = new GridData(SWT.RIGHT, SWT.CENTER,
				false, false);
		gd_pageUnitSpinner.widthHint = 129;
		pageUnitCountSpinner.setLayoutData(gd_pageUnitSpinner);

		queryPlanBtn = new Button(composite, SWT.CHECK);
		queryPlanBtn.setText(Messages.enableQueryPlan);
		new Label(composite, SWT.NONE);

		oidBtn = new Button(composite, SWT.CHECK);
		oidBtn.setText(Messages.getOid);
		new Label(composite, SWT.NONE);

		final Group group_2 = new Group(this, SWT.NONE);
		group_2.setLayoutData(new GridData(SWT.FILL, SWT.CENTER, true, false));
		group_2.setLayout(new GridLayout());

		final Composite composite_1 = new Composite(group_2, SWT.NONE);
		composite_1.setLayoutData(new GridData(SWT.FILL, SWT.CENTER, true,
				false));
		GridLayout gridLayout2 = new GridLayout();
		gridLayout2.numColumns = 3;
		composite_1.setLayout(gridLayout2);

		if (server != null) {

			useDefDriverBtn = new Button(composite_1, SWT.CHECK);
			final GridData gd_useDefDriverBtn = new GridData(SWT.LEFT,
					SWT.CENTER, false, false, 3, 1);
			useDefDriverBtn.setLayoutData(gd_useDefDriverBtn);
			useDefDriverBtn.setText(Messages.useDefDriver);
			useDefDriverBtn.setEnabled(false);
			useDefDriverBtn.addSelectionListener(new SelectionAdapter() {

				@Override
				public void widgetSelected(SelectionEvent e) {
					driver.setEnabled(!useDefDriverBtn.getSelection());
					fileButton.setEnabled(!useDefDriverBtn.getSelection());
				}

			});
			final Label jdbcDriverLabel = new Label(composite_1, SWT.NONE);
			jdbcDriverLabel.setText(Messages.jdbcDriver);
			driver = new Text(composite_1, SWT.BORDER);
			final GridData gd_driver = new GridData(SWT.FILL, SWT.CENTER, true,
					false);
			driver.setLayoutData(gd_driver);
			fileButton = new Button(composite_1, SWT.NONE);
			fileButton.setText(Messages.choose);
			fileButton.addSelectionListener(new SelectionAdapter() {

				@Override
				public void widgetSelected(SelectionEvent e) {
					FileDialog dialog = new FileDialog(getShell());
					dialog.setFilterExtensions(new String[] { "*.jar" });
					String path = dialog.open();
					if (path != null) {
						driver.setText(path);
					}
				}

			});
		} else {
			charSetBtn = new Button(composite_1, SWT.CHECK);
			final GridData gd_charSetBtn = new GridData(SWT.FILL, SWT.CENTER,
					false, false);
			charSetBtn.setLayoutData(gd_charSetBtn);
			charSetBtn.setText(Messages.charSet);
			charSetBtn.addSelectionListener(new SelectionAdapter() {

				@Override
				public void widgetSelected(SelectionEvent e) {
					charsetText.setEnabled(charSetBtn.getSelection());
				}

			});
			charsetText = new Combo(composite_1, SWT.BORDER);
			final GridData gd_charsetText = new GridData(SWT.FILL, SWT.CENTER,
					true, false, 2, 1);
			charsetText.setLayoutData(gd_charsetText);
			charsetText.setItems(QueryOptions.ALlCHARSET);
		}
		final Group changeFontGroup = new Group(this, SWT.NONE);
		changeFontGroup.setText(Messages.changeFont);
		final GridData gd_changeFontGroup = new GridData(SWT.FILL, SWT.CENTER,
				true, false);
		changeFontGroup.setLayoutData(gd_changeFontGroup);
		changeFontGroup.setLayout(new GridLayout());

		final Composite composite_2 = new Composite(changeFontGroup, SWT.NONE);
		composite_2.setLayoutData(new GridData(SWT.FILL, SWT.CENTER, true,
				false));
		final GridLayout gridLayout_1 = new GridLayout();
		gridLayout_1.numColumns = 3;
		composite_2.setLayout(gridLayout_1);

		final Label fontLabel = new Label(composite_2, SWT.NONE);
		fontLabel.setText(Messages.font);

		fontText = new Text(composite_2, SWT.BORDER);
		final GridData gd_fontText = new GridData(SWT.FILL, SWT.CENTER, true,
				false);
		fontText.setLayoutData(gd_fontText);

		changeBtn = new Button(composite_2, SWT.NONE);
		final GridData gd_changeBtn = new GridData(SWT.FILL, SWT.CENTER, false,
				false);
		changeBtn.setLayoutData(gd_changeBtn);
		changeBtn.setText(Messages.change);
		changeBtn.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
			public void widgetSelected(org.eclipse.swt.events.SelectionEvent e) {
				FontDialog dlg = new FontDialog(getShell());
				FontData fontdata = null;
				if (fontString != null && !fontString.equals("")) {
					fontdata = new FontData(fontString);
					fontdata.setStyle(fontStyle);
					FontData fontList[] = new FontData[1];
					fontList[0] = fontdata;
					dlg.setRGB(new RGB(fontColorRed, fontColorGreen,
							fontColorBlue));
					dlg.setFontList(fontList);
				}

				fontdata = dlg.open();

				if (fontdata != null) {
					fontString = fontdata.toString();
					fontText.setText(fontdata.getName());
					fontStyle = fontdata.getStyle();
					sizeText.setText(String.valueOf(fontdata.getHeight()));
					RGB rgb = dlg.getRGB();
					if (rgb != null) {
						fontColorRed = rgb.red;
						fontColorBlue = rgb.blue;
						fontColorGreen = rgb.green;
					}
				}
			}
		});

		final Label sizeLabel = new Label(composite_2, SWT.NONE);
		sizeLabel.setText(Messages.size);

		sizeText = new Text(composite_2, SWT.BORDER);
		final GridData gd_sizeText = new GridData(SWT.FILL, SWT.CENTER, true,
				false);
		sizeText.setLayoutData(gd_sizeText);

		restoreBtn = new Button(composite_2, SWT.TOGGLE);
		restoreBtn.setLayoutData(new GridData(SWT.FILL, SWT.CENTER, false,
				false));
		restoreBtn.setText(Messages.restoreDefault);
		restoreBtn.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
			public void widgetSelected(org.eclipse.swt.events.SelectionEvent e) {
				fontString = "";
				fontColorRed = 0;
				fontColorGreen = 0;
				fontColorBlue = 0;
				fontText.setText("");
				sizeText.setText("");
				fontStyle = 0;
			}
		});
	}

}
