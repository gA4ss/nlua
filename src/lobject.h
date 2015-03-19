/*
** $Id: lobject.h,v 2.20.1.2 2008/08/06 13:29:48 roberto Exp $
** Type definitions for Lua objects
** See Copyright Notice in lua.h
*/


#ifndef lobject_h
#define lobject_h


#include <stdarg.h>


#include "llimits.h"
#include "lua.h"



#define LAST_TAG	LUA_TTHREAD       /* lua中可见的类型标记 */
#define NUM_TAGS	(LAST_TAG+1)      /* 标记的数量 */

/* 扩展的标记为了一些没有值的类型 */
#define LUA_TPROTO      (LAST_TAG+1)
#define LUA_TUPVAL      (LAST_TAG+2)
#define LUA_TDEADKEY    (LAST_TAG+3)

/* 可回收对象联合体 */
typedef union GCObject GCObject;

/* 所有可回收内存对象的共用头 
 * next 下一个对象的指针
 * tt 类型
 * marked 标记
 */
#define CommonHeader	GCObject *next; lu_byte tt; lu_byte marked

/* 可回收对象公共头的结构形式 */
typedef struct GCheader {
  CommonHeader;
} GCheader;

/* 所有lua值的联合体 */
typedef union {
  GCObject *gc;       /* 可回收对象 */
  void *p;            /* 轻型用户数据 */
  lua_Number n;       /* 实数 */
  int b;              /* 布尔 */
} Value;

/* lua类型的区域 */
#define TValuefields	Value value; int tt
typedef struct lua_TValue {
  TValuefields;
} TValue;


/*
 * 测试对象的宏
 */
#define ttisnil(o)	(ttype(o) == LUA_TNIL)                      /* 当前值是否是空的 */
#define ttisnumber(o)	(ttype(o) == LUA_TNUMBER)                 /* 当前值是实数 */
#define ttisstring(o)	(ttype(o) == LUA_TSTRING)                 /* 当前值是字符串 */
#define ttistable(o)	(ttype(o) == LUA_TTABLE)                  /* 当前值是表 */
#define ttisfunction(o)	(ttype(o) == LUA_TFUNCTION)             /* 当前值是函数 */
#define ttisboolean(o)	(ttype(o) == LUA_TBOOLEAN)              /* 当前值是布尔值 */
#define ttisuserdata(o)	(ttype(o) == LUA_TUSERDATA)             /* 当前值是用户自定义类型 */
#define ttisthread(o)	(ttype(o) == LUA_TTHREAD)                 /* 当前值是线程 */
#define ttislightuserdata(o)	(ttype(o) == LUA_TLIGHTUSERDATA)  /* 当前值是轻型数据，不是可回收对象 */

/* 访问值所需的宏 */
#define ttype(o)	((o)->tt)
#define gcvalue(o)	check_exp(iscollectable(o), (o)->value.gc)
#define pvalue(o)	check_exp(ttislightuserdata(o), (o)->value.p)
#define nvalue(o)	check_exp(ttisnumber(o), (o)->value.n)
#define rawtsvalue(o)	check_exp(ttisstring(o), &(o)->value.gc->ts)
#define tsvalue(o)	(&rawtsvalue(o)->tsv)
#define rawuvalue(o)	check_exp(ttisuserdata(o), &(o)->value.gc->u)
#define uvalue(o)	(&rawuvalue(o)->uv)
#define clvalue(o)	check_exp(ttisfunction(o), &(o)->value.gc->cl)
#define hvalue(o)	check_exp(ttistable(o), &(o)->value.gc->h)
#define bvalue(o)	check_exp(ttisboolean(o), (o)->value.b)
#define thvalue(o)	check_exp(ttisthread(o), &(o)->value.gc->th)

#define l_isfalse(o)	(ttisnil(o) || (ttisboolean(o) && bvalue(o) == 0))

/* 仅为内部调试支持 */
#define checkconsistency(obj) \
  lua_assert(!iscollectable(obj) || (ttype(obj) == (obj)->value.gc->gch.tt))

#define checkliveness(g,obj) \
  lua_assert(!iscollectable(obj) || \
  ((ttype(obj) == (obj)->value.gc->gch.tt) && !isdead(g, (obj)->value.gc)))


/* 设置值的宏 */
#define setnilvalue(obj) ((obj)->tt=LUA_TNIL)

#define setnvalue(obj,x) \
  { TValue *i_o=(obj); i_o->value.n=(x); i_o->tt=LUA_TNUMBER; }

