/*
 * Copyright (C) 1994, Index Data I/S 
 * All rights reserved.
 * Sebastian Hammer, Adam Dickmeiss
 *
 * $Log: grepper.c,v $
 * Revision 1.6  1996-01-08 09:09:20  adam
 * Function dfa_parse got 'const' string argument.
 * New functions to define char mappings made public.
 *
 * Revision 1.5  1995/09/04  12:33:26  adam
 * Various cleanup. YAZ util used instead.
 *
 * Revision 1.4  1995/01/24  16:00:21  adam
 * Added -ansi to CFLAGS.
 * Some changes to the dfa module.
 *
 * Revision 1.3  1994/10/04  17:46:43  adam
 * Function options now returns arg with error option.
 *
 * Revision 1.2  1994/10/03  17:22:18  adam
 * Optimization of grepper.
 *
 * Revision 1.1  1994/09/27  16:31:18  adam
 * First version of grepper: grep with error correction.
 *
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <assert.h>

#include <alexutil.h>
#include <dfa.h>
#include "imalloc.h"

char *prog;
static int show_line = 0;

typedef unsigned MatchWord;
#define WORD_BITS 32

typedef struct {
    int n;           /* no of MatchWord needed */
    int range;       /* max no. of errors */
    MatchWord *Sc;   /* Mask Sc */
} MatchContext;

#define INFBUF_SIZE 16384

#define INLINE 

static INLINE void set_bit (MatchContext *mc, MatchWord *m, int ch, int state)
{
    int off = state & (WORD_BITS-1);
    int wno = state / WORD_BITS;

    m[mc->n * ch + wno] |= 1<<off;
}

static INLINE void reset_bit (MatchContext *mc, MatchWord *m, int ch,
                              int state)
{
    int off = state & (WORD_BITS-1);
    int wno = state / WORD_BITS;

    m[mc->n * ch + wno] &= ~(1<<off);
}

static INLINE MatchWord get_bit (MatchContext *mc, MatchWord *m, int ch,
                                 int state)
{
    int off = state & (WORD_BITS-1);
    int wno = state / WORD_BITS;

    return m[mc->n * ch + wno] & (1<<off);
}

static MatchContext *mk_MatchContext (struct DFA *dfa, int range)
{
    MatchContext *mc = imalloc (sizeof(*mc));
    int i;

    mc->n = (dfa->no_states+WORD_BITS) / WORD_BITS;
    mc->range = range;
    mc->Sc = icalloc (sizeof(*mc->Sc) * 256 * mc->n);
    
    for (i=0; i<dfa->no_states; i++)
    {
        int j;
        struct DFA_state *state = dfa->states[i];

        for (j=0; j<state->tran_no; j++)
        {
            int ch;
            int ch0 = state->trans[j].ch[0];
            int ch1 = state->trans[j].ch[1];
            assert (ch0 >= 0 && ch1 >= 0);
            
            for (ch = ch0; ch <= ch1; ch++)
                set_bit (mc, mc->Sc, ch, i);
        }
    }
    return mc;
}


static void mask_shift (MatchContext *mc, MatchWord *Rdst, MatchWord *Rsrc,
                        struct DFA *dfa, int ch)
{
    int j, s = 0;
    MatchWord *Rsrc_p = Rsrc, mask;

