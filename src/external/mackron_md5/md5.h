/*
MD5 hashing. Choice of public domain or MIT-0. See license statements at the end of this file.

David Reid - mackron@gmail.com
*/

/*
A simple MD5 hashing implementation. Usage:

    unsigned char digest[MD5_SIZE];
    md5_context ctx;
    md5_init(&ctx);
    {
        md5_update(&ctx, src, sz);
    }
    md5_finalize(&ctx, digest);

The above code is the literal implementation of `md5()` which is a high level helper for hashing
data of a known size:

    unsigned char hash[MD5_SIZE];
    md5(hash, data, dataSize);

Use `md5_format()` to format the digest as a hex string. The capacity of the output buffer needs to
be at least `MD5_SIZE_FORMATTED` bytes.

This library does not perform any memory allocations and does not use anything from the standard
library except for `size_t` and `NULL`, both of which are drawn in from stddef.h. No other standard
headers are included.

There is no need to link to anything with this library. You can use MD5_IMPLEMENTATION to define
the implementation section, or you can use md5.c if you prefer a traditional header/source pair.
*/
#ifndef md5_h
#define md5_h

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h> /* For size_t and NULL. */

#if defined(_MSC_VER)
    typedef unsigned __int64   md5_uint64;
#else
    typedef unsigned long long md5_uint64;
#endif

#if !defined(MD5_API)
    #define MD5_API
#endif

#define MD5_SIZE            16
#define MD5_SIZE_FORMATTED  33

typedef struct
{
    unsigned int a, b, c, d;    /* Registers. RFC 1321 section 3.3. */
    md5_uint64 sz;              /* 64-bit size. Since this is library operates on bytes, this is a byte count rather than a bit count. */
    unsigned char cache[64];    /* The cache will be filled with data, and when full will be processed. */
    unsigned int cacheLen;      /* Number of valid bytes in the cache. */
} md5_context;

MD5_API void md5_init(md5_context* ctx);
MD5_API void md5_update(md5_context* ctx, const void* src, size_t sz);
MD5_API void md5_finalize(md5_context* ctx, unsigned char* digest);
MD5_API void md5(unsigned char* digest, const void* src, size_t sz);
MD5_API void md5_format(char* dst, size_t dstCap, const unsigned char* hash);

#ifdef __cplusplus
}
#endif
#endif  /* md5_h */

#if defined(MD5_IMPLEMENTATION)
#ifndef md5_c
#define md5_c

static void md5_zero_memory(void* p, size_t sz)
{
    size_t i;
    for (i = 0; i < sz; i += 1) {
        ((unsigned char*)p)[i] = 0;
    }
}

static void md5_copy_memory(void* dst, const void* src, size_t sz)
{
    size_t i;
    for (i = 0; i < sz; i += 1) {
        ((unsigned char*)dst)[i] = ((unsigned char*)src)[i];
    }
}


/* RFC 1321 - Section 3.4. */
#define MD5_F(x, y, z) ((x & y) | (~x &  z))
#define MD5_G(x, y, z) ((x & z) | ( y & ~z))
#define MD5_H(x, y, z) (x ^ y ^ z)
#define MD5_I(x, y, z) (y ^ (x | ~z))

/*
RFC 1321 - Section 2.

    Let X <<< s denote the 32-bit value obtained by circularly shifting (rotating) X left by s bit positions.
*/
#define MD5_ROTATE_LEFT(x, n)   (((x) << (n)) | ((x) >> (32 - (n))))

/*
From appendix in RFC 1321.
*/
#define MD5_FF(a, b, c, d, x, s, ac)                        \
    (a) += MD5_F((b), (c), (d)) + (x) + (unsigned int)(ac), \
    (a)  = MD5_ROTATE_LEFT((a), (s)),                       \
    (a) += (b)

#define MD5_GG(a, b, c, d, x, s, ac)                        \
    (a) += MD5_G((b), (c), (d)) + (x) + (unsigned int)(ac), \
    (a)  = MD5_ROTATE_LEFT((a), (s)),                       \
    (a) += (b)

