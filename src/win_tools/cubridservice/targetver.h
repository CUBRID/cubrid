/*
 * Copyright 2008 Search Solution Corporation
 * Copyright 2016 CUBRID Corporation
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 */

#pragma once

// ���� ��ũ�δ� �ʿ��� �ּ� �÷����� �����մϴ�. �ʿ��� �ּ� �÷�����
// ���� ���α׷��� �����ϴ� �� �ʿ��� ����� ���Ե� ���� ���� ������ Windows, Internet Explorer
// ���Դϴ�. �� ��ũ�δ� ������ ���� �̻��� �÷��� �������� ��� ������ ��� ����� Ȱ��ȭ�ؾ�
// �۵��մϴ�.

// �Ʒ� ������ �÷����� �켱�ϴ� �÷����� ������� �ϴ� ��� ���� ���Ǹ� �����Ͻʽÿ�.
// �ٸ� �÷����� ���Ǵ� �ش� ���� �ֽ� ������ MSDN�� �����Ͻʽÿ�.
#ifndef _WIN32_WINNT		// �ʿ��� �ּ� �÷����� Windows Vista�� �����մϴ�.
#define _WIN32_WINNT 0x0600	// �ٸ� ������ Windows�� �µ��� ������ ������ ������ �ֽʽÿ�.
#endif
