#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include <dict.h>

static void pr_lru (Dict_BFile bf)
{
    struct Dict_file_block *p;
    for (p=bf->lru_back; p; p = p->lru_next)
    {
        printf (" %d", p->no);
    }
    printf ("\n");
    fflush (stdout);
}

static struct Dict_file_block *find_block (Dict_BFile bf, int no)
{
    struct Dict_file_block *p;

    for (p=bf->hash_array[no% bf->hash_size]; p; p=p->h_next)
        if (p->no == no)
            break;
    return p;
}

static void release_block (Dict_BFile bf, struct Dict_file_block *p)
{
    assert (p);

    /* remove from lru queue */    
    if (p->lru_prev)
        p->lru_prev->lru_next = p->lru_next;
    else
        bf->lru_back = p->lru_next;
    if (p->lru_next)
        p->lru_next->lru_prev = p->lru_prev;
    else
        bf->lru_back = p->lru_prev;

    /* remove from hash chain */
    *p->h_prev = p->h_next;
    if (p->h_next)
        p->h_next->h_prev = p->h_prev;

    /* move to list of free blocks */
    p->h_next = bf->free_list;
    bf->free_list = p;
}

void dict_bf_flush_blocks (Dict_BFile bf, int no_to_flush)
{
    struct Dict_file_block *p;
    int i;
    for (i=0; i != no_to_flush && bf->lru_back; i++)
    {
        p = bf->lru_back;
        if (p->dirty)
            bf_write (bf->bf, p->no, 0, 0, p->data);
        release_block (bf, p);
    }
}

static struct Dict_file_block *alloc_block (Dict_BFile bf, int no)
{
    struct Dict_file_block *p, **pp;

    if (!bf->free_list)
        dict_bf_flush_blocks (bf, 1);
    assert (bf->free_list);
    p = bf->free_list;
    bf->free_list = p->h_next;
    p->dirty = 0;
    p->no = no;

    /* insert at front in lru chain */
    p->lru_next = NULL;
    p->lru_prev = bf->lru_front;
    if (bf->lru_front)
        bf->lru_front->lru_next = p;
    else
        bf->lru_back = p;
    bf->lru_front = p;

    /* insert in hash chain */
    pp = bf->hash_array + (no % bf->hash_size);
    p->h_next = *pp;
    p->h_prev = pp;
    if (*pp)
        (*pp)->h_prev = &p->h_next;
    *pp = p;
   
    return p;
}

static void move_to_front (Dict_BFile bf, struct Dict_file_block *p)
{
    /* Already at front? */
    if (!p->lru_next)
        return ;   

    /* Remove */
    if (p->lru_prev)
        p->lru_prev->lru_next = p->lru_next;
    else
        bf->lru_back = p->lru_next;
    p->lru_next->lru_prev = p->lru_prev;

    /* Insert at front */
    p->lru_next = NULL;
    p->lru_prev = bf->lru_front;
    if (bf->lru_front)
        bf->lru_front->lru_next = p;
    else
        bf->lru_back = p;
    bf->lru_front = p;
}

int bf_readp (Dict_BFile bf, int no, void **bufp)
{
    struct Dict_file_block *p;
    int i;
    if ((p = find_block (bf, no)))
    {
        *bufp = p->data;
        move_to_front (bf, p);
        bf->hits++;
        return 1;
    }
    bf->misses++;
    p = alloc_block (bf, no);
    i = bf_read (bf->bf, no, 0, 0, p->data);
    if (i > 0)
    {
        *bufp = p->data;
        return i;
    }
    release_block (bf, p);
    *bufp = NULL;
    return i;
}

int bf_newp (Dict_BFile bf, int no, void **bufp)
{
    struct Dict_file_block *p;
    if (!(p = find_block (bf, no)))
        p = alloc_block (bf, no);
    else
        move_to_front (bf, p);
    *bufp = p->data;
    memset (p->data, 0, bf->bf->block_size);
    p->dirty = 1;
#if 0
    printf ("bf_newp of %d:", no);
    pr_lru (bf);
#endif
    return 1;
}

int bf_touch (Dict_BFile bf, int no)
{
    struct Dict_file_block *p;
    if ((p = find_block (bf, no)))
    {
        p->dirty = 1;
        return 0;
    }
    return -1;
}