#define MD5_HH(a, b, c, d, x, s, ac)                        \
    (a) += MD5_H((b), (c), (d)) + (x) + (unsigned int)(ac), \
    (a)  = MD5_ROTATE_LEFT((a), (s)),                       \
    (a) += (b)

#define MD5_II(a, b, c, d, x, s, ac)                        \
    (a) += MD5_I((b), (c), (d)) + (x) + (unsigned int)(ac), \
    (a)  = MD5_ROTATE_LEFT((a), (s)),                       \
    (a) += (b)

#define MD5_S11 7
#define MD5_S12 12
#define MD5_S13 17
#define MD5_S14 22
#define MD5_S21 5
#define MD5_S22 9
#define MD5_S23 14
#define MD5_S24 20
#define MD5_S31 4
#define MD5_S32 11
#define MD5_S33 16
#define MD5_S34 23
#define MD5_S41 6
#define MD5_S42 10
#define MD5_S43 15
#define MD5_S44 21

static void md5_decode(unsigned int* x, const unsigned char* src)
{
    size_t i, j;

    for (i = 0, j = 0; i < 16; i += 1, j += 4) {
        x[i] = ((unsigned int)src[j+0]) | (((unsigned int)src[j+1]) << 8) | (((unsigned int)src[j+2]) << 16) | (((unsigned int)src[j+3]) << 24);
    }
}

