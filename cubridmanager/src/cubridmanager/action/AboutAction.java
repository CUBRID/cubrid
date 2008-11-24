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

package cubridmanager.action;

import org.eclipse.core.runtime.IProduct;
import org.eclipse.core.runtime.Platform;
import org.eclipse.jface.action.Action;
import org.eclipse.ui.IWorkbenchWindow;
import org.eclipse.ui.actions.ActionFactory;
import org.eclipse.ui.internal.IWorkbenchHelpContextIds;
import cubridmanager.Messages;
import cubridmanager.dialog.AboutDialog;

public class AboutAction extends Action implements
		ActionFactory.IWorkbenchAction {

	/**
	 * The workbench window; or <code>null</code> if this action has been
	 * <code>dispose</code>d.
	 */
	private IWorkbenchWindow workbenchWindow;

	/**
	 * Creates a new <code>AboutAction</code>.
	 * 
	 * @param window
	 *            the window
	 */
	public AboutAction(IWorkbenchWindow window) {
		if (window == null)
			throw new IllegalArgumentException();

		this.workbenchWindow = window;

		// use message with no fill-in
		IProduct product = Platform.getProduct();
		String productName = null;
		if (product != null)
			productName = product.getName();
		if (productName == null)
			productName = ""; //$NON-NLS-1$
		setText(Messages.getString("MENU.ABOUT"));
		setToolTipText(Messages.getString("TOOL.ABOUT"));
		setId("about"); //$NON-NLS-1$
		setActionDefinitionId("org.eclipse.ui.help.aboutAction"); //$NON-NLS-1$
		window.getWorkbench().getHelpSystem().setHelp(this,
				IWorkbenchHelpContextIds.ABOUT_ACTION);
	}

	/*
	 * (non-Javadoc) Method declared on IAction.
	 */
	public void run() {
		// make sure action is not disposed
		if (workbenchWindow != null)
			new AboutDialog(workbenchWindow.getShell()).open();
	}

	/*
	 * (non-Javadoc) Method declared on ActionFactory.IWorkbenchAction.
	 */
	public void dispose() {
		workbenchWindow = null;
	}
}