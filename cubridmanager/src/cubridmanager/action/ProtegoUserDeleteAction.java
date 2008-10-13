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
import cubridmanager.dialog.PROPAGE_ProtegoUserManagementDialog;

public class ProtegoUserDeleteAction extends Action {
	public ProtegoUserDeleteAction(String text) {
		super(text);
		// The id is used to refer to the action in a menu or toolbar
		setId("ProtegoUserDeleteAction");
	}

	public void run() {
		ArrayList usrInfo = null;
		usrInfo = PROPAGE_ProtegoUserManagementDialog.getSelectedUserInfo();
		if ((usrInfo == null) || (usrInfo.size() == 0))
			return;

		try {
			MessageBox mb = new MessageBox(Application.mainwindow.getShell(),
					SWT.ICON_QUESTION | SWT.YES | SWT.NO);
			mb.setText(Messages.getString("TITLE.REMOVEAUTHORITY"));
			mb.setMessage(Messages.getString("TEXT.CONFIRMAUTHREMOVE"));
			int state = mb.open();
			if (state == SWT.YES) {
				for (int i = 0; i < usrInfo.size(); i++) {
					UpaClient.admUserDel(
							ProtegoUserManagementAction.dlg.upaKey,
							UpaClient.UPA_USER_DEL_APID_DN_DBNAME,
							(UpaUserInfo) usrInfo.get(i));
				}
			}
		} catch (UpaException ee) {
			CommonTool.ErrorBox(Application.mainwindow.getShell(), Messages
					.getString("TEXT.FAILEDREMOVEUSERAUTH"));
			return;
		}

		PROPAGE_ProtegoUserManagementDialog.refresh();
	}
}