/*
This is the main MD5 function. Everything is processed in blocks of 64 bytes.
*/
static void md5_update_block(md5_context* ctx, const unsigned char* src)
{
    unsigned int a;
    unsigned int b;
    unsigned int c;
    unsigned int d;
    unsigned int x[16];

    /* assert(ctx != NULL); */
    /* assert(src != NULL); */

    a = ctx->a;
    b = ctx->b;
    c = ctx->c;
    d = ctx->d;

    md5_decode(x, src);

    MD5_FF(a, b, c, d, x[ 0], MD5_S11, 0xd76aa478);
    MD5_FF(d, a, b, c, x[ 1], MD5_S12, 0xe8c7b756);
    MD5_FF(c, d, a, b, x[ 2], MD5_S13, 0x242070db);
    MD5_FF(b, c, d, a, x[ 3], MD5_S14, 0xc1bdceee);
    MD5_FF(a, b, c, d, x[ 4], MD5_S11, 0xf57c0faf);
    MD5_FF(d, a, b, c, x[ 5], MD5_S12, 0x4787c62a);
    MD5_FF(c, d, a, b, x[ 6], MD5_S13, 0xa8304613);
    MD5_FF(b, c, d, a, x[ 7], MD5_S14, 0xfd469501);
    MD5_FF(a, b, c, d, x[ 8], MD5_S11, 0x698098d8);
    MD5_FF(d, a, b, c, x[ 9], MD5_S12, 0x8b44f7af);
    MD5_FF(c, d, a, b, x[10], MD5_S13, 0xffff5bb1);
    MD5_FF(b, c, d, a, x[11], MD5_S14, 0x895cd7be);
    MD5_FF(a, b, c, d, x[12], MD5_S11, 0x6b901122);
    MD5_FF(d, a, b, c, x[13], MD5_S12, 0xfd987193);
    MD5_FF(c, d, a, b, x[14], MD5_S13, 0xa679438e);
    MD5_FF(b, c, d, a, x[15], MD5_S14, 0x49b40821);

    MD5_GG(a, b, c, d, x[ 1], MD5_S21, 0xf61e2562);
    MD5_GG(d, a, b, c, x[ 6], MD5_S22, 0xc040b340);
    MD5_GG(c, d, a, b, x[11], MD5_S23, 0x265e5a51);
    MD5_GG(b, c, d, a, x[ 0], MD5_S24, 0xe9b6c7aa);
    MD5_GG(a, b, c, d, x[ 5], MD5_S21, 0xd62f105d);
    MD5_GG(d, a, b, c, x[10], MD5_S22, 0x02441453);
    MD5_GG(c, d, a, b, x[15], MD5_S23, 0xd8a1e681);
    MD5_GG(b, c, d, a, x[ 4], MD5_S24, 0xe7d3fbc8);
    MD5_GG(a, b, c, d, x[ 9], MD5_S21, 0x21e1cde6);
    MD5_GG(d, a, b, c, x[14], MD5_S22, 0xc33707d6);
    MD5_GG(c, d, a, b, x[ 3], MD5_S23, 0xf4d50d87);
    MD5_GG(b, c, d, a, x[ 8], MD5_S24, 0x455a14ed);
    MD5_GG(a, b, c, d, x[13], MD5_S21, 0xa9e3e905);
    MD5_GG(d, a, b, c, x[ 2], MD5_S22, 0xfcefa3f8);
    MD5_GG(c, d, a, b, x[ 7], MD5_S23, 0x676f02d9);
    MD5_GG(b, c, d, a, x[12], MD5_S24, 0x8d2a4c8a);

    MD5_HH(a, b, c, d, x[ 5], MD5_S31, 0xfffa3942);
    MD5_HH(d, a, b, c, x[ 8], MD5_S32, 0x8771f681);
    MD5_HH(c, d, a, b, x[11], MD5_S33, 0x6d9d6122);
    MD5_HH(b, c, d, a, x[14], MD5_S34, 0xfde5380c);
    MD5_HH(a, b, c, d, x[ 1], MD5_S31, 0xa4beea44);
    MD5_HH(d, a, b, c, x[ 4], MD5_S32, 0x4bdecfa9);
    MD5_HH(c, d, a, b, x[ 7], MD5_S33, 0xf6bb4b60);
    MD5_HH(b, c, d, a, x[10], MD5_S34, 0xbebfbc70);
    MD5_HH(a, b, c, d, x[13], MD5_S31, 0x289b7ec6);
    MD5_HH(d, a, b, c, x[ 0], MD5_S32, 0xeaa127fa);
    MD5_HH(c, d, a, b, x[ 3], MD5_S33, 0xd4ef3085);
    MD5_HH(b, c, d, a, x[ 6], MD5_S34, 0x04881d05);
    MD5_HH(a, b, c, d, x[ 9], MD5_S31, 0xd9d4d039);
    MD5_HH(d, a, b, c, x[12], MD5_S32, 0xe6db99e5);
    MD5_HH(c, d, a, b, x[15], MD5_S33, 0x1fa27cf8);
    MD5_HH(b, c, d, a, x[ 2], MD5_S34, 0xc4ac5665);

    MD5_II(a, b, c, d, x[ 0], MD5_S41, 0xf4292244);
    MD5_II(d, a, b, c, x[ 7], MD5_S42, 0x432aff97);
    MD5_II(c, d, a, b, x[14], MD5_S43, 0xab9423a7);
    MD5_II(b, c, d, a, x[ 5], MD5_S44, 0xfc93a039);
    MD5_II(a, b, c, d, x[12], MD5_S41, 0x655b59c3);
    MD5_II(d, a, b, c, x[ 3], MD5_S42, 0x8f0ccc92);
    MD5_II(c, d, a, b, x[10], MD5_S43, 0xffeff47d);
    MD5_II(b, c, d, a, x[ 1], MD5_S44, 0x85845dd1);
    MD5_II(a, b, c, d, x[ 8], MD5_S41, 0x6fa87e4f);
    MD5_II(d, a, b, c, x[15], MD5_S42, 0xfe2ce6e0);
    MD5_II(c, d, a, b, x[ 6], MD5_S43, 0xa3014314);
    MD5_II(b, c, d, a, x[13], MD5_S44, 0x4e0811a1);
    MD5_II(a, b, c, d, x[ 4], MD5_S41, 0xf7537e82);
    MD5_II(d, a, b, c, x[11], MD5_S42, 0xbd3af235);
    MD5_II(c, d, a, b, x[ 2], MD5_S43, 0x2ad7d2bb);
    MD5_II(b, c, d, a, x[ 9], MD5_S44, 0xeb86d391);

    ctx->a += a;
    ctx->b += b;
    ctx->c += c;
    ctx->d += d;
    
    /* We'll only ever be calling this if the context's cache is full. At this point the cache will also be empty. */
    ctx->cacheLen = 0;
}

