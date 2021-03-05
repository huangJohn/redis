/* SDSLib 2.0 -- A C dynamic strings library
 *
 * Copyright (c) 2006-2015, Salvatore Sanfilippo <antirez at gmail dot com>
 * Copyright (c) 2015, Oran Agra
 * Copyright (c) 2015, Redis Labs, Inc
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

#ifndef __SDS_H
#define __SDS_H

//32位系统，1个int 4字节，每个字节8bit，0000 0001 值为 0~2^8-1
//定义sds最大分配的内存1M=1024KB=1024*1024Byte * 8 bit
#define SDS_MAX_PREALLOC (1024*1024)
extern const char *SDS_NOINIT;

#include <sys/types.h>
#include <stdarg.h>
#include <stdint.h>

//定义sds结构，char类型
typedef char *sds;

/* Note: sdshdr5 is never used, we just access the flags byte directly.
 * However is here to document the layout of type 5 SDS strings. */
/*
 * __attribute__ ((__packed__)) 标识c编译阶段需要取消内存对齐优化
 * */
struct __attribute__ ((__packed__)) sdshdr5 {
    //比其他类型header 少len和alloc字段存储
    //flags低三位表示类型，高五位标识长度，高五位不一定是0
    unsigned char flags; /* 3 lsb of type, and 5 msb of string length */
    char buf[];
};
struct __attribute__ ((__packed__)) sdshdr8 {
    //1个字节整形，刚好8位，sds已使用长度, 0 - 2^8-1
    uint8_t len; /* used */
    //sds预分配的长度，不包含hdr部分和null终止符空间大小, 0 - 2^8-1
    uint8_t alloc; /* excluding the header and null terminator */
    //sds sdr flags字段 该字节的低三位决定hdr类型，flag=0000 0001，1，则hdr是sdshdr8类型，有5位没使用
    unsigned char flags; /* 3 lsb of type, 5 unused bits */
    //s字符串指针数组，是一个柔性数组，通过sizeof（len+alloc+flags）求出buf[]的索引
    char buf[];
};
struct __attribute__ ((__packed__)) sdshdr16 {
    //2个字节代表已用长度, 0 - 2^16 -1
    uint16_t len; /* used */
    //同理，预分配空间大小
    uint16_t alloc; /* excluding the header and null terminator */
    //低三位0000 0010 2 标识 sdshdr16类型
    unsigned char flags; /* 3 lsb of type, 5 unused bits */
    char buf[];
};
struct __attribute__ ((__packed__)) sdshdr32 {
    //4字节存储长度 0 - 2^32 - 1
    uint32_t len; /* used */
    uint32_t alloc; /* excluding the header and null terminator */
    //0000 0011 = 3
    unsigned char flags; /* 3 lsb of type, 5 unused bits */
    char buf[];
};
struct __attribute__ ((__packed__)) sdshdr64 {
    uint64_t len; /* used */
    uint64_t alloc; /* excluding the header and null terminator */
    unsigned char flags; /* 3 lsb of type, 5 unused bits */
    char buf[];
};

#define SDS_TYPE_5  0 /*0x00*/
#define SDS_TYPE_8  1 /*0x01*/
#define SDS_TYPE_16 2 /*0x02*/
#define SDS_TYPE_32 3 /*0x03*/
#define SDS_TYPE_64 4 /*0x04*/
#define SDS_TYPE_MASK 7 /*0x07*/
#define SDS_TYPE_BITS 3
#define SDS_HDR_VAR(T,s) struct sdshdr##T *sh = (void*)((s)-(sizeof(struct sdshdr##T)));/*sizeof 计算 8型结构len+alloc+flags，后在向低位移动找到起始header位置*/
#define SDS_HDR(T,s) ((struct sdshdr##T *)((s)-(sizeof(struct sdshdr##T))))/*求header位置*/
#define SDS_TYPE_5_LEN(f) ((f)>>SDS_TYPE_BITS)/*5类型hdr，根据定义，直接高位移动3位计算值得len*/

/*
 * 内嵌函数，计算一个sds字符串长度大小
 * */
