/*
 * Copyright (C) 1994-1996, Index Data I/S 
 * All rights reserved.
 * Sebastian Hammer, Adam Dickmeiss
 *
 * $Log: zinfo.h,v $
 * Revision 1.1  1996-05-13 14:23:07  adam
 * Work on compaction of set/use bytes in dictionary.
 *
 */

#include "recindex.h"

typedef struct zebTargetInfo ZebTargetInfo;

ZebTargetInfo *zebTargetInfo_open (Records records, int writeFlag);
void zebTargetInfo_close (ZebTargetInfo *zti, int writeFlag);
int zebTargetInfo_curDatabase (ZebTargetInfo *zti, const char *database);
int zebTargetInfo_newDatabase (ZebTargetInfo *zti, const char *database);
int zebTargetInfo_lookupSU (ZebTargetInfo *zti, int set, int use);
int zebTargetInfo_addSU (ZebTargetInfo *zti, int set, int use);
