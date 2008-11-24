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

/**
 * class UStatement method updateRow에서 update할 row의 column parameter들을
 * 관리하기 위한 class이다. update하고자 하는 attribute의 info를 알 수 없기
 * 때문에 주어진 value의 java type에 따라 CUBRID type으로 match하였다. 따라서
 * user는 해당 attribute의 type을 미리알고 그 type에 해당되는 java type
 * columnValues를 넘겨야 한다.
 * 
 * since 2.0
 */

class UUpdateParameter extends UParameter
{
  private int indexes[]; /* parameter의 column index */

  public UUpdateParameter(UColumnInfo columnInfo[], int[] columnIndexes,
      Object[] columnValues) throws UJciException
  {
    super(columnValues.length);

    /* check acceptable argument */
    if (columnIndexes == null || columnValues == null
        || columnIndexes.length != columnValues.length)
    {
      throw new UJciException(UErrorCode.ER_INVALID_ARGUMENT);
    }

    for (int i = 0; i < columnIndexes.length; i++)
    {
      if (columnIndexes[i] < 0 || columnIndexes[i] > columnInfo.length)
        throw new UJciException(UErrorCode.ER_INVALID_ARGUMENT);
    }

    UColumnInfo info[] = columnInfo;
    byte[] pTypes = new byte[number];
    indexes = new int[number];

    for (int i = 0; i < types.length; i++)
    {
      pTypes[i] = info[columnIndexes[i]].getColumnType();
    }

    setParameters(pTypes, columnValues);

    for (int i = 0; i < number; i++)
    {
      /* JCI index는 언제나 0부터, server쪽 index는 언제나 1부터 시작 */
      indexes[i] = columnIndexes[i] + 1;
    }
  }

  /*
   * parameter를 output buffer에 write한다.
   */

  synchronized void writeParameter(UOutputBuffer outBuffer)
      throws UJciException
  {
    for (int i = 0; i < number; i++)
    {
      outBuffer.addInt(indexes[i]);
      outBuffer.addByte(types[i]);
      outBuffer.writeParameter(types[i], values[i]);
    }
  }
}
