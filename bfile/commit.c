/*
 * Copyright (C) 1995, Index Data I/S 
 * All rights reserved.
 * Sebastian Hammer, Adam Dickmeiss
 *
 * $Log: commit.c,v $
 * Revision 1.2  1995-12-01 11:37:24  adam
 * Cached/commit files implemented as meta-files.
 *
 * Revision 1.1  1995/11/30  08:33:13  adam
 * Started work on commit facility.
 *
 */

#include <assert.h>
#include <stdlib.h>

#include <alexutil.h>
#include <mfile.h>
#include "cfile.h"

void cf_commit (CFile cf)
{
    int i, r, bucket_no;
    int hash_bytes;
    struct CFile_ph_bucket *p;

    if (cf->bucket_in_memory)
    {
        logf (LOG_FATAL, "Cannot commit potential dirty cache");
        exit (1);
    }
    p = xmalloc (sizeof(*p));
    hash_bytes = cf->head.hash_size * sizeof(int);
    bucket_no = (hash_bytes+sizeof(cf->head))/HASH_BSIZE + 2;
    for (; bucket_no < cf->head.next_bucket; bucket_no++)
    {
        if (!mf_read (cf->hash_mf, bucket_no, 0, 0, p))
        {
            logf (LOG_FATAL, "read commit hash");
            exit (1);
        }
        for (i = 0; i<HASH_BUCKET && p->vno[i]; i++)
        {
            if (!mf_read (cf->block_mf, p->vno[i], 0, 0, cf->iobuf))
            {
                logf (LOG_FATAL, "read commit block");
                exit (1);
            }
            mf_write (cf->rmf, p->no[i], 0, 0, cf->iobuf);
        }
    }
    xfree (p);
}

