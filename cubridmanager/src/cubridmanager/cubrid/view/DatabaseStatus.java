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

package cubridmanager.cubrid.view;

import java.util.ArrayList;

import org.eclipse.swt.SWT;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.ui.part.ViewPart;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.Group;
import org.eclipse.swt.custom.ScrolledComposite;
import org.eclipse.swt.events.PaintListener;
import org.eclipse.swt.events.PaintEvent;
import org.eclipse.swt.graphics.*;
import org.eclipse.swt.layout.FillLayout;

import cubridmanager.CommonTool;
import cubridmanager.MainConstants;
import cubridmanager.MainRegistry;
import cubridmanager.Messages;
import cubridmanager.cubrid.AuthItem;
import cubridmanager.cubrid.VolumeInfo;

public class DatabaseStatus extends ViewPart {

	public static final String ID = "workview.DatabaseStatus";
	// TODO Needs to be whatever is mentioned in plugin.xml
	private Composite top = null;
	private Group group1 = null;
	private Composite composite1 = null;
	private String DB_Version = null;
	private AuthItem DB_Auth = null;
	private ArrayList Volinfo = null;
	private long Voltot = 0, Volfree = 0;
	ScrolledComposite sc = null;

	public DatabaseStatus() {
		super();
		if (CubridView.Current_db.length() <= 0)
			this.dispose();
		else {
			DB_Version = CommonTool.GetCubridVersion();
			DB_Auth = MainRegistry.Authinfo_find(CubridView.Current_db);
			Volinfo = DB_Auth.Volinfo;
			VolumeInfo virec;
			Voltot = Volfree = 0;
			for (int i = 0, n = Volinfo.size(); i < n; i++) {
				virec = (VolumeInfo) Volinfo.get(i);
				if (virec.type.equals("Active_log")
						|| virec.type.equals("Archive_log"))
					continue;
				Voltot += CommonTool.atol(virec.tot);
				Volfree += CommonTool.atol(virec.free);
			}

		}
	}

	public void createPartControl(Composite parent) {
		sc = new ScrolledComposite(parent, SWT.H_SCROLL | SWT.V_SCROLL);
		FillLayout flayout = new FillLayout();
		sc.setLayout(flayout);
		top = new Composite(sc, SWT.NONE);
		top.setBackground(Display.getCurrent().getSystemColor(SWT.COLOR_WHITE));
		createGroup1();
		createComposite1();
		sc.setContent(top);
		sc.setExpandHorizontal(true);
		sc.setExpandVertical(true);
	}

	public void setFocus() {
	}

	public void adjustWindows() {
		Rectangle toprect = top.getBounds();
		Rectangle wrkrect = null;
		if (toprect.width - 10 <= 600)
			wrkrect = new Rectangle(toprect.x + 5, toprect.y + 5, 600,
					toprect.height - 10);
		else
			wrkrect = new Rectangle(toprect.x + 5, toprect.y + 5,
					toprect.width - 10, toprect.height - 10);
		wrkrect.height = 150;
		group1.setBounds(wrkrect);

		wrkrect.y = 155;
		wrkrect.height = toprect.height - 160;
		if (wrkrect.height <= 0)
			wrkrect.height = 0;
		composite1.setBounds(wrkrect);
	}

	/**
	 * This method initializes group1
	 * 
	 */
	private void createGroup1() {
		group1 = new Group(top, SWT.NONE);
		group1.setBackground(Display.getCurrent().getSystemColor(
				SWT.COLOR_WHITE));
		group1.setLocation(new org.eclipse.swt.graphics.Point(8, 4));
		group1.setSize(new org.eclipse.swt.graphics.Point(277, 70));
		group1.addPaintListener(new PaintListener() {
			public void paintControl(PaintEvent e) {
				try {
					String totalSize = new String();
					String freeSize = new String();
					adjustWindows();
					FontMetrics fm = e.gc.getFontMetrics();
					int yy = 0, chary = fm.getHeight() + 2;
					Font forg = e.gc.getFont();
					Font flarge = new Font(group1.getDisplay(), e.gc.getFont()
							.toString(), 14, SWT.BOLD);

					long val = Math
							.round(Voltot * DB_Auth.pagesize / 1048576.0);
					if (val > 1000) {
						/* giga */
						totalSize = String.valueOf((int) (val / 1000)) + ".";
						totalSize += String.valueOf(Math
								.round((val % 1000) / 10.0))
								+ "G";
					} else if (val == 0) {
						/* smaller then mega */
						totalSize = String.valueOf((int) Math.round(Voltot
								* DB_Auth.pagesize / 1024.0))
								+ "K";
					} else {
						/* mega */
						totalSize = String.valueOf(val) + "M";
					}

					val = Math.round(Volfree * DB_Auth.pagesize / 1048576.0);
					if (val > 1000) {
						/* giga */
						freeSize = String.valueOf((int) (val / 1000)) + ".";
						freeSize += String.valueOf(Math
								.round((val % 1000) / 10.0))
								+ "G";
					} else if (val == 0) {
						/* smaller then mega */
						totalSize = String.valueOf((int) Math.round(Volfree
								* DB_Auth.pagesize / 1024.0))
								+ "K";
					} else {
						/* mega */
						freeSize = String.valueOf(val) + "M";
					}

					e.gc.setFont(flarge);
					e.gc.drawText(CubridView.Current_db, 10, 20);
					flarge.dispose();
					e.gc.setFont(forg);
					e.gc.drawText(Messages.getString("TXT.VERSION") + " : "
							+ DB_Version, 10, 50 + chary * yy++);
					e.gc
							.drawText(
									Messages.getString("TXT.STATUS")
											+ " : "
											+ ((DB_Auth.status == MainConstants.STATUS_START) ? Messages
													.getString("TXT.STATUSSTART")
													: Messages
															.getString("TXT.STATUSSTOP")),
									10, 50 + chary * yy++);
					e.gc.drawText(Messages.getString("TABLE.USERAUTHORITY")
							+ " : "
							+ ((DB_Auth.dbuser == null || DB_Auth.dbuser
									.length() < 1) ? Messages
									.getString("TXT.NOAUTHORITY")
									: DB_Auth.dbuser), 10, 50 + chary * yy++);
					e.gc
							.drawText(Messages.getString("TXT.PAGESIZE")
									+ " : " + DB_Auth.pagesize + " byte", 10,
									50 + chary * yy++);
					e.gc.drawText(Messages.getString("TXT.TOTALSIZE") + " : "
							+ Voltot + " pages(" + totalSize + ")", 10, 50
							+ chary * yy++);
					e.gc.drawText(Messages.getString("TXT.REMAINSIZE") + " : "
							+ Volfree + " pages(" + freeSize + ")", 10, 50
							+ chary * yy++);
				} catch (Exception ex) {
					CommonTool.debugPrint(ex);
				}
			}
		});

	}

