/*
 * Copyright (C) 1994-1997, Index Data I/S 
 * All rights reserved.
 * Sebastian Hammer, Adam Dickmeiss
 *
 * $Log: zinfo.c,v $
 * Revision 1.4  1997-09-25 14:57:08  adam
 * Added string.h.
 *
 * Revision 1.3  1996/05/22 08:21:59  adam
 * Added public ZebDatabaseInfo structure.
 *
 * Revision 1.2  1996/05/14 06:16:41  adam
 * Compact use/set bytes used in search service.
 *
 * Revision 1.1  1996/05/13 14:23:07  adam
 * Work on compaction of set/use bytes in dictionary.
 *
 */

#include <stdlib.h>
#include <assert.h>
#include <string.h>

#include "zinfo.h"

struct zebSUInfo {
    int set;
    int use;
    int ordinal;
};

struct zebSUInfoB {
    struct zebSUInfo info;
    struct zebSUInfoB *next;
};

struct zebDatabaseInfoB {
    struct zebSUInfoB *SUInfo;
    char *databaseName;
    int sysno;
    int readFlag;
    int dirty;
    struct zebDatabaseInfo info;
    struct zebDatabaseInfoB *next;
};

struct zebTargetInfo {
    int  dictNum;
    int  dirty;
    Records records;
    struct zebDatabaseInfoB *databaseInfo;
    struct zebDatabaseInfoB *curDatabaseInfo;
};

void zebTargetInfo_close (ZebTargetInfo *zti, int writeFlag)
{
    struct zebDatabaseInfoB *zdi, *zdi1;
    
    if (writeFlag)
    {
        char p0[4096], *p = p0;

        memcpy (p, &zti->dictNum, sizeof(zti->dictNum));
        p += sizeof(zti->dictNum);
        for (zdi = zti->databaseInfo; zdi; zdi=zdi->next)
        {
            if (zdi->dirty)
            {
                char q0[4096], *q = q0;
                struct zebSUInfoB *zsui;
                Record drec;
                int no = 0;
                
                if (zdi->sysno)
                    drec = rec_get (zti->records, zdi->sysno);
                else
                {
                    drec = rec_new (zti->records);
                    zdi->sysno = drec->sysno;
                }
                assert (drec);
                for (zsui = zdi->SUInfo; zsui; zsui=zsui->next)
                    no++;
		memcpy (q, &zdi->info, sizeof(zdi->info));
                q += sizeof(zdi->info);
                memcpy (q, &no, sizeof(no));
                q += sizeof(no);
                for (zsui = zdi->SUInfo; zsui; zsui=zsui->next)
                {
                    memcpy (q, &zsui->info, sizeof(zsui->info));
                    q += sizeof(zsui->info);
                }
                xfree (drec->info[0]);
                drec->size[0] = q-q0;
                drec->info[0] = xmalloc (drec->size[0]);
                memcpy (drec->info[0], q0, drec->size[0]);
                rec_put (zti->records, &drec);
            }
            strcpy (p, zdi->databaseName);
            p += strlen(p)+1;
            memcpy (p, &zdi->sysno, sizeof(zdi->sysno));
            p += sizeof(zdi->sysno);
        }
        *p++ = '\0';
        if (zti->dirty)
        {
            Record grec = rec_get (zti->records, 1);

            assert (grec);
            xfree (grec->info[0]);
            grec->size[0] = p-p0;
            grec->info[0] = xmalloc (grec->size[0]);
            memcpy (grec->info[0], p0, grec->size[0]);
            rec_put (zti->records, &grec);
        }
    }
    for (zdi = zti->databaseInfo; zdi; zdi = zdi1)
    {
        struct zebSUInfoB *zsui, *zsui1;

        zdi1 = zdi->next;
        for (zsui = zdi->SUInfo; zsui; zsui = zsui1)
        {
            zsui1 = zsui->next;
            xfree (zsui);
        }
        xfree (zdi->databaseName);
        xfree (zdi);
    }
    xfree (zti);
}

ZebTargetInfo *zebTargetInfo_open (Records records, int writeFlag)
{
    Record rec;
    ZebTargetInfo *zti;
    struct zebDatabaseInfoB **zdi;

    zti = xmalloc (sizeof(*zti));
    zti->dirty = 0;
    zti->curDatabaseInfo = NULL;
    zti->records = records;

    zdi = &zti->databaseInfo;
    
    rec = rec_get (records, 1);
    if (rec)
    {
        const char *p;

        p = rec->info[0];

        memcpy (&zti->dictNum, p, sizeof(zti->dictNum));
        p += sizeof(zti->dictNum);
        while (*p)
        {
            *zdi = xmalloc (sizeof(**zdi));
            (*zdi)->SUInfo = NULL;
            (*zdi)->databaseName = xstrdup (p);
            p += strlen(p)+1;
            memcpy (&(*zdi)->sysno, p, sizeof((*zdi)->sysno));
            p += sizeof((*zdi)->sysno);
            (*zdi)->readFlag = 1;
            (*zdi)->dirty = 0;
            zdi = &(*zdi)->next;
        }
        assert (p - rec->info[0] == rec->size[0]-1);
    }
    else
    {
        zti->dictNum = 1;
        if (writeFlag)
        {
            rec = rec_new (records);
            rec->info[0] = xmalloc (1+sizeof(zti->dictNum));
            memcpy (rec->info[0], &zti->dictNum, sizeof(zti->dictNum));
            rec->info[0][sizeof(zti->dictNum)] = '\0';
            rec->size[0] = sizeof(zti->dictNum)+1;
            rec_put (records, &rec);
        }
    }
    *zdi = NULL;
    rec_rm (&rec);
    return zti;
}

