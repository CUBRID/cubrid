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

public class DiagActivityResult {
	private String EventClass = new String();
	private String TextData = new String();
	private String BinData = new String();
	private String IntegerData = new String();
	private String Time = new String();

	public DiagActivityResult() {
		EventClass = "";
		TextData = "";
		BinData = "";
		IntegerData = "";
		Time = "";
	}

	public DiagActivityResult(DiagActivityResult clone) {
		SetEventClass(clone.GetEventClass());
		SetTextData(clone.GetTextData());
		SetBinData(clone.GetBinData());
		SetIntegerData(clone.GetIntegerData());
		SetTimeData(clone.GetTimeData());
	}

	public void SetEventClass(String value) {
		EventClass = value;
	}

	public void SetTextData(String value) {
		TextData = value;
	}

	public void SetBinData(String value) {
		BinData = value;
	}

	public void SetIntegerData(String value) {
		IntegerData = value;
	}

	public void SetTimeData(String value) {
		Time = value;
	}

	public String GetEventClass() {
		return EventClass;
	}

	public String GetTextData() {
		return TextData;
	}

	public String GetBinData() {
		return BinData;
	}

	public String GetIntegerData() {
		return IntegerData;
	}

	public String GetTimeData() {
		return Time;
	}
}
