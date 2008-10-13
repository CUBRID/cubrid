package cubridmanager.cubrid.view;

import java.util.TimerTask;

import cubridmanager.Application;
import cubridmanager.ClientSocket;
import cubridmanager.CommonTool;
import cubridmanager.MainRegistry;
import cubridmanager.cubrid.dialog.JOB_GETDBSTATUSDialog;
import org.eclipse.swt.widgets.Shell;

public class CubridViewTimer extends TimerTask {
	public void run() {
		if (MainRegistry.IsConnected) {
			if (MainRegistry.MONPARA_STATUS.equals("ON")) {
				Application.mainwindow.getShell().getDisplay().syncExec(
						new Runnable() {
							public void run() {
								Shell dlgShell = new Shell();
								ClientSocket cs = new ClientSocket();
								if (!cs.SendClientMessage(dlgShell, "",
										"getdberror")) {
									CommonTool.ErrorBox(dlgShell, cs.ErrorMsg);
									return;
								}
								if (MainRegistry.Tmpary.size() > 0) {
									JOB_GETDBSTATUSDialog dlg = new JOB_GETDBSTATUSDialog(
											dlgShell);
									dlg.doModal();
								}
							}
						});
			}
		}
	}
}
