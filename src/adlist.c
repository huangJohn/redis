/* adlist.c - A generic doubly linked list implementation
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


#include <stdlib.h>
#include "adlist.h"
#include "zmalloc.h"

/* Create a new list. The created list can be freed with
 * listRelease(), but private value of every node need to be freed
 * by the user before to call listRelease(), or by setting a free method using
 * listSetFreeMethod.
 *
 * On error, NULL is returned. Otherwise the pointer to the new list. */
list *listCreate(void)
{
    struct list *list;

    if ((list = zmalloc(sizeof(*list))) == NULL)
        //oom后返回null
        return NULL;
    //内存分配成功初始化成员变量
    list->head = list->tail = NULL;
    list->len = 0;
    list->dup = NULL;
    list->free = NULL;
    list->match = NULL;
    return list;
}

/* Remove all the elements from the list without destroying the list itself. */
/*
 * 将链表变成空，不会free内存
 * */
void listEmpty(list *list)
{
    unsigned long len;
    listNode *current, *next;

    //current指向head
    current = list->head;
    //拿到len
    len = list->len;
    while(len--) {
        //next指向下一个
        next = current->next;
        //有free函数，则清除current的value数据
        if (list->free) list->free(current->value);
        //free current内存
        zfree(current);
        //current又指向next
        current = next;
    }
    //遍历list后，清除head和tail
    list->head = list->tail = NULL;
    //len set = 0
    list->len = 0;
}

/* Free the whole list.
 *
 * This function can't fail. */
void listRelease(list *list)
{
    //清空list数据
    listEmpty(list);
    //free list
    zfree(list);
}

/* Add a new node to the list, to head, containing the specified 'value'
 * pointer as value.
 *
 * On error, NULL is returned and no operation is performed (i.e. the
 * list remains unaltered).
 * On success the 'list' pointer you pass to the function is returned. */
/*
 * 从头add
 * */
list *listAddNodeHead(list *list, void *value)
{
    listNode *node;

    if ((node = zmalloc(sizeof(*node))) == NULL)
        return NULL;
    //set value
    node->value = value;
    if (list->len == 0) {
        //没有list，head和tail指向node
        list->head = list->tail = node;
        //node prev和next = null
        node->prev = node->next = NULL;
    } else {
        //有list
        node->prev = NULL;/*prev置null*/
        node->next = list->head;/*next指向head指向*/
        list->head->prev = node;/*原head prev 指向node*/
        list->head = node;/*移动head至最新*/
    }
    list->len++;/*len++*/
    return list;
}

/* Add a new node to the list, to tail, containing the specified 'value'
 * pointer as value.
 *
 * On error, NULL is returned and no operation is performed (i.e. the
 * list remains unaltered).
 * On success the 'list' pointer you pass to the function is returned. */
list *listAddNodeTail(list *list, void *value)
{
    listNode *node;

    if ((node = zmalloc(sizeof(*node))) == NULL)
        return NULL;
    node->value = value;
    if (list->len == 0) {
        //head和tail指向node。prev=null，next=null
        list->head = list->tail = node;
        node->prev = node->next = NULL;
    } else {
        //node prev指向tail
        node->prev = list->tail;
        //next null
        node->next = NULL;
        //移动原tail next只node
        list->tail->next = node;
        //移动tail只node
        list->tail = node;
    }
    //len++
    list->len++;
    return list;
}

//插入节点
list *listInsertNode(list *list, listNode *old_node, void *value, int after) {
    listNode *node;

    //申请空间
    if ((node = zmalloc(sizeof(*node))) == NULL)
        return NULL;
    //赋值val
    node->value = value;
    if (after) {
        //old node之后insert
        //prev指向前节点
        node->prev = old_node;
        //next指向后节点
        node->next = old_node->next;
        if (list->tail == old_node) {
            //如果是tail，则移动tail
            list->tail = node;
        }
    } else {
        //node之前insert
        //node next指向old node
        node->next = old_node;
        //node prev指向old前节点
        node->prev = old_node->prev;
        if (list->head == old_node) {
            //如果是head，移动head
            list->head = node;
        }
    }
    if (node->prev != NULL) {
        //前节点的next指向新node
        node->prev->next = node;
    }
    if (node->next != NULL) {
        //后节点的prev指向新node
        node->next->prev = node;
    }
    //len ++
    list->len++;
    return list;
}

/* Remove the specified node from the specified list.
 * It's up to the caller to free the private value of the node.
 *
 * This function can't fail. */
/*
 * list删除某个node节点
 * */
void listDelNode(list *list, listNode *node)
{
    if (node->prev)
        //node prev存在，prev 的next直接指向node的next
        node->prev->next = node->next;
    else
        //否则head指向node的next
        list->head = node->next;
    if (node->next)
        //next存在，后节点的prev直接指向node prev
        node->next->prev = node->prev;
    else
        //否则是tail直接指向node prev
        list->tail = node->prev;
    //调用free函数清理data val值
    if (list->free) list->free(node->value);
    //free函数清除
    zfree(node);
    //len--
    list->len--;
}

/* Returns a list iterator 'iter'. After the initialization every
 * call to listNext() will return the next element of the list.
 *
 * This function can't fail. */
listIter *listGetIterator(list *list, int direction)
{
    listIter *iter;

    if ((iter = zmalloc(sizeof(*iter))) == NULL) return NULL;
    if (direction == AL_START_HEAD)
        iter->next = list->head;
    else
        iter->next = list->tail;
    iter->direction = direction;
    return iter;
}

/* Release the iterator memory */
void listReleaseIterator(listIter *iter) {
    zfree(iter);
}

/* Create an iterator in the list private iterator structure */
/*构造head的迭代器*/
void listRewind(list *list, listIter *li) {
    li->next = list->head;
    li->direction = AL_START_HEAD;
}