    Rdst[0] = 1;
    for (j = 1; j<mc->n; j++)
        Rdst[j] = 0;
    while (1)
    {
        mask = *Rsrc_p++;
        for (j = 0; j<WORD_BITS/4; j++)
        {
            if (mask & 15)
            {
                if (mask & 1)
                {
                    struct DFA_state *state = dfa->states[s];
                    int i = state->tran_no;
                    while (--i >= 0)
                        if (ch >= state->trans[i].ch[0] &&
                            ch <= state->trans[i].ch[1])
                            set_bit (mc, Rdst, 0, state->trans[i].to);
                }
                if (mask & 2)
                {
                    struct DFA_state *state = dfa->states[s+1];
                    int i = state->tran_no;
                    while (--i >= 0)
                        if (ch >= state->trans[i].ch[0] &&
                            ch <= state->trans[i].ch[1])
                            set_bit (mc, Rdst, 0, state->trans[i].to);
                }
                if (mask & 4)
                {
                    struct DFA_state *state = dfa->states[s+2];
                    int i = state->tran_no;
                    while (--i >= 0)
                        if (ch >= state->trans[i].ch[0] &&
                            ch <= state->trans[i].ch[1])
                            set_bit (mc, Rdst, 0, state->trans[i].to);
                }
                if (mask & 8)
                {
                    struct DFA_state *state = dfa->states[s+3];
                    int i = state->tran_no;
                    while (--i >= 0)
                        if (ch >= state->trans[i].ch[0] &&
                            ch <= state->trans[i].ch[1])
                            set_bit (mc, Rdst, 0, state->trans[i].to);
                }
            }
            s += 4;
            if (s >= dfa->no_states)
                return;
            mask >>= 4;
        }
    }
}

static void shift (MatchContext *mc, MatchWord *Rdst, MatchWord *Rsrc,
                   struct DFA *dfa)
{
    int j, s = 0;
    MatchWord *Rsrc_p = Rsrc, mask;
    for (j = 0; j<mc->n; j++)
        Rdst[j] = 0;
    while (1)
    {
        mask = *Rsrc_p++;
        for (j = 0; j<WORD_BITS/4; j++)
        {
            if (mask & 15)
            {
                if (mask & 1)
                {
                    struct DFA_state *state = dfa->states[s];
                    int i = state->tran_no;
                    while (--i >= 0)
                        set_bit (mc, Rdst, 0, state->trans[i].to);
                }
                if (mask & 2)
                {
                    struct DFA_state *state = dfa->states[s+1];
                    int i = state->tran_no;
                    while (--i >= 0)
                        set_bit (mc, Rdst, 0, state->trans[i].to);
                }
                if (mask & 4)
                {
                    struct DFA_state *state = dfa->states[s+2];
                    int i = state->tran_no;
                    while (--i >= 0)
                        set_bit (mc, Rdst, 0, state->trans[i].to);
                }
                if (mask & 8)
                {
                    struct DFA_state *state = dfa->states[s+3];
                    int i = state->tran_no;
                    while (--i >= 0)
                        set_bit (mc, Rdst, 0, state->trans[i].to);
                }
            }
            s += 4;
            if (s >= dfa->no_states)
                return;
            mask >>= 4;
        }
    }
}

static void or (MatchContext *mc, MatchWord *Rdst,
                MatchWord *Rsrc1, MatchWord *Rsrc2)
{
    int i;
    for (i = 0; i<mc->n; i++)
        Rdst[i] = Rsrc1[i] | Rsrc2[i];
}


