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

/*
 * class UConnection method executeBatch,
 * ( Interface Statement method batchExecute를 위한 interface )
 * class UStatement method batchExecute
 * ( Interface PreparedStatement method batchExecute를 위한 interface )
 * 와 같은 batch statement를 위한 두개의 interface에서 얻어지는 result를
 * 관리하는 class이다.
 *
 * since 2.0
 */

public class UBatchResult
{
  private boolean errorFlag;
  private int resultNumber; /* batch job으로 수행된 statement의 개수 */
  private int result[]; /* batch statement의 각 result count */
  private int statementType[]; /* batch statement의 각 statement type */
  private int errorCode[]; /* batch statement의 각 error code */
  private String errorMessage[]; /* batch statement의 각 error message */

  UBatchResult(int number)
  {
    resultNumber = number;
    result = new int[resultNumber];
    statementType = new int[resultNumber];
    errorCode = new int[resultNumber];
    errorMessage = new String[resultNumber];
    errorFlag = false;
  }

  /*
   * batch statement의 execution 후 얻어지는 각 statement별 error code를
   * return한다.
   */

  public int[] getErrorCode()
  {
    return errorCode;
  }

  /*
   * batch statement의 execution 후 얻어지는 각 statement별 error Message를
   * return한다.
   */

  public String[] getErrorMessage()
  {
    return errorMessage;
  }

  /*
   * batch statement의 execution 후 얻어지는 각 statement별 result count를
   * return한다. error가 발생하였을 경우 result count는 -3이다.
   */

  public int[] getResult()
  {
    return result;
  }

  /*
   * batch job으로 execute된 statement 개수를 return한다.
   */

  public int getResultNumber()
  {
    return resultNumber;
  }

  /*
   * batch statement의 각 statement별 type을 return한다. class
   * CUBRIDCommandType에서 type을 identify할 수 있다.
   */

  public int[] getStatementType()
  {
    return statementType;
  }

  /*
   * error가 발생하지 않은 statement의 결과를 set하기 위한 interface이다. error
   * code는 0으로, error message는 null로 지정된다.
   */

  synchronized void setResult(int index, int count)
  {
    if (index < 0 || index >= resultNumber)
      return;
    result[index] = count;
    errorCode[index] = 0;
    errorMessage[index] = null;
  }

  /*
   * error가 발생한 statement의 결과를 set하기 위한 interface이다. result
   * count는 -3으로, error code와 error message는 server쪽에서 넘어온 정보를
   * set한다.
   */

  synchronized void setResultError(int index, int code, String message)
  {
    if (index < 0 || index >= resultNumber)
      return;
    result[index] = -3;
    errorCode[index] = code;
    errorMessage[index] = message;
    errorFlag = true;
  }

  /*
   * index에 해당하는 statement의 type을 지정하기 위한 interface이다. statement
   * type은 class CUBRIDCommandType에서 identify할 수 있다.
   */

  public boolean getErrorFlag()
  {
    return errorFlag;
  }

  synchronized void setStatementType(int index, int type)
  {
    if (index < 0 || index >= resultNumber)
      return;
    statementType[index] = type;
  }
}
