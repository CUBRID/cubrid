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

import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.text.DateFormat;
import java.text.ParseException;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Calendar;
import java.util.Date;
import java.util.GregorianCalendar;
import java.util.Locale;
import java.util.Properties;

import org.eclipse.swt.SWT;
import org.eclipse.swt.graphics.Rectangle;
import org.eclipse.swt.widgets.MessageBox;
import org.eclipse.swt.widgets.Shell;

public class CommonTool {
	public static int MsgBox(Shell sh, int style, String title, String msg) {
		MessageBox messageBox = new MessageBox(sh, style);
		messageBox.setText(title);
		messageBox.setMessage(msg);
		return messageBox.open();
	}

	public static int MsgBox(Shell sh, String title, String msg) {
		return MsgBox(sh, SWT.OK | SWT.APPLICATION_MODAL, title, msg);
	}

	public static void InformationBox(Shell sh, String title, String msg) {
		MsgBox(sh, SWT.ICON_INFORMATION | SWT.OK, title, msg);
	}

	public static void InformationBox(String title, String msg) {
		InformationBox(Application.mainwindow.getShell(), title, msg);
	}

	public static int WarnYesNo(Shell sh, String msg) {
		MessageBox messageBox = new MessageBox(sh, SWT.YES | SWT.NO
				| SWT.APPLICATION_MODAL);
		messageBox.setText(Messages.getString("MSG.WARNING"));
		messageBox.setMessage(msg);
		return messageBox.open();
	}

	public static int WarnYesNo(String msg) {
		return WarnYesNo(Application.mainwindow.getShell(), msg);
	}

	public static void ErrorBox(String msg) {
		MsgBox(Application.mainwindow.getShell(), SWT.ICON_ERROR
				| SWT.APPLICATION_MODAL, Messages.getString("MSG.ERROR"), msg);
	}

	public static void ErrorBox(Shell sh, String msg) {
		MsgBox(sh, SWT.ICON_ERROR | SWT.APPLICATION_MODAL, Messages
				.getString("MSG.ERROR"), msg);
	}

	public static void WarnBox(Shell sh, String msg) {
		MsgBox(sh, SWT.ICON_WARNING | SWT.APPLICATION_MODAL, Messages
				.getString("MSG.WARNING"), msg);
	}

	public static String GetCubridVersion() {
		// "getenv"
		return MainRegistry.CUBRIDVer;
	}

	public static String GetCASVersion() {
		// "getenv"
		return MainRegistry.BROKERVer;
	}

	public static int atoi(String inval) {
		int ret = 0;

		try {
			ret = Integer.parseInt(inval);
		} catch (Exception e) {
			ret = 0;
		}
		return ret;
	}

	public static long atol(String inval) {
		long ret = 0;

		try {
			ret = Long.parseLong(inval);
		} catch (Exception e) {
			ret = 0;
		}
		return ret;
	}

	public static double atof(String inval) {
		double ret = 0.0;

		try {
			ret = Double.parseDouble(inval);
		} catch (Exception e) {
			ret = 0.0;
		}
		return ret;
	}

	public static boolean LoadProperties(Properties prop) {
		try {
			InputStream in = new FileInputStream(
					MainConstants.FILE_CONFIGURATION);
			if (in != null) {
				prop.load(in);
				in.close();
				return true;
			}
		} catch (Exception ex) {
		}
		return false;
	}

	public static boolean SaveProperties(Properties prop) {
		try {
			OutputStream out = new FileOutputStream(
					MainConstants.FILE_CONFIGURATION);
			prop.store(out, MainConstants.PROP_HOSTCONFIGURATION);
			out.close();
			return true;
		} catch (Exception e) {
			CommonTool.debugPrint(e);
		}
		return false;
	}

