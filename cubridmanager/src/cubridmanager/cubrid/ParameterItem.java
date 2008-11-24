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

package cubridmanager.cubrid;

import cubridmanager.MainConstants;
import cubridmanager.Messages;

public class ParameterItem {
	static public final short tBoolean = 0;
	static public final short tPositiveNumber = 1;
	static public final short tInteger = 2;
	static public final short tFloat = 3;
	static public final short tString = 4;
	static public final short tIsolation = 5;
	static public final short tUnknown = -1;
	static public final short uClient = 0;
	static public final short uServer = 1;
	static public final short uBoth = 2;
	static public final short uNone = 3;
	static public final short uNotUse = -1;
	public String name;
	public short type;
	public String defaultValue;
	public String min;
	public String max;
	public String desc;
	public short use;
	private String errmsg;

	/**
	 * ParameterItem - it takes sqlx.init parameter's name, type, default value, 
	 *            minimum value, maximum value, descripton, server/client's uses information.
	 * 
	 * @param name
	 *            Parameter name
	 * @param type
	 *            Parameter value's type (tBoolean, tInteger, tFloat, tString, tIsolation)
	 * @param defaultValue
	 *            defalut value
	 * @param min
	 *            minimum value
	 * @param max
	 *            maximum value
	 * @param desc
	 *            description
	 * @param use
	 *            uses (uClient, uServer, uBoth, uNone, uNotUse)
	 */
	public ParameterItem(String name, short type, String defaultValue,
			String min, String max, String desc, short use) {
		this.name = name;
		this.type = type;
		this.defaultValue = defaultValue;
		this.min = min;
		this.max = max;
		this.desc = desc;
		this.use = use;

		errmsg = new String();
	}

	/**
	 * checkValue -
	 * 
	 * @param value
	 * @param errmsg
	 * @return
	 */
	public boolean checkValue(String value) {
		int iValue;
		float fValue;

		if (value.length() == 0)
			return true;
		else {
			switch (type) {
			case tBoolean:
				try {
					iValue = Integer.parseInt(value);
					if (iValue == 0 || iValue == 1)
						return true;
					else
						throw new Exception();
				} catch (Exception e) {
					errmsg = Messages.getString("ERROR.NOTBOOLEAN");
					errmsg += MainConstants.NEW_LINE
							+ Messages.getString("SQLXINIT.TYPE")
							+ Messages.getString("SQLXINIT.BOOLEAN");
					return false;
				}
			case tInteger:
				try {
					iValue = Integer.parseInt(value);
					if (iValue == Integer.parseInt(defaultValue))
						return true;
					else if (max.length() > 0 && iValue > Integer.parseInt(max))
						throw new Exception();
					else if (min.length() > 0 && iValue < Integer.parseInt(min))
						throw new Exception();
					else
						return true;
				} catch (Exception e) {
					errmsg = Messages.getString("ERROR.OUTOFBOUNDS");
					errmsg += MainConstants.NEW_LINE
							+ Messages.getString("SQLXINIT.TYPE")
							+ Messages.getString("SQLXINIT.INTEGER");
					if (min.length() > 0)
						errmsg += MainConstants.NEW_LINE
								+ Messages.getString("LABEL.MIN") + ":" + min;
					if (max.length() > 0)
						errmsg += MainConstants.NEW_LINE
								+ Messages.getString("LABEL.MAX") + ":" + max;
					return false;
				}
			case tFloat:
				try {
					fValue = Float.parseFloat(value);
					if (fValue == Float.parseFloat(defaultValue))
						return true;
					else if (max.length() > 0 && fValue > Float.parseFloat(max))
						throw new Exception();
					else if (min.length() > 0 && fValue < Float.parseFloat(min))
						throw new Exception();
					else
						return true;
				} catch (Exception e) {
					errmsg = Messages.getString("ERROR.OUTOFBOUNDS");
					errmsg += MainConstants.NEW_LINE
							+ Messages.getString("SQLXINIT.TYPE")
							+ Messages.getString("SQLXINIT.FLOAT");
					if (min.length() > 0)
						errmsg += MainConstants.NEW_LINE
								+ Messages.getString("LABEL.MIN") + ":" + min;
					if (max.length() > 0)
						errmsg += MainConstants.NEW_LINE
								+ Messages.getString("LABEL.MAX") + ":" + max;
					return false;
				}
			case tString:
				if (value.indexOf(" ") >= 0) {
					errmsg += Messages.getString("ERROR.INCLUDESPACE");
					return false;
				} else
					return true;
			case tIsolation:
				return true;
			case tUnknown:
			default:
			}
		}

		return true;
	}

	public String getErrmsg() {
		return errmsg;
	}
}
