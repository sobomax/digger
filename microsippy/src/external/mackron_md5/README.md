<h4 align="center">MD5 Hashing</h4>

<p align="center">
    <a href="https://discord.gg/9vpqbjU"><img src="https://img.shields.io/discord/712952679415939085?label=discord&logo=discord" alt="discord"></a>
    <a href="https://twitter.com/mackron"><img src="https://img.shields.io/twitter/follow/mackron?style=flat&label=twitter&color=1da1f2&logo=twitter" alt="twitter"></a>
</p>

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