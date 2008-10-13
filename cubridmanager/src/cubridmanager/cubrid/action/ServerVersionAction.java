package cubridmanager.cubrid.action;

import org.eclipse.jface.action.Action;
import org.eclipse.jface.dialogs.MessageDialog;

import cubridmanager.Application;
import cubridmanager.CommonTool;
import cubridmanager.Messages;

public class ServerVersionAction extends Action {
	public ServerVersionAction(String text, String img) {
		super(text);
		// The id is used to refer to the action in a menu or toolbar
		setId("ServerVersionAction");
		if (img != null) {
			setImageDescriptor(cubridmanager.CubridmanagerPlugin
					.getImageDescriptor(img));
			setDisabledImageDescriptor(cubridmanager.CubridmanagerPlugin
					.getImageDescriptor(img.replaceFirst("icons",
							"disable_icons")));
		}
		setToolTipText(text);
	}

	public void run() {
		String DB_Version = CommonTool.GetCubridVersion();
		MessageDialog.openInformation(Application.mainwindow.getShell(),
				Messages.getString("TITLE.CUBRIDVERSION"), DB_Version);
	}
}
