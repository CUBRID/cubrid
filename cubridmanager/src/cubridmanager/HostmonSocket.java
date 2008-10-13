package cubridmanager;

import java.io.DataOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.Socket;
import java.net.UnknownHostException;

import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.Shell;

import cubridmanager.diag.dialog.DiagActivityMonitorDialog;

public class HostmonSocket {
	public static String errmsg = null;
	private static String monhost = "";
	private static int monport = 0;
	private static String monmsg = "";

	Thread prc = null;
	Socket sd = null;
	OutputStream out = null;
	InputStream in = null;
	DataOutputStream output;

	private boolean wait_flag = false;
	public Object sender = null;
	public HostmonSocket() {
		MainRegistry.IsConnected = false;
	}

	public boolean ConnectHost(String host, int port, String msg) {
		monhost = host;
		monport = port;
		monmsg = msg;

		prc = new Thread() {
			Shell sh = new Shell();
			String[] toks = null;
			ClientSocket cs = null;

			public void run() {
				String buf = null;
				byte tmp[] = new byte[2048];

				try {
					if (sd != null) {
						sd.close();
					}
					sd = new Socket(monhost, monport);
					// sd.setTcpNoDelay(true);

					out = sd.getOutputStream();
					output = new DataOutputStream(out);
					out.flush();
					output.flush();

					in = sd.getInputStream();
					try {
						output.writeBytes(monmsg);
						output.flush();
					} catch (IOException e) {
						errmsg = CubridException.getErrorMessage(CubridException.ER_CONNECT);
						MainRegistry.IsConnected = false;
						MainRegistry.WaitDlg = false;
						return;
					}

					MainRegistry.IsConnected = true;

					int len;
					while (MainRegistry.IsConnected) {
						try {
							len = in.read(tmp);
						} catch (IOException e) {
							len = -1; // stream closed
						}
						if (len < 0) { // EOF
							// disconnected
							MainRegistry.IsConnected = false;
							if (MainRegistry.WaitDlg) {
								HostmonSocket.errmsg = Messages.getString("ERROR.NETWORKFAIL");
								MainRegistry.WaitDlg = false;
							} else {
								Display display = Application.mainwindow.getShell().getDisplay();
								HostmonSocket.errmsg = Messages.getString("ERROR.DISCONNECTED");
								display.syncExec(new Runnable() {
									public void run() {
										CommonTool.ErrorBox(HostmonSocket.errmsg);
										MainRegistry.soc = null;
										ApplicationActionBarAdvisor.disconnectAction.run();
									}
								});
							}

							break;
						} else if (len > 0) {
							if (buf == null) {
								buf = new String(tmp, 0, len);
							} else {
								buf = buf.concat(new String(tmp, 0, len));
							}
							if (buf.indexOf("\n\n") >= 0) {
								if (buf.length() <= 1) {
									MainRegistry.IsConnected = false;
									if (MainRegistry.WaitDlg) {
										HostmonSocket.errmsg = Messages.getString("ERROR.NETWORKFAIL");
										MainRegistry.WaitDlg = false;
									}
									break;
								}

								System.out.println(buf);
								String[] lines = buf.split("\n");
								toks = new String[lines.length * 2];
								for (int l1 = 0; l1 < lines.length; l1++) {
									int l2 = lines[l1].indexOf(":");
									toks[l1 * 2] = lines[l1].substring(0, l2);
									toks[l1 * 2 + 1] = lines[l1].substring(l2 + 1);
								}

								if (toks[0].equals("task")) {
									if (toks[1].equals("authenticate")) {
										if (GetValueFor(toks, "status").equals("success")) {
											MainRegistry.HostToken = new String(GetValueFor(toks, "token"));
											// Need to get cas port, ems-js port, and other things from server
											MainRegistry.HostJSPort = MainRegistry.HostPort + 1; // @@@IMSI
											cs = new ClientSocket();
											// if (!cs.SendClientMessage(sh, "",
											// "checkaccessright")) {
											// HostmonSocket.errmsg=cs.ErrorMsg;
											// MainRegistry.IsConnected=false;
											// MainRegistry.WaitDlg=false;
											// break;
											// }
											if (!cs.SendClientMessage(sh, "", "getdbmtuserinfo")) {
												HostmonSocket.errmsg = cs.ErrorMsg;
												MainRegistry.IsConnected = false;
												MainRegistry.WaitDlg = false;
												break;
											}
											if (!cs.SendClientMessage(sh, "", "getbrokersinfo")) {
												HostmonSocket.errmsg = cs.ErrorMsg;
												MainRegistry.IsConnected = false;
												MainRegistry.WaitDlg = false;
												break;
											}
											HostmonSocket.errmsg = null;
											MainRegistry.WaitDlg = false;
										} else {
											HostmonSocket.errmsg = Messages.getString("ERROR.LOGINFAILED")
													+ "\n" + GetValueFor(toks, "note");
											MainRegistry.IsConnected = false;
											MainRegistry.WaitDlg = false;
											break;
										}
									} else if (toks[1]
											.equals("setclientdiaginfo")) {
										if (!toks[0].equals("task") || // Check Message Format
												!toks[2].equals("status")
												|| !toks[4].equals("note")) {
											MainRegistry.diagErrorString = Messages
													.getString("ERROR.MESSAGEFORMAT");
										}

										if (!toks[3].equals("success")) {
											MainRegistry.diagErrorString = toks[5];
										}
										if (toks[6].equals("start_time_sec")) {
											((DiagActivityMonitorDialog) sender).monitor_start_time_sec = toks[7];
										}
										if (toks[8].equals("start_time_usec")) {
											((DiagActivityMonitorDialog) sender).monitor_start_time_usec = toks[9];
										}
									} else if (toks[1]
											.equals("removeclientdiaginfo")) {
										if (!toks[0].equals("task") || // Check Message Format
												!toks[2].equals("status")
												|| !toks[4].equals("note")) {
											MainRegistry.diagErrorString = Messages
													.getString("ERROR.MESSAGEFORMAT");
										}

										if (!toks[3].equals("success")) {
											MainRegistry.diagErrorString = toks[5];
										}
									} else {
										HostmonSocket.errmsg = Messages
												.getString("ERROR.CANNOTCONNECT");
										MainRegistry.IsConnected = false;
										MainRegistry.WaitDlg = false;
										break;
									}

									wait_flag = false;
								}
								buf = null;
							}
						} else {
							try {
								sleep(100);
							} catch (Exception e) {
							}
						}
					}
					if (sd != null) {
						try {
							sd.close();
							sd = null;
						} catch (IOException e) {
						}
					}
				} catch (UnknownHostException e) {
					errmsg = CubridException
							.getErrorMessage(CubridException.ER_UNKNOWNHOST);
					MainRegistry.IsConnected = false;
					MainRegistry.WaitDlg = false;
				} catch (IOException e) {
					errmsg = CubridException
							.getErrorMessage(CubridException.ER_CONNECT);
					MainRegistry.IsConnected = false;
					MainRegistry.WaitDlg = false;
				}
			}
		};
		prc.start();
		return true;
	}

