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

import org.eclipse.jface.action.Action;
import org.eclipse.ui.IWorkbenchWindow;

import cubridmanager.Application;
import cubridmanager.CommonTool;
import cubridmanager.Messages;
import cubridmanager.dialog.HelpDialog;

public class HelpAction extends Action {
	// private final IWorkbenchWindow window;

	public HelpAction(String text, IWorkbenchWindow window) {
		super(text);
		// this.window = window;
		// The id is used to refer to the action in a menu or toolbar
		setId("HelpAction");
		setActionDefinitionId("HelpAction");
		setImageDescriptor(cubridmanager.CubridmanagerPlugin
				.getImageDescriptor("/icons/help.png"));
	}

	public void run() {
		if (System.getProperty("os.name").startsWith("Linux")) {
			boolean start_browser = false;
			String url = null;
			int indexOfStartup = -1;
			String[] tmp = System.getProperty("java.class.path").split(":");
			for (int i = 0; i < tmp.length; i++) {
				indexOfStartup = tmp[i].indexOf("startup.jar");
				if (indexOfStartup < 0)
					continue;
				else {
					url = tmp[i].substring(0, indexOfStartup);
					break;
				}
			}
			url = url.concat("../../doc/Index.htm");
			String browsers[] = { "mozilla", "firefox", "netscape", "opera" };
			Runtime rt = Runtime.getRuntime();
			for (int i = 0; i < browsers.length; i++) {
				try {
					if (start_browser == false) {
						rt.exec(browsers[i] + " " + url);
					}
					start_browser = true;
					break;
				} catch (Exception e) {
					start_browser = false;
				}
			}
			if (start_browser == false) {
				CommonTool.ErrorBox(Application.mainwindow.getShell(), Messages
						.getString("ERROR.BROWSERNOTFOUND"));
			}
		} else {
			HelpDialog dlg = new HelpDialog(Application.mainwindow.getShell());
			dlg.doModal();
		}

	}

}
