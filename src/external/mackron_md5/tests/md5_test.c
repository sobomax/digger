#define MD5_IMPLEMENTATION
#include "../md5.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

int do_test(const char* input, const char* expected)
{
    int successful;
    unsigned char hash[MD5_SIZE];
    char hashStr[33];

    md5(hash, input, strlen(input));

    /* Check and compare results. */
    md5_format(hashStr, sizeof(hashStr), hash);

    successful = (strcmp(hashStr, expected) == 0);

    printf("%s = %s : %s\n", input, hashStr, (successful) ? "success" : "failed");

    return successful;
}

int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;

    do_test("",                                                                                 "d41d8cd98f00b204e9800998ecf8427e");
    do_test("a",                                                                                "0cc175b9c0f1b6a831c399e269772661");
    do_test("abc",                                                                              "900150983cd24fb0d6963f7d28e17f72");
    do_test("message digest",                                                                   "f96b697d7cb7938d525a2f31aaf161d0");
    do_test("abcdefghijklmnopqrstuvwxyz",                                                       "c3fcd3d76192e4007dfb496cca67e13b");
    do_test("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789",                   "d174ab98d277d9f5a5611c2c9f419d9f");
    do_test("12345678901234567890123456789012345678901234567890123456789012345678901234567890", "57edf4a22be3c955ac49da2e2107b67a");

    return 0;
}
