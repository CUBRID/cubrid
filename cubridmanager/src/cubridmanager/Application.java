package cubridmanager;

import org.eclipse.core.runtime.IPlatformRunnable;
import org.eclipse.core.runtime.Platform;
import org.eclipse.swt.widgets.Display;
import org.eclipse.ui.IWorkbenchWindow;
import org.eclipse.ui.PlatformUI;

import cubridmanager.dialog.ConnectDialog;

/**
 * This class controls all aspects of the application's execution
 */
public class Application implements IPlatformRunnable {
	public static IWorkbenchWindow mainwindow;

	/*
	 * (non-Javadoc)
	 * 
	 * @see org.eclipse.core.runtime.IPlatformRunnable#run(java.lang.Object)
	 */
	public Object run(Object args) throws Exception {
		Display display = PlatformUI.createDisplay();

		String[] cmds = Platform.getCommandLineArgs();

		ConnectDialog.Cmd_site = null;
		ConnectDialog.Cmd_pass = null;

		if (cmds != null && cmds.length >= 2) {
			if (cmds[cmds.length - 2].equals("CMDS")) {
				ConnectDialog.Cmd_site = cmds[cmds.length - 1];
				ConnectDialog.Cmd_pass = null;
				ApplicationWorkbenchWindowAdvisor.myconfigurer = null;
			} else if (cmds[cmds.length - 3].equals("CMDS")) {
				ConnectDialog.Cmd_site = cmds[cmds.length - 2];
				ConnectDialog.Cmd_pass = cmds[cmds.length - 1];
				ApplicationWorkbenchWindowAdvisor.myconfigurer = null;
			}
		}
		Platform.endSplash();

		try {
			int returnCode = PlatformUI.createAndRunWorkbench(display,
					new ApplicationWorkbenchAdvisor());
			if (returnCode == PlatformUI.RETURN_RESTART) {
				return IPlatformRunnable.EXIT_RESTART;
			}
			return IPlatformRunnable.EXIT_OK;
		} finally {
			display.dispose();
		}
	}
}
