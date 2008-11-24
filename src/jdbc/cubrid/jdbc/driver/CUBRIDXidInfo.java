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

package cubrid.jdbc.driver;

import javax.transaction.xa.*;
import java.util.*;
import cubrid.jdbc.jci.*;

class CUBRIDXidInfo
{
  static final int STATUS_NOFLAG = 0, STATUS_STARTED = 1, STATUS_SUSPENDED = 2,
      STATUS_PREPARED = 3, STATUS_RECOVERED = 4, STATUS_COMPLETED = 5;

  Xid xid;
  UConnection ucon;
  int status;

  CUBRIDXidInfo(Xid xid, UConnection ucon, int status)
  {
    this.xid = xid;
    this.ucon = ucon;
    this.status = status;
  }

  boolean compare(CUBRIDXidInfo x)
  {
    return (compare(x.xid));
  }

  boolean compare(Xid x)
  {
    if ((xid.getFormatId() == x.getFormatId())
        && Arrays.equals(xid.getBranchQualifier(), x.getBranchQualifier())
        && Arrays.equals(xid.getGlobalTransactionId(), x
            .getGlobalTransactionId()))
    {
      return true;
    }
    return false;
  }
}
