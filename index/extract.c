/*
 * Copyright (C) 1994-1995, Index Data I/S 
 * All rights reserved.
 * Sebastian Hammer, Adam Dickmeiss
 *
 * $Log: extract.c,v $
 * Revision 1.12  1995-09-28 14:22:56  adam
 * Sort uses smaller temporary files.
 *
 * Revision 1.11  1995/09/28  12:10:31  adam
 * Bug fixes. Field prefix used in queries.
 *
 * Revision 1.10  1995/09/28  09:19:41  adam
 * xfree/xmalloc used everywhere.
 * Extract/retrieve method seems to work for text records.
 *
 * Revision 1.9  1995/09/27  12:22:28  adam
 * More work on extract in record control.
 * Field name is not in isam keys but in prefix in dictionary words.
 *
 * Revision 1.8  1995/09/14  07:48:22  adam
 * Record control management.
 *
 * Revision 1.7  1995/09/11  13:09:32  adam
 * More work on relevance feedback.
 *
 * Revision 1.6  1995/09/08  14:52:27  adam
 * Minor changes. Dictionary is lower case now.
 *
 * Revision 1.5  1995/09/06  16:11:16  adam
 * Option: only one word key per file.
 *
 * Revision 1.4  1995/09/05  15:28:39  adam
 * More work on search engine.
 *
 * Revision 1.3  1995/09/04  12:33:41  adam
 * Various cleanup. YAZ util used instead.
 *
 * Revision 1.2  1995/09/04  09:10:34  adam
 * More work on index add/del/update.
 * Merge sort implemented.
 * Initial work on z39 server.
 *
 * Revision 1.1  1995/09/01  14:06:35  adam
 * Split of work into more files.
 *
 */
#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>

#include <alexutil.h>
#include <recctrl.h>
#include "index.h"

static Dict file_idx;
static SYSNO sysno_next;
static int sys_idx_fd = -1;

static int key_cmd;
static int key_sysno;
static char **key_buf;
static size_t ptr_top;
static size_t ptr_i;
static size_t kused;
static int key_file_no;

static int sort_compare (const void *p1, const void *p2)
{
    int r;
    size_t l;
    char *cp1 = *(char**) p1;
    char *cp2 = *(char**) p2;

    if ((r = strcmp (cp1, cp2)))
        return r;
    l = strlen(cp1); 
    if ((r = key_compare (cp1+l, cp2+l)))
        return r;
    return cp1[l+sizeof(struct it_key)] -
           cp2[l+sizeof(struct it_key)];
}

void key_open (int mem)
{
    void *file_key;

    if (mem < 50000)
        mem = 50000;
    key_buf = xmalloc (mem);
    ptr_top = mem/sizeof(char*);
    ptr_i = 0;
    kused = 0;
    key_file_no = 0;

    if (!(file_idx = dict_open (FNAME_FILE_DICT, 40, 1)))
    {
        logf (LOG_FATAL, "dict_open fail of %s", "fileidx");
        exit (1);
    }
    file_key = dict_lookup (file_idx, ".");
    if (file_key)
        memcpy (&sysno_next, (char*)file_key+1, sizeof(sysno_next));
    else
        sysno_next = 1;
    if ((sys_idx_fd = open (FNAME_SYS_IDX, O_RDWR|O_CREAT, 0666)) == -1)
    {
        logf (LOG_FATAL|LOG_ERRNO, "open %s", FNAME_SYS_IDX);
        exit (1);
    }
}
    
