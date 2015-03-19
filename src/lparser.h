/*
** $Id: lparser.h,v 1.57.1.1 2007/12/27 13:02:25 roberto Exp $
** Lua Parser
** See Copyright Notice in lua.h
*/

#ifndef lparser_h
#define lparser_h

#include "llimits.h"
#include "lobject.h"
#include "lzio.h"

/*
 * 表达式描述
 */
typedef enum {
  VVOID,          /* 无值 */
  VNIL,           /* 空值 */
  VTRUE,          /* 真值 */
  VFALSE,         /* 假值 */
  VK,             /* info = `k'常量表索引 */
  VKNUM,          /* nval = 实数值 */
  VLOCAL,         /* info = 局部变量寄存器索引 */
  VUPVAL,         /* info = `upvalues'中的索引 */
  VGLOBAL,        /* info = 表的索引; aux = 在`k'表中全局名称的索引 */
  VINDEXED,       /* info = 表寄存器索引; aux = 寄存器索引或者在`k'中的索引 */
  VJMP,           /* info = 当前值了的pc值 */
  VRELOCABLE,     /* info = 指令的pc值 */
  VNONRELOC,      /* info = 结果寄存器索引 */
  VCALL,          /* info = 指令的pc值 */
  VVARARG         /* info = 指令的pc值 */
} expkind;

/* 表达式结构 */
typedef struct expdesc {
  expkind k;      /* 表达式类型 */
  union {
    struct { int info, aux; } s;
    lua_Number nval;
  } u;
  int t;  /* patch list of `exit when true' */
  int f;  /* patch list of `exit when false' */
} expdesc;

/* upvalue描述 */
typedef struct upvaldesc {
  lu_byte k;
  lu_byte info;
} upvaldesc;

struct BlockCnt;  /* 在lparser.c文件中定义 */

/* 产生代码时需要这个函数状态 */
typedef struct FuncState {
  Proto *f;                     /* 当前函数头 */
  Table *h;                     /* `k`的常量哈希表 */
  struct FuncState *prev;       /* 上一个函数状态 */
  struct LexState *ls;          /* 词法分析状态 */
  struct lua_State *L;          /* lua线程状态 */
  struct BlockCnt *bl;          /* 代码块链表 */
  int pc;                       /* 下一条指令的位置(等于`ncode`) */
  int lasttarget;               /* 最后一个jmp指令的目标地址 */
  int jpc;                      /* list of pending jumps to `pc' */
  int freereg;                  /* 第一个空闲可用的寄存器 */
  int nk;                       /* 常量k的个数 */
  int np;                       /* 函数p的个数 */
  short nlocvars;               /* 局部变量的个数 */
  lu_byte nactvar;              /* 当前活动局部变量的个数 */
  upvaldesc upvalues[LUAI_MAXUPVALUES];   /* upvalues */
  unsigned short actvar[LUAI_MAXVARS];    /* 纪录了对应的真实变量在栈中的索引 */
} FuncState;


LUAI_FUNC Proto *luaY_parser (lua_State *L, ZIO *z, Mbuffer *buff,
                                            const char *name);


#endif