	public static void SetDefaultParameter() {
		Properties prop = new Properties();

		LoadProperties(prop);

		prop.setProperty(MainConstants.SYSPARA_HOSTCNT, "1");
		prop.setProperty(MainConstants.SYSPARA_HOSTBASE + 0, "localhost");
		prop.setProperty(MainConstants.SYSPARA_HOSTBASE + 0
				+ MainConstants.SYSPARA_HOSTADDR, "localhost");
		prop.setProperty(MainConstants.SYSPARA_HOSTBASE + 0
				+ MainConstants.SYSPARA_HOSTPORT, "8001");
		prop.setProperty(MainConstants.SYSPARA_HOSTBASE + 0
				+ MainConstants.SYSPARA_HOSTID, "admin");
		prop.setProperty(MainConstants.SQLX_DATABUFS, "10000");
		prop.setProperty(MainConstants.SQLX_MEDIAFAIL, "1");
		prop.setProperty(MainConstants.SQLX_MAXCLI, "50");
		prop.setProperty(MainConstants.DBPARA_GENERICNUM, "5000");
		prop.setProperty(MainConstants.DBPARA_LOGNUM, "10000");
		prop.setProperty(MainConstants.DBPARA_PAGESIZE, "4096");
		prop.setProperty(MainConstants.DBPARA_DATANUM, "20000");
		prop.setProperty(MainConstants.DBPARA_INDEXNUM, "10000");
		prop.setProperty(MainConstants.DBPARA_TEMPNUM, "20000");
		prop.setProperty(MainConstants.MONPARA_STATUS, "OFF");
		prop.setProperty(MainConstants.MONPARA_INTERVAL, "5");
		prop.setProperty(MainConstants.queryEditorOptionAucoCommit, "yes");
		prop.setProperty(MainConstants.queryEditorOptionGetQueryPlan, "yes");
		prop.setProperty(MainConstants.queryEditorOptionRecordLimit, "5000");
		prop.setProperty(MainConstants.queryEditorOptionGetOidInfo, "no");
		prop.setProperty(MainConstants.queryEditorOptionCasPort, "30000");
		prop.setProperty(MainConstants.queryEditorOptionCharSet, "");
		prop.setProperty(MainConstants.queryEditorOptionFontString, "");
		prop.setProperty(MainConstants.queryEditorOptionFontColorRed, "0");
		prop.setProperty(MainConstants.queryEditorOptionFontColorGreen, "0");
		prop.setProperty(MainConstants.queryEditorOptionFontColorBlue, "0");
		prop.setProperty(MainConstants.mainWindowX, "1024");
		prop.setProperty(MainConstants.mainWindowY, "768");
		prop.setProperty(MainConstants.mainWindowMaximize, "no");
		prop.setProperty(MainConstants.protegoLoginType,
				MainConstants.protegoLoginTypeCert);

		try {
			OutputStream out = new FileOutputStream(
					MainConstants.FILE_CONFIGURATION);
			prop.store(out, MainConstants.PROP_HOSTCONFIGURATION);
			out.close();
		} catch (Exception e) {
			e.printStackTrace();
		}
	}

	public static String BooleanYN(boolean inyn) {
		if (inyn)
			return "Y";
		else
			return "N";
	}

	public static String BooleanYesNo(boolean inyn) {
		if (inyn)
			return "Yes";
		else
			return "No";
	}

	public static String BooleanO(boolean inyn) {
		if (inyn)
			return Messages.getString("STRING.O");
		else
			return "";
	}

	public static boolean yesNoToBoolean(String input) {
		if (input.toLowerCase().equals("yes"))
			return true;
		else
			return false;
	}

	public static String ArrayToString(ArrayList al) {
		String ret = null;
		for (int i = 0, n = al.size(); i < n; i++) {
			if (ret == null)
				ret = (String) al.get(i);
			else
				ret = ret.concat(", " + al.get(i));
		}
		if (ret == null)
			ret = new String("");
		return ret;
	}

	public static boolean isValidDBName(String DBName) {
		if (DBName == null || DBName.equals(""))
			return false;
		//it is better that unix file name does not contain space(" ") character
		if (DBName.indexOf(" ") >= 0)
			return false;
		//Unix file name is not allowed to begin with "#" character
		if (DBName.charAt(0) == '#')
			return false;
		//Unix file name is not allowed to begin with "-" character
		if (DBName.charAt(0) == '-')
			return false;
		//9 character(*&%$|^/~\) are not allowed in Unix file name
		if (DBName.matches(".*[*&%$\\|^/~\\\\].*")){ 
			return false;			
		}
		//Unix file name is not allowed to be named as "." or ".."
		if (DBName.equals(".")||DBName.equals("..")){
			return false;			
		}
		return true;
	}

