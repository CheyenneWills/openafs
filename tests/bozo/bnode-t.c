/*
 * Copyright 2021, Sine Nomine Associates and others.
 * All Rights Reserved.
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
#include <opr/queue.h>

#include <afs/afsutil.h>
#include <tests/tap/basic.h>

#include "afs/bnode.h"
#include "bnode_internal.h"
#include "bosprototypes.h"

char *test1str = "one two   three four //abc 1234 -xyzzy";
char *test1toks[] = {
    "one",
    "two",
    "three",
    "four",
    "//abc",
    "1234",
    "-xyzzy"
};
#define NTOKENS 7
void
test_bnode_ParseLine(void)
{
    int code;
    int i;
    struct bnode_token *toklist, *tok;

    /* Ensure empty strings are handled */
    toklist = (struct bnode_token *)-1;
    code = bnode_ParseLine("", &toklist);
    is_int(-1, code, "empty string returns -1");
    ok(toklist == NULL, "empty string sets toklist to NULL");

    toklist = NULL;
    code = bnode_ParseLine(test1str, &toklist);
    is_int(0, code, "none empty string returned 1");
    ok(toklist != NULL, "none empty string sets toklist to value");

    for (i=0, tok=toklist; tok != NULL && i < NTOKENS; tok = tok-> next, i++) {
	is_string(test1toks[i], tok->key, "token matchs");
    }
    is_int(NTOKENS, i, "all tokens matched");
    ok(tok == NULL, "no extra tokens");
    bnode_FreeTokens(toklist);
}
int
main(int argc, char **argv)
{
    plan(1293);
    test_bnode_ParseLine();
    return 0;
}
