/*
 * Copyright (C) 1994, Index Data I/S 
 * All rights reserved.
 * Sebastian Hammer, Adam Dickmeiss
 *
 * $Log: charmap.c,v $
 * Revision 1.1  1996-05-31 09:07:18  quinn
 * Work on character-set handling
 *
 *
 */

/*
 * Support module to handle character-conversions into and out of the
 * Zebra dictionary.
 */

#include <ctype.h>

#include <alexutil.h>
#include <yaz-util.h>
#include <charmap.h>
#include <tpath.h>

const char *CHR_UNKNOWN = "\001";
const char *CHR_SPACE   = "\002";
const char *CHR_BASE    = "\003";

extern char *data1_tabpath;

/*
 * Character map trie node.
 */
struct chr_t_entry
{
    chr_t_entry **children; /* array of children */
    unsigned char *target;  /* target for this node, if any */
    unsigned char *equiv;   /* equivalent to, or sumthin */
} t_entry;

/*
 * Add an entry to the character map.
 */
static chr_t_entry *set_map_string(chr_t_entry *root, char *from, int len,
    char *to)
{
    if (!root)
    {
	root = xmalloc(sizeof(*root));
	root->children = 0;
	root->target = 0;
    }
    if (!len)
	root->target = (unsigned char *) xstrdup(to);
    else
    {
	if (!root->children)
	{
	    int i;

	    root->children = xmalloc(sizeof(chr_t_entry*) * 256);
	    for (i = 0; i < 256; i++)
		root->children[i] = 0;
	}
	root->children[(unsigned char) *from] =
	    set_map_string(root->children[(unsigned char) *from], from + 1,
	    len - 1, to);
    }
    return root;
}

int chr_map_chrs(chr_t_entry *t, char **from, int len, int *read, char **to,
    int max)
{
    int i = 0;
    unsigned char *s;

    while (len && t->children && t->children[(unsigned char) **from])
    {
	t = t->children[(unsigned char) **from];
	(*from)++;
	len--;
    }
    /* if there were no matches, we are still at the root node,
       which always has a null mapping */
    for (s = t->target; *s && max; s++)
    {
	**to = *s;
	s++;
	(*to)++;
	max--;
	i++;
    }
    return i;
}

char **chr_map_input(chr_t_entry *t, char **from, int len)
{
    static char *buf[2] = {0, 0}, str[2] = {0, 0};
    char *start = *from;

    if (t)
    {
	while (len && t->children && t->children[(unsigned char) **from])
	{
	    t = t->children[(unsigned char) **from];
	    (*from)++;
	    len--;
	}
	buf[0] = (char*) t->target;
    }
    else /* null mapping */
    {
	if (isalnum(**from))
	{
	    str[0] = **from;
	    buf[0] = str;
	}
	else if (isspace(**from))
	    buf[0] = (char*) CHR_SPACE;
	else
	    buf[0] = (char*) CHR_UNKNOWN;
    }
    if (start == *from)
	(*from)++;
    return buf;
    /* return (char*) t->target; */
}

static unsigned char prim(char **s)
{
    unsigned char c;
    unsigned int i;

    if (**s == '\\')
    {
	(*s)++;
	c = **s;
	switch (c)
	{
	    case '\\': c = '\\'; (*s)++; break;
	    case 'r': c = '\r'; (*s)++; break;
	    case 'n': c = '\n'; (*s)++; break;
	    case 't': c = '\t'; (*s)++; break;
	    case 's': c = ' '; (*s)++; break;
	    case 'x': sscanf(*s, "x%2x", &i); c = i; *s += 3; break;
	    case '{': case '[': case '(': case '}': case ']': case ')':
	        (*s)++;
		break;
	    default: sscanf(*s, "%3o", &i); c = i; *s += 3; break;
	}
	return c;
    }
    c = **s;
    ++(*s);
    return c;
}

/*
 * Callback function.
 * Add an entry to the value space.
 */
static void fun_addentry(char *s, void *data, int num)
{
    chrmaptab *tab = data;
    char tmp[2];

    tmp[0] = num; tmp[1] = '\0';
    tab->input = set_map_string(tab->input, s, strlen(s), tmp);
    tab->output[num + tab->base_uppercase] = (unsigned char *) xstrdup(s);
}

/* 
 * Callback function.
 * Add a space-entry to the value space.
 */
static void fun_addspace(char *s, void *data, int num)
{
    chrmaptab *tab = data;
    tab->input = set_map_string(tab->input, s, strlen(s), (char*) CHR_SPACE);
}