MD5_API void md5_init(md5_context* ctx)
{
    if (ctx == NULL) {
        return;
    }

    md5_zero_memory(ctx, sizeof(*ctx));

    /* RFC 1321 - Section 3.3. */
    ctx->a  = 0x67452301;
    ctx->b  = 0xefcdab89;
    ctx->c  = 0x98badcfe;
    ctx->d  = 0x10325476;
    ctx->sz = 0;
}

MD5_API void md5_update(md5_context* ctx, const void* src, size_t sz)
{
    const unsigned char* bytes = (const unsigned char*)src;
    size_t totalBytesProcessed = 0;

    if (ctx == NULL || (src == NULL && sz > 0)) {
        return;
    }

    /* Keep processing until all data has been exhausted. */
    while (totalBytesProcessed < sz) {
        /* Optimization. Bypass the cache if there's nothing in it and the number of bytes remaining to process is larger than 64. */
        size_t bytesRemainingToProcess = sz - totalBytesProcessed;
        if (ctx->cacheLen == 0 && bytesRemainingToProcess > sizeof(ctx->cache)) {
            /* Fast path. Bypass the cache and just process directly. */
            md5_update_block(ctx, bytes + totalBytesProcessed);
            totalBytesProcessed += sizeof(ctx->cache);
        } else {
            /* Slow path. Need to store in the cache. */
            size_t cacheRemaining = sizeof(ctx->cache) - ctx->cacheLen;
            if (cacheRemaining > 0) {
                /* There's still some room left in the cache. Write as much data to it as we can. */
                size_t bytesToProcess = bytesRemainingToProcess;
                if (bytesToProcess > cacheRemaining) {
                    bytesToProcess = cacheRemaining;
                }

                md5_copy_memory(ctx->cache + ctx->cacheLen, bytes + totalBytesProcessed, bytesToProcess);
                ctx->cacheLen       += (unsigned int)bytesToProcess;    /* Safe cast. bytesToProcess will always be <= sizeof(ctx->cache) which is 64. */
                totalBytesProcessed +=               bytesToProcess;

                /* Update the number of bytes remaining in the cache so we can use it later. */
                cacheRemaining = sizeof(ctx->cache) - ctx->cacheLen;
            }

            /* If the cache is full, get it processed. */
            if (cacheRemaining == 0) {
                md5_update_block(ctx, ctx->cache);
            }
        }
    }

    ctx->sz += sz;
}