static inline size_t sdslen(const sds s) {
    /*获取flag位的值，s的指针向第低位移动得到flag值*/
    unsigned char flags = s[-1];
    /*
     * 低三位与 111 计算
     * 0x00 & 0x07 = 000 & 111 = 0 or 0011 1000 & 111 = 0
     * 0x01 & 0x07 = 001 & 111 = 1
     * 010 & 111 = 2
     * 011 & 111 = 3
     * 100 & 111 = 4
     *
     * */
    switch(flags&SDS_TYPE_MASK) {
        case SDS_TYPE_5:
            //0
            return SDS_TYPE_5_LEN(flags);
        case SDS_TYPE_8:
            //1，求得header位置后求len常数值，O(1)
            return SDS_HDR(8,s)->len;
        case SDS_TYPE_16:
            //2
            return SDS_HDR(16,s)->len;
        case SDS_TYPE_32:
            //3
            return SDS_HDR(32,s)->len;
        case SDS_TYPE_64:
            //4
            return SDS_HDR(64,s)->len;
    }
    return 0;
}

/*
 * 计算s可用空间大小
 * */
static inline size_t sdsavail(const sds s) {
    //求flags值
    unsigned char flags = s[-1];
    switch(flags&SDS_TYPE_MASK) {
        case SDS_TYPE_5: {
            //5型header，可用0，静态定义
            return 0;
        }
        case SDS_TYPE_8: {
            //8型header，先找打header位置
            SDS_HDR_VAR(8,s);
            //知道了header位置，访问字段计算可用空间alloc -len
            return sh->alloc - sh->len;
        }
        case SDS_TYPE_16: {
            //同理
            SDS_HDR_VAR(16,s);
            return sh->alloc - sh->len;
        }
        case SDS_TYPE_32: {
            //同理
            SDS_HDR_VAR(32,s);
            return sh->alloc - sh->len;
        }
        case SDS_TYPE_64: {
            //同理
            SDS_HDR_VAR(64,s);
            return sh->alloc - sh->len;
        }
    }
    return 0;
}

/*
 * sds设置新长度，不会涉及内存分配
 * */
static inline void sdssetlen(sds s, size_t newlen) {
    //计算flags位置
    unsigned char flags = s[-1];
    switch(flags&SDS_TYPE_MASK) {
        case SDS_TYPE_5:
            {
                //拿到char[]指针
                unsigned char *fp = ((unsigned char*)s)-1;
                //重新指向 或操作 后得到高位值为新的len
                *fp = SDS_TYPE_5 | (newlen << SDS_TYPE_BITS);
            }
            break;
        //其他类型直接改变len
        case SDS_TYPE_8:
            SDS_HDR(8,s)->len = newlen;
            break;
        case SDS_TYPE_16:
            SDS_HDR(16,s)->len = newlen;
            break;
        case SDS_TYPE_32:
            SDS_HDR(32,s)->len = newlen;
            break;
        case SDS_TYPE_64:
            SDS_HDR(64,s)->len = newlen;
            break;
    }
}

/*
 * 长度自增inc
 * */
static inline void sdsinclen(sds s, size_t inc) {
    unsigned char flags = s[-1];
    switch(flags&SDS_TYPE_MASK) {
        case SDS_TYPE_5:
            {
                unsigned char *fp = ((unsigned char*)s)-1;
                //计算新的长度，根据原来的大小再加步长inc
                unsigned char newlen = SDS_TYPE_5_LEN(flags)+inc;
                //高位推，或一次得到flags新值
                *fp = SDS_TYPE_5 | (newlen << SDS_TYPE_BITS);
            }
            break;
        //其他header类型直接加inc步长
        case SDS_TYPE_8:
            SDS_HDR(8,s)->len += inc;
            break;
        case SDS_TYPE_16:
            SDS_HDR(16,s)->len += inc;
            break;
        case SDS_TYPE_32:
            SDS_HDR(32,s)->len += inc;
            break;
        case SDS_TYPE_64:
            SDS_HDR(64,s)->len += inc;
            break;
    }
}

/* sdsalloc() = sdsavail() + sdslen() */
/*
 * 计算sds预分配大小
 * */
static inline size_t sdsalloc(const sds s) {
    unsigned char flags = s[-1];
    switch(flags&SDS_TYPE_MASK) {
        case SDS_TYPE_5:
            return SDS_TYPE_5_LEN(flags);
        case SDS_TYPE_8:
            return SDS_HDR(8,s)->alloc;
        case SDS_TYPE_16:
            return SDS_HDR(16,s)->alloc;
        case SDS_TYPE_32:
            return SDS_HDR(32,s)->alloc;
        case SDS_TYPE_64:
            return SDS_HDR(64,s)->alloc;
    }
    return 0;
}

