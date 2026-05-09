#ifndef PARSER_H
#define PARSER_H

#include <stdbool.h>

enum { PARSER_ERR_MAX = 512 };

typedef struct Parser
{
    const char* filename;
    int offset;
    const char* data;
    int row;
    int col;
    char err[PARSER_ERR_MAX];
} Parser;

void ParserInit(Parser* par, const char* filename, const char* data);
char ParserPeek(const Parser* par);
char ParserPeekForward(const Parser* par, int steps);
bool ParserMatch(const Parser* par, char match);
bool ParserOneOf(const Parser* par, const char* matches);
bool ParserStartsWithCaseless(const Parser* par, const char* prefix);
void ParserInc(Parser* par);
void ParserAdvance(Parser* par, int num);
char* ParserCharName(char c);

#define ParserError(par, fmt, ...) \
    snprintf((par)->err, PARSER_ERR_MAX, "%s:%i:%i: error: " fmt, (par)->filename, (par)->row, (par)->col, ##__VA_ARGS__)

#endif
