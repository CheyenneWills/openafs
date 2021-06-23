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
 * cmd_Tokenize() and cmd_Split() tests.
 */

#include <afsconfig.h>
#include <afs/param.h>
#include <roken.h>
#include <afs/cmd.h>
#include <tests/tap/basic.h>

#define MAX_TEST_ARGC 15

struct test_case {
    char *line;
    char *argv[MAX_TEST_ARGC + 1];
};

static struct test_case valid_lines[] = {
    /* Empty and blank strings. */
    {
	.line = "",
	.argv = {NULL}
    },
    {
	.line = "    ",
	.argv = {NULL}
    },
    {
	.line = "\t\n",
	.argv = {NULL}
    },
    /* Tokens separated with whitespace and no quotes. */
    {
	.line = "hello world",
	.argv = {"hello", "world", NULL}
    },
    {
	.line = "hello, world!",
	.argv = {"hello,", "world!", NULL}
    },
    {
	.line = "testing: one two   three",
	.argv = {"testing:", "one", "two", "three", NULL}
    },
    {
	.line = "tabs\tand newlines\nare whitespace",
	.argv = {"tabs", "and", "newlines", "are", "whitespace", NULL}
    },
    /* Simple quotes. */
    {
	.line = "'single quotes with spaces' and 4 more args",
	.argv = {"single quotes with spaces", "and", "4", "more", "args", NULL}
    },
    {
	.line = "\"double quotes with spaces\" and 4 more args",
	.argv = {"double quotes with spaces", "and", "4", "more", "args", NULL}
    },
    {
	.line = "unquoted args 'followed by quoted'",
	.argv = {"unquoted", "args", "followed by quoted", NULL}
    },
    {
	.line = "unquoted args \"followed by double quoted\"",
	.argv = {"unquoted", "args", "followed by double quoted", NULL}
    },
    {
	.line = "\"Not all those who wander are lost\" - Tolkien",
	.argv = {"Not all those who wander are lost", "-", "Tolkien", NULL}
    },
    /* Escaped spaces. */
    {
	.line = "this\\ is\\ one\\ arg",
	.argv = {"this is one arg", NULL}
    },
    {
	.line = "this is\\ two\\ args",
	.argv = {"this", "is two args", NULL}
    },
    /* Escaped single quotes. */
    {
	.line = "dont\\'t worry, be happy",
	.argv = {"dont't", "worry,", "be", "happy", NULL}
    },
    {
	.line = "\\'not quoted\\'",
	.argv = {"'not", "quoted'", NULL}
    },
    /* Embedded quote characters. */
    {
	.line = "\"don't worry,\" be happy",
	.argv = {"don't worry,", "be", "happy", NULL}
    },
    {
	.line = "don\"'\"t' 'worry, be happy",
	.argv = {"don't worry,", "be", "happy", NULL}
    },
    /* Quotes characters are modal. */
    {
	.line = "this is three' 'args",
	.argv = {"this", "is", "three args", NULL}
    },
    {
	.line = "this is t'hree arg's",
	.argv = {"this", "is", "three args", NULL}
    },
    {
	.line = "this is three\" \"args",
	.argv = {"this", "is", "three args", NULL}
    },
    {
	.line = "this is t\"hree arg\"s",
	.argv = {"this", "is", "three args", NULL}
    },
    /* Nested quotes. */
    {
	.line = "\"double with 'single' quotes\"",
	.argv = {"double with 'single' quotes", NULL}
    },
    {
	.line = "\"double with escaped \\\"double\\\" quotes\"",
	.argv = {"double with escaped \"double\" quotes", NULL}
    },
    {
	.line = "\"double with 'single' and escaped \\\"double\\\" quotes\"",
	.argv = {"double with 'single' and escaped \"double\" quotes", NULL}
    },
    {
	.line = "'single with escaped \\\"double\\\" quotes'",
	.argv = {"single with escaped \\\"double\\\" quotes", NULL}
    },
    {
	.line = "'single with quote-escaped \"'\"single\"'\" quotes'",
	.argv = {"single with quote-escaped \"single\" quotes", NULL}
    },
    {
	.line = "'\"Not all those who wander are lost\" - Tolkien'",
	.argv = {"\"Not all those who wander are lost\" - Tolkien", NULL}
    },
    {
	.line = "\"\\\"Not all those who wander are lost\\\" - Tolkien\"",
	.argv = {"\"Not all those who wander are lost\" - Tolkien", NULL}
    },
    {0}
};

static struct test_case no_closing_quotes[] = {
    {.line = "'"},
    {.line = "\""},
    {.line = "'missing closing single quote"},
    {.line = "missing closing 'single quote"},
    {.line = "missing closing single quote'"},
    {.line = "\"missing closing double quote"},
    {.line = "'\"\"missing closing single quote"},
    {.line = "'backslashes are \\'literals\\' in single quotes'"},
    {0}
};

static struct test_case no_escaped_character[] = {
    {.line = "\\"},
    {.line = "a character must follow a backslash\\"},
    {0}
};

static char*
sanitize(const char *text)
{
    char *sanitized = bstrdup(text);
    char *p;

    for (p = sanitized; *p != '\0'; p++) {
	if (*p == '\n' || *p == '\r')
	    *p = ' ';
    }
    return sanitized;
}

static int
count_args(char **argv)
{
    int i;

    for (i = 0; argv[i] != NULL; i++)
	;  /* empty */
    return i;
}

