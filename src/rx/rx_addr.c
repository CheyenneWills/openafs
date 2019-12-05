/*
 * Copyright (c) 2014-2019, Sine Nomine Associates
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ``AS
 * IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDERS OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <afsconfig.h>
#include <afs/param.h>

#ifdef KERNEL
# include "afs/sysincludes.h"
# include "afsincludes.h"
#else
# include <roken.h>
#endif

#include "rx.h"

/**
 * Format an IPV4 network address to a string ddd.ddd.ddd.ddd
 *
 * @param[in]  addr     IPV4 network address in network byte order
 * @param[out] buf      format work buffer
 *
 * @returns pointer to buffer with string representation of the network address
 */
char *
rx_inet2str(afs_uint32 addr, struct rx_inet_fmtbuf *buf)
{
    afs_uint32 taddr;

    taddr = ntohl(addr);
    snprintf(buf->buffer, sizeof(buf->buffer), "%u.%u.%u.%u",
	     (taddr >> 24) & 0xff,
	     (taddr >> 16) & 0xff,
	     (taddr >> 8) & 0xff,
	     (taddr) & 0xff);
    return buf->buffer;
}

char *
rx_sockaddr2str(struct rx_sockaddr *sa, struct rx_sockaddr_fmtbuf *buf)
{
    /* TODO */
    return buf->buffer;
}

/**
 * Return true if a has the same value as b.
 *
 * @param[in] a  sockaddr to compare
 * @param[in] b  sockaddr to compare
 */
int
rx_sockaddr_equal(struct rx_sockaddr *a, struct rx_sockaddr *b)
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
rx_sockaddr_copy(struct rx_sockaddr *dst, struct rx_sockaddr *src)
{
    /* TODO */
    return 0;
}
