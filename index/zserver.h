/*
 * Copyright (C) 1994-1995, Index Data I/S 
 * All rights reserved.
 * Sebastian Hammer, Adam Dickmeiss
 *
 * $Log: zserver.h,v $
 * Revision 1.11  1995-10-17 18:02:12  adam
 * New feature: databases. Implemented as prefix to words in dictionary.
 *
 * Revision 1.10  1995/10/09  16:18:38  adam
 * Function dict_lookup_grep got extra client data parameter.
 *
 * Revision 1.9  1995/10/06  14:38:01  adam
 * New result set method: r_score.
 * Local no (sysno) and score is transferred to retrieveCtrl.
 *
 * Revision 1.8  1995/10/06  13:52:06  adam
 * Bug fixes. Handler may abort further scanning.
 *
 * Revision 1.7  1995/10/06  10:43:57  adam
 * Scan added. 'occurrences' in scan entries not set yet.
 *
 * Revision 1.6  1995/09/28  09:19:48  adam
 * xfree/xmalloc used everywhere.
 * Extract/retrieve method seems to work for text records.
 *
 * Revision 1.5  1995/09/27  16:17:32  adam
 * More work on retrieve.
 *
 * Revision 1.4  1995/09/14  11:53:28  adam
 * First work on regular expressions/truncations.
 *
 * Revision 1.3  1995/09/08  08:53:23  adam
 * Record buffer maintained in server_info.
 *
 * Revision 1.2  1995/09/06  16:11:19  adam
 * Option: only one word key per file.
 *
 * Revision 1.1  1995/09/05  15:28:40  adam
 * More work on search engine.
 *
 */

#include "index.h"
#include <backend.h>
#include <rset.h>

typedef struct {
    int sysno;
    int score;
} ZServerSetSysno;

typedef struct ZServerSet_ {
    char *name;
    RSET rset;
    int size;
    struct ZServerSet_ *next;
} ZServerSet;
   
typedef struct {
    ZServerSet *sets;
    Dict wordDict;
    ISAM wordIsam;
    Dict fileDict;
    int sys_idx_fd;
    int errCode;
    char *errString;
    ODR odr;
} ZServerInfo;

int rpn_search (ZServerInfo *zi, 
                Z_RPNQuery *rpn, int num_bases, char **basenames, 
                const char *setname, int *hits);

int rpn_scan (ZServerInfo *zi, ODR odr, Z_AttributesPlusTerm *zapt,
              int num_bases, char **basenames,
              int *position, int *num_entries, struct scan_entry **list,
              int *status);

ZServerSet *resultSetAdd (ZServerInfo *zi, const char *name,
                          int ov, RSET rset);
ZServerSet *resultSetGet (ZServerInfo *zi, const char *name);
ZServerSetSysno *resultSetSysnoGet (ZServerInfo *zi, const char *name,
                                    int num, int *positions);
void resultSetSysnoDel (ZServerInfo *zi, ZServerSetSysno *records, int num);
