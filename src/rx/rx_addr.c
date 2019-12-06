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

#include <rx/rx.h>

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