#define setpvalue(obj,x) \
  { TValue *i_o=(obj); i_o->value.p=(x); i_o->tt=LUA_TLIGHTUSERDATA; }

#define setbvalue(obj,x) \
  { TValue *i_o=(obj); i_o->value.b=(x); i_o->tt=LUA_TBOOLEAN; }

/* 以下是设置可回收对象 */

#define setsvalue(L,obj,x) \
  { TValue *i_o=(obj); \
    i_o->value.gc=cast(GCObject *, (x)); i_o->tt=LUA_TSTRING; \
    checkliveness(G(L),i_o); }

#define setuvalue(L,obj,x) \
  { TValue *i_o=(obj); \
    i_o->value.gc=cast(GCObject *, (x)); i_o->tt=LUA_TUSERDATA; \
    checkliveness(G(L),i_o); }

#define setthvalue(L,obj,x) \
  { TValue *i_o=(obj); \
    i_o->value.gc=cast(GCObject *, (x)); i_o->tt=LUA_TTHREAD; \
    checkliveness(G(L),i_o); }

#define setclvalue(L,obj,x) \
  { TValue *i_o=(obj); \
    i_o->value.gc=cast(GCObject *, (x)); i_o->tt=LUA_TFUNCTION; \
    checkliveness(G(L),i_o); }

#define sethvalue(L,obj,x) \
  { TValue *i_o=(obj); \
    i_o->value.gc=cast(GCObject *, (x)); i_o->tt=LUA_TTABLE; \
    checkliveness(G(L),i_o); }

#define setptvalue(L,obj,x) \
  { TValue *i_o=(obj); \
    i_o->value.gc=cast(GCObject *, (x)); i_o->tt=LUA_TPROTO; \
    checkliveness(G(L),i_o); }

/* 将一个对象的值设置到另外一个 */
#define setobj(L,obj1,obj2) \
  { const TValue *o2=(obj2); TValue *o1=(obj1); \
    o1->value = o2->value; o1->tt=o2->tt; \
    checkliveness(G(L),o1); }


/*
 * 不同类型的设置
 */

/* from stack to (same) stack */
#define setobjs2s	setobj
/* to stack (not from same stack) */
#define setobj2s	setobj
#define setsvalue2s	setsvalue
#define sethvalue2s	sethvalue
#define setptvalue2s	setptvalue
/* from table to same table */
#define setobjt2t	setobj
/* to table */
#define setobj2t	setobj
/* to new object */
#define setobj2n	setobj
#define setsvalue2n	setsvalue

#define setttype(obj, tt) (ttype(obj) = (tt))


#define iscollectable(o)	(ttype(o) >= LUA_TSTRING)


typedef TValue *StkId;  /* 指向栈元素 */

/* 字符串头指针 */
typedef union TString {
  L_Umaxalign dummy;  /* 最大的对齐粒度 */
  struct {
    CommonHeader;
    lu_byte reserved;   /* lua的保留值，这里为1 */
    unsigned int hash;  /* 字符串hash值 */
    size_t len;         /* 字符串长度 */
  } tsv;
} TString;


#define getstr(ts)      cast(const char *, (ts) + 1)
#define svalue(o)       getstr(rawtsvalue(o))

/* 用户自定义的数据 */
typedef union Udata {
  L_Umaxalign dummy;  /* 最大的对齐粒度 */
  struct {
    CommonHeader;
    struct Table *metatable;    /* 用户数据元操作表 */
    struct Table *env;          /* 环境表 */
    size_t len;                 /* 用户数据长度 */
  } uv;
} Udata;

/*
 * 函数原型
 */
typedef struct Proto {
  CommonHeader;
  TValue *k;                    /* 函数使用的常量队列 */
  Instruction *code;            /* 函数的代码队列 */
  struct Proto **p;             /* 在当前函数中定义的函数队列 */
  int *lineinfo;                /* 每条指令对应的源代码行数 */
  struct LocVar *locvars;       /* 局部变量信息 */
  TString **upvalues;           /* upvalue的名称 */
  TString  *source;             /* 函数所对应的源代码 */
  int sizeupvalues;             /* upvalue的数量 */
  int sizek;                    /* 常量的个数 */
  int sizecode;                 /* 指令的个数 */
  int sizelineinfo;             /* 行号信息个数 */
  int sizep;                    /* 子函数的数量 */
  int sizelocvars;              /* 局部变量的个数 */
  int linedefined;              /* 行定义 */
  int lastlinedefined;          /* 最后的行定义 */
  GCObject *gclist;             /* 可回收对象链表 */
  lu_byte nups;                 /* upvalues的数量 */
  lu_byte numparams;            /* 参数个数 */
  lu_byte is_vararg;            /* 是否是可变参数 */
  lu_byte maxstacksize;         /* 最大的栈数量 */
} Proto;


