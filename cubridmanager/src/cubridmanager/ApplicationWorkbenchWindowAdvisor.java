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

package cubridmanager;

import java.util.Properties;

import org.eclipse.jface.action.IStatusLineManager;
import org.eclipse.swt.SWT;
import org.eclipse.swt.graphics.Point;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.ui.application.ActionBarAdvisor;
import org.eclipse.ui.application.IActionBarConfigurer;
import org.eclipse.ui.application.IWorkbenchWindowConfigurer;
import org.eclipse.ui.application.WorkbenchWindowAdvisor;

import com.gpki.gpkiapi.GpkiApi;

public class ApplicationWorkbenchWindowAdvisor extends WorkbenchWindowAdvisor {
	public static IWorkbenchWindowConfigurer myconfigurer = null;
	private Properties prop = new Properties();

	public ApplicationWorkbenchWindowAdvisor(
			IWorkbenchWindowConfigurer configurer) {
		super(configurer);
	}

	public ActionBarAdvisor createActionBarAdvisor(
			IActionBarConfigurer configurer) {
		return new ApplicationActionBarAdvisor(configurer);
	}

	public void preWindowOpen() {
		IWorkbenchWindowConfigurer configurer = getWindowConfigurer();
		configurer.setShowCoolBar(true);
		configurer.setShowStatusLine(true);
		configurer.setShowProgressIndicator(true);
		myconfigurer = configurer;
	}

	public void postWindowCreate() {
		if (!CommonTool.LoadProperties(prop)) {
			CommonTool.SetDefaultParameter();
			CommonTool.LoadProperties(prop);
		}

		if (MainRegistry.isProtegoBuild()) {
			String protegoLoginType = null;
			protegoLoginType = prop.getProperty(MainConstants.protegoLoginType);
			if ((protegoLoginType != null)
					&& (protegoLoginType
							.equals(MainConstants.protegoLoginTypeMtId))) {
				MainRegistry.isCertificateLogin = false;
			} else
				MainRegistry.isCertificateLogin = true;

			ApplicationActionBarAdvisor
					.setCheckCertificationLogin(MainRegistry.isCertificateLogin);
		}

		if (prop.getProperty(MainConstants.mainWindowMaximize) != null
				&& prop.getProperty(MainConstants.mainWindowMaximize).equals(
						"yes"))
			Application.mainwindow.getShell().setMaximized(true);
		else
			Application.mainwindow.getShell().setMaximized(false);

		Point size = new Point(0, 0);

		try {
			size.x = Integer.parseInt(prop
					.getProperty(MainConstants.mainWindowX));
			if (size.x < 1)
				throw new Exception();
		} catch (Exception e) { // if mainWindowsX is null or not numeric value then exception handling
			size.x = 1024;
		}

		try {
			size.y = Integer.parseInt(prop
					.getProperty(MainConstants.mainWindowY));
			if (size.y < 1)
				throw new Exception();
		} catch (Exception e) { // if mainWindowsX is null or not numeric value then exception handling
			size.y = 768;
		}

		Application.mainwindow.getShell().setSize(size);
	}

	public boolean preWindowShellClose() {
		Shell sh = new Shell();
		CommonTool.centerShell(sh);
		if (CommonTool.MsgBox(sh, SWT.ICON_WARNING | SWT.YES | SWT.NO, Messages
				.getString("MSG.WARNING"), Messages
				.getString("MSG.QUIT_PROGRAM")) == SWT.YES) {
			if (CommonTool.LoadProperties(prop)) {
				if (Application.mainwindow.getShell().getMaximized())
					prop.setProperty(MainConstants.mainWindowMaximize, "yes");
				else
					prop.setProperty(MainConstants.mainWindowMaximize, "no");

				Application.mainwindow.getShell().setMaximized(false);

				prop.setProperty(MainConstants.mainWindowX,
						Integer.toString(Application.mainwindow.getShell()
								.getSize().x));
				prop.setProperty(MainConstants.mainWindowY,
						Integer.toString(Application.mainwindow.getShell()
								.getSize().y));
				CommonTool.SaveProperties(prop);
			}
			if (MainRegistry.cb != null) {
				MainRegistry.cb.dispose();
				MainRegistry.cb = null;
			}
			return true;
		}
		return false;
	}

	public void postWindowOpen() {
		IStatusLineManager statusline = getWindowConfigurer()
				.getActionBarConfigurer().getStatusLineManager();
		statusline.setMessage("");
		statusline.update(true);
		if (MainRegistry.isProtegoBuild()) {
			// API initialize
			try {
				GpkiApi.init(".");
			} catch (Exception e) {
				CommonTool.debugPrint(e);
			}

		}
		if (MainRegistry.FirstLogin) {
			ApplicationActionBarAdvisor.connectAction.run();
			MainRegistry.FirstLogin = false;
		}
	}

}
