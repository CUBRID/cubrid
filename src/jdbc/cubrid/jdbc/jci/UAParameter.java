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

import java.math.BigDecimal;
import java.sql.Date;
import java.sql.Time;
import java.sql.Timestamp;

import cubrid.sql.CUBRIDOID;

/**
 * Collection type에 data를 add하거나 delete할 때 JCI에서는 add하고자하는
 * attribute의 collection base type정보를 알 수 없기 때문에 End user로부터
 * 입력받은 data의 type으로부터 CUBRID type을
 */

class UAParameter extends UParameter
{
  private String attributeName;

  UAParameter(String pName, Object pValue) throws UJciException
  {
    super(1);

    byte[] pTypes = new byte[1];
    Object attributeValue[] = new Object[1];

    attributeName = pName;
    attributeValue[0] = pValue;
    if (pValue == null)
    {
      pTypes[0] = UUType.U_TYPE_NULL;
    }
    else
    {
      pTypes[0] = UUType.getObjectDBtype(pValue);
      if (pTypes[0] == UUType.U_TYPE_NULL
          || pTypes[0] == UUType.U_TYPE_SEQUENCE)
        throw new UJciException(UErrorCode.ER_INVALID_ARGUMENT);
    }

    setParameters(pTypes, attributeValue);
  }

  synchronized void writeParameter(UOutputBuffer outBuffer)
      throws UJciException
  {
    if (attributeName != null)
      outBuffer.addStringWithNull(attributeName);
    else
      outBuffer.addNull();
    outBuffer.addByte(types[0]);
    outBuffer.writeParameter(types[0], values[0]);
  }
}