	public static String ValidateCheckInIdentifier(String Identifier) {
		String ret_string; // add string if Identifier has invalid string. 

		ret_string = ""; // Last status is "", it is valid identifier.
		if (Identifier == null || Identifier.length() <= 0) {
			ret_string = "empty";
			return ret_string;
		}

		if (Identifier.indexOf(" ") >= 0)
			ret_string += " ";
		if (Identifier.indexOf("\t") >= 0)
			ret_string += "\t";
		if (Identifier.indexOf("/") >= 0)
			ret_string += "/";
		if (Identifier.indexOf(".") >= 0)
			ret_string += ".";
		if (Identifier.indexOf("~") >= 0)
			ret_string += "~";
		if (Identifier.indexOf(",") >= 0)
			ret_string += ",";
		if (Identifier.indexOf("\\") >= 0)
			ret_string += "\\";
		if (Identifier.indexOf("\"") >= 0)
			ret_string += "\"";
		if (Identifier.indexOf("|") >= 0)
			ret_string += "|";
		if (Identifier.indexOf("]") >= 0)
			ret_string += "]";
		if (Identifier.indexOf("[") >= 0)
			ret_string += "[";
		if (Identifier.indexOf("}") >= 0)
			ret_string += "}";
		if (Identifier.indexOf("{") >= 0)
			ret_string += "{";
		if (Identifier.indexOf(")") >= 0)
			ret_string += ")";
		if (Identifier.indexOf("(") >= 0)
			ret_string += "(";
		if (Identifier.indexOf("=") >= 0)
			ret_string += "=";
		if (Identifier.indexOf("-") >= 0)
			ret_string += "-";
		if (Identifier.indexOf("+") >= 0)
			ret_string += "+";
		if (Identifier.indexOf("?") >= 0)
			ret_string += "?";
		if (Identifier.indexOf("<") >= 0)
			ret_string += "<";
		if (Identifier.indexOf(">") >= 0)
			ret_string += ">";
		if (Identifier.indexOf(":") >= 0)
			ret_string += ":";
		if (Identifier.indexOf(";") >= 0)
			ret_string += ";";
		if (Identifier.indexOf("!") >= 0)
			ret_string += "!";
		if (Identifier.indexOf("'") >= 0)
			ret_string += "'";
		if (Identifier.indexOf("@") >= 0)
			ret_string += "@";
		if (Identifier.indexOf("$") >= 0)
			ret_string += "$";
		if (Identifier.indexOf("^") >= 0)
			ret_string += "^";
		if (Identifier.indexOf("&") >= 0)
			ret_string += "&";
		if (Identifier.indexOf("*") >= 0)
			ret_string += "*";

		return ret_string;
	}

	public static void centerShell(Shell sShell) {
		Rectangle mainBounds;
		Rectangle displayBounds = sShell.getDisplay().getClientArea();

		Rectangle shellBounds;

		if (sShell == null)
			return;

		if (sShell.getShell() == null)
			mainBounds = displayBounds;
		else
			mainBounds = Application.mainwindow.getShell().getBounds();

		shellBounds = sShell.getBounds();

		int x = mainBounds.x + (mainBounds.width - shellBounds.width) / 2;
		int y = mainBounds.y + (mainBounds.height - shellBounds.height) / 2;

		if (x < 0)
			x = 0;
		if (y < 0)
			y = 0;

		if ((x + shellBounds.width) > displayBounds.width)
			x = displayBounds.width - shellBounds.width;
		if ((y + shellBounds.height) > displayBounds.height)
			y = displayBounds.height - shellBounds.height;

		sShell.setLocation(x, y);
	}