static int scan_string(char *s, void (*fun)(char *c, void *data, int num),
    void *data, int *num)
{
    unsigned char c, str[1024], begin, end;

    while (*s)
    {
	switch (*s)
	{
	    case '{':
	        s++;
	        begin = prim(&s);
		if (*s != '-')
		{
		    logf(LOG_FATAL, "Bad range in char-map");
		    return -1;
		}
		s++;
		end = prim(&s);
		if (end <= begin)
		{
		    logf(LOG_FATAL, "Bad range in char-map");
		    return -1;
		}
		s++;
		for (c = begin; c <= end; c++)
		{
		    str[0] = c; str[1] = '\0';
		    (*fun)((char *) str, data, num ? (*num)++ : 0);
		}
		break;
	    case '[': s++; abort(); break;
	    case '(': s++; abort(); break;
	    default:
	        c = prim(&s);
	        str[0] = c; str[1] = '\0';
		(*fun)((char *) str, data, num ? (*num)++ : 0);
	}
    }
    return 0;
}

chrmaptab *chr_read_maptab(char *name)
{
    FILE *f;
    char line[512], *argv[50];
    chrmaptab *res = xmalloc(sizeof(*res));
    int argc, num = (int) *CHR_BASE, i;

    if (!(f = yaz_path_fopen(data1_tabpath, name, "r")))
    {
	logf(LOG_WARN|LOG_ERRNO, "%s", name);
	return 0;
    }
    res = xmalloc(sizeof(*res));
    res->input = xmalloc(sizeof(*res->input));
    res->input->target = (unsigned char*) CHR_UNKNOWN;
    res->input->equiv = 0;
#if 0
    res->input->children = xmalloc(sizeof(res->input) * 256);
    for (i = 0; i < 256; i++)
    {
	res->input->children[i] = xmalloc(sizeof(*res->input));
	res->input->children[i]->children = 0;
	res->input->children[i]->target = CHR_UNKNOWN;
	res->input->children[i]->equiv = 0;
    }
#else
    res->input->children = 0;
#endif
    res->query_equiv = 0;
    for (i = 0; i < 256; i++)
    {
	char *t = xmalloc(2);

	t[0] = i;
	t[1] = '\0';
	res->output[i] = (unsigned char*)t;
    }
    res->output[(int) *CHR_SPACE] = (unsigned char *) " ";
    res->output[(int) *CHR_UNKNOWN] = (unsigned char*) "@";
    res->base_uppercase = 0;

    while ((argc = readconf_line(f, line, 512, argv, 50)))
	if (!yaz_matchstr(argv[0], "lowercase"))
	{
	    if (argc != 2)
	    {
		logf(LOG_FATAL, "Syntax error in charmap");
		fclose(f);
		return 0;
	    }
	    if (scan_string(argv[1], fun_addentry, res, &num) < 0)
	    {
		logf(LOG_FATAL, "Bad value-set specification");
		fclose(f);
		return 0;
	    }
	    res->base_uppercase = num;
	    res->output[(int) *CHR_SPACE + num] = (unsigned char *) " ";
	    res->output[(int) *CHR_UNKNOWN + num] = (unsigned char*) "@";
	    num = (int) *CHR_BASE;
	}
	else if (!yaz_matchstr(argv[0], "uppercase"))
	{
	    if (!res->base_uppercase)
	    {
		logf(LOG_FATAL, "Uppercase directive with no lowercase set");
		fclose(f);
		return 0;
	    }
	    if (argc != 2)
	    {
		logf(LOG_FATAL, "Syntax error in charmap");
		fclose(f);
		return 0;
	    }
	    if (scan_string(argv[1], fun_addentry, res, &num) < 0)
	    {
		logf(LOG_FATAL, "Bad value-set specification");
		fclose(f);
		return 0;
	    }
	}
	else if (!yaz_matchstr(argv[0], "space"))
	{
	    if (argc != 2)
	    {
		logf(LOG_FATAL, "Syntax error in charmap");
		fclose(f);
		return 0;
	    }
	    if (scan_string(argv[1], fun_addspace, res, 0))
	    {
		logf(LOG_FATAL, "Bad space specification");
		fclose(f);
		return 0;
	    }
	}
	else
	{
#if 0
	    logf(LOG_WARN, "Syntax error at '%s' in %s", line, file);
	    fclose(f);
	    return 0;
#endif
	}
    fclose(f);
    return res;
}