static void zebTargetInfo_readDatabase (ZebTargetInfo *zti,
                                        struct zebDatabaseInfoB *zdi)
{
    const char *p;
    struct zebSUInfoB **zsuip = &zdi->SUInfo;
    int i, no;
    Record rec;

    rec = rec_get (zti->records, zdi->sysno);
    assert (rec);
    p = rec->info[0];
    memcpy (&zdi->info, p, sizeof(zdi->info));
    p += sizeof(zdi->info);
    memcpy (&no, p, sizeof(no));
    p += sizeof(no);
    for (i = 0; i<no; i++)
    {
        *zsuip = xmalloc (sizeof(**zsuip));
        memcpy (&(*zsuip)->info, p, sizeof((*zsuip)->info));
        p += sizeof((*zsuip)->info);
        zsuip = &(*zsuip)->next;
    }
    *zsuip = NULL;
    zdi->readFlag = 0;
    rec_rm (&rec);
}

int zebTargetInfo_curDatabase (ZebTargetInfo *zti, const char *database)
{
    struct zebDatabaseInfoB *zdi;
    
    assert (zti);
    if (zti->curDatabaseInfo &&
        !strcmp (zti->curDatabaseInfo->databaseName, database))
        return 0;
    for (zdi = zti->databaseInfo; zdi; zdi=zdi->next)
    {
        if (!strcmp (zdi->databaseName, database))
            break;
    }
    if (!zdi)
        return -1;
    if (zdi->readFlag)
        zebTargetInfo_readDatabase (zti, zdi);
    zti->curDatabaseInfo = zdi;
    return 0;
}

int zebTargetInfo_newDatabase (ZebTargetInfo *zti, const char *database)
{
    struct zebDatabaseInfoB *zdi;

    assert (zti);
    for (zdi = zti->databaseInfo; zdi; zdi=zdi->next)
    {
        if (!strcmp (zdi->databaseName, database))
            break;
    }
    if (zdi)
        return -1;
    zdi = xmalloc (sizeof(*zdi));
    zdi->next = zti->databaseInfo;
    zti->databaseInfo = zdi;
    zdi->sysno = 0;
    zdi->readFlag = 0;
    zdi->databaseName = xstrdup (database);
    zdi->SUInfo = NULL;
    zdi->dirty = 1;
    zti->dirty = 1;
    zti->curDatabaseInfo = zdi;
    return 0;
}

int zebTargetInfo_lookupSU (ZebTargetInfo *zti, int set, int use)
{
    struct zebSUInfoB *zsui;

    assert (zti->curDatabaseInfo);
    for (zsui = zti->curDatabaseInfo->SUInfo; zsui; zsui=zsui->next)
        if (zsui->info.use == use && zsui->info.set == set)
            return zsui->info.ordinal;
    return -1;
}

int zebTargetInfo_addSU (ZebTargetInfo *zti, int set, int use)
{
    struct zebSUInfoB *zsui;

    assert (zti->curDatabaseInfo);
    for (zsui = zti->curDatabaseInfo->SUInfo; zsui; zsui=zsui->next)
        if (zsui->info.use == use && zsui->info.set == set)
            return -1;
    zsui = xmalloc (sizeof(*zsui));
    zsui->next = zti->curDatabaseInfo->SUInfo;
    zti->curDatabaseInfo->SUInfo = zsui;
    zti->curDatabaseInfo->dirty = 1;
    zti->dirty = 1;
    zsui->info.set = set;
    zsui->info.use = use;
    zsui->info.ordinal = (zti->dictNum)++;
    return zsui->info.ordinal;
}

ZebDatabaseInfo *zebTargetInfo_getDB (ZebTargetInfo *zti)
{
    assert (zti->curDatabaseInfo);

    return &zti->curDatabaseInfo->info;
}

void zebTargetInfo_setDB (ZebTargetInfo *zti, ZebDatabaseInfo *zdi)
{
    assert (zti->curDatabaseInfo);

    zti->curDatabaseInfo->dirty = 1;
    memcpy (&zti->curDatabaseInfo->info, zdi, sizeof(*zdi));
}
