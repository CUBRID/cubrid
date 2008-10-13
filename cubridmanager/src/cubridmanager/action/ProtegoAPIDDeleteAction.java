package cubridmanager.action;

import java.util.ArrayList;

import org.eclipse.jface.action.Action;
import org.eclipse.swt.SWT;
import org.eclipse.swt.widgets.MessageBox;

import cubrid.upa.UpaClient;
import cubrid.upa.UpaException;
import cubrid.upa.UpaUserInfo;
import cubridmanager.Application;
import cubridmanager.CommonTool;
import cubridmanager.Messages;
import cubridmanager.dialog.PROPAGE_ProtegoAPIDManagementDialog;

public class ProtegoAPIDDeleteAction extends Action {
	public ProtegoAPIDDeleteAction(String text) {
		super(text);
		// The id is used to refer to the action in a menu or toolbar
		setId("ProtegoMTUserAddAction");
	}

	public void run() {
		ArrayList usrInfo = null;
		usrInfo = PROPAGE_ProtegoAPIDManagementDialog.getSelectedUserInfo();
		if ((usrInfo == null) || (usrInfo.size() == 0))
			return;

		try {
			MessageBox mb = new MessageBox(Application.mainwindow.getShell(),
					SWT.ICON_QUESTION | SWT.YES | SWT.NO);
			mb.setText(Messages.getString("TITLE.REMOVEAPID"));
			mb.setMessage(Messages.getString("TEXT.CONFIRMAPIDREMOVE"));
			int state = mb.open();
			if (state == SWT.YES) {
				for (int i = 0; i < usrInfo.size(); i++) {
					UpaClient.admAppCmd(ProtegoUserManagementAction.dlg.upaKey,
							UpaClient.UPA_USER_APID_DEL, (UpaUserInfo) usrInfo
									.get(i));
				}
			}
		} catch (UpaException ee) {
			CommonTool.ErrorBox(Application.mainwindow.getShell(), Messages
					.getString("TEXT.FAILEDREMOVEAPID"));
			return;
		}

		PROPAGE_ProtegoAPIDManagementDialog.refresh();
	}
}
