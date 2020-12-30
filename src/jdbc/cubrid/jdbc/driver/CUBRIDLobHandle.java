/*
 * Copyright (C) 2008 Search Solution Corporation. 
 * Copyright (c) 2016 CUBRID Corporation.
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

package cubrid.jdbc.driver;

public class CUBRIDLobHandle {
	private int lobType; // U_TYPE_BLOB or U_TYPE_CLOB
	private long lobSize;
	private byte[] packedLobHandle;
	private String locator;

	public CUBRIDLobHandle(int lobType, byte[] packedLobHandle, boolean isLobLocator) {
		this.lobType = lobType;
		this.packedLobHandle = packedLobHandle;
		initLob(isLobLocator);
	}

	private void initLob(boolean isLobLocator) {
		int pos = 0;

		if (packedLobHandle == null) {
			throw new NullPointerException();
		}

		if (isLobLocator == true) {
			pos += 4; // skip db_type

			lobSize = 0;
			for (int i = pos; i < pos + 8; i++) {
				lobSize <<= 8;
				lobSize |= (packedLobHandle[i] & 0xff);
			}
			pos += 8; // lob_size

			int locatorSize = 0;
			for (int i = pos; i < pos + 4; i++) {
				locatorSize <<= 8;
				locatorSize |= (packedLobHandle[i] & 0xff);
			}
			pos += 4; // locator_size

			locator = new String(packedLobHandle, pos, locatorSize - 1);
			// remove terminating null character
		} else
		{
			lobSize = packedLobHandle.length;
			locator = packedLobHandle.toString();
		}
	}

	public void setLobSize(long size) {
		int pos = 0;

		if (packedLobHandle == null) {
			throw new NullPointerException();
		}

		pos += 4; // skip db_type

		lobSize = size;
		int bitpos = 64;
		for (int i = pos; i < pos + 8; i++) {
			bitpos -= 8;
			packedLobHandle[i] = (byte) ((lobSize >>> bitpos) & 0xFF);
		}
	}

	public long getLobSize() {
		return lobSize;
	}

	public byte[] getPackedLobHandle() {
		return packedLobHandle;
	}

	public String toString() {
		return locator;
	}

	public boolean equals(Object obj) {
		if (obj instanceof CUBRIDLobHandle) {
			CUBRIDLobHandle that = (CUBRIDLobHandle) obj;
			return lobType == that.lobType && lobSize == that.lobSize
					&& locator.equals(that.locator);
		}
		return false;
	}
}
