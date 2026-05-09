#include <string.h>
#include <ctype.h>
#include "parser.h"

void ParserInit(Parser* par, const char* filename, const char* data)
{
    par->filename = filename;
    par->offset = 0;
    par->data = data;
    par->row = 0;
    par->col = 0;
    par->err[0] = '\0';
}

char ParserPeek(const Parser* par) { return par->data[par->offset]; }
char ParserPeekForward(const Parser* par, int steps) { return par->data[par->offset + steps]; }
bool ParserMatch(const Parser* par, char match) { return match == par->data[par->offset]; }
bool ParserOneOf(const Parser* par, const char* matches) { return strchr(matches, par->data[par->offset]); }

bool ParserStartsWithCaseless(const Parser* par, const char* prefix)
{
    const char* start = par->data + par->offset;
    while (*prefix) {
        if (tolower(*prefix) != tolower(*start)) return false;
        prefix++; start++;
    }
    return true;
}

void ParserInc(Parser* par)
{
    if (par->data[par->offset] == '\n') { par->row++; par->col = 0; }
    else { par->col++; }
    par->offset++;
}

void ParserAdvance(Parser* par, int num) { for (int i = 0; i < num; i++) ParserInc(par); }

char* ParserCharName(char c)
{
    static char parserCharName[2];
    switch (c) {
        case '\0': return "end of file";
        case '\r': return "new line";
        case '\n': return "new line";
        case '\t': return "tab";
        case '\v': return "vertical tab";
        case '\b': return "backspace";
        case '\f': return "form feed";
        default:
            parserCharName[0] = c;
            parserCharName[1] = '\0';
            return parserCharName;
    }
}
