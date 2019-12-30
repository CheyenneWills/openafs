/*
 * Copyright (c) 2019 Sine Nomine Associates. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR `AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <afsconfig.h>
#include <afs/param.h>
#include <roken.h>
#include <opr/sockaddr.h>
#include <tests/tap/basic.h>

int
main(int argc, char **argv)
{
    struct in_addr addr;
    afs_uint16 port;
    opr_sockaddr a;
    opr_sockaddr b;
    opr_sockaddr_str astr;
    opr_sockaddr_str bstr;

    plan(5);

    memset(&a, 0, sizeof(a));
    memset(&b, 0, sizeof(b));

    inet_pton(AF_INET, "192.168.1.1", &addr);
    port = htons(7001);
    opr_sockaddr_by_inet(&a, addr, port);

    is_string("192.168.1.1:7001", opr_sockaddr2str(&a, &astr), "sockaddr2str prints 192.168.1.1:7001");

    opr_sockaddr_copy(&b, &a);
    is_string("192.168.1.1:7001", opr_sockaddr2str(&b, &bstr), "sockaddr_copy a -> b");
    ok(opr_sockaddr_equal(&a, &b), "sockaddr_equal when a == b");

    inet_pton(AF_INET, "192.168.1.2", &addr);
    port = htons(7001);
    opr_sockaddr_by_inet(&a, addr, port);

    is_string("192.168.1.2:7001", opr_sockaddr2str(&a, &astr), "sockaddr2str prints 192.168.1.2:7001");
    ok(!opr_sockaddr_equal(&a, &b), "sockaddr_equal when a != b");

    return 0;
}
