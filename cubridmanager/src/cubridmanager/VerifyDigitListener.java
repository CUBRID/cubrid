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

import org.eclipse.swt.widgets.Event;
import org.eclipse.swt.widgets.Listener;

import cubridmanager.cubrid.ParameterItem;

public class VerifyDigitListener implements Listener {
	int type = ParameterItem.tPositiveNumber;

	public VerifyDigitListener() {
		type = ParameterItem.tPositiveNumber;
	}

	public VerifyDigitListener(int type) {
		this.type = type;
	}

	public void handleEvent(Event e) {
		String string = e.text;
		char[] chars = new char[string.length()];
		string.getChars(0, chars.length, chars, 0);
		switch (type) {
		case ParameterItem.tBoolean:
			for (int i = 0; i < chars.length; i++) {
				if (!(chars[i] == '0' || chars[i] == '1')) {
					e.doit = false;
					return;
				}
			}
		case ParameterItem.tPositiveNumber:
			for (int i = 0; i < chars.length; i++) {
				if (!('0' <= chars[i] && chars[i] <= '9')) {
					e.doit = false;
					return;
				}
			}
		case ParameterItem.tInteger:
			for (int i = 0; i < chars.length; i++) {
				if (!(('0' <= chars[i] && chars[i] <= '9') || chars[i] == '-')) {
					e.doit = false;
					return;
				}
			}
		case ParameterItem.tFloat:
			for (int i = 0; i < chars.length; i++) {
				if (!(('0' <= chars[i] && chars[i] <= '9') || chars[i] == '.' || chars[i] == '-')) {
					e.doit = false;
					return;
				}
			}

		// Normal text
		case ParameterItem.tIsolation:
		case ParameterItem.tString:
		case ParameterItem.tUnknown:
		default:
			;
		}
	}
}
