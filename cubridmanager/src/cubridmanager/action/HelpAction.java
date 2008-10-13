package cubridmanager.action;

import org.eclipse.jface.action.Action;
import org.eclipse.ui.IWorkbenchWindow;

import cubridmanager.Application;
import cubridmanager.CommonTool;
import cubridmanager.Messages;
import cubridmanager.dialog.HelpDialog;

public class HelpAction extends Action {
	// private final IWorkbenchWindow window;

	public HelpAction(String text, IWorkbenchWindow window) {
		super(text);
		// this.window = window;
		// The id is used to refer to the action in a menu or toolbar
		setId("HelpAction");
		setActionDefinitionId("HelpAction");
		setImageDescriptor(cubridmanager.CubridmanagerPlugin
				.getImageDescriptor("/icons/help.png"));
	}

	public void run() {
		if (System.getProperty("os.name").startsWith("Linux")) {
			boolean start_browser = false;
			String url = null;
			int indexOfStartup = -1;
			String[] tmp = System.getProperty("java.class.path").split(":");
			for (int i = 0; i < tmp.length; i++) {
				indexOfStartup = tmp[i].indexOf("startup.jar");
				if (indexOfStartup < 0)
					continue;
				else {
					url = tmp[i].substring(0, indexOfStartup);
					break;
				}
			}
			url = url.concat("../Documents/Index.htm");
			String browsers[] = { "mozilla", "firefox", "netscape", "opera" };
			Runtime rt = Runtime.getRuntime();
			for (int i = 0; i < browsers.length; i++) {
				try {
					if (start_browser == false) {
						rt.exec(browsers[i] + " " + url);
					}
					start_browser = true;
					break;
				} catch (Exception e) {
					start_browser = false;
				}
			}
			if (start_browser == false) {
				CommonTool.ErrorBox(Application.mainwindow.getShell(), Messages
						.getString("ERROR.BROWSERNOTFOUND"));
			}
		} else {
			HelpDialog dlg = new HelpDialog(Application.mainwindow.getShell());
			dlg.doModal();
		}

	}

}