static void
is_argv(int got_argc, char **got_argv, char **expected_argv, char *format, ...)
{
    va_list ap;
    int success;
    int i;
    int expected_argc = count_args(expected_argv);

    /* Check the argument count. */
    if (got_argc != expected_argc) {
	diag("argc mismatch");
	diag("     got: %d", got_argc);
	diag("expected: %d", expected_argc);
	success = 0;
	goto done;
    }

    /* Check each argument. */
    for (i = 0; i <= got_argc; i++) {
	char *got = got_argv[i];
	char *expected = expected_argv[i];

	if (expected != NULL) {
	    if (got == NULL) {
		diag("argv[%d] mismatch", i);
		diag("     got: NULL");
		diag("expected: %s", expected);
		success = 0;
		goto done;
	    }
	    if (strcmp(got, expected) != 0) {
		diag("argv[%d] mismatch", i);
		diag("     got: %s", got);
		diag("expected: %s", expected);
		success = 0;
		goto done;
	    }
	} else {
	    /* Check for terminating NULL. */
	    if (got != NULL) {
		diag("argv[%d] mismatch", i);
		diag("     got: %s", got);
		diag("expected: NULL");
		success = 0;
		goto done;
	    }
	}
    }
    success = 1;

  done:
    va_start(ap, format);
    okv(success, format, ap);
    va_end(ap);
}

struct tokens {
    int got_argc;
    char *got_argv[MAX_TEST_ARGC + 1];
};

static int
append_token(char *token, void *rock)
{
    struct tokens *tokens = rock;

    if (tokens->got_argc >= MAX_TEST_ARGC)
	bail("Exceeded number of test tokens");
    tokens->got_argv[tokens->got_argc++] = token;
    return 0;
}

static void
free_tokens(struct tokens *tokens)
{
    int i;

    for (i = 0; i < sizeof(tokens->got_argv)/sizeof(*tokens->got_argv); i++) {
	free(tokens->got_argv[i]);
    }
}

static void
test_tokenize_valid_lines(void)
{
    struct test_case *t;
    struct tokens tokens;
    int code;
    char *line;

    for (t = valid_lines; t->line != NULL; t++) {
	line = sanitize(t->line);
	memset(&tokens, 0, sizeof(tokens));
	code = cmd_Tokenize(t->line, append_token, &tokens);
	is_int(0, code, "cmd_Tokenize succeeds: %s", line);
	if (code != 0) {
	    skip(".. skipping argv check; cmd_Tokenize failed");
	} else {
	    is_argv(tokens.got_argc, tokens.got_argv, t->argv,
		    ".. argv matches: %s", line);
	}
	free_tokens(&tokens);
	free(line);
    }
}

static void
test_tokenize_no_closing_quotes(void)
{
    struct test_case *t;
    int code;
    char *line;

    for (t = no_closing_quotes; t->line != NULL; t++) {
	line = sanitize(t->line);
	code = cmd_Tokenize(t->line, NULL, NULL);
	is_int(CMD_NOCLOSINGQUOTE, code,
	    "cmd_Tokenize fails with CMD_NOCLOSINGQUOTE when the closing quote "
	    "is missing: %s", line);
	free(line);
    }
    return;
}

static void
test_tokenize_no_escaped_character(void)
{
    struct test_case *t;
    int code;

    for (t = no_escaped_character; t->line != NULL; t++) {
	code = cmd_Tokenize(t->line, NULL, NULL);
	is_int(CMD_NOESCAPEDCHAR, code,
	    "cmd_Tokenize fails with CMD_NOESCAPEDCHAR when no character "
	    "follows a backslash");
    }
}


void
test_split_valid_lines(void)
{
    struct test_case *t;
    int code;
    char *line;
    int argc;
    char **argv;

    for (t = valid_lines; t->line != NULL; t++) {
	line = sanitize(t->line);
	argc = 0;
	argv = NULL;
	code = cmd_Split(t->line, &argc, &argv);
	is_int(0, code, "cmd_Split succeeds: %s", line);
	if (code != 0) {
	    skip(".. skipping argv check; cmd_Split failed");
	} else {
	    is_argv(argc, argv, t->argv, ".. argv matches: %s", line);
	}
	cmd_FreeSplit(&argv);
	ok(argv == NULL, ".. cmd_FreeSplit set argv to NULL");
	free(line);
    }
}

static void
test_split_no_closing_quotes(void)
{
    struct test_case *t;
    int code;
    char *line;
    int argc = 0;
    char **argv;

    for (t = no_closing_quotes; t->line != NULL; t++) {
	line = sanitize(t->line);
	argv = NULL;
	code = cmd_Split(t->line, &argc, &argv);
	is_int(CMD_NOCLOSINGQUOTE, code,
	    "cmd_Split fails with CMD_NOCLOSINGQUOTE when the closing quote "
	    "is missing: %s", line);
	ok(argv == NULL, ".. argv is NULL after cmd_Split fails");
	free(line);
    }
}

static void
test_split_no_escaped_character(void)
{
    struct test_case *t;
    int code;
    char *line;
    int argc = 0;
    char **argv;

    for (t = no_escaped_character; t->line != NULL; t++) {
	line = sanitize(t->line);
	argv = NULL;
	code = cmd_Split(t->line, &argc, &argv);
	is_int(CMD_NOESCAPEDCHAR, code,
	    "cmd_Split fails with CMD_NOESCAPEDCHAR when no character "
	    "follows a backslash: %s", line);
	ok(argv == NULL, ".. argv is NULL after cmd_Split fails");
	free(line);
    }
}

int
main(int argc, char **argv)
{
    plan(175);

    test_tokenize_valid_lines();
    test_tokenize_no_closing_quotes();
    test_tokenize_no_escaped_character();

    test_split_valid_lines();
    test_split_no_closing_quotes();
    test_split_no_escaped_character();

    return 0;
}
