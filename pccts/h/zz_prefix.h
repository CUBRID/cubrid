/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * zz_prefix.h - Rename macros for prefixing external antlr references.
 */


#ifndef ZZ_PREFIX
#define ZZ_PREFIX _
#endif

#define ZZ_PREF(a) ZZ_GLUE(ZZ_PREFIX, a)
#define ZZ_GLUE(a,b) ZZ_GLUE2(a,b)
#define ZZ_GLUE2(a,b) a ## b

#define zzresynch ZZ_PREF(zzresynch)
#define zzedecode ZZ_PREF(zzedecode)
#define zzsyn ZZ_PREF(zzsyn)
#define zzadvance ZZ_PREF(zzadvance)
#define zzStackOvfMsg ZZ_PREF(zzStackOvfMsg)
#define zztokens ZZ_PREF(zztokens)
#define zzasp ZZ_PREF(zzasp)
#define zzbufsize ZZ_PREF(zzbufsize)
#define zzaStack ZZ_PREF(zzaStack)
#define zzempty_attr ZZ_PREF(zzempty_attr)
#define zzconstr_attr ZZ_PREF(zzconstr_attr)
#define zztokenLA ZZ_PREF(zztokenLA)
#define zztextLA ZZ_PREF(zztextLA)
#define zzlap ZZ_PREF(zzlap)
#define zztoken ZZ_PREF(zztoken)
#define zzast_sp ZZ_PREF(zzast_sp)
#define zzerr_in ZZ_PREF(zzerr_in)
#define zzlextext ZZ_PREF(zzlextext)
#define zzbegexpr ZZ_PREF(zzbegexpr)
#define zzendexpr ZZ_PREF(zzendexpr)
#define zzbufsize ZZ_PREF(zzbufsize)
#define zzbegcol ZZ_PREF(zzbegcol)
#define zzendcol ZZ_PREF(zzendcol)
#define zzline ZZ_PREF(zzline)
#define zzchar ZZ_PREF(zzchar)
#define zzerr ZZ_PREF(zzerr)
#define zzadvance ZZ_PREF(zzadvance)
#define zzskip ZZ_PREF(zzskip)
#define zzmore ZZ_PREF(zzmore)
#define zzmode ZZ_PREF(zzmode)
#define zzrdstream ZZ_PREF(zzrdstream)
#define zzclose_stream ZZ_PREF(zzclose_stream)
#define zzrdfunc ZZ_PREF(zzrdfunc)
#define zzgettok ZZ_PREF(zzgettok)
#define zzreplchar ZZ_PREF(zzreplchar)
#define zzreplstr ZZ_PREF(zzreplstr)
#define zzbufovfcnt  ZZ_PREF (zzbufovfcnt)
#define zzcharfull ZZ_PREF(zzcharfull)
#define zzerrstd ZZ_PREF(zzerrstd)
#define zzerraction ZZ_PREF(zzerraction)

#define zzauto ZZ_PREF(zzauto)
#define zzstream_in ZZ_PREF(zzstream_in)
#define zzfunc_in ZZ_PREF(zzfunc_in)
#define zzconsume ZZ_PREF(zzconsume)
#define zzconsume2 ZZ_PREF(zzconsume2)
#define zzmakeattr ZZ_PREF(zzmakeattr)
#define zzoverflow ZZ_PREF(zzoverflow)
