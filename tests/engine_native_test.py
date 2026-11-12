#!/usr/bin/env python3
"""Run the production JSON reader and Windows argument quoting on Linux.

This complements engine_test.exe; it does not claim to test Win32 process APIs.
The functions are extracted verbatim, rather than reimplemented in the test.
"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "src/engine.c").read_text()
process_source = (root / "src/engine_process.c").read_text()
quoting = process_source[process_source.index("BOOL engine_command_arg("):process_source.index("/* A one-file extractor")]
json = source[source.index("typedef struct { const char *p, *end; } Json;"):source.index("static void utf8_wide(")]
harness = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
typedef int BOOL;
typedef wchar_t WCHAR;
#define TRUE 1
#define FALSE 0
#define COMMAND_CAP 32768
'''+quoting+json+r'''
static int checks, failed;
#define CHECK(x) do { ++checks; if (!(x)) { ++failed; printf("FAIL %d: %s\n",__LINE__,#x); } } while (0)
static BOOL read_string(const char *s, char *out, size_t cap) {
    Json j = {s,s+strlen(s)};
    return json_string(&j,out,cap) && j.p==j.end;
}
static BOOL read_value(const char *s) {
    Json j = {s,s+strlen(s)};
    return skip_value(&j,0) && j.p==j.end;
}
int main(void) {
    char out[128], small[4], deep[100];
    WCHAR command[COMMAND_CAP];
    size_t used=0;
    int i;
    CHECK(read_string("\"caf\\u00e9 \\ud83d\\ude80\"",out,sizeof(out)));
    CHECK(!strcmp(out,"caf\xc3\xa9 \xf0\x9f\x9a\x80"));
    CHECK(read_string("\"a\\\"b\\\\c\\tD\"",out,sizeof(out)));
    CHECK(!strcmp(out,"a\"b\\c D"));
    CHECK(read_string("\"abcdefghijk\"",small,sizeof(small)) && !strcmp(small,"abc"));
    CHECK(!read_string("\"bad\\ud800\"",out,sizeof(out)));
    CHECK(!read_string("\"bad\\udc00\"",out,sizeof(out)));
    CHECK(!read_string("\"bad\\u0000suffix\"",out,sizeof(out)));
    CHECK(!read_string("\"bad\\q\"",out,sizeof(out)));
    CHECK(!read_string("\"unterminated",out,sizeof(out)));
    CHECK(!read_string("\"newline\n\"",out,sizeof(out)));
    CHECK(read_value("{\"nested\":[1,2,{\"k\":false}],\"null\":null}"));
    CHECK(read_value("-1.25e+3"));
    CHECK(!read_value("00"));
    CHECK(!read_value("1x"));
    CHECK(!read_value("1."));
    CHECK(!read_value("1e"));
    CHECK(!read_value("invalid"));
    CHECK(!read_value("{\"a\":1,}"));
    CHECK(!read_value("[1,,2]"));
    CHECK(duration_seconds("96.5","96.5"+4)==96);
    CHECK(duration_seconds("1.234e3","1.234e3"+7)==1234);
    CHECK(duration_seconds("0.001e3","0.001e3"+7)==1);
    CHECK(duration_seconds("0.5","0.5"+3)==0);
    CHECK(duration_seconds("1e-999","1e-999"+6)==0);
    CHECK(duration_seconds("1e999","1e999"+5)==-1);
    CHECK(duration_seconds("2147483647.9","2147483647.9"+12)==2147483647);
    CHECK(duration_seconds("2147483648","2147483648"+10)==-1);
    for(i=0;i<40;++i)deep[i]='[';
    deep[40]='0';
    for(i=41;i<81;++i)deep[i]=']';deep[81]=0;
    CHECK(!read_value(deep));
    CHECK(engine_command_arg(command,&used,L"a b"));
    CHECK(!wcscmp(command,L"\"a b\""));
    CHECK(engine_command_arg(command,&used,L"quote\""));
    CHECK(!wcscmp(command,L"\"a b\" \"quote\\\"\""));
    used=0;
    CHECK(engine_command_arg(command,&used,L"end\\"));
    CHECK(!wcscmp(command,L"\"end\\\\\""));
    used=0;
    CHECK(engine_command_arg(command,&used,L""));
    CHECK(!wcscmp(command,L"\"\""));
    used=COMMAND_CAP-1;
    CHECK(!engine_command_arg(command,&used,L"overflow"));
    printf("%d native parser/quoting checks; %d failed\n",checks,failed);
    return failed?1:0;
}
'''
with tempfile.TemporaryDirectory(prefix="tubeliviano-engine-") as tmp:
    c = Path(tmp) / "test.c"
    exe = Path(tmp) / "test"
    c.write_text(harness)
    subprocess.run(["cc", "-std=c99", "-Wall", "-Wextra", "-Werror", "-Wno-misleading-indentation",
                    "-fsanitize=address,undefined", str(c), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
