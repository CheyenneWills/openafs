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
#include "sockaddr.h"

/*
 * Supports only AF_INET (IPv4) at this time.
 */

char *
opr_sockaddr2str(struct opr_sockaddr *addr, struct opr_sockaddr_str *str)
{
    afs_uint32 taddr = ntohl(addr->u.in.sin_addr.s_addr);
    afs_uint16 tport = ntohs(addr->u.in.sin_port);

    snprintf(str->buffer, sizeof(str->buffer), "%u.%u.%u.%u:%hu",
	     (taddr >> 24) & 0xff,
	     (taddr >> 16) & 0xff,
	     (taddr >> 8) & 0xff,
	     (taddr) & 0xff,
	     tport);
    return str->buffer;
}

/**
 * Return true if a has the same value as b.
 *
 * @param[in] a  sockaddr to compare
 * @param[in] b  sockaddr to compare
 */
int
opr_sockaddr_equal(struct opr_sockaddr *a, struct opr_sockaddr *b)
{
    return a->u.in.sin_addr.s_addr == b->u.in.sin_addr.s_addr
	   && a->u.in.sin_port == b->u.in.sin_port;
}

/**
 * Copy a sockaddr.
 *
 * @param[out] dst destination address
 * @param[in] src source address
 */
int
opr_sockaddr_copy(struct opr_sockaddr *dst, struct opr_sockaddr *src)
{
    dst->u.in.sin_family = AF_INET;
    dst->u.in.sin_addr.s_addr = src->u.in.sin_addr.s_addr;
    dst->u.in.sin_port = src->u.in.sin_port;
    return 0;
}
