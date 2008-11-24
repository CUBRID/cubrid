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

package cubridmanager.cubrid.action;

import org.eclipse.jface.action.Action;
import org.eclipse.swt.widgets.Shell;

import cubridmanager.CommonTool;
import cubridmanager.cubrid.LogFileInfo;
import cubridmanager.cubrid.dialog.LogViewDialog;
import java.util.ArrayList;

public class LogViewAction extends Action {
	public static ArrayList viewlist = null;
	public static String viewitem = null;

	public LogViewAction(String text, String img) {
		super(text);
		// The id is used to refer to the action in a menu or toolbar
		setId("LogViewAction");
		if (img != null)
			setImageDescriptor(cubridmanager.CubridmanagerPlugin
					.getImageDescriptor(img));
		setToolTipText(text);
	}

	public void run() {
		if (viewlist == null || viewitem == null)
			return;
		LogFileInfo firec = null;
		int i, n;
		for (i = 0, n = viewlist.size(); i < n; i++) {
			firec = (LogFileInfo) viewlist.get(i);
			if (firec.filename.equals(viewitem))
				break;
		}
		if (i >= n)
			return;
		Shell shell = new Shell();
		LogViewDialog dlg = new LogViewDialog(shell);
		if (!dlg.doModal(firec))
			CommonTool.ErrorBox("Cannot view log.");
	}
}