	/**
	 * This method initializes composite1
	 * 
	 */
	private void createComposite1() {
		composite1 = new Composite(top, SWT.BORDER);
		composite1.setBackground(Display.getCurrent().getSystemColor(
				SWT.COLOR_WHITE));
		composite1.setLocation(new org.eclipse.swt.graphics.Point(6, 80));
		composite1.setSize(new org.eclipse.swt.graphics.Point(276, 73));
		composite1.addPaintListener(new PaintListener() {
			public void paintControl(PaintEvent e) {
				try {
					FontMetrics fm = e.gc.getFontMetrics();
					int yy = 0, chary = fm.getHeight() + 2;
					Font forg = e.gc.getFont();
					Font flarge = new Font(group1.getDisplay(), e.gc.getFont()
							.toString(), 14, SWT.BOLD);

					e.gc.setFont(flarge);
					e.gc.drawText(Messages.getString("TXT.VOLUMEINFO"), 10, 20);
					flarge.dispose();
					e.gc.setFont(forg);

					e.gc.drawText(Messages.getString("TXT.VOLUMENAME"), 10, 50);
					e.gc.drawText(Messages.getString("TXT.USE_REMAINSIZE"),
							130, 50);
					e.gc.drawText(Messages.getString("TXT.TOTALSIZE"), 320, 50);
					e.gc.drawText(Messages.getString("TXT.TYPE"), 400, 50);
					yy++;
					e.gc.setForeground(Display.getCurrent().getSystemColor(
							SWT.COLOR_DARK_BLUE));
					e.gc
							.fillGradientRectangle(5, 50 + chary * yy, 500, 8,
									true);
					yy++;
					VolumeInfo virec;
					int freeint, totint, usedint, usedpage;
					for (int i = 0, n = Volinfo.size(); i < n; i++) {
						virec = (VolumeInfo) Volinfo.get(i);
						if (virec.type.equals("Active_log")
								|| virec.type.equals("Archive_log"))
							continue;
						freeint = CommonTool.atoi(virec.free);
						totint = CommonTool.atoi(virec.tot);
						if (totint <= 0)
							continue;
						e.gc.setForeground(Display.getCurrent().getSystemColor(
								SWT.COLOR_BLACK));
						e.gc.drawText(virec.spacename, 10, 50 + chary * yy);

						usedpage = totint - freeint;
						freeint = ((freeint * 100) / totint);
						usedint = 100 - freeint;
						if (usedint > 0) {
							e.gc.setBackground(Display.getCurrent()
									.getSystemColor(SWT.COLOR_DARK_BLUE));
							e.gc.fillRectangle(130, 50 + chary * yy,
									170 * usedint / 100, chary - 2);
						}
						if (freeint > 0) {
							e.gc.setBackground(Display.getCurrent()
									.getSystemColor(SWT.COLOR_DARK_YELLOW));
							e.gc.fillRectangle(130 + (170 * usedint / 100), 50
									+ chary * yy, 170 - (170 * usedint / 100),
									chary - 2);
						}
						e.gc.setBackground(Display.getCurrent().getSystemColor(
								SWT.COLOR_WHITE));
						e.gc.setForeground(Display.getCurrent().getSystemColor(
								SWT.COLOR_WHITE));
						e.gc.drawText(usedpage + "/" + virec.free, 170, 50
								+ chary * yy, true);
						e.gc.setForeground(Display.getCurrent().getSystemColor(
								SWT.COLOR_BLACK));
						e.gc.drawText(virec.tot, 320, 50 + chary * yy);
						e.gc.drawText(virec.type, 400, 50 + chary * yy);
						yy++;
					}
					Rectangle toprect = composite1.getBounds();

					sc.setMinWidth(toprect.x + toprect.width);
					sc.setMinHeight(toprect.y + (50 + chary * yy) + 50);
				} catch (Exception ex) {
					CommonTool.debugPrint(ex);
				}
			}
		});
	}

}
