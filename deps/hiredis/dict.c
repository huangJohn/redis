/* Hash table implementation.
 *
 * This file implements in memory hash tables with insert/del/replace/find/
 * get-random-element operations. Hash tables will auto resize if needed
 * tables of power of two in size are used, collisions are handled by
 * chaining. See the source code for more information... :)
 *
 * Copyright (c) 2006-2010, Salvatore Sanfilippo <antirez at gmail dot com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of Redis nor the names of its contributors may be used
 *     to endorse or promote products derived from this software without
 *     specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "fmacros.h"
#include "alloc.h"
#include <stdlib.h>
#include <assert.h>
#include <limits.h>
#include "dict.h"

/* -------------------------- private prototypes ---------------------------- */

static int _dictExpandIfNeeded(dict *ht);
static unsigned long _dictNextPower(unsigned long size);
static int _dictKeyIndex(dict *ht, const void *key);
static int _dictInit(dict *ht, dictType *type, void *privDataPtr);

/* -------------------------- hash functions -------------------------------- */

/* Generic hash function (a popular one from Bernstein).
 * I tested a few and this was the best. */
static unsigned int dictGenHashFunction(const unsigned char *buf, int len) {
    unsigned int hash = 5381;

    while (len--)
        hash = ((hash << 5) + hash) + (*buf++); /* hash * 33 + c */
    return hash;
}

/* ----------------------------- API implementation ------------------------- */

/* Reset an hashtable already initialized with ht_init().
 * NOTE: This function should only called by ht_destroy(). */
static void _dictReset(dict *ht) {
    //重置map
    ht->table = NULL;
    ht->size = 0;
    ht->sizemask = 0;
    ht->used = 0;
}

/* Create a new hash table */
static dict *dictCreate(dictType *type, void *privDataPtr) {
    /*申请ht内存*/
    dict *ht = hi_malloc(sizeof(*ht));
    if (ht == NULL)
        return NULL;

    /*内存获取成功后，init ht*/
    _dictInit(ht,type,privDataPtr);
    return ht;
}

/* Initialize the hash table */
static int _dictInit(dict *ht, dictType *type, void *privDataPtr) {
    //重置一次ht
    _dictReset(ht);
    //set type
    ht->type = type;
    //set privdata
    ht->privdata = privDataPtr;
    return DICT_OK;
}

/* Expand or create the hashtable */
static int dictExpand(dict *ht, unsigned long size) {
    dict n; /* the new hashtable */
    //扩容realsize必须要next最小的2的整数幂
    unsigned long realsize = _dictNextPower(size), i;

    /* the size is invalid if it is smaller than the number of
     * elements already inside the hashtable */
    if (ht->used > size)
        //如果ht已经用了大小>size，error
        return DICT_ERR;

    _dictInit(&n, ht->type, ht->privdata);
    n.size = realsize;/*set real size*/
    n.sizemask = realsize-1;/*计算掩码*/
    n.table = hi_calloc(realsize,sizeof(dictEntry*));/*new ht*/
    if (n.table == NULL)
        return DICT_ERR;

    /* Copy all the elements from the old to the new table:
     * note that if the old hash table is empty ht->size is zero,
     * so dictExpand just creates an hash table. */
    n.used = ht->used;/*set already used*/
    for (i = 0; i < ht->size && ht->used > 0; i++) {
        dictEntry *he, *nextHe;

        if (ht->table[i] == NULL) continue;/*ht idx上是null，不需要操作*/

        /* For each hash entry on this slot... */
        he = ht->table[i];/*拿到ht位置上的 entry*/
        while(he) {
            unsigned int h;

            nextHe = he->next;/*获取链表指针*/
            /* Get the new element index */
            h = dictHashKey(ht, he->key) & n.sizemask;/*计算key hash值，然后直接与计算得到新的index，求&计算*/
            //头插链表
            he->next = n.table[h];
            n.table[h] = he;
            //ht used --
            ht->used--;
            /* Pass to the next element */
            //set he to next
            he = nextHe;
        }
    }
    //assert used = 0
    assert(ht->used == 0);
    //调用hi redis c库函数free ht结构
    hi_free(ht->table);

    /* Remap the new hashtable in the old */
    //重新指向ht的指针为new ht
    *ht = n;
    //return ok
    return DICT_OK;
}

/* Add an element to the target hash table */
static int dictAdd(dict *ht, void *key, void *val) {
    int index;
    dictEntry *entry;

    /* Get the index of the new element, or -1 if
     * the element already exists. */
    if ((index = _dictKeyIndex(ht, key)) == -1)
        return DICT_ERR;

    /* Allocates the memory and stores key */
    entry = hi_malloc(sizeof(*entry));
    if (entry == NULL)
        return DICT_ERR;

    //头插
    entry->next = ht->table[index];
    ht->table[index] = entry;

    /* Set the hash entry fields. */
    dictSetHashKey(ht, entry, key);
    dictSetHashVal(ht, entry, val);
    ht->used++;
    return DICT_OK;
}

/* Add an element, discarding the old if the key already exists.
 * Return 1 if the key was added from scratch, 0 if there was already an
 * element with such key and dictReplace() just performed a value update
 * operation. */
