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

package cubridmanager;

import org.eclipse.swt.widgets.Dialog;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.*;
import org.eclipse.swt.graphics.*;
import org.eclipse.core.runtime.Platform;
import java.net.URL;
import java.sql.SQLException;

import org.eclipse.core.runtime.Path;
import java.io.InputStream;
import org.eclipse.swt.SWT;
import org.eclipse.swt.custom.CLabel;
import org.eclipse.swt.graphics.Color;

import cubrid.jdbc.driver.CUBRIDPreparedStatement;
import org.eclipse.swt.widgets.Button;

public class WaitingMsgBox extends Dialog {
	public CUBRIDPreparedStatement hStmt = null;
	Display display;
	GC shellGC;
	Color shellBackground;
	ImageLoader loader;
	ImageData[] imageDataArray;
	Image image;
	boolean useGIFBackground = false;
	public boolean is_timeout = false;
	private boolean isExecQuery;
	private Shell sShell = null; // @jve:decl-index=0:visual-constraint="10,51"
	private CLabel Msg = null;
	private CLabel cLabel = null;
	private boolean isJobEnded = false;
	private Button buttonCancel = null;

	public WaitingMsgBox(Shell parent) {
		super(parent);
		MainRegistry.WaitDlg = true;
		is_timeout = false;
		isExecQuery = false;
	}

	public void setJobEndState(boolean endState) {
		isJobEnded = endState;
	}

	public boolean getJobEndState() {
		return isJobEnded;
	}

	public void runForQueryExec() {
		isExecQuery = true;
		run(Messages.getString("WAITING.RUNNINGQUERY"));
	}

	public void run(String msg) {
		run(msg, 0);
	}

	public void run(String msg, int timeout) {
		long fromtime, totime;

		createSShell();
		CommonTool.centerShell(sShell);
		display = sShell.getDisplay();
		Msg.setText(msg);
		sShell.open();

		if (!MainRegistry.hostOsInfo.equals("NT")) {
			if (isExecQuery)
				buttonCancel.setVisible(true);
		}

		shellGC = new GC(sShell);
		shellBackground = sShell.getBackground();
		loader = new ImageLoader();
		fromtime = System.currentTimeMillis();
		try {
			Path imageFilePath = new Path("icons/waiting.gif");
			URL imageFileUrl = Platform.find(Platform
					.getBundle("cubridmanager"), imageFilePath);
			try {
				InputStream imageFileStream = imageFileUrl.openStream();
				imageDataArray = loader.load(imageFileStream);
				imageFileStream.close();
			} catch (Exception e) {
				CommonTool.debugPrint(e);
			}

			if (imageDataArray.length > 1) {
				/*
				 * Create an off-screen image to draw on, and fill it with the
				 * shell background.
				 */
				Image offScreenImage = new Image(display,
						loader.logicalScreenWidth, loader.logicalScreenHeight);
				GC offScreenImageGC = new GC(offScreenImage);
				offScreenImageGC.setBackground(shellBackground);
				offScreenImageGC.fillRectangle(0, 0, loader.logicalScreenWidth,
						loader.logicalScreenHeight);
				try {
					/*
					 * Create the first image and draw it on the off-screen
					 * image.
					 */
					int imageDataIndex = 0;
					ImageData imageData = imageDataArray[imageDataIndex];
					if (image != null && !image.isDisposed())
						image.dispose();
					image = new Image(display, imageData);
					offScreenImageGC.drawImage(image, 0, 0, imageData.width,
							imageData.height, imageData.x, imageData.y,
							imageData.width, imageData.height);

					while (MainRegistry.WaitDlg && !sShell.isDisposed()) {
						switch (imageData.disposalMethod) {
						case SWT.DM_FILL_BACKGROUND:
							/* Fill with the background color before drawing. */
							Color bgColor = null;
							if (useGIFBackground
									&& loader.backgroundPixel != -1) {
								bgColor = new Color(display, imageData.palette
										.getRGB(loader.backgroundPixel));
							}
							offScreenImageGC
									.setBackground(bgColor != null ? bgColor
											: shellBackground);
							offScreenImageGC.fillRectangle(imageData.x,
									imageData.y, imageData.width,
									imageData.height);
							if (bgColor != null)
								bgColor.dispose();
							break;
						case SWT.DM_FILL_PREVIOUS:
							/* Restore the previous image before drawing. */
							offScreenImageGC.drawImage(image, 0, 0,
									imageData.width, imageData.height,
									imageData.x, imageData.y, imageData.width,
									imageData.height);
							break;
						}
						imageDataIndex = (imageDataIndex + 1)
								% imageDataArray.length;
						imageData = imageDataArray[imageDataIndex];
						image.dispose();
						image = new Image(display, imageData);
						offScreenImageGC.drawImage(image, 0, 0,
								imageData.width, imageData.height, imageData.x,
								imageData.y, imageData.width, imageData.height);

						/* Draw the off-screen image to the shell. */
						shellGC.drawImage(offScreenImage, 200, 85);
						shellGC.setLineWidth(7);
						shellGC.setForeground(sShell.getDisplay()
								.getSystemColor(SWT.COLOR_DARK_BLUE));
						shellGC.drawRectangle(3, 3, 293, 163);

						/*
						 * Sleep for the specified delay time (adding
						 * commonly-used slow-down fudge factors).
						 */
						try {
							// int ms = imageData.delayTime * 10;
							Thread.sleep(30);
						} catch (InterruptedException e) {
						}
						/*
						 * If we have just drawn the last image, decrement the
						 * repeat count and start again.
						 */
						if (imageDataIndex == imageDataArray.length - 1) {
							if (timeout > 0) {
								totime = System.currentTimeMillis();
								if ((totime - fromtime) > (timeout * 1000)) {
									MainRegistry.WaitDlg = false;
									is_timeout = true;
								}
							}
						}

						if (!display.readAndDispatch())
							display.sleep();

						if (isJobEnded) {
							break;
						}
					} // end while
				} catch (SWTException ex) {
					CommonTool
							.debugPrint("There was an error animating the GIF");
					MainRegistry.WaitDlg = false;
				} finally {
					if (offScreenImage != null && !offScreenImage.isDisposed())
						offScreenImage.dispose();
					if (offScreenImageGC != null
							&& !offScreenImageGC.isDisposed())
						offScreenImageGC.dispose();
					if (image != null && !image.isDisposed())
						image.dispose();
					MainRegistry.WaitDlg = false;
				}
			}
		} catch (SWTException ex) {
			CommonTool.debugPrint("There was an error loading the GIF");
		}

		if (!sShell.isDisposed())
			sShell.dispose();
		return;
	} // end run

