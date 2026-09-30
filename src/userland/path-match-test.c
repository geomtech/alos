#include <fnmatch.h>
#include <libgen.h>
#include <locale.h>
#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(x) do { if (!(x)) { printf("path-match-test FAIL %d: %s\n", __LINE__, #x); failures++; } } while (0)

struct match_case {
    const char* pattern;
    const char* string;
    int flags;
    int result;
};

static void path_case(const char* path, const char* base, const char* directory) {
    char first[128], second[128];
    CHECK(strlen(path) < sizeof(first));
    strcpy(first, path);
    strcpy(second, path);
    CHECK(!strcmp(basename(first), base));
    CHECK(!strcmp(dirname(second), directory));
}

int main(void) {
    CHECK(setlocale(LC_CTYPE, "C.UTF-8") != NULL);
    const struct match_case cases[] = {
        {"", "", 0, 0},
        {"", "x", 0, FNM_NOMATCH},
        {"*.cc", "file.cc", FNM_NOESCAPE, 0},
        {"*.cc", "file.h", FNM_NOESCAPE, FNM_NOMATCH},
        {"a?c", "abc", 0, 0},
        {"a?c", "ac", 0, FNM_NOMATCH},
        {"a*c", "abbbc", 0, 0},
        {"a*c", "ac", 0, 0},
        {"a\\*b", "a*b", 0, 0},
        {"a\\?b", "a?b", 0, 0},
        {"a\\*b", "a*b", FNM_NOESCAPE, FNM_NOMATCH},
        {"a\\*b", "a\\zzz b", FNM_NOESCAPE, 0},
        {"*", "a/b", 0, 0},
        {"*", "a/b", FNM_PATHNAME, FNM_NOMATCH},
        {"a/?", "a/b", FNM_PATHNAME, 0},
        {"a/?", "a//", FNM_PATHNAME, FNM_NOMATCH},
        {"a/*/c", "a/b/c", FNM_PATHNAME, 0},
        {"a/*/c", "a/b/d/c", FNM_PATHNAME, FNM_NOMATCH},
        {"*", ".hidden", FNM_PERIOD, FNM_NOMATCH},
        {".*", ".hidden", FNM_PERIOD, 0},
        {"[.]hidden", ".hidden", FNM_PERIOD, FNM_NOMATCH},
        {"a/*", "a/.hidden", FNM_PATHNAME | FNM_PERIOD, FNM_NOMATCH},
        {"a/.*", "a/.hidden", FNM_PATHNAME | FNM_PERIOD, 0},
        {"[a-c]", "b", 0, 0},
        {"[a-c]", "d", 0, FNM_NOMATCH},
        {"[!a-c]", "d", 0, 0},
        {"[^a-c]", "b", 0, FNM_NOMATCH},
        {"[]a]", "]", 0, 0},
        {"[-a]", "-", 0, 0},
        {"[a-]", "-", 0, 0},
        {"[abc", "[abc", 0, 0},
        {"[[:digit:]]", "7", 0, 0},
        {"[[:alpha:]]", "A", 0, 0},
        {"[[:space:]]", "\t", 0, 0},
        {"[[:unknown:]]", "x", 0, FNM_NOMATCH},
        {"?", "\xc3\xa9", 0, 0},
        {"??", "\xc3\xa9", 0, FNM_NOMATCH},
        {"\xc3\xa9*", "\xc3\xa9toile", 0, 0},
        {"*?", "x\xc3\xa9", 0, 0},
        {"[\xc3\xa9-\xc3\xaa]", "\xc3\xaa", 0, 0},
        {"?", "\xf0\x9f\x98\x80", 0, 0},
        {"?", "\xff", 0, FNM_NOMATCH},
        {"?", "\xc3", 0, FNM_NOMATCH},
        {"\xff", "x", 0, FNM_NOMATCH},
        /* Musl permits malformed bytes covered entirely by a '*' span. */
        {"*x", "\xffx", 0, 0},
        {"Case", "case", 0, FNM_NOMATCH},
        {"*", "anything", 0x10, FNM_NOMATCH},
    };
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        int result = fnmatch(cases[i].pattern, cases[i].string, cases[i].flags);
        if (result != cases[i].result) {
            printf("path-match-test FAIL match case %u result=%d expected=%d\n",
                   i, result, cases[i].result);
            failures++;
        }
    }
    char stars[130], letters[129];
    for (unsigned i = 0; i < 64; i++) {
        stars[2*i] = '*';
        stars[2*i+1] = 'a';
        letters[2*i] = letters[2*i+1] = 'a';
    }
    stars[128] = '*'; stars[129] = 0; letters[128] = 0;
    CHECK(fnmatch(stars, letters, 0) == 0);
    path_case("", ".", ".");
    path_case("/", "/", "/");
    path_case("//", "/", "/");
    path_case("////", "/", "/");
    path_case("foo", "foo", ".");
    path_case("foo/", "foo", ".");
    path_case("/foo/", "foo", "/");
    path_case("/foo/bar//", "bar", "/foo");
    path_case("foo///bar", "bar", "foo");
    path_case("foo//bar//baz/", "baz", "foo//bar");
    path_case(".", ".", ".");
    path_case("..", "..", ".");
    path_case("/.", ".", "/");
    path_case("a/..", "..", "a");
    CHECK(!strcmp(basename(NULL), "."));
    CHECK(!strcmp(dirname(NULL), "."));
    CHECK(setlocale(LC_CTYPE, "C") != NULL);
    CHECK(fnmatch("?", "\xc3\xa9", 0) == 0);
    printf("path-match-test: %s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