/* masks for new-style vararg */
#define VARARG_HASARG     1
#define VARARG_ISVARARG		2     /* 是多参数函数 */
#define VARARG_NEEDSARG		4

/* 局部变量 */
typedef struct LocVar {
  TString *varname;         /* 变量名称 */
  int startpc;              /* 变量诞生的指令的位置 */
  int endpc;                /* 变量死亡的指令的位置 */
} LocVar;

/* Upvalues
 */
typedef struct UpVal {
  CommonHeader;
  TValue *v;                /* 指向堆栈活着它自身的值 */
  union {
    TValue value;           /* 当关闭时，upvalue的值 */
    struct {                /* 当upvalue存在时的链表 */
      struct UpVal *prev;
      struct UpVal *next;
    } l;
  } u;
} UpVal;

/* 闭包的头结构 
 * isC 是C函数
 * nupvalues upvalue的数量
 * gclist 可回收对象的链表
 * env 当前环境变量
 */
#define ClosureHeader \
	CommonHeader; lu_byte isC; lu_byte nupvalues; GCObject *gclist; \
	struct Table *env

/* c闭包 */
typedef struct CClosure {
  ClosureHeader;
  lua_CFunction f;
  TValue upvalue[1];
} CClosure;

/* lua闭包 */
typedef struct LClosure {
  ClosureHeader;
  struct Proto *p;
  UpVal *upvals[1];
} LClosure;

/* 闭包函数 */
typedef union Closure {
  CClosure c;           /* c闭包结构 */
  LClosure l;           /* lua闭包结构 */
} Closure;

/* 判断c函数 */
#define iscfunction(o)	(ttype(o) == LUA_TFUNCTION && clvalue(o)->c.isC)
/* 判断lua函数 */
#define isLfunction(o)	(ttype(o) == LUA_TFUNCTION && !clvalue(o)->c.isC)


/*
 * hash表
 */

/* hash键 */
typedef union TKey {
  struct {
    TValuefields;
    struct Node *next;        /* 指向下一个节点 */
  } nk;
  TValue tvk;                 /* 键指向的值 */
} TKey;

/* 哈希节点 */
typedef struct Node {
  TValue i_val;               /* 值 */
  TKey i_key;                 /* 键 */
} Node;

/* 哈希表 */
typedef struct Table {
  CommonHeader;
  lu_byte flags;            /* 1<<p means tagmethod(p) is not present */
  lu_byte lsizenode;        /* log2 of size of `node' array */
  struct Table *metatable;
  TValue *array;            /* array part */
  Node *node;
  Node *lastfree;  /* any free position is before this position */
  GCObject *gclist;
  int sizearray;  /* size of `array' array */
} Table;



/*
** `module' operation for hashing (size is always a power of 2)
*/
/* hash算法的`模`操作
 * s 要hash的字符串
 * size s的长度,size必须是2的次冥
 * 
 * 其实这里就做了一个s的值与长度减1的并操作
 */
#define lmod(s,size) \
	(check_exp((size&(size-1))==0, (cast(int, (s) & ((size)-1)))))


#define twoto(x)	(1<<(x))
#define sizenode(t)	(twoto((t)->lsizenode))


#define luaO_nilobject		(&luaO_nilobject_)

LUAI_DATA const TValue luaO_nilobject_;

#define ceillog2(x)	(luaO_log2((x)-1) + 1)

LUAI_FUNC int luaO_log2 (unsigned int x);
LUAI_FUNC int luaO_int2fb (unsigned int x);
LUAI_FUNC int luaO_fb2int (int x);
LUAI_FUNC int luaO_rawequalObj (const TValue *t1, const TValue *t2);
LUAI_FUNC int luaO_str2d (const char *s, lua_Number *result);
LUAI_FUNC const char *luaO_pushvfstring (lua_State *L, const char *fmt,
                                                       va_list argp);
LUAI_FUNC const char *luaO_pushfstring (lua_State *L, const char *fmt, ...);
LUAI_FUNC void luaO_chunkid (char *out, const char *source, size_t len);


#endif