static int dictReplace(dict *ht, void *key, void *val) {
    dictEntry *entry, auxentry;

    /* Try to add the element. If the key
     * does not exists dictAdd will succeed. */
    if (dictAdd(ht, key, val) == DICT_OK)
        //add成功，直接返回了
        return 1;
    /* It already exists, get the entry */
    entry = dictFind(ht, key);
    if (entry == NULL)
        //没有
        return 0;

    /* Free the old value and set the new one */
    /* Set the new value and free the old one. Note that it is important
     * to do that in this order, as the value may just be exactly the same
     * as the previous one. In this context, think to reference counting,
     * you want to increment (set), and then decrement (free), and not the
     * reverse. */
    auxentry = *entry;
    //set new
    dictSetHashVal(ht, entry, val);
    //free old
    dictFreeEntryVal(ht, &auxentry);
    return 0;
}

/* Search and remove an element */
static int dictDelete(dict *ht, const void *key) {
    unsigned int h;
    dictEntry *de, *prevde;

    if (ht->size == 0)
        return DICT_ERR;
    h = dictHashKey(ht, key) & ht->sizemask;
    de = ht->table[h];

    //链表操作删除node
    prevde = NULL;
    while(de) {
        if (dictCompareHashKeys(ht,key,de->key)) {
            /* Unlink the element from the list */
            if (prevde)
                //prev node next指向de next
                prevde->next = de->next;
            else
                //没有，ht上的头引用，直接删除头元素
                ht->table[h] = de->next;

            //free
            dictFreeEntryKey(ht,de);
            dictFreeEntryVal(ht,de);
            //free de
            hi_free(de);
            //used --
            ht->used--;
            return DICT_OK;
        }
        //没找到，prev等于当前，当前再向下找
        prevde = de;
        de = de->next;
    }
    return DICT_ERR; /* not found */
}

/* Destroy an entire hash table */
static int _dictClear(dict *ht) {
    unsigned long i;

    /* Free all the elements */
    for (i = 0; i < ht->size && ht->used > 0; i++) {
        dictEntry *he, *nextHe;

        if ((he = ht->table[i]) == NULL) continue;
        while(he) {
            nextHe = he->next;
            //free
            dictFreeEntryKey(ht, he);
            dictFreeEntryVal(ht, he);
            //free node
            hi_free(he);
            ht->used--;
            he = nextHe;
        }
    }
    /* Free the table and the allocated cache structure */
    //free table结构
    hi_free(ht->table);
    /* Re-initialize the table */
    //reset
    _dictReset(ht);
    return DICT_OK; /* never fails */
}

/* Clear & Release the hash table */
static void dictRelease(dict *ht) {
    _dictClear(ht);/*先clear元素*/
    hi_free(ht);/*再free ht结构*/
}

static dictEntry *dictFind(dict *ht, const void *key) {
    dictEntry *he;
    unsigned int h;

    if (ht->size == 0) return NULL;
    h = dictHashKey(ht, key) & ht->sizemask;
    he = ht->table[h];
    while(he) {
        if (dictCompareHashKeys(ht, key, he->key))
            //找到链表上的entry返回
            return he;
        he = he->next;
    }
    //没有null
    return NULL;
}

static dictIterator *dictGetIterator(dict *ht) {
    dictIterator *iter = hi_malloc(sizeof(*iter));
    if (iter == NULL)
        return NULL;

    iter->ht = ht;
    iter->index = -1;
    iter->entry = NULL;
    iter->nextEntry = NULL;
    return iter;
}

static dictEntry *dictNext(dictIterator *iter) {
    while (1) {
        if (iter->entry == NULL) {
            iter->index++;
            if (iter->index >=
                    (signed)iter->ht->size) break;
            iter->entry = iter->ht->table[iter->index];
        } else {
            iter->entry = iter->nextEntry;
        }
        if (iter->entry) {
            /* We need to save the 'next' here, the iterator user
             * may delete the entry we are returning. */
            iter->nextEntry = iter->entry->next;
            return iter->entry;
        }
    }
    return NULL;
}

static void dictReleaseIterator(dictIterator *iter) {
    hi_free(iter);
}

/* ------------------------- private functions ------------------------------ */

/* Expand the hash table if needed */
static int _dictExpandIfNeeded(dict *ht) {
    /* If the hash table is empty expand it to the initial size,
     * if the table is "full" double its size. */
    if (ht->size == 0)
        //ht size = 0, 则扩容默认大小4
        return dictExpand(ht, DICT_HT_INITIAL_SIZE);
    if (ht->used == ht->size)
        //如果过存储数据大小==ht 总大小，执行扩容, size*2
        return dictExpand(ht, ht->size*2);
    return DICT_OK;
}

/* Our hash table capability is a power of two */
static unsigned long _dictNextPower(unsigned long size) {
    unsigned long i = DICT_HT_INITIAL_SIZE;/*默认初始4*/

    if (size >= LONG_MAX) return LONG_MAX;/*size超过了存储限制，取long max*/
    /*寻找最小的2整数幂，满足2^x > size*/
    while(1) {
        if (i >= size)
            //满足了
            //只能是4,8,16,32,64...
            return i;
        //*2
        i *= 2;
    }
}

/* Returns the index of a free slot that can be populated with
 * an hash entry for the given 'key'.
 * If the key already exists, -1 is returned. */
static int _dictKeyIndex(dict *ht, const void *key) {
    unsigned int h;
    dictEntry *he;

    /* Expand the hashtable if needed */
    if (_dictExpandIfNeeded(ht) == DICT_ERR)
        //扩容失败
        return -1;
    /* Compute the key hash value */
    //位计算key的hash值
    h = dictHashKey(ht, key) & ht->sizemask;
    /* Search if this slot does not already contain the given key */
    //获取链表
    he = ht->table[h];
    while(he) {
        //key已经存在了slot上
        if (dictCompareHashKeys(ht, key, he->key))
            return -1;
        //遍历下去
        he = he->next;
    }
    //return 新key的hash
    return h;
}

