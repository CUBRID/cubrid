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
import org.eclipse.swt.widgets.Shell;

import cubridmanager.IntervalTimer;
import cubridmanager.cas.dialog.REFRESH_INTERVALDialog;
import cubridmanager.cas.view.CASView;

public class RefreshIntervalAction extends Action {
	public static int time_interval = 0;
	public static boolean prc_loop = false;
	static IntervalTimer prc = null;
	static RefreshIntervalAction myaction = null;
	public static String broker_name = new String("");
	public RefreshIntervalAction(String text, String img) {
		super(text);
		// The id is used to refer to the action in a menu or toolbar
		setId("RefreshIntervalAction");
		if (img != null) {
			setImageDescriptor(cubridmanager.CubridmanagerPlugin
					.getImageDescriptor(img));
			setDisabledImageDescriptor(cubridmanager.CubridmanagerPlugin
					.getImageDescriptor(img.replaceFirst("icons",
							"disable_icons")));
		}
		setToolTipText(text);
		myaction = this;
	}

	public void run() {
		Shell shell = new Shell();
		REFRESH_INTERVALDialog dlg = new REFRESH_INTERVALDialog(shell);
		dlg.time_interval = time_interval;
		dlg.doModal();
		if (time_interval >= REFRESH_INTERVALDialog.MIN_BROKER_REFRESH_SEC)
			settimer();
		else
			stoptimer();
	}

	public static void settimer() {
		if (CASView.Current_broker.length() > 0)
			broker_name = new String(CASView.Current_broker);
		if (prc != null && prc_loop)
			return;
		prc = new IntervalTimer(myaction);
		prc_loop = true;
		prc.start();
	}

	public static void stoptimer() {
		if (prc != null && prc_loop)
			prc.stop();
		prc = null;
		prc_loop = false;
	}

}
