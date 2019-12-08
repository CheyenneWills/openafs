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

#ifndef OPENAFS_OPR_SOCKADDR_H
#define OPENAFS_OPR_SOCKADDR_H 1

#include <netinet/in.h>

struct opr_sockaddr {
    union {
	struct sockaddr sa;
	struct sockaddr_in in;
#ifdef HAVE_IPV6
	struct sockaddr_in6 in6;
#endif
	struct sockaddr_storage ss;
    } u;
};

struct opr_sockaddr_str {
    char buffer[23]; /**< ipv4addr:port ddd.ddd.ddd.ddd:dddddz */
};

extern char *opr_sockaddr2str(struct opr_sockaddr *sa, struct opr_sockaddr_str *str);
extern int opr_sockaddr_equal(struct opr_sockaddr *a, struct opr_sockaddr *b);
extern int opr_sockaddr_copy(struct opr_sockaddr *dst, struct opr_sockaddr *src);

#endif
