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

/**
 * Title:        CUBRID Java Client Interface<p>
 * Description:  CUBRID Java Client Interface<p>
 * @version 2.0
 */

package cubrid.jdbc.jci;

import java.io.IOException;

class UUpdateParameter extends UParameter {
	private int indexes[]; /* parameter's column index */

	public UUpdateParameter(UColumnInfo columnInfo[], int[] columnIndexes,
			Object[] columnValues) throws UJciException {
		super(columnValues.length);

		/* check acceptable argument */
		if (columnIndexes == null || columnValues == null
				|| columnIndexes.length != columnValues.length) {
			throw new UJciException(UErrorCode.ER_INVALID_ARGUMENT);
		}

		for (int i = 0; i < columnIndexes.length; i++) {
			if (columnIndexes[i] < 0 || columnIndexes[i] > columnInfo.length)
				throw new UJciException(UErrorCode.ER_INVALID_ARGUMENT);
		}

		UColumnInfo info[] = columnInfo;
		byte[] pTypes = new byte[number];
		indexes = new int[number];

		for (int i = 0; i < types.length; i++) {
			pTypes[i] = info[columnIndexes[i]].getColumnType();
		}

		setParameters(pTypes, columnValues);

		for (int i = 0; i < number; i++) {
			indexes[i] = columnIndexes[i] + 1;
		}
	}

	synchronized void writeParameter(UOutputBuffer outBuffer)
			throws UJciException {
	    	try {
            		for (int i = 0; i < number; i++) {
            			outBuffer.addInt(indexes[i]);
            			outBuffer.addByte(types[i]);
            			outBuffer.writeParameter(types[i], values[i]);
            		}
	    	} catch (IOException e) {
	    	    	throw new UJciException(UErrorCode.ER_INVALID_ARGUMENT);    
	    	}
	}
}
