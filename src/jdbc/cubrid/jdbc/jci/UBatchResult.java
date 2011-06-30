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

package cubrid.jdbc.jci;

public class UBatchResult {
	private boolean errorFlag;
	private int resultNumber;
	private int result[];
	private int statementType[];
	private int errorCode[];
	private String errorMessage[];

	UBatchResult(int number) {
		resultNumber = number;
		result = new int[resultNumber];
		statementType = new int[resultNumber];
		errorCode = new int[resultNumber];
		errorMessage = new String[resultNumber];
		errorFlag = false;
	}

	public int[] getErrorCode() {
		return errorCode;
	}

	public String[] getErrorMessage() {
		return errorMessage;
	}

	public int[] getResult() {
		return result;
	}

	public int getResultNumber() {
		return resultNumber;
	}

	public int[] getStatementType() {
		return statementType;
	}

	synchronized void setResult(int index, int count) {
		if (index < 0 || index >= resultNumber)
			return;
		result[index] = count;
		errorCode[index] = 0;
		errorMessage[index] = null;
	}

	synchronized void setResultError(int index, int code, String message) {
		if (index < 0 || index >= resultNumber)
			return;
		result[index] = -3;
		errorCode[index] = code;
		errorMessage[index] = message;
		errorFlag = true;
	}

	public boolean getErrorFlag() {
		return errorFlag;
	}

	synchronized void setStatementType(int index, int type) {
		if (index < 0 || index >= resultNumber)
			return;
		statementType[index] = type;
	}
}