MD5_API void md5_finalize(md5_context* ctx, unsigned char* digest)
{
    size_t cacheRemaining;
    unsigned int szLo;
    unsigned int szHi;

    if (digest == NULL) {
        return;
    }

    if (ctx == NULL) {
        md5_zero_memory(digest, MD5_SIZE);
        return;
    }

    /*
    Padding must be applied. First thing to do is clear the cache if there's no room for at least
    one byte. This should never happen, but leaving this logic here for safety.
    */
    cacheRemaining = sizeof(ctx->cache) - ctx->cacheLen;
    if (cacheRemaining == 0) {
        md5_update_block(ctx, ctx->cache);
    }

    /* Now we need to write a byte with the most significant bit set (0x80). */
    ctx->cache[ctx->cacheLen] = 0x80;
    ctx->cacheLen += 1;

    /* If there isn't enough room for 8 bytes we need to padd with zeroes and get the block processed. */
    cacheRemaining = sizeof(ctx->cache) - ctx->cacheLen;
    if (cacheRemaining < 8) {
        md5_zero_memory(ctx->cache + ctx->cacheLen, cacheRemaining);
        md5_update_block(ctx, ctx->cache);
        cacheRemaining = sizeof(ctx->cache);
    }
    
    /* Now we need to fill the buffer with zeros until we've filled 56 bytes (8 bytes left over for the length). */
    md5_zero_memory(ctx->cache + ctx->cacheLen, cacheRemaining - 8);

    szLo = (unsigned int)(((ctx->sz >>  0) & 0xFFFFFFFF) << 3);
    szHi = (unsigned int)(((ctx->sz >> 32) & 0xFFFFFFFF) << 3);
    ctx->cache[56] = (unsigned char)((szLo >>  0) & 0xFF);
    ctx->cache[57] = (unsigned char)((szLo >>  8) & 0xFF);
    ctx->cache[58] = (unsigned char)((szLo >> 16) & 0xFF);
    ctx->cache[59] = (unsigned char)((szLo >> 24) & 0xFF);
    ctx->cache[60] = (unsigned char)((szHi >>  0) & 0xFF);
    ctx->cache[61] = (unsigned char)((szHi >>  8) & 0xFF);
    ctx->cache[62] = (unsigned char)((szHi >> 16) & 0xFF);
    ctx->cache[63] = (unsigned char)((szHi >> 24) & 0xFF);
    md5_update_block(ctx, ctx->cache);

    /* Now write out the digest. */
    digest[ 0] = (unsigned char)(ctx->a >> 0); digest[ 1] = (unsigned char)(ctx->a >> 8); digest[ 2] = (unsigned char)(ctx->a >> 16); digest[ 3] = (unsigned char)(ctx->a >> 24);
    digest[ 4] = (unsigned char)(ctx->b >> 0); digest[ 5] = (unsigned char)(ctx->b >> 8); digest[ 6] = (unsigned char)(ctx->b >> 16); digest[ 7] = (unsigned char)(ctx->b >> 24);
    digest[ 8] = (unsigned char)(ctx->c >> 0); digest[ 9] = (unsigned char)(ctx->c >> 8); digest[10] = (unsigned char)(ctx->c >> 16); digest[11] = (unsigned char)(ctx->c >> 24);
    digest[12] = (unsigned char)(ctx->d >> 0); digest[13] = (unsigned char)(ctx->d >> 8); digest[14] = (unsigned char)(ctx->d >> 16); digest[15] = (unsigned char)(ctx->d >> 24);
}

MD5_API void md5(unsigned char* digest, const void* src, size_t sz)
{
    md5_context ctx;
    md5_init(&ctx);
    {
        md5_update(&ctx, src, sz);
    }
    md5_finalize(&ctx, digest);
}


static void md5_format_byte(char* dst, unsigned char byte)
{
    const char* hex = "0123456789abcdef";
    dst[0] = hex[(byte & 0xF0) >> 4];
    dst[1] = hex[(byte & 0x0F)     ];
}

MD5_API void md5_format(char* dst, size_t dstCap, const unsigned char* hash)
{
    size_t i;

    if (dst == NULL) {
        return;
    }

    if (dstCap < MD5_SIZE_FORMATTED) {
        if (dstCap > 0) {
            dst[0] = '\0';
        }

        return;
    }

    for (i = 0; i < MD5_SIZE; i += 1) {
        md5_format_byte(dst + (i*2), hash[i]);
    }

    /* Always null terminate. */
    dst[MD5_SIZE_FORMATTED-1] = '\0';
}
#endif  /* md5_c */
#endif  /* MD5_IMPLEMENTATION */

/*
This software is available as a choice of the following licenses. Choose
whichever you prefer.

===============================================================================
ALTERNATIVE 1 - Public Domain (www.unlicense.org)
===============================================================================
This is free and unencumbered software released into the public domain.

Anyone is free to copy, modify, publish, use, compile, sell, or distribute this
software, either in source code form or as a compiled binary, for any purpose,
commercial or non-commercial, and by any means.

In jurisdictions that recognize copyright laws, the author or authors of this
software dedicate any and all copyright interest in the software to the public
domain. We make this dedication for the benefit of the public at large and to
the detriment of our heirs and successors. We intend this dedication to be an
overt act of relinquishment in perpetuity of all present and future rights to
this software under copyright law.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

For more information, please refer to <http://unlicense.org/>

===============================================================================
ALTERNATIVE 2 - MIT No Attribution
===============================================================================
Copyright 2022 David Reid

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/
