/* jsmin.c
   2013-03-29

Copyright (c) 2002 Douglas Crockford  (www.crockford.com)

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

The Software shall be used for Good, not Evil.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include <stdlib.h>
#include <stdio.h>
#include "jsmin.h"

static int   theA;
static int   theB;
static int   theLookahead = EOF;
static int   theX = EOF;
static int   theY = EOF;

static char *bufp = NULL;
static FILE *infilep;


static void
error(char* s)
{
    fputs("JSMIN Error: ", stderr);
    fputs(s, stderr);
    fputc('\n', stderr);
    exit(1);
}

static int
putcinbuf(int c)
{
    static size_t chunk_size = 32, last_chunk = 0, cursor = 0, free_space = 0;
    char *tmp_bufp = NULL;
    const int exp_base = 2; /* base of exponential growth of allocation */

    if (!free_space) {
        tmp_bufp = realloc(bufp, (chunk_size *= exp_base) * sizeof(*bufp));
        if (tmp_bufp == NULL)
            error("Failed to allocate memory!");
        bufp = tmp_bufp;
        free_space = (chunk_size - last_chunk) - 1; /* leave space for \0 */
        last_chunk = chunk_size;
    }

    bufp[cursor++] = (unsigned char) c;
    --free_space;
    bufp[cursor] = '\0';

    return (int) bufp[cursor - 1];
}

/* isAlphanum -- return true if the character is a letter, digit, underscore,
        dollar sign, or non-ASCII character.
*/

static int
isAlphanum(int c)
{
    return ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
        (c >= 'A' && c <= 'Z') || c == '_' || c == '$' || c == '\\' ||
        c > 126);
}


/* get -- return the next character from infilep. Watch out for lookahead. If
        the character is a control character, translate it to a space or
        linefeed.
*/

static int
get()
{
    int c = theLookahead;
    theLookahead = EOF;
    if (c == EOF) {
        c = getc(infilep);
    }
    if (c >= ' ' || c == '\n' || c == EOF) {
        return c;
    }
    if (c == '\r') {
        return '\n';
    }
    return ' ';
}


/* peek -- get the next character without getting it.
*/

static int
peek()
{
    theLookahead = get();
    return theLookahead;
}


/* next -- get the next character, excluding comments. peek() is used to see
        if a '/' is followed by a '/' or '*'.
*/

static int
next()
{
    int c = get();
    if  (c == '/') {
        switch (peek()) {
        case '/':
            for (;;) {
                c = get();
                if (c <= '\n') {
                    break;
                }
            }
            break;
        case '*':
            get();
            while (c != ' ') {
                switch (get()) {
                case '*':
                    if (peek() == '/') {
                        get();
                        c = ' ';
                    }
                    break;
                case EOF:
                    error("Unterminated comment.");
                }
            }
            break;
        }
    }
    theY = theX;
    theX = c;
    return c;
}


/* action -- do something! What you do is determined by the argument:
        1   Output A. Copy B to A. Get the next B.
        2   Copy B to A. Get the next B. (Delete A).
        3   Get the next B. (Delete B).
   action treats a string as a single character. Wow!
   action recognizes a regular expression if it is preceded by ( or , or =.
*/

static void
action(int d)
{
    switch (d) {
    case 1:
        putcinbuf(theA);
        if (
            (theY == '\n' || theY == ' ') &&
            (theA == '+' || theA == '-' || theA == '*' || theA == '/') &&
            (theB == '+' || theB == '-' || theB == '*' || theB == '/')
        ) {
            putcinbuf(theY);
        }
    case 2:
        theA = theB;
        if (theA == '\'' || theA == '"' || theA == '`') {
            for (;;) {
                putcinbuf(theA);
                theA = get();
                if (theA == theB) {
                    break;
                }
                if (theA == '\\') {
                    putcinbuf(theA);
                    theA = get();
                }
                if (theA == EOF) {
                    error("Unterminated string literal.");
                }
            }
        }
    case 3:
        theB = next();
        if (theB == '/' && (
            theA == '(' || theA == ',' || theA == '=' || theA == ':' ||
            theA == '[' || theA == '!' || theA == '&' || theA == '|' ||
            theA == '?' || theA == '+' || theA == '-' || theA == '~' ||
            theA == '*' || theA == '/' || theA == '{' || theA == '\n'
        )) {
            putcinbuf(theA);
            if (theA == '/' || theA == '*') {
                putcinbuf(' ');
            }
            putcinbuf(theB);
            for (;;) {
                theA = get();
                if (theA == '[') {
                    for (;;) {
                        putcinbuf(theA);
                        theA = get();
                        if (theA == ']') {
                            break;
                        }
                        if (theA == '\\') {
                            putcinbuf(theA);
                            theA = get();
                        }
                        if (theA == EOF) {
                            error("Unterminated set in Regular Expression literal.");
                        }
                    }
                } else if (theA == '/') {
                    switch (peek()) {
                    case '/':
                    case '*':
                        error("Unterminated set in Regular Expression literal.");
                    }
                    break;
                } else if (theA =='\\') {
                    putcinbuf(theA);
                    theA = get();
                }
                if (theA == EOF) {
                    error("Unterminated Regular Expression literal.");
                }
                putcinbuf(theA);
            }
            theB = next();
        }
    }
}


/* jsmin -- Copy the input to the output, deleting the characters which are
        insignificant to JavaScript. Comments will be removed. Tabs will be
        replaced with spaces. Carriage returns will be replaced with linefeeds.
        Most spaces and linefeeds will be removed.
*/

char *
jsmin(FILE *fp)
{
    infilep = fp;

    if (peek() == 0xEF) {
        get();
        get();
        get();
    }
    theA = '\n';
    action(3);
    while (theA != EOF) {
        switch (theA) {
        case ' ':
            action(isAlphanum(theB) ? 1 : 2);
            break;
        case '\n':
            switch (theB) {
            case '{':
            case '[':
            case '(':
            case '+':
            case '-':
            case '!':
            case '~':
                action(1);
                break;
            case ' ':
                action(3);
                break;
            default:
                action(isAlphanum(theB) ? 1 : 2);
            }
            break;
        default:
            switch (theB) {
            case ' ':
                action(isAlphanum(theA) ? 1 : 3);
                break;
            case '\n':
                switch (theA) {
                case '}':
                case ']':
                case ')':
                case '+':
                case '-':
                case '"':
                case '\'':
                case '`':
                    action(1);
                    break;
                default:
                    action(isAlphanum(theA) ? 1 : 3);
                }
                break;
            default:
                action(1);
                break;
            }
        }
    }

    return bufp;
}
