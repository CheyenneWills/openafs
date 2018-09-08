/*
 * Copyright (c) 2018, Sine Nomine Associates
 * All Rights Reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   1. Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *   2. Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in
 *      the documentation and/or other materials provided with the
 *      distribution.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <afsconfig.h>
#include <afs/param.h>

#include <stdio.h>
#include <afs/afsutil.h>
#include <afs/afsutil_prototypes.h>
#include <afs/tabular_output.h>
#include <tests/tap/basic.h>

int Type = UTIL_T_TYPE_ASCII;
int numColumns = 2;
char *ColumnHeaders[] = {"column1", "column2"};
int ColumnContentTypes[] = {UTIL_T_CONTENTTYPE_STRING, UTIL_T_CONTENTTYPE_STRING};
int ColumnWidths[] = {12, 12};
int sortByColumn = 0;
char *Contents[] = {"test1", "test2"};

struct testcase {
    int header;
    int footer;
    int rows;
} testcases[] = {
    {0, 0, 0},
    {1, 0, 0},
    {0, 1, 0},
    {1, 1, 0},

    {0, 0, 1},
    {1, 0, 1},
    {0, 1, 1},
    {1, 1, 1},

    {0, 0, 11},
    {1, 0, 11},
    {0, 1, 11},
    {1, 1, 11},
};


int
test_free_table(int header, int footer, int rows)
{
    int code = 0;
    int pass = 1;
    struct util_Table *table;
    int i;

    table = util_newTable(Type, numColumns, ColumnHeaders, ColumnContentTypes, ColumnWidths, sortByColumn);
    if (!table) {
        diag("unable to allocate table");
        return 0;
    }
    if (header) {
        code = util_setTableHeader(table, Contents);
        if (code) {
            diag("util_setTableHeader(): code=%d", code);
            pass = 0;
        }
    }
    for (i = 0; i < rows; i++) {
        code = util_addTableBodyRow(table, Contents);
        if (code != i) {
            diag("util_addTableBodyRow: code=%d", code);
            pass = 0;
        }
    }
    if (footer) {
        code = util_setTableFooter(table, Contents);
        if (code) {
            diag("util_setTableFooter(): code=%d", code);
            pass = 0;
        }
    }
    code = util_freeTable(table);
    if (code) {
        diag("util_freeTable(): code=%d", code);
        pass = 0;
    }

    return pass;
}

int
main(void)
{
    int i;
    int pass;
    struct testcase *tc;
    int num_tests = sizeof(testcases)/sizeof(*testcases);

    plan(num_tests);

    for (i = 0; i < num_tests; i++) {
	tc = testcases + i;
	pass = test_free_table(tc->header, tc->footer, tc->rows);
	ok(pass, "table free test %d", i);
    }

    return 0;
}
