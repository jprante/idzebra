/*
 * Copyright (C) 1994, Index Data I/S 
 * All rights reserved.
 * Sebastian Hammer, Adam Dickmeiss
 *
 * $Log: lexer.h,v $
 * Revision 1.2  1995-01-24 16:00:22  adam
 * Added -ansi to CFLAGS.
 * Some changes to the dfa module.
 *
 * Revision 1.1  1994/09/26  10:16:55  adam
 * First version of dfa module in alex. This version uses yacc to parse
 * regular expressions. This should be hand-made instead.
 *
 */


int        read_file      ( const char *, struct DFA * );
void       error          ( const char *, ... );

extern int ccluse;

