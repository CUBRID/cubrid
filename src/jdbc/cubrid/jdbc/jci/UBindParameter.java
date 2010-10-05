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

package @CUBRID_JCI@;

import java.util.Vector;

/*
 * PreparedStatement에 사용되는 parameter들을 관리하는 class이다.
 * 1.0에서 parameter를 관리하던 class UBindParameterInfo가 1차원으로 parameter를
 * 관리하여 2.0에서는 사용하기가 곤란하여 만들어진 대체 class이다.
 * parameter는 1차원 array를 element로 갖는 java.util.Vector class로 관리된다.
 *
 * since 2.0
 */

class UBindParameter extends UParameter
{
  private final static byte PARAM_MODE_UNKNOWN = 0;
  private final static byte PARAM_MODE_IN = 1;
  private final static byte PARAM_MODE_OUT = 2;
  private final static byte PARAM_MODE_INOUT = 3;

  byte paramMode[];

  private boolean isBinded[];

  UBindParameter(int parameterNumber)
  {
    super(parameterNumber);

    isBinded = new boolean[number];
    paramMode = new byte[number];

    clear();
  }

  /*
   * ======================================================================= |
   * PACKAGE ACCESS METHODS
   * =======================================================================
   */

  /*
   * parameter의 current cursor에서 모든 parameter들이 bind되었는지를 check한다.
   */
  boolean checkAllBinded()
  {
    for (int i = 0; i < number; i++)
    {
      if (isBinded[i] == false && paramMode[i] == PARAM_MODE_UNKNOWN)
        return false;
    }
    return true;
  }

  void clear()
  {
    for (int i = 0; i < number; i++)
    {
      isBinded[i] = false;
      paramMode[i] = PARAM_MODE_UNKNOWN;
      values[i] = null;
      types[i] = UUType.U_TYPE_NULL;
    }
  }

  synchronized void close()
  {
    for (int i = 0; i < number; i++)
      values[i] = null;
    isBinded = null;
    paramMode = null;
    values = null;
    types = null;
  }

  /*
   * current cursor의 index번 째 parameter value를 set한다.
   */
  synchronized void setParameter(int index, byte bType, Object bValue)
      throws UJciException
  {
    if (index < 0 || index >= number)
      throw new UJciException(UErrorCode.ER_INVALID_ARGUMENT);

    types[index] = bType;
    values[index] = bValue;

    isBinded[index] = true;
    paramMode[index] |= PARAM_MODE_IN;
  }

  void setOutParam(int index) throws UJciException
  {
    if (index < 0 || index >= number)
      throw new UJciException(UErrorCode.ER_INVALID_ARGUMENT);
    paramMode[index] |= PARAM_MODE_OUT;
  }

  synchronized void writeParameter(UOutputBuffer outBuffer)
      throws UJciException
  {
    for (int i = 0; i < number; i++)
    {
      if (values[i] == null)
      {
        outBuffer.addByte(UUType.U_TYPE_NULL);
        outBuffer.addNull();
      }
      else
      {
        outBuffer.addByte((byte) types[i]);
        outBuffer.writeParameter(((byte) types[i]), values[i]);
      }
    }
  }
}
