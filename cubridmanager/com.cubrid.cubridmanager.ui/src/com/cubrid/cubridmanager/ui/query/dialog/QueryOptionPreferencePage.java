/*
 * Copyright (C) 2009 Search Solution Corporation. All rights reserved by Search Solution. 
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
package com.cubrid.cubridmanager.ui.query.dialog;

import org.eclipse.jface.preference.PreferencePage;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Control;
import org.eclipse.ui.IWorkbench;
import org.eclipse.ui.IWorkbenchPreferencePage;
import org.eclipse.ui.PlatformUI;
import org.eclipse.ui.help.IWorkbenchHelpSystem;

import com.cubrid.cubridmanager.help.CubridManagerHelpContextIDs;
import com.cubrid.cubridmanager.ui.query.Messages;
import com.cubrid.cubridmanager.ui.query.control.QueryPropertyComposite;
import com.cubrid.cubridmanager.ui.spi.model.CubridServer;

/**
 * Preference page for query options
 * 
 * @author wangsl 2009-3-24
 */
public class QueryOptionPreferencePage extends
		PreferencePage implements
		IWorkbenchPreferencePage {

	private QueryPropertyComposite container;
	private CubridServer server = null;

	public QueryOptionPreferencePage() {
		super(Messages.queryEditorTitle, null);
		noDefaultAndApplyButton();
	}

	public QueryOptionPreferencePage(CubridServer server) {
		super(Messages.queryTitle, null);
		noDefaultAndApplyButton();
		this.server = server;
	}

	public void init(IWorkbench workbench) {

	}

	private void loadPreference() {
		container.loadPreference();
	}

	@Override
	protected void performDefaults() {
		super.performDefaults();
	}

	@Override
	public boolean performOk() {
		if (!checkValues()) {
			return false;
		}
		if (container != null) {
			container.save();
		}
		return true;
	}

	/**
	 * check input value
	 * 
	 * @return
	 */
	private boolean checkValues() {
		return true;
	}

	@Override
	protected Control createContents(Composite parent) {
		IWorkbenchHelpSystem whs = PlatformUI.getWorkbench().getHelpSystem();
		whs.setHelp(parent, CubridManagerHelpContextIDs.queryOption);
		container = new QueryPropertyComposite(parent, server);
		loadPreference();
		return container;
	}

}