	/**
	 * This method initializes sShell
	 * 
	 */
	private void createSShell() {
		sShell = new Shell(getParent(), SWT.SYSTEM_MODAL | SWT.NO_TRIM);
		// sShell = new Shell(SWT.SYSTEM_MODAL | SWT.NO_TRIM);
		sShell.setBackground(Display.getCurrent().getSystemColor(
				SWT.COLOR_WHITE));
		sShell.setSize(new org.eclipse.swt.graphics.Point(300, 170));
		Msg = new CLabel(sShell, SWT.CENTER);
		Msg.setBackground(Display.getCurrent().getSystemColor(SWT.COLOR_WHITE));
		Msg.setBounds(new org.eclipse.swt.graphics.Rectangle(17, 24, 262, 40));
		cLabel = new CLabel(sShell, SWT.RIGHT);
		cLabel.setText(Messages.getString("MSG.WAITING"));
		cLabel.setBackground(Display.getCurrent().getSystemColor(
				SWT.COLOR_WHITE));
		cLabel
				.setBounds(new org.eclipse.swt.graphics.Rectangle(37, 92, 152,
						23));
		buttonCancel = new Button(sShell, SWT.BORDER);
		buttonCancel.setBounds(new org.eclipse.swt.graphics.Rectangle(114, 123,
				72, 34));
		buttonCancel.setText(Messages.getString("BUTTON.CANCEL"));
		buttonCancel.setVisible(false);
		buttonCancel.setBackground(Display.getCurrent().getSystemColor(
				SWT.COLOR_WHITE));
		buttonCancel
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						try {
							if (hStmt != null) {
								hStmt.cancel();
							}
						} catch (SQLException ee) {
							CommonTool.debugPrint(ee);
						}
					}
				});
	}
}