//set sds 预分配大小
static inline void sdssetalloc(sds s, size_t newlen) {
    unsigned char flags = s[-1];
    switch(flags&SDS_TYPE_MASK) {
        case SDS_TYPE_5:
            /* Nothing to do, this type has no total allocation info. */
            //5型headr，没有预分配代销字段
            break;
        case SDS_TYPE_8:
            SDS_HDR(8,s)->alloc = newlen;
            break;
        case SDS_TYPE_16:
            SDS_HDR(16,s)->alloc = newlen;
            break;
        case SDS_TYPE_32:
            SDS_HDR(32,s)->alloc = newlen;
            break;
        case SDS_TYPE_64:
            SDS_HDR(64,s)->alloc = newlen;
            break;
    }
}

sds sdsnewlen(const void *init, size_t initlen);/*根据字符串大小初始化sds，内存不够会oom*/
sds sdstrynewlen(const void *init, size_t initlen);/*根据字符串大小初始化sds，try尝试，内存不够，返回null*/
sds sdsnew(const char *init);/*c string new 一个sds，默认根据内部header类型分配内存*/
sds sdsempty(void);/*new 一个空sds，内部给的是8型，因为可能有append操作*/
sds sdsdup(const sds s);/*复制一个出新sds*/
void sdsfree(sds s);/*free sds*/
sds sdsgrowzero(sds s, size_t len);/*让s大小加到指定len，多余空间用0填充*/
sds sdscatlen(sds s, const void *t, size_t len);/*把t字符串的指定len大小的值append至sds上*/
sds sdscat(sds s, const char *t);/*c cat拼接操作*/
sds sdscatsds(sds s, const sds t);/*cat sds*/
sds sdscpylen(sds s, const char *t, size_t len);/*copy str，按照指定大小*/
sds sdscpy(sds s, const char *t);/*copy str至s*/

sds sdscatvprintf(sds s, const char *fmt, va_list ap);
#ifdef __GNUC__
sds sdscatprintf(sds s, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));
#else
sds sdscatprintf(sds s, const char *fmt, ...);
#endif

sds sdscatfmt(sds s, char const *fmt, ...);
sds sdstrim(sds s, const char *cset);/*str去掉指定的字符*/
void sdsrange(sds s, ssize_t start, ssize_t end);/*截取指定范围str*/
void sdsupdatelen(sds s);
void sdsclear(sds s);/*len置0，free空间*/
int sdscmp(const sds s1, const sds s2);/*比较str*/
sds *sdssplitlen(const char *s, ssize_t len, const char *sep, int seplen, int *count);
void sdsfreesplitres(sds *tokens, int count);
void sdstolower(sds s);
void sdstoupper(sds s);
sds sdsfromlonglong(long long value);
sds sdscatrepr(sds s, const char *p, size_t len);
sds *sdssplitargs(const char *line, int *argc);
sds sdsmapchars(sds s, const char *from, const char *to, size_t setlen);
sds sdsjoin(char **argv, int argc, char *sep);
sds sdsjoinsds(sds *argv, int argc, const char *sep, size_t seplen);

/* Callback for sdstemplate. The function gets called by sdstemplate
 * every time a variable needs to be expanded. The variable name is
 * provided as variable, and the callback is expected to return a
 * substitution value. Returning a NULL indicates an error.
 */
typedef sds (*sdstemplate_callback_t)(const sds variable, void *arg);
sds sdstemplate(const char *template, sdstemplate_callback_t cb_func, void *cb_arg);

/* Low level functions exposed to the user API */
sds sdsMakeRoomFor(sds s, size_t addlen);
void sdsIncrLen(sds s, ssize_t incr);
sds sdsRemoveFreeSpace(sds s);
size_t sdsAllocSize(sds s);
void *sdsAllocPtr(sds s);

/* Export the allocator used by SDS to the program using SDS.
 * Sometimes the program SDS is linked to, may use a different set of
 * allocators, but may want to allocate or free things that SDS will
 * respectively free or allocate. */
void *sds_malloc(size_t size);
void *sds_realloc(void *ptr, size_t size);
void sds_free(void *ptr);

#ifdef REDIS_TEST
int sdsTest(int argc, char *argv[]);
#endif

#endif
