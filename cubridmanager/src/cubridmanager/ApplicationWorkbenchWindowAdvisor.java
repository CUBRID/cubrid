package cubridmanager;

import java.util.Properties;

import org.eclipse.jface.action.IStatusLineManager;
import org.eclipse.swt.SWT;
import org.eclipse.swt.graphics.Point;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.ui.application.ActionBarAdvisor;
import org.eclipse.ui.application.IActionBarConfigurer;
import org.eclipse.ui.application.IWorkbenchWindowConfigurer;
import org.eclipse.ui.application.WorkbenchWindowAdvisor;

import com.gpki.gpkiapi.GpkiApi;

public class ApplicationWorkbenchWindowAdvisor extends WorkbenchWindowAdvisor {
	public static IWorkbenchWindowConfigurer myconfigurer = null;
	private Properties prop = new Properties();

	public ApplicationWorkbenchWindowAdvisor(
			IWorkbenchWindowConfigurer configurer) {
		super(configurer);
	}

	public ActionBarAdvisor createActionBarAdvisor(
			IActionBarConfigurer configurer) {
		return new ApplicationActionBarAdvisor(configurer);
	}

	public void preWindowOpen() {
		IWorkbenchWindowConfigurer configurer = getWindowConfigurer();
		configurer.setShowCoolBar(true);
		configurer.setShowStatusLine(true);
		myconfigurer = configurer;
	}

	public void postWindowCreate() {
		if (!CommonTool.LoadProperties(prop)) {
			CommonTool.SetDefaultParameter();
			CommonTool.LoadProperties(prop);
		}

		if (MainRegistry.isProtegoBuild()) {
			String protegoLoginType = null;
			protegoLoginType = prop.getProperty(MainConstants.protegoLoginType);
			if ((protegoLoginType != null)
					&& (protegoLoginType
							.equals(MainConstants.protegoLoginTypeMtId))) {
				MainRegistry.isCertificateLogin = false;
			} else
				MainRegistry.isCertificateLogin = true;

			ApplicationActionBarAdvisor
					.setCheckCertificationLogin(MainRegistry.isCertificateLogin);
		}

		if (prop.getProperty(MainConstants.mainWindowMaximize) != null
				&& prop.getProperty(MainConstants.mainWindowMaximize).equals("yes"))
			Application.mainwindow.getShell().setMaximized(true);
		else
			Application.mainwindow.getShell().setMaximized(false);

		Point size = new Point(0, 0);

		try {
			size.x = Integer.parseInt(prop
					.getProperty(MainConstants.mainWindowX));
			if (size.x < 1)
				throw new Exception();
		} catch (Exception e) { // if mainWindowsX is null or not numeric value then exception handling
			size.x = 1024;
		}

		try {
			size.y = Integer.parseInt(prop
					.getProperty(MainConstants.mainWindowY));
			if (size.y < 1)
				throw new Exception();
		} catch (Exception e) { // if mainWindowsX is null or not numeric value then exception handling
			size.y = 768;
		}

		Application.mainwindow.getShell().setSize(size);
	}

	public boolean preWindowShellClose() {
		Shell sh = new Shell();
		if (CommonTool.MsgBox(sh, SWT.ICON_WARNING | SWT.YES | SWT.NO, Messages
				.getString("MSG.WARNING"), Messages
				.getString("MSG.QUIT_PROGRAM")) == SWT.YES) {
			if (CommonTool.LoadProperties(prop)) {
				if (Application.mainwindow.getShell().getMaximized())
					prop.setProperty(MainConstants.mainWindowMaximize, "yes");
				else
					prop.setProperty(MainConstants.mainWindowMaximize, "no");

				Application.mainwindow.getShell().setMaximized(false);

				prop.setProperty(MainConstants.mainWindowX,
						Integer.toString(Application.mainwindow.getShell()
								.getSize().x));
				prop.setProperty(MainConstants.mainWindowY,
						Integer.toString(Application.mainwindow.getShell()
								.getSize().y));
				CommonTool.SaveProperties(prop);
			}
			return true;
		}
		return false;
	}

	public void postWindowOpen() {
		IStatusLineManager statusline = getWindowConfigurer()
				.getActionBarConfigurer().getStatusLineManager();
		statusline.setMessage("");
		statusline.update(true);
		if (MainRegistry.isProtegoBuild()) {
			// API initialize
			try {
				GpkiApi.init(".");
			} catch (Exception e) {
				CommonTool.debugPrint(e);
			}

		}
		if (MainRegistry.FirstLogin) {
			ApplicationActionBarAdvisor.connectAction.run();
			MainRegistry.FirstLogin = false;
		}
	}

}
