/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution. 
 *
 *   This program is free software; you can redistribute it and/or modify 
 *   it under the terms of the GNU General Public License as published by 
 *   the Free Software Foundation; version 2 of the License. 
 *
 *  This program is distributed in the hope that it will be useful, 
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of 
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the 
 *  GNU General Public License for more details. 
 *
 *  You should have received a copy of the GNU General Public License 
 *  along with this program; if not, write to the Free Software 
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA 
 *
 */

#pragma once

// 다음 매크로는 필요한 최소 플랫폼을 정의합니다. 필요한 최소 플랫폼은
// 응용 프로그램을 실행하는 데 필요한 기능이 포함된 가장 빠른 버전의 Windows, Internet Explorer
// 등입니다. 이 매크로는 지정된 버전 이상의 플랫폼 버전에서 사용 가능한 모든 기능을 활성화해야
// 작동합니다.

// 아래 지정된 플랫폼에 우선하는 플랫폼을 대상으로 하는 경우 다음 정의를 수정하십시오.
// 다른 플랫폼에 사용되는 해당 값의 최신 정보는 MSDN을 참조하십시오.
#ifndef _WIN32_WINNT            // 필요한 최소 플랫폼을 Windows Vista로 지정합니다.
#define _WIN32_WINNT 0x0600     // 다른 버전의 Windows에 맞도록 적합한 값으로 변경해 주십시오.
#endif

