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

/*
 * zzpref.h - Rename macros for prefixing external antlr references.
 */

#ifndef ZZ_PREFIX
#define ZZ_PREFIX _
#endif

#define ZZ_PREF(a) ZZ_GLUE(ZZ_PREFIX, a)
#define ZZ_GLUE(a,b) ZZ_GLUE2(a,b)
#ifdef VMS
#define ZZ_GLUE2(a,b) a/**/b
#else
#define ZZ_GLUE2(a,b) a ## b
#endif

#define zzresynch ZZ_PREF(_zzresynch)
#define zzedecode ZZ_PREF(_zzedecode)
#define zzsyn ZZ_PREF(_zzsyn)
#define zzadvance ZZ_PREF(_zzadvance)
#define zzStackOvfMsg ZZ_PREF(_zzStackOvfMsg)
#define zztokens ZZ_PREF(_zztokens)
#define zzasp ZZ_PREF(_zzasp)
#define zzbufsize ZZ_PREF(_zzbufsize)
#define zzaStack ZZ_PREF(_zzaStack)
#define zzempty_attr ZZ_PREF(_zzempty_attr)
#define zzconstr_attr ZZ_PREF(_zzconstr_attr)
#define zztokenLA ZZ_PREF(_zztokenLA)
#define zztextLA ZZ_PREF(_zztextLA)
#define zzlap ZZ_PREF(_zzlap)
#define zztoken ZZ_PREF(_zztoken)
#define zzast_sp ZZ_PREF(_zzast_sp)
#define zzerr_in ZZ_PREF(_zzerr_in)
#define zzlextext ZZ_PREF(_zzlextext)
#define zzlextextend ZZ_PREF(_zzlextextend)
#define zzbegexpr ZZ_PREF(_zzbegexpr)
#define zzendexpr ZZ_PREF(_zzendexpr)
#define zzbufsize ZZ_PREF(_zzbufsize)
#define zzbegcol ZZ_PREF(_zzbegcol)
#define zzendcol ZZ_PREF(_zzendcol)
#define zzline ZZ_PREF(_zzline)
#define zzchar ZZ_PREF(_zzchar)
#define zzerr ZZ_PREF(_zzerr)
#define zzadvance ZZ_PREF(_zzadvance)
#define zzskip ZZ_PREF(_zzskip)
#define zzmore ZZ_PREF(_zzmore)
#define zzmode ZZ_PREF(_zzmode)
#define zzrdstream ZZ_PREF(_zzrdstream)
#define zzclose_stream ZZ_PREF(_zzclose_stream)
#define zzrdfunc ZZ_PREF(_zzrdfunc)
#define zzgettok ZZ_PREF(_zzgettok)
#define zzreplchar ZZ_PREF(_zzreplchar)
#define zzreplstr ZZ_PREF(_zzreplstr)
#define zzbufovfcnt ZZ_PREF (_zzbufovfcnt)
#define zzcharfull ZZ_PREF(_zzcharfull)
#define zzerrstd ZZ_PREF(_zzerrstd)
#define zzerraction ZZ_PREF(_zzerraction)

#define zzauto ZZ_PREF(_zzauto)
#define zzstream_in ZZ_PREF(_zzstream_in)
#define zzfunc_in ZZ_PREF(_zzfunc_in)
#define zzconsume ZZ_PREF(_zzconsume)
#define zzconsume2 ZZ_PREF(_zzconsume2)
#define zzmakeattr ZZ_PREF(_zzmakeattr)
#define zzoverflow ZZ_PREF(_zzoverflow)
#define zzTRACE ZZ_PREF(_zzTRACE)
#define zzlineLA ZZ_PREF(_zzlineLA)
#define zzcolumnLA ZZ_PREF(_zzcolumnLA)
#define zztextend ZZ_PREF(_zztextend)
