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

import org.eclipse.ui.IViewPart;
import org.eclipse.ui.IViewReference;

import cubridmanager.action.RefreshIntervalAction;
import cubridmanager.cas.view.BrokerJob;
import cubridmanager.cas.view.BrokerStatus;

public class IntervalTimer extends Thread {
	Object intobj = null;

	public IntervalTimer(Object obj) {
		intobj = obj;
	}

	public void run() {
		int i, n;
		byte view_find_flag;
		if (intobj instanceof RefreshIntervalAction) {
			while (RefreshIntervalAction.prc_loop) {
				view_find_flag = MainConstants.viewNone;
				final IViewReference[] viewref = WorkView.workwindow
						.getActivePage().getViewReferences();
				for (i = 0, n = viewref.length; i < n; i++) {
					// BrokerJob: broker status, BrokerStatus: broker list
					if (viewref[i].getId().equals(BrokerJob.ID)) {
						view_find_flag = MainConstants.viewBrokerJob;
						break;
					} else if (viewref[i].getId().equals(BrokerStatus.ID)) {
						view_find_flag = MainConstants.viewBrokerStatus;
						break;
					}
				}

				if (view_find_flag == MainConstants.viewNone) {
					try {
						Thread
								.sleep(RefreshIntervalAction.time_interval * 1000);
					} catch (Exception e) {
					}
					continue;
				} else {
					final int index = i;
					final IViewPart currentView = viewref[index].getView(false);
					final byte viewFlag = view_find_flag;

					Application.mainwindow.getShell().getDisplay().syncExec(
							new Runnable() {
								public void run() {
									try {
										if (viewFlag == MainConstants.viewBrokerJob) {
											((BrokerJob) currentView)
													.refreshItem();
										} else if (viewFlag == MainConstants.viewBrokerStatus) {
											((BrokerStatus) currentView)
													.refreshItem();
										}
									} catch (Exception ee) {/* Do Nothing */
									}
								}
							});
					try {
						sleep(RefreshIntervalAction.time_interval * 1000);
					} catch (Exception e) {
					}
				}
			}
			RefreshIntervalAction.prc_loop = false;
		}
	}
}