static int go (MatchContext *mc, struct DFA *dfa, FILE *inf)
{
    MatchWord *Rj, *Rj1, *Rj_a, *Rj_b, *Rj_c;
    int s, d, ch;
    int lineno = 1;
    char *infbuf;
    int inf_ptr = 1;
    int no_match = 0;

    infbuf = imalloc (INFBUF_SIZE);
    infbuf[0] = '\n';
    Rj = icalloc (mc->n * (mc->range+1) * sizeof(*Rj));
    Rj1 = icalloc (mc->n * (mc->range+1) * sizeof(*Rj));
    Rj_a = icalloc (mc->n * sizeof(*Rj));
    Rj_b = icalloc (mc->n * sizeof(*Rj));
    Rj_c = icalloc (mc->n * sizeof(*Rj));

    set_bit (mc, Rj, 0, 0);
    for (d = 1; d<=mc->range; d++)
    {
        int s;
        memcpy (Rj + mc->n * d, Rj + mc->n * (d-1), mc->n * sizeof(*Rj));
        for (s = 0; s<dfa->no_states; s++)
        {
            if (get_bit (mc, Rj, d-1, s))
            {
                struct DFA_state *state = dfa->states[s];
                int i = state->tran_no;
                while (--i >= 0)
                    set_bit (mc, Rj, d, state->trans[i].to);
            }
        }
    }
    while ((ch = getc (inf)) != EOF)
    {
        MatchWord *Rj_t;
        
        infbuf[inf_ptr] = ch;
        if (ch == '\n')
        {
            if (no_match)
            {
                int i = inf_ptr;
                if (show_line)
                    printf ("%5d:", lineno);
                do
                {
                    if (--i < 0)
                        i = INFBUF_SIZE-1;
                } while (infbuf[i] != '\n');
                do
                {
                    if (++i == INFBUF_SIZE)
                        i = 0;
                    putchar (infbuf[i]);
                } while (infbuf[i] != '\n');
                no_match = 0;
            }
            lineno++;
        }
        if (++inf_ptr == INFBUF_SIZE)
            inf_ptr = 0;
        mask_shift (mc, Rj1, Rj, dfa, ch);
        for (d = 1; d <= mc->range; d++)
        {
            mask_shift (mc, Rj_b, Rj+d*mc->n, dfa, ch);    /* 1 */

            or (mc, Rj_a, Rj+(d-1)*mc->n, Rj1+(d-1)*mc->n); /* 2,3 */

            shift (mc, Rj_c, Rj_a, dfa);

            or (mc, Rj_a, Rj_b, Rj_c);                      /* 1,2,3*/

            or (mc, Rj1+d*mc->n, Rj_a, Rj+(d-1)*mc->n);     /* 1,2,3,4 */
        }
        for (s = 0; s<dfa->no_states; s++)
        {
            if (dfa->states[s]->rule_no)
                if (get_bit (mc, Rj1+mc->range*mc->n, 0, s))
                    no_match++;
        }
        for (d = 0; d <= mc->range; d++)
            reset_bit (mc, Rj1+d*mc->n, 0, dfa->no_states);
        Rj_t = Rj1;
        Rj1 = Rj;
        Rj = Rj_t;
    }
    ifree (Rj);
    ifree (Rj1);
    ifree (Rj_a);
    ifree (Rj_b);
    ifree (Rj_c);
    ifree (infbuf);
    return 0;
}

static int grep_file (struct DFA *dfa, const char *fname, int range)
{
    FILE *inf;
    MatchContext *mc;

    if (fname)
    {
        inf = fopen (fname, "r");
        if (!inf)
        {
            logf (LOG_FATAL|LOG_ERRNO, "cannot open `%s'", fname);
            exit (1);
        }
    }
    else
        inf = stdin;
     
    mc = mk_MatchContext (dfa, range);

    go (mc, dfa, inf);

    if (fname)
        fclose (inf);
    return 0;
}

int main (int argc, char **argv)
{
    int ret;
    int range = 0;
    char *arg;
    const char *pattern = NULL;
    int no_files = 0;
    struct DFA *dfa = dfa_init();

    prog = argv[0];
    while ((ret = options ("nr:dsv:", argv, argc, &arg)) != -2)
    {
        if (ret == 0)
        {
            if (!pattern)
            {
                int i;
                pattern = arg;
                i = dfa_parse (dfa, &pattern);
                if (i || *pattern)
                {
                    fprintf (stderr, "%s: illegal pattern\n", prog);
                    return 1;
                }
                dfa_mkstate (dfa);
            }
            else
            {
                no_files++;
                grep_file (dfa, arg, range);
            }
        }
        else if (ret == 'v')
        {
            log_init (log_mask_str(arg), prog, NULL);
        }
        else if (ret == 's')
        {
            dfa_verbose = 1;
        }
        else if (ret == 'd')
        {
            debug_dfa_tran = 1;
            debug_dfa_followpos = 1;
            debug_dfa_trav = 1;
        }
        else if (ret == 'r')
        {
            range = atoi (arg);
        }
        else if (ret == 'n')
        {
            show_line = 1;
        }
        else
        {
            logf (LOG_FATAL, "Unknown option '-%s'", arg);
            exit (1);
        }
    }
    if (!pattern)
    {
        fprintf (stderr, "usage:\n "
                 " %s [-d] [-n] [-r n] [-s] [-v n] pattern file ..\n", prog);
        exit (1);
    }
    else if (no_files == 0)
    {
        grep_file (dfa, NULL, range);
    }
    dfa_delete (&dfa);
    return 0;
}