	public static boolean checkDate(String yyyymmdd) {
		if (yyyymmdd.length() != 10)
			return false;

		int lef = CommonTool.atoi(yyyymmdd.substring(0, 4));
		int mid = CommonTool.atoi(yyyymmdd.substring(5, 7));
		int ri = CommonTool.atoi(yyyymmdd.substring(8, 10));
		if (lef <= 0 || mid < 1 || mid > 12 || ri < 1 || ri > 31
				|| yyyymmdd.charAt(4) != '-' || yyyymmdd.charAt(7) != '-')
			return false;
		return true;
	}

	public static boolean checkTime(String time) {
		try {
			if (time.length() > 4)
				throw new Exception();

			int hh24 = Integer.parseInt(time.substring(0, 2));
			int mm = Integer.parseInt(time.substring(2, 4));
			if (hh24 < 0 || hh24 > 23 || mm < 0 || mm > 59)
				throw new Exception();

			return true;
		} catch (Exception e) {
			return false;
		}

	}

	public static String convertYYYYMMDD(String date) {
		if (date.length() != 8)
			return date;
		String year = date.substring(0, 4);
		String month = date.substring(4, 6);
		String day = date.substring(6);
		return year + "-" + month + "-" + day;
	}

	public static String convertHH24MON(String time) {
		if (time.length() != 4)
			return time;
		String hour = time.substring(0, 2);
		String minute = time.substring(2);
		return hour + ":" + minute;
	}

	public static void debugPrint(String msg) {
		if (Version.cmDebugMode)
			System.out.println(msg);
	}

	public static void debugPrint(Throwable e) {
		if (Version.cmDebugMode)
			e.printStackTrace();
	}

