/*
 * Copyright 2021, Sine Nomine Associates
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

/*
 * cmd_Join() tests.
 */

#include <afsconfig.h>
#include <afs/param.h>
#include <roken.h>
#include <afs/cmd.h>
#include <tests/tap/basic.h>

#define MAX_TEST_ARGC 15

struct join_test_case {
    char *name;
    char *argv[MAX_TEST_ARGC];
    char *line;
} join_test_cases[] =
{
    {
	.name = "empty argv",
	.argv = {NULL},
	.line = "",
    },
    {
	.name = "empty argument",
	.argv = {"", NULL},
	.line = "''",
    },
    {
	.name = "empty arguments",
	.argv = {"", "", "", NULL},
	.line = "'' '' ''",
    },
    {
	.name = "space arguments",
	.argv = {" ", "  ", NULL},
	.line = "' ' '  '",
    },
    {
	.name = "whitespace arguments",
	.argv = {" ", "\t", "\n", NULL},
	.line = "' ' '\t' '\n'",
    },
    {
	.name = "normal arguments",
	.argv = {"hello", "world", NULL},
	.line = "hello world",
    },
    {
	.name = "normal arguments with punctuation",
	.argv = {"hello,", "world!", NULL},
	.line = "hello, 'world!'",
    },
    {
	.name = "normal arguments with more punctuation",
	.argv = {"testing:", "one", "two", "three?", NULL},
	.line = "testing: one two 'three?'",
    },
    {
	.name = "args with spaces and 4 more args",
	.argv = {"args with spaces", "and", "4", "more", "args", NULL},
	.line = "'args with spaces' and 4 more args",
    },
    {
	.name = "args with leading spaces and trailing spaces",
	.argv = {"  args with leading spaces", "and trailing spaces ", NULL},
	.line = "'  args with leading spaces' 'and trailing spaces '",
    },
    {
	.name = "arg with single quotes",
	.argv = {"'Not all those who wander are lost' - Tolkien", NULL},
	.line = "''\"'\"'Not all those who wander are lost'\"'\"' - Tolkien'",
    },
    {
	.name = "arg with double quotes",
	.argv = {"\"Not all those who wander are lost\" - Tolkien", NULL},
	.line = "'\"Not all those who wander are lost\" - Tolkien'",
    },
    {
	.name = "one long arg with spaces",
	.argv = {"this\\ is\\ one\\ long\\ arg", NULL},
	.line = "'this\\ is\\ one\\ long\\ arg'",
    },
    {
	.name = "one short arg and one long arg with spaces",
	.argv = {"this", "is\\ two\\ args", NULL},
	.line = "this 'is\\ two\\ args'",
    },
    {
	.name = "arg with one single quote",
	.argv = {"dont't", "worry,", "be", "happy", NULL},
	.line = "'dont'\"'\"'t' worry, be happy",
    },
    {
	.name = "arg with escaped single quote",
	.argv = {"dont\\'t", "worry,", "be", "happy", NULL},
	.line = "'dont\\'\"'\"'t' worry, be happy",
    },
    {
	.name = "args with unquoted single quotes",
	.argv = {"'not", "quoted'", NULL},
	.line = "''\"'\"'not' 'quoted'\"'\"''",
    },
    {
	.name = "arg with escaped double quote",
	.argv = {"don\"t worry,", "be", "happy", NULL},
	.line = "'don\"t worry,' be happy",
    },
    {
	.name = "arg with two single quotes",
	.argv = {"with 'single' quotes", NULL},
	.line = "'with '\"'\"'single'\"'\"' quotes'",
    },
    {
	.name = "arg with escaped double quotes",
	.argv = {"with escaped \"double\" quotes", NULL},
	.line = "'with escaped \"double\" quotes'",
    },
    {
	.name = "arg with single quotes and escaped double quotes",
	.argv = {"with 'single' and escaped \\\"double\\\" quotes", NULL},
	.line = "'with '\"'\"'single'\"'\"' and escaped \\\"double\\\" quotes'",
    },
    {
	.name = "arg with escaped double quotes",
	.argv = {"with escaped \\\"double\\\" quotes", NULL},
	.line = "'with escaped \\\"double\\\" quotes'",
    },
    {
	.name = "arg with quote-escaped single quotes",
	.argv = {"single with quote-escaped \"'\"single\"'\" quotes", NULL},
	.line = "'single with quote-escaped \"'\"'\"'\"single\"'\"'\"'\" quotes'",
    },
    {
	.name = "arg with quote-escaped double quotes",
	.argv = {"\"Not all those who wander are lost\" - Tolkien", NULL},
	.line = "'\"Not all those who wander are lost\" - Tolkien'",
    },
    {
	.name = "arg with quote-escaped double quotes and more args",
	.argv = {"\"Not all those who wander are lost\"", "-", "Tolkien", NULL},
	.line = "'\"Not all those who wander are lost\"' - Tolkien",
    },
    {0}
};

static int
count_args(char **argv)
{
    int i;

    for (i = 0; argv[i] != NULL; i++)
	;  /* empty */
    return i;
}

void
test_join(void)
{
    struct join_test_case *t;
    int argc;
    int code;
    char *line = NULL;

    for (t = join_test_cases; t->line != NULL; t++) {
	argc = count_args(t->argv);
	code = cmd_Join(argc, t->argv, &line);
	is_int(code, 0, "cmd_Join succeeds: %s", t->name);
	is_string(line, t->line, ".. line matches");
	free(line);
	line = NULL;
    }
}

int
main(int argc, char **argv)
{
    plan(50);
    test_join();
    return 0;
}
