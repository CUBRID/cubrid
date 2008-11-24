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

import cubridmanager.ApplicationWorkbenchWindowAdvisor;
import cubridmanager.CommonTool;
import cubridmanager.MainConstants;
import cubridmanager.MainRegistry;
import cubridmanager.Messages;
import cubridmanager.WorkView;
import cubridmanager.cas.view.CASView;
import cubridmanager.cubrid.view.CubridView;
import cubridmanager.diag.DiagSiteDiagData;
import cubridmanager.diag.view.DiagView;

public class DisconnectAction extends Action {

	public DisconnectAction(String text, IWorkbenchWindow window) {
		super(text);
		setId("DisconnectAction");
		setActionDefinitionId("DisconnectAction");
		setImageDescriptor(cubridmanager.CubridmanagerPlugin
				.getImageDescriptor("/icons/disconnect.png"));
		setDisabledImageDescriptor(cubridmanager.CubridmanagerPlugin
				.getImageDescriptor("/disable_icons/disconnect.png"));
	}

	public void run() {
		// Delete all view
		for (int i = 0; i < MainRegistry.diagSiteDiagDataList.size(); i++) {
			DiagSiteDiagData diagSiteData = (DiagSiteDiagData) MainRegistry.diagSiteDiagDataList
					.get(i);
			if (diagSiteData.site_name.equals(MainRegistry.HostDesc))
				MainRegistry.diagSiteDiagDataList.remove(i);
		}

		// Environment initial
		if (MainRegistry.soc != null)
			MainRegistry.soc.stoploop();
		MainRegistry.IsConnected = false;
		MainRegistry.HostAddr = null;
		MainRegistry.HostPort = 0;
		MainRegistry.UserID = null;
		MainRegistry.HostJSPort = 0;
		MainRegistry.DiagAuth = MainConstants.AUTH_NONE;
		MainRegistry.CASAuth = MainConstants.AUTH_NONE;
		MainRegistry.Authinfo.clear();
		MainRegistry.IsSecurityManager = false;
		MainRegistry.NaviDraw_CUBRID = false;
		MainRegistry.NaviDraw_CAS = false;
		MainRegistry.NaviDraw_DIAG = false;
		MainRegistry.IsDBAAuth = false;
		MainRegistry.IsCASStart = false;
		MainRegistry.IsCASinfoReady = false;
		MainRegistry.CASinfo.clear();
		ApplicationWorkbenchWindowAdvisor.myconfigurer.setTitle(Messages
				.getString("TITLE.CUBRIDMANAGER"));

		CubridView.removeAll();
		CASView.removeAll();
		DiagView.removeAll();
		// Window refresh
		WorkView.DeleteViewAll();
		CubridView.source.dispose();
		CubridView.types = null;
		try {
			WorkView.TopView(CubridView.ID);
			CubridView.myNavi.setFocus();
		} catch (Exception e) {
			CommonTool.debugPrint(e);
		}
	}

}
