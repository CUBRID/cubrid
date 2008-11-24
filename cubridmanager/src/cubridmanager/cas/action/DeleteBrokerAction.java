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
import org.eclipse.swt.SWT;
import org.eclipse.swt.widgets.Shell;

import cubridmanager.ClientSocket;
import cubridmanager.CommonTool;
import cubridmanager.MainConstants;
import cubridmanager.MainRegistry;
import cubridmanager.Messages;
import cubridmanager.cas.CASItem;
import cubridmanager.cas.view.CASView;

public class DeleteBrokerAction extends Action {
	public DeleteBrokerAction(String text, String img) {
		super(text);
		// The id is used to refer to the action in a menu or toolbar
		setId("DeleteBrokerAction");
		if (img != null)
			setImageDescriptor(cubridmanager.CubridmanagerPlugin
					.getImageDescriptor(img));
		setToolTipText(text);
	}

	public void run() {
		if (CASView.Current_broker.length() <= 0)
			return;
		CASItem casrec = MainRegistry.CASinfo_find(CASView.Current_broker);
		if (casrec == null || casrec.status != MainConstants.STATUS_STOP)
			return;
		if (CommonTool.WarnYesNo(Messages.getString("WARNYESNO.DELETE")) != SWT.YES)
			return;

		Shell shell = new Shell();

		ClientSocket cs = new ClientSocket();
		if (!cs.SendBackGround(shell, "bname:" + CASView.Current_broker,
				"deletebroker", Messages.getString("WAITING.DELBROKER"))) {
			CommonTool.ErrorBox(shell, cs.ErrorMsg);
			return;
		}
		MainRegistry.DeletedBrokers.add(CASView.Current_broker);
		cs = new ClientSocket();
		if (!cs.SendClientMessage(shell, "", "getbrokersinfo")) {
			CommonTool.ErrorBox(shell, cs.ErrorMsg);
			return;
		}
		if (MainRegistry.IsCASStart) {
			CommonTool
					.WarnBox(shell, Messages.getString("MSG.AFTERRESTARTCAS"));
		}
		CASView.Current_broker = "";
		CASView.myNavi.createModel();
		CASView.viewer.refresh();
	}
}
