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

#include <rx/rx.h>

#define UBIK_INTERNALS
#include "ubik.h"

/**
 * Format a string representation of ubik server interface for logging.
 *
 * @param[in]  ts      pointer to a struct ubik_server (network byte order)
 * @param[in]  iface_index Index for interface
 * @param[out] buf     format work buffer
 *
 * @returns pointer to buffer with string representation of <netaddr>
 */
char *
ubik_ServerInterface2str(struct ubik_server *ts, int iface_index,
			 struct rx_inet_fmtbuf *buf)
{
    return rx_inet2str(ts->addr[iface_index], buf);
}

/**
 * Format a string representation of an ubik interface for logging.
 *
 * @param[in]  addr    pointer to a UbikInterfaceAddr (elements
 *                     are in host byte order)
 * @param[in]  iface_index Index for interface
 * @param[out] buf     format work buffer
 *
 * @returns pointer to buffer with string representation of <netaddr>
 */
char *
ubik_InterfaceAddr2str(UbikInterfaceAddr *addr, int iface_index,
		       struct rx_inet_fmtbuf *buf)
{
    return rx_inet2str(htonl(addr->hostAddr[iface_index]), buf);
}
