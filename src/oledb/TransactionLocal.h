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
  
#pragma once

{

  
  
  {
    
    
    
      return E_FAIL;
    
    
    
      return E_FAIL;
    
  
  
  {
    
    
      
      {
      
	
	break;
      
	
	break;
      
	
	break;
      
	
      
    
    
    
      return E_FAIL;
    
    
  

  {
    
    
    
    
      return;
    
    
    
  
  
  {
    
      return S_OK;
    
    
    
      return E_FAIL;
    
  
  
    // AutoCommit�� ���� �θ� �� �ֵ��� �����ִ� �Լ�
  static HRESULT AutoCommit (IObjectWithSite * object) 
  {
    
    
    
    
      return hr;
    
    
  
  
    // statement ������ ������ �� �Լ��� ȣ���Ѵ�.
    // Transaction�� �������� ���� ���¸� Commit �Ѵ�.
    // Transaction�� ���������� Commit �Ǵ� Rollback��
    // ITransaction::Commit�� ITransaction::Abort���� �Ѵ�.
    HRESULT AutoCommit () 
  {
    
    
      
      {
	
	
	  return hr;
      
    
  
  
  {
    
    
  
  
				  ULONG * pulTransactionLevel) 
  {
    
    
      // we do not support nested transactions
      if (!m_bAutoCommit)
      return XACT_E_XTIONEXISTS;
    
      return XACT_E_NOISORETAIN;
    
    
    
      return hr;
    
    
      return hr;
    
      // flat transaction�̹Ƿ� �׻� 1
      if (pulTransactionLevel)
      *pulTransactionLevel = 1;
    
    
  
  
  {
    
    
      return XACT_E_NOTSUPPORTED;
    
      return XACT_E_NOTRANSACTION;
    
    
      return hr;
    
      // fRetaining==true �� transaction�� �����Ѵ�.
      if (!fRetaining)
      EnterAutoCommitMode ();
    
  
  
  {
    
    
      return XACT_E_NOTSUPPORTED;
    
      return XACT_E_NOTRANSACTION;
    
    
      return hr;
    
      // fRetaining==true �� transaction�� �����Ѵ�.
      if (!fRetaining)
      EnterAutoCommitMode ();
    
  
  
  {
    
    
      return E_INVALIDARG;
    
      return XACT_E_NOTRANSACTION;
    
    
      // TODO: pinfo->uow?
      pinfo->isoLevel = m_isoLevel;
    
    
  


