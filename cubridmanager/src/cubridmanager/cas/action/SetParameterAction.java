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

package cubridmanager.cas.action;

import org.eclipse.jface.action.Action;
import org.eclipse.jface.wizard.IWizardPage;
import org.eclipse.jface.wizard.Wizard;
import org.eclipse.jface.wizard.WizardPage;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.TableItem;

import cubridmanager.ApplicationActionBarAdvisor;
import cubridmanager.ClientSocket;
import cubridmanager.CommonTool;
import cubridmanager.CubridWizardDialog;
import cubridmanager.MainRegistry;
import cubridmanager.Messages;
import cubridmanager.WorkView;
import cubridmanager.cas.CASItem;
import cubridmanager.cas.BrokerParamInfo;
import cubridmanager.cas.dialog.BROKER_EDITORDialog;
import cubridmanager.cas.view.BrokerStatus;
import cubridmanager.cas.view.CASView;
import cubridmanager.cubrid.dialog.SqlxinitEditor;

public class SetParameterAction extends Action {
	public static String masterShmId = new String();

	public static BrokerParamInfo bpi = new BrokerParamInfo();

	public static boolean needRestart = true;

	public SetParameterAction(String text, String img) {
		super(text);
		// The id is used to refer to the action in a menu or toolbar
		setId("SetParameterAction");
		if (img != null)
			setImageDescriptor(cubridmanager.CubridmanagerPlugin
					.getImageDescriptor(img));
		setToolTipText(text);
	}

	public void run() {
		Shell shell = new Shell();
		ClientSocket cs = new ClientSocket();
		if (!cs.SendBackGround(shell, "confname:brokerconf", "getaddbrokerinfo",
				Messages.getString("WAITING.GETTINGSERVERINFO"))) {
			CommonTool.ErrorBox(shell, cs.ErrorMsg);
			return;
		}
		BROKER_EDITORDialog dlg = new BROKER_EDITORDialog(shell);
		dlg.doModal();		
	}
}

