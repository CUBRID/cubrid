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

/**
 * CUBRID의 Schema type을 정의하는 class이다.
 * 
 * since 1.0
 */

abstract public class USchType
{
  public static final int SCH_MIN = 1;
  public static final int SCH_MAX = 16;

  public static final int SCH_CLASS = 1;
  public static final int SCH_VCLASS = 2;
  public static final int SCH_QUERY_SPEC = 3;
  public static final int SCH_ATTRIBUTE = 4;
  public static final int SCH_CLASS_ATTRIBUTE = 5;
  public static final int SCH_METHOD = 6;
  public static final int SCH_CLASS_METHOD = 7;
  public static final int SCH_METHOD_FILE = 8;
  public static final int SCH_SUPERCLASS = 9;
  public static final int SCH_SUBCLASS = 10;
  public static final int SCH_CONSTRAIT = 11;
  public static final int SCH_TRIGGER = 12;
  public static final int SCH_CLASS_PRIVILEGE = 13;
  public static final int SCH_ATTR_PRIVILEGE = 14;
  public static final int SCH_DIRECT_SUPER_CLASS = 15;
  public static final int SCH_PRIMARY_KEY = 16;
}
