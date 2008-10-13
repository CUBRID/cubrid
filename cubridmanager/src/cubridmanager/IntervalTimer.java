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
