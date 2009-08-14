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
package com.cubrid.cubridmanager.app;

import java.net.URL;

import org.eclipse.core.runtime.IProgressMonitor;
import org.eclipse.swt.SWT;
import org.eclipse.swt.browser.Browser;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Label;
import org.eclipse.ui.IEditorInput;
import org.eclipse.ui.IEditorSite;
import org.eclipse.ui.PartInitException;
import org.eclipse.ui.PlatformUI;
import org.eclipse.ui.browser.IWebBrowser;
import org.eclipse.ui.browser.IWorkbenchBrowserSupport;
import org.eclipse.ui.part.EditorPart;

import com.cubrid.cubridmanager.ui.spi.CommonTool;

/**
 * 
 * CUBRID new information editor part
 * 
 * @author pangqiren
 * @version 1.0 - 2009-6-23 created by pangqiren
 */
public class CubridNewInfoEditorPart extends
		EditorPart {

	public static final String ID = "com.cubrid.cubridmanager.app.CubridNewInfoEditorPart";

	public void doSave(IProgressMonitor monitor) {

	}

	public void doSaveAs() {

	}

	public void init(IEditorSite site, IEditorInput input) throws PartInitException {
		setSite(site);
		setInput(input);
		setTitleToolTip(input.getToolTipText());
	}

	public void dispose() {
	}

	public boolean isDirty() {
		return false;
	}

	public boolean isSaveAsAllowed() {
		return false;
	}

	public void createPartControl(Composite parent) {
		try {
			Browser browser = new Browser(parent, SWT.NONE);
			browser.setUrl(UrlConnUtil.checkNewInfoUrl);
		} catch (Exception e) {
			Label label = new Label(parent, SWT.NONE);
			IWorkbenchBrowserSupport support = PlatformUI.getWorkbench().getBrowserSupport();
			try {
				IWebBrowser browser = support.getExternalBrowser();
				browser.openURL(new URL(
						CommonTool.urlEncodeForSpaces(UrlConnUtil.checkNewInfoUrl.toCharArray())));
			} catch (Exception ignored) {
				label.setText(Messages.errCannotOpenExternalBrowser);
				return;
			}
			label.setText(Messages.errCannotOpenInternalBrowser);
		}
	}

	public void setFocus() {

	}

}
