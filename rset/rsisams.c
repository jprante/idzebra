/* $Id: rsisams.c,v 1.11 2004-08-31 10:43:39 heikki Exp $
   Copyright (C) 1995,1996,1997,1998,1999,2000,2001,2002
   Index Data Aps

This file is part of the Zebra server.

Zebra is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 2, or (at your option) any later
version.

Zebra is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with Zebra; see the file LICENSE.zebra.  If not, write to the
Free Software Foundation, 59 Temple Place - Suite 330, Boston, MA
02111-1307, USA.
*/



#include <stdio.h>
#include <assert.h>
#include <zebrautl.h>
#include <rsisams.h>

static RSFD r_open (RSET ct, int flag);
static void r_close (RSFD rfd);
static void r_delete (RSET ct);
static void r_rewind (RSFD rfd);
static int r_read (RSFD rfd, void *buf);
static int r_write (RSFD rfd, const void *buf);
static void r_pos (RSFD rfd, double *current, double *total);

static const struct rset_control control = 
{
    "isams",
    r_delete,
    r_open,
    r_close,
    r_rewind,
    rset_default_forward,
    r_pos,
    r_read,
    r_write,
};

const struct rset_control *rset_kind_isams = &control;

struct rset_pp_info {
    ISAMS_PP pt;
};

struct rset_isams_info {
    ISAMS   is;
    ISAMS_P pos;
};


RSET rsisams_create( NMEM nmem, int key_size, 
            int (*cmp)(const void *p1, const void *p2),
            ISAMS is, ISAMS_P pos)
{
    RSET rnew=rset_create_base(&control, nmem);
    struct rset_isams_info *info;
    info = (struct rset_isams_info *) nmem_malloc(rnew->nmem,sizeof(*info));
    assert(key_size); /* FIXME - these belong to the general rset */
    assert(cmp);
    info->is=is;
    info->pos=pos;
    rnew->priv=info;
    return rnew;
}

static void r_delete (RSET ct)
{
    logf (LOG_DEBUG, "rsisams_delete");
    rset_delete(ct);
}


RSFD r_open (RSET ct, int flag)
{
    struct rset_isams_info *info = (struct rset_isams_info *) ct->priv;
    RSFD rfd;
    struct rset_pp_info *ptinfo;

    logf (LOG_DEBUG, "risams_open");
    if (flag & RSETF_WRITE)
    {
        logf (LOG_FATAL, "ISAMS set type is read-only");
        return NULL;
    }
    rfd=rfd_create_base(ct);
    if (rfd->priv)
        ptinfo=(struct rset_pp_info *)(rfd->priv);
    else {
        ptinfo = (struct rset_pp_info *) nmem_malloc(ct->nmem,sizeof(*ptinfo));
        ptinfo->pt = isams_pp_open (info->is, info->pos);
        rfd->priv=ptinfo;
    }
    return rfd;
}

static void r_close (RSFD rfd)
{
    struct rset_pp_info *ptinfo=(struct rset_pp_info *)(rfd->priv);

    isams_pp_close (ptinfo->pt);
    rfd_delete_base(rfd);
}

static void r_rewind (RSFD rfd)
{   
    logf (LOG_DEBUG, "rsisams_rewind");
    abort ();
}


static int r_read (RSFD rfd, void *buf)
{
    struct rset_pp_info *ptinfo=(struct rset_pp_info *)(rfd->priv);
    return isams_pp_read(ptinfo->pt, buf);
}

static int r_write (RSFD rfd, const void *buf)
{
    logf (LOG_FATAL, "ISAMS set type is read-only");
    return -1;
}

static void r_pos (RSFD rfd, double *current, double *total)
{
    *current=-1;  /* sorry, not implemented yet */
    *total=-1;
}