	public static char getPathSeperator(String path) {
		if (path.indexOf("\\") != -1)
			return '\\';
		else
			return '/';
	}
	/**
	 * support multi input time string, return the timestamp, a long type with unit second
	 * <li>"hh:mm[:ss] a"
	 * <li>"a hh:mm[:ss]"
	 * <li>"HH:mm[:ss]"
	 * @param timestring String time string eg: 11:12:13 am
	 * @return long timestamp
	 * @throws ParseException
	 */
	public static long getTime(String timestring)throws ParseException{
		String[] supportedInputDateTime={
				"hh:mm:ss a",
				"a hh:mm:ss",
				"HH:mm:ss",	
				"hh:mm a",
				"a hh:mm",
				"HH:mm",	
				"''hh:mm:ss a''",
				"''a hh:mm:ss''",
				"''HH:mm:ss''",
				"''hh:mm a''",
				"''a hh:mm''",
				"''HH:mm''",
		};
		for(String datepattern:supportedInputDateTime){
			if(validateTimestamp(timestring,datepattern)){
				try {
					return getTimestamp(timestring, datepattern);
				} catch (Exception e) {
					//it is designed not to run at here,so throws nothing
					CommonTool.debugPrint("an unexpected exception is throwed.\n"+e.getMessage());
				}
			}			
		}		
		throw new ParseException("Unparseable date: \"" + timestring + "\"",0);		
	}
	/**
	 * support multi input date string, return the timestamp, a long type with unit second
	 * <li>"MM/dd/yyyy",
	 * <li>"yyyy/MM/dd",
	 * <li>"yyyy-MM-dd"
	 * @param datestring String date string eg: 2009-02-20
	 * @return long timestamp
	 */
	public static long getDate(String datestring)throws ParseException{
		String[] supportedInputDateTime={
				"MM/dd/yyyy",				
				"yyyy/MM/dd",
				"yyyy-MM-dd",
				"''MM/dd/yyyy''",
				"''yyyy/MM/dd''",
				"''yyyy-MM-dd''"
		};
		for(String datepattern:supportedInputDateTime){
			if(validateTimestamp(datestring,datepattern)){
				try {
					return getTimestamp(datestring, datepattern);
				} catch (Exception e) {
					//it is designed not to run at here,so throws nothing
					CommonTool.debugPrint("an unexpected exception is throwed.\n"+e.getMessage());
				}
			}			
		}		
		throw new ParseException("Unparseable date: \"" + datestring + "\"",0);		
	}
	/**
	 * support multi input data string, return the timestamp, a long type with unit second
	 * <li>"hh:mm[:ss] a MM/dd/yyyy",
	 * <li>"HH:mm[:ss] MM/dd/yyyy",
	 * <li>"yyyy/MM/dd a hh:mm[:ss]",
	 * <li>"yyyy-MM-dd a hh:mm[:ss]",
	 * <li>"yyyy/MM/dd HH:mm[:ss]",
	 * <li>"yyyy-MM-dd HH:mm[:ss]"
	 * @param datestring String date string eg: 2009-02-20 16:42:46
	 * @return long timestamp
	 */
	public static long getTimestamp(String datestring)throws ParseException{
		String[] supportedInputDateTime={				
				"yyyy/MM/dd a hh:mm:ss",
				"yyyy-MM-dd a hh:mm:ss",
				"yyyy/MM/dd HH:mm:ss",
				"yyyy-MM-dd HH:mm:ss",
				"hh:mm:ss a MM/dd/yyyy",
				"HH:mm:ss MM/dd/yyyy",
				"yyyy/MM/dd a hh:mm",
				"yyyy-MM-dd a hh:mm",
				"yyyy/MM/dd HH:mm",
				"yyyy-MM-dd HH:mm",
				"hh:mm a MM/dd/yyyy",
				"HH:mm MM/dd/yyyy",
				"''yyyy/MM/dd a hh:mm:ss''",
				"''yyyy-MM-dd a hh:mm:ss''",
				"''yyyy/MM/dd HH:mm:ss''",
				"''yyyy-MM-dd HH:mm:ss''",
				"''hh:mm:ss a MM/dd/yyyy''",
				"''HH:mm:ss MM/dd/yyyy''",
				"''yyyy/MM/dd a hh:mm''",
				"''yyyy-MM-dd a hh:mm''",
				"''yyyy/MM/dd HH:mm''",
				"''yyyy-MM-dd HH:mm''",
				"''hh:mm a MM/dd/yyyy''",
				"''HH:mm MM/dd/yyyy''"
		};
		for(String datepattern:supportedInputDateTime){
			if(validateTimestamp(datestring,datepattern)){
				try {
					return getTimestamp(datestring, datepattern);
				} catch (Exception e) {
					//it is designed not to run at here,so throws nothing
					CommonTool.debugPrint("an unexpected exception is throwed.\n"+e.getMessage());
				}
			}			
		}		
		throw new ParseException("Unparseable date: \"" + datestring + "\"",0);		
	}
	/**
	 * validate whether a date string can be parsed by a given date pattern
	 * @param datestring String  a date string
	 * @param datepattern String  a given date pattern
	 * @return boolean true: can be parsed; false: can not
	 */
	public static boolean validateTimestamp(String datestring,String datepattern) {
		try {
			DateFormat formatter = new SimpleDateFormat(datepattern,Locale.US);
			formatter.setLenient(false);
			formatter.parse(datestring);
			return true;
		} catch (ParseException e) {
			return false;
		}
	}
	/**
	 * parse date string with a given date pattern, return long type timestamp, unit:second 
	 * 
	 * precondition: it is better to call cubridmanager.CommonTool.validateTimestamp(String, String)
	 * first to void throwing an ParseException
	 * @param datestring String date string eg: 2009-02-20 16:42:46
	 * @param datepattern String date pattern eg: yyyy-MM-dd HH:mm:ss
	 * @return long timestamp
	 * @throws ParseException
	 */
	public static long getTimestamp(String datestring,String datepattern) throws ParseException{
			DateFormat formatter = new SimpleDateFormat(datepattern,Locale.US);
			Date date=formatter.parse(datestring);
			long time=date.getTime()/1000;
			return time;		
	}
	/**
	 * format a timestamp into a given date pattern string
	 * @param timestamp long type timestamp, unit:second 
	 * @param datepattern a given date pattern
	 * @return
	 */
	public static String getTimestampString(long timestamp,String datepattern){
		DateFormat formatter = new SimpleDateFormat(datepattern,Locale.US);		
		Date date=new Date(timestamp*1000);		
		return formatter.format(date);
	}
}
