/*
 * Copyright (c) 2021 Sine Nomine Associates. All rights reserved.
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

#include <tests/tap/basic.h>
#include <stdlib.h>
#include <string.h>

#include "prname.h"

int
main(void)
{
    char name[64], junk[64];
    int ret, len, i;

    srand(0);
    for (i = 0; i < 64; i++) {
	/* initialize this buffer with junk */
	junk[i] = rand() % 256;
    }

    plan(18);

    /* regular user */
    memcpy(name, junk, sizeof(junk));
    strcpy(name, "foo");
    len = strlen(name);
    ret = pr_IsNameBlank(name, len);
    ok(ret == 0, "Regular user");

    /* long regular user */
    memcpy(name, junk, sizeof(junk));
    memset(name, 'a', sizeof(name));
    len = sizeof(name);
    ret = pr_IsNameBlank(name, len);
    ok(ret == 0, "Regular user (64 characters)");

    /* foreign user */
    memcpy(name, junk, sizeof(junk));
    strcpy(name, "foo@bar");
    len = strlen(name);
    ret = pr_IsNameBlank(name, len);
    ok(ret == 0, "Foreign user");

    /* empty user */
    memcpy(name, junk, sizeof(junk));
    strcpy(name, "");
    len = strlen(name);
    ret = pr_IsNameBlank(name, len);
    ok(ret == 1, "Empty user (0 blank characters)");

    /* empty user */
    memcpy(name, junk, sizeof(junk));
    strcpy(name, "   ");
    len = strlen(name);
    ret = pr_IsNameBlank(name, len);
    ok(ret == 1, "Empty user (3 blank characters)");

    /* long empty user */
    memcpy(name, junk, sizeof(junk));
    memset(name, ' ', sizeof(name));
    len = sizeof(name);
    ret = pr_IsNameBlank(name, len);
    ok(ret == 1, "Empty user (64 blank characters)");

    /* empty foreign user */
    memcpy(name, junk, sizeof(junk));
    strcpy(name, "@bar");
    len = strlen(name);
    ret = pr_IsNameBlank(name, len);
    ok(ret == 1, "Empty foreign user (0 blank characters)");

    /* empty foreign user */
    memcpy(name, junk, sizeof(junk));
    strcpy(name, "     @bar");
    len = strlen(name);
    ret = pr_IsNameBlank(name, len);
    ok(ret == 1, "Empty foreign user (5 blank characters)");

    /* regular group */
    memcpy(name, junk, sizeof(junk));
    strcpy(name, "owner:foo");
    len = strlen(name);
    ret = pr_IsNameBlank(name, len);
    ok(ret == 0, "Regular group");

    /* foreign group */
    memcpy(name, junk, sizeof(junk));
    strcpy(name, "owner:foo@bar");
    len = strlen(name);
    ret = pr_IsNameBlank(name, len);
    ok(ret == 0, "Foreign group");

    /* empty group */
    memcpy(name, junk, sizeof(junk));
    strcpy(name, "owner:");
    len = strlen(name);
    ret = pr_IsNameBlank(name, len);
    ok(ret == 1, "Empty group (0 blank characters)");

    /* empty group */
    memcpy(name, junk, sizeof(junk));
    strcpy(name, "owner:   ");
    len = strlen(name);
    ret = pr_IsNameBlank(name, len);
    ok(ret == 1, "Empty group (3 blank characters)");

    /* empty foreign group */
    memcpy(name, junk, sizeof(junk));
    strcpy(name, "owner:@bar");
    len = strlen(name);
    ret = pr_IsNameBlank(name, len);
    ok(ret == 1, "Empty foreign group (0 blank characters)");

    /* empty foreign group */
    memcpy(name, junk, sizeof(junk));
    strcpy(name, "owner:     @bar");
    len = strlen(name);
    ret = pr_IsNameBlank(name, len);
    ok(ret == 1, "Empty foreign group (5 blank characters)");

    /* malformed name */
    memcpy(name, junk, sizeof(junk));
    strcpy(name, "user@cell:with:colon");
    len = strlen(name);
    ret = pr_IsNameBlank(name, len);
    ok(ret == 0, "Malformed name (colon)");

    /* malformed name */
    memcpy(name, junk, sizeof(junk));
    strcpy(name, "@cell:with:colon");
    len = strlen(name);
    ret = pr_IsNameBlank(name, len);
    ok(ret == 1, "Empty malformed name (colon)");

    /* malformed name */
    len = 23;
    memcpy(name, junk, sizeof(junk));
    memcpy(name, "user@cell\0with:trailing", len);
    ret = pr_IsNameBlank(name, len);
    ok(ret == 0, "Malformed name (trailing)");

    /* malformed name */
    len = 22;
    memcpy(name, junk, sizeof(junk));
    memcpy(name, "   @cell\0with:trailing", len);
    ret = pr_IsNameBlank(name, len);
    ok(ret == 1, "Empty malformed name (trailing)");

    return 0;
}