void key_flush (void)
{
    FILE *outf;
    char out_fname[200];
    char *prevcp, *cp;
    
    if (ptr_i <= 0)
        return;

    key_file_no++;
    logf (LOG_LOG, "sorting section %d", key_file_no);
    qsort (key_buf + ptr_top-ptr_i, ptr_i, sizeof(char*), sort_compare);
    sprintf (out_fname, TEMP_FNAME, key_file_no);


    if (!(outf = fopen (out_fname, "w")))
    {
        logf (LOG_FATAL|LOG_ERRNO, "fopen (4) %s", out_fname);
        exit (1);
    }
    logf (LOG_LOG, "writing section %d", key_file_no);
    prevcp = cp = key_buf[ptr_top-ptr_i];
    
    if (fwrite (cp, strlen (cp)+2+sizeof(struct it_key), 1, outf) != 1)
    {
        logf (LOG_FATAL|LOG_ERRNO, "fwrite %s", out_fname);
        exit (1);
    }
    while (--ptr_i > 0)
    {
        cp = key_buf[ptr_top-ptr_i];
        if (strcmp (cp, prevcp))
        {
            if (fwrite (cp, strlen (cp)+2+sizeof(struct it_key), 1,
                        outf) != 1)
            {
                logf (LOG_FATAL|LOG_ERRNO, "fwrite %s", out_fname);
                exit (1);
            }
            prevcp = cp;
        }
        else
        {
            cp = strlen (cp) + cp;
            if (fwrite (cp, 2+sizeof(struct it_key), 1, outf) != 1)
            {
                logf (LOG_FATAL|LOG_ERRNO, "fwrite %s", out_fname);
                exit (1);
            }
        }
    }
    if (fclose (outf))
    {
        logf (LOG_FATAL|LOG_ERRNO, "fclose %s", out_fname);
        exit (1);
    }
    logf (LOG_LOG, "finished section %d", key_file_no);
    ptr_i = 0;
    kused = 0;
}

int key_close (void)
{
    key_flush ();
    xfree (key_buf);
    close (sys_idx_fd);
    dict_insert (file_idx, ".", sizeof(sysno_next), &sysno_next);
    dict_close (file_idx);
    return key_file_no;
}

static void wordInit (RecWord *p)
{
    p->attrSet = 1;
    p->attrUse = 1016;
    p->which = Word_String;
}

static void wordAdd (const RecWord *p)
{
    struct it_key key;
    size_t i;

    if (kused + 1024 > (ptr_top-ptr_i)*sizeof(char*))
        key_flush ();
    ++ptr_i;
    key_buf[ptr_top-ptr_i] = (char*)key_buf + kused;
    kused += index_word_prefix ((char*)key_buf + kused,
                                p->attrSet, p->attrUse);
    switch (p->which)
    {
    case Word_String:
        for (i = 0; p->u.string[i]; i++)
            ((char*)key_buf) [kused++] = index_char_cvt (p->u.string[i]);
        ((char*)key_buf) [kused++] = '\0';
        break;
    default:
        return ;
    }
    key.sysno = key_sysno;
    key.seqno = p->seqno;
    memcpy ((char*)key_buf + kused, &key, sizeof(key));
    kused += sizeof(key);

    ((char*) key_buf)[kused++] = ((key_cmd == 'a') ? 1 : 0);
}

void file_extract (int cmd, const char *fname, const char *kname)
{
    int i;
    char ext[128];
    SYSNO sysno;
    char ext_res[128];
    const char *file_type;
    void *file_info;
    FILE *inf;
    struct recExtractCtrl extractCtrl;
    RecType rt;

    logf (LOG_DEBUG, "%c %s k=%s", cmd, fname, kname);
    for (i = strlen(fname); --i >= 0; )
        if (fname[i] == '/')
        {
            strcpy (ext, "");
            break;
        }
        else if (fname[i] == '.')
        {
            strcpy (ext, fname+i+1);
            break;
        }
    sprintf (ext_res, "fileExtension.%s", ext);
    if (!(file_type = res_get (common_resource, ext_res)))
        return;
    if (!(rt = recType_byName (file_type)))
        return;
    file_info = dict_lookup (file_idx, kname);
    if (!file_info)
    {
        sysno = sysno_next++;
        dict_insert (file_idx, kname, sizeof(sysno), &sysno);
        lseek (sys_idx_fd, sysno * SYS_IDX_ENTRY_LEN, SEEK_SET);
        write (sys_idx_fd, file_type, strlen (file_type)+1);
        write (sys_idx_fd, kname, strlen(kname)+1);
    }
    else
        memcpy (&sysno, (char*) file_info+1, sizeof(sysno));

    if (!(inf = fopen (fname, "r")))
    {
        logf (LOG_WARN|LOG_ERRNO, "open %s", fname);
        return;
    }
    extractCtrl.inf = inf;
    extractCtrl.subType = "";
    extractCtrl.init = wordInit;
    extractCtrl.add = wordAdd;
    key_sysno = sysno;
    key_cmd = cmd;
    (*rt->extract)(&extractCtrl);
    fclose (inf);
}
