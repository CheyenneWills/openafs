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

#include <afs/afsutil.h>
#include <afs/bosint.h>
#include <afs/bnode.h>
#include <tests/tap/basic.h>

#include <bnode_internal.h> /* Required for bosprotoypes.h */
#include <bosprototypes.h>  /* bnode protoptypes are not in bnode.h */

/* Globals and stubs to satisfy bnode.c dependencies. */
const char *bozo_fileName = NULL;
struct ktime bozo_nextRestartKT;
struct ktime bozo_nextDayKT;
int bozo_isrestricted = 0;
int bozo_restdisable = 0;
char *DoCore = NULL;
void bozo_Log(const char *format, ...) { }
void * bozo_ShutdownAndExit(void *param) { return NULL; }
int osi_audit(void ) { return 0; }
int WriteBozoFile(char *aname) { return 0; }


int
main(int argc, char **argv)
{
    int code;
    struct bnode_token *tokens = NULL;

    plan(1);

    code = bnode_ParseLine("", &tokens);
    is_int(0, code, "parse-line");

    return 0;
}
