/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * tea.h - 
 */

#ifndef _TEA_H_
#define _TEA_H_

#ident "$Id$"

void uEncrypt (int len, char *src, char *trg);
void uDecrypt (int len, char *src, char *trg);

#endif /* _TEA_H_ */
