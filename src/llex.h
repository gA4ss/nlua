/*
** $Id: llex.h,v 1.58.1.1 2007/12/27 13:02:25 roberto Exp $
** Lexical Analyzer
** See Copyright Notice in lua.h
*/

#ifndef llex_h
#define llex_h

#include "lobject.h"
#include "lzio.h"


#define FIRST_RESERVED	257

/* 保留字最大的字符串长度 */
#define TOKEN_LEN	(sizeof("function")/sizeof(char))


/* 标记 */
enum RESERVED {
  /* terminal symbols denoted by reserved words */
  TK_AND = FIRST_RESERVED, TK_BREAK,
  TK_DO, TK_ELSE, TK_ELSEIF, TK_END, TK_FALSE, TK_FOR, TK_FUNCTION,
  TK_IF, TK_IN, TK_LOCAL, TK_NIL, TK_NOT, TK_OR, TK_REPEAT,
  TK_RETURN, TK_THEN, TK_TRUE, TK_UNTIL, TK_WHILE,
  /* other terminal symbols */
  TK_CONCAT, TK_DOTS, TK_EQ, TK_GE, TK_LE, TK_NE, TK_NUMBER,
  TK_NAME, TK_STRING, TK_EOS
};

/* number of reserved words */
#define NUM_RESERVED	(cast(int, TK_WHILE-FIRST_RESERVED+1))


/* array with token `names' */
LUAI_DATA const char *const luaX_tokens [];

/* 标记字面值 */
typedef union {
  lua_Number r;
  TString *ts;
} SemInfo;  /* semantics information */

/* 标记结构 */
typedef struct Token {
  int token;
  SemInfo seminfo;
} Token;

/* 词法状态 */
typedef struct LexState {
  int current;              /* 当前字符 */
  int linenumber;           /* 行号 */
  int lastline;             /* 最后标记的行号 */
  Token t;                  /* 当前词法标记 */
  Token lookahead;          /* 向前查看一个标记 */
  struct FuncState *fs;     /* `FuncState' 结构指针，词语法分析专有 */
  struct lua_State *L;      /* lua线程状态指针 */
  ZIO *z;                   /* 输入流 */
  Mbuffer *buff;            /* 词法分析的临时缓存 */
  TString *source;          /* 当前源程序名 */
  char decpoint;            /* 本地 点 字符 */
} LexState;


LUAI_FUNC void luaX_init (lua_State *L);
LUAI_FUNC void luaX_setinput (lua_State *L, LexState *ls, ZIO *z,
                              TString *source);
LUAI_FUNC TString *luaX_newstring (LexState *ls, const char *str, size_t l);
LUAI_FUNC void luaX_next (LexState *ls);
LUAI_FUNC void luaX_lookahead (LexState *ls);
LUAI_FUNC void luaX_lexerror (LexState *ls, const char *msg, int token);
LUAI_FUNC void luaX_syntaxerror (LexState *ls, const char *s);
LUAI_FUNC const char *luaX_token2str (LexState *ls, int token);


#endif
