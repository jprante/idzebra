/* $Id: rset.c,v 1.19 2004-01-22 11:27:22 adam Exp $
   Copyright (C) 1995,1996,1997,1998,1999,2000,2001,2002,2003,2004
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
#include <string.h>
#include <zebrautl.h>

#include <rset.h>

RSET rset_create(const struct rset_control *sel, void *parms)
{
    RSET rnew;
    int i;

    logf (LOG_DEBUG, "rs_create(%s)", sel->desc);
    rnew = (RSET) xmalloc(sizeof(*rnew));
    rnew->control = sel;
    rnew->flags = 0;
    rnew->count = 1;
    rnew->rset_terms = NULL;
    rnew->no_rset_terms = 0;
    rnew->buf = (*sel->f_create)(rnew, sel, parms);
    logf (LOG_DEBUG, "no_rset_terms: %d", rnew->no_rset_terms);
    for (i = 0; i<rnew->no_rset_terms; i++)
	logf (LOG_DEBUG, " %s", rnew->rset_terms[i]->name);
    return rnew;
}

void rset_delete (RSET rs)
{
    (rs->count)--;
    if (!rs->count)
    {
	(*rs->control->f_delete)(rs);
	xfree(rs);
    }
}

RSET rset_dup (RSET rs)
{
    (rs->count)++;
    return rs;
}

int rset_default_forward(RSFD rfd, void *buf, const void *untilbuf)
{
    logf (LOG_FATAL, "rset_default-forward not yet implemented");
    return 0;
}

RSET_TERM *rset_terms(RSET rs, int *no)
{
    *no = rs->no_rset_terms;
    return rs->rset_terms;
}

RSET_TERM rset_term_create (const char *name, int length, const char *flags,
                            int type)
{
    RSET_TERM t = (RSET_TERM) xmalloc (sizeof(*t));
    if (!name)
	t->name = NULL;
    else if (length == -1)
	t->name = xstrdup (name);
    else
    {
	t->name = (char*) xmalloc (length+1);
	memcpy (t->name, name, length);
	t->name[length] = '\0';
    }
    if (!flags)
	t->flags = NULL;
    else
	t->flags = xstrdup (flags);
    t->nn = -1;
    t->count = 0;
    t->type = type;
    return t;
}

void rset_term_destroy (RSET_TERM t)
{
    xfree (t->name);
    xfree (t->flags);
    xfree (t);
}

RSET_TERM rset_term_dup (RSET_TERM t)
{
    RSET_TERM nt = (RSET_TERM) xmalloc (sizeof(*nt));
    if (t->name)
	nt->name = xstrdup (t->name);
    else
	nt->name = NULL;
    if (t->flags)
	nt->flags = xstrdup (t->flags);
    else
	nt->flags = NULL;
    nt->nn = t->nn;
    return nt;
}