/*构造tail的反向迭代器*/
void listRewindTail(list *list, listIter *li) {
    li->next = list->tail;
    li->direction = AL_START_TAIL;
}

/* Return the next element of an iterator.
 * It's valid to remove the currently returned element using
 * listDelNode(), but not to remove other elements.
 *
 * The function returns a pointer to the next element of the list,
 * or NULL if there are no more elements, so the classical usage
 * pattern is:
 *
 * iter = listGetIterator(list,<direction>);
 * while ((node = listNext(iter)) != NULL) {
 *     doSomethingWith(listNodeValue(node));
 * }
 *
 * */
listNode *listNext(listIter *iter)
{
    //next element
    listNode *current = iter->next;

    if (current != NULL) {
        //存在
        if (iter->direction == AL_START_HEAD)
            //正向，向下
            iter->next = current->next;
        else
            //反向，向上
            iter->next = current->prev;
    }
    //返回迭代下一个的list node
    return current;
}

/* Duplicate the whole list. On out of memory NULL is returned.
 * On success a copy of the original list is returned.
 *
 * The 'Dup' method set with listSetDupMethod() function is used
 * to copy the node value. Otherwise the same pointer value of
 * the original node is used as value of the copied node.
 *
 * The original list both on success or error is never modified. */
list *listDup(list *orig)
{
    list *copy;
    listIter iter;
    listNode *node;

    if ((copy = listCreate()) == NULL)/*内存oom，null return null*/
        return NULL;
    /*复制lost的dup、free、match函数*/
    copy->dup = orig->dup;
    copy->free = orig->free;
    copy->match = orig->match;
    listRewind(orig, &iter);
    while((node = listNext(&iter)) != NULL) {/*while head向下迭代*/
        void *value;

        if (copy->dup) {
            //dup函数有，执行dup函数，复制val
            value = copy->dup(node->value);
            if (value == NULL) {
                //遇到复制的val是null后，释放copy，return null
                listRelease(copy);
                return NULL;
            }
        } else
            /*否则，单执行value赋值*/
            value = node->value;
        /*加入到copy list后的尾部，尾插*/
        if (listAddNodeTail(copy, value) == NULL) {
            //遇到失败，release copy and return null
            listRelease(copy);
            return NULL;
        }
    }
    //迭代完成，return copied list
    return copy;
}

/* Search the list for a node matching a given key.
 * The match is performed using the 'match' method
 * set with listSetMatchMethod(). If no 'match' method
 * is set, the 'value' pointer of every node is directly
 * compared with the 'key' pointer.
 *
 * On success the first matching node pointer is returned
 * (search starts from head). If no matching node exists
 * NULL is returned. */
listNode *listSearchKey(list *list, void *key)
{
    listIter iter;
    listNode *node;

    listRewind(list, &iter);
    while((node = listNext(&iter)) != NULL) {
        if (list->match) {
            //match函数计算相等后，return找到的node
            if (list->match(node->value, key)) {
                return node;
            }
        } else {
            //否则，只是val相等return node
            if (key == node->value) {
                return node;
            }
        }
    }
    //没找到，return null
    return NULL;
}

/* Return the element at the specified zero-based index
 * where 0 is the head, 1 is the element next to head
 * and so on. Negative integers are used in order to count
 * from the tail, -1 is the last element, -2 the penultimate
 * and so on. If the index is out of range NULL is returned. */
listNode *listIndex(list *list, long index) {
    listNode *n;

    if (index < 0) {
        //倒向计算idx
        index = (-index)-1;
        n = list->tail;
        while(index-- && n) n = n->prev;
    } else {
        //正向计算idx
        n = list->head;
        //idx -- 不等于并且 n 不是null，while继续
        while(index-- && n) n = n->next;
    }
    //return idx指向的node
    return n;
}

/* Rotate the list removing the tail node and inserting it to the head. */
//把tail旋转到list head上
void listRotateTailToHead(list *list) {
    if (listLength(list) <= 1) return;/*len==1，return*/

    /* Detach current tail */
    /*取出tail节点*/
    listNode *tail = list->tail;
    //tail新指向倒数第二
    list->tail = tail->prev;
    //tail新next = null
    list->tail->next = NULL;
    /* Move it as head */
    //移动到head，head prev指向tail
    list->head->prev = tail;
    //prev 置为null
    tail->prev = NULL;
    //next 指向head
    tail->next = list->head;
    //重新head移动
    list->head = tail;
}

/* Rotate the list removing the head node and inserting it to the tail. */
void listRotateHeadToTail(list *list) {
    if (listLength(list) <= 1) return;

    //取到head
    listNode *head = list->head;
    /* Detach current head */
    //移动head到next处
    list->head = head->next;
    //新head的prev=null
    list->head->prev = NULL;
    /* Move it as tail */
    //tail next 指向新head
    list->tail->next = head;
    //next 置为 null
    head->next = NULL;
    //新tail 的prev指向原tail
    head->prev = list->tail;
    //移动tail
    list->tail = head;
}

/* Add all the elements of the list 'o' at the end of the
 * list 'l'. The list 'other' remains empty but otherwise valid. */
void listJoin(list *l, list *o) {
    //o list len = 0 return
    if (o->len == 0) return;

    //o 的head prev指向l的tail
    o->head->prev = l->tail;

    if (l->tail)
        //l tail有，next指向o head
        l->tail->next = o->head;
    else
        //没有，head指向head
        l->head = o->head;

    //l的tail 指向o tail
    l->tail = o->tail;
    //l len + o len
    l->len += o->len;

    /* Setup other as an empty list. */
    //o 清理
    o->head = o->tail = NULL;
    // o len set to 0
    o->len = 0;
}
