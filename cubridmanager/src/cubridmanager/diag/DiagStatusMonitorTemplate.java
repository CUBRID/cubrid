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

package cubridmanager.diag;

import org.eclipse.swt.SWT;

public class DiagStatusMonitorTemplate {
	public String templateName = null;
	public String desc = null;
	public String sampling_term = null;
	public DiagMonitorConfig monitor_config = new DiagMonitorConfig();
	public static final int color_size = 16;
	public String targetdb = new String();

	public DiagStatusMonitorTemplate() {
		monitor_config.init_client_monitor_config();
	}

	public static String getColorString(int clr) {
		switch (clr) {
		case SWT.COLOR_DARK_RED:
			return "DARK_RED";
		case SWT.COLOR_DARK_GREEN:
			return "DARK_GREEN";
		case SWT.COLOR_DARK_YELLOW:
			return "DARK_YELLOW";
		case SWT.COLOR_DARK_BLUE:
			return "DARK_BLUE";
		case SWT.COLOR_DARK_MAGENTA:
			return "DARK_MAGENTA";
		case SWT.COLOR_DARK_CYAN:
			return "DARK_CYAN";
		case SWT.COLOR_DARK_GRAY:
			return "DARK_GRAY";
		case SWT.COLOR_WHITE:
			return "WHITE";
		case SWT.COLOR_RED:
			return "RED";
		case SWT.COLOR_BLACK:
			return "BLACK";
		case SWT.COLOR_YELLOW:
			return "YELLOW";
		case SWT.COLOR_BLUE:
			return "BLUE";
		case SWT.COLOR_MAGENTA:
			return "MAGENTA";
		case SWT.COLOR_CYAN:
			return "CYAN";
		case SWT.COLOR_GRAY:
			return "GRAY";
		}

		return "GREEN";
	}

	/*
	 * public static int getCurrentColorConstant(int clr) { switch (clr) { case
	 * 2 : return SWT.COLOR_DARK_RED; case 3 : return SWT.COLOR_DARK_GREEN; case
	 * 4 : return SWT.COLOR_DARK_YELLOW; case 5 : return SWT.COLOR_DARK_BLUE;
	 * case 6 : return SWT.COLOR_DARK_MAGENTA; case 7 : return
	 * SWT.COLOR_DARK_CYAN; case 8 : return SWT.COLOR_DARK_GRAY; case 9 : return
	 * SWT.COLOR_WHITE; case 10: return SWT.COLOR_RED; case 11: return
	 * SWT.COLOR_BLACK; case 12: return SWT.COLOR_YELLOW; case 13: return
	 * SWT.COLOR_BLUE; case 14: return SWT.COLOR_MAGENTA; case 15: return
	 * SWT.COLOR_CYAN; case 16: return SWT.COLOR_GRAY; } return SWT.COLOR_GREEN; //
	 * default }
	 */
	public static int getCurrentColorConstant(String clr) {
		if (clr.equals("DARK_RED"))
			return SWT.COLOR_DARK_RED;
		else if (clr.equals("DARK_GREEN"))
			return SWT.COLOR_DARK_GREEN;
		else if (clr.equals("DARK_YELLOW"))
			return SWT.COLOR_DARK_YELLOW;
		else if (clr.equals("DARK_BLUE"))
			return SWT.COLOR_DARK_BLUE;
		else if (clr.equals("DARK_MAGENTA"))
			return SWT.COLOR_DARK_MAGENTA;
		else if (clr.equals("DARK_CYAN"))
			return SWT.COLOR_DARK_CYAN;
		else if (clr.equals("DARK_GRAY"))
			return SWT.COLOR_DARK_GRAY;
		else if (clr.equals("WHITE"))
			return SWT.COLOR_WHITE;
		else if (clr.equals("RED"))
			return SWT.COLOR_RED;
		else if (clr.equals("BLACK"))
			return SWT.COLOR_BLACK;
		else if (clr.equals("YELLOW"))
			return SWT.COLOR_YELLOW;
		else if (clr.equals("BLUE"))
			return SWT.COLOR_BLUE;
		else if (clr.equals("MAGENTA"))
			return SWT.COLOR_MAGENTA;
		else if (clr.equals("CYAN"))
			return SWT.COLOR_CYAN;
		else if (clr.equals("GRAY"))
			return SWT.COLOR_GRAY;
		else
			return SWT.COLOR_GREEN;
	}
}