	public boolean SendMessage(Shell sh, String requestMsg, String taskType) {
		int wait_count = 0;
		if (MainRegistry.IsConnected == false) {
			errmsg = Messages.getString("ERROR.DISCONNECTED");
			return false;
		}

		wait_flag = true;
		try {
			String message = "task:" + taskType + "\n";
			message += "token:" + MainRegistry.HostToken + "\n";
			message += requestMsg + "\n";
			CommonTool.debugPrint(message + "<<<");

			output.write(message.getBytes());
			output.flush();
			while (wait_flag && (wait_count < 50)) {
				try {
					Thread.sleep(100);
				} catch (Exception e) {
				}
				wait_count++;
			}

			if (!wait_flag) {
				errmsg = Messages.getString("ERROR.NETWORKFAIL");
				return false;
			}

		} catch (IOException ee) {
			errmsg = CubridException.getErrorMessage(CubridException.ER_CONNECT)
					+ " " + ee.getMessage();
			return false;
		}

		return true;
	}

	public void stoploop() {
		MainRegistry.IsConnected = false;
		errmsg = CubridException.getErrorMessage(CubridException.ER_CONNECT);
		if (prc != null) {
			prc.interrupt();
		}
	}

	String GetValueFor(String[] valary, String var) {
		int i1, n;
		for (i1 = 0, n = valary.length; i1 < n; i1 += 2) {
			if (valary[i1].equals(var))
				break;
		}
		if (i1 < n) {
			return valary[i1 + 1];
		}
		return "";
	}

}
