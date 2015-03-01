/*
** $Id: lundump.c,v 2.7.1.4 2008/04/04 19:51:41 roberto Exp $
** load precompiled Lua chunks
** See Copyright Notice in lua.h
*/

#include <string.h>

#define lundump_c
#define LUA_CORE

#include "nlua.h"

#include "ldebug.h"
#include "ldo.h"
#include "lfunc.h"
#include "lmem.h"
#include "lobject.h"
#include "lstring.h"
#include "lundump.h"
#include "lzio.h"
#include "lopcodes.h"

/* 字节码读取结构 */
typedef struct {
 lua_State* L;        /* 线程状态指针 */
 ZIO* Z;              /* 字节码IO结构指针 */
 Mbuffer* b;          /* scanner使用的缓存 */
 const char* name;    /* 代码名称 */
} LoadState;

/* 一些出错输出宏 */
#ifdef LUAC_TRUST_BINARIES
#define IF(c,s)
#define error(S,s)
#else
#define IF(c,s)		if (c) error(S,s)

static void error(LoadState* S, const char* why) {
 luaO_pushfstring(S->L,"%s: %s in precompiled chunk",S->name,why);
 luaD_throw(S->L,LUA_ERRSYNTAX);
}
#endif

#define LoadMem(S,b,n,size)     LoadBlock(S,b,(n)*(size))
#define	LoadByte(S)             (lu_byte)LoadChar(S)
#define LoadVar(S,x)            LoadMem(S,&x,1,sizeof(x))
#define LoadVector(S,b,n,size)	LoadMem(S,b,n,size)

/* 从S中读取size个字节到b中 */
static void LoadBlock(LoadState* S, void* b, size_t size) {
  size_t r=luaZ_read(S->Z,b,size);
  IF (r!=0, "unexpected end");
}

/* 从S中读取一个字符 */
static int LoadChar(LoadState* S) {
  char x;
  LoadVar(S,x);
  return x;
}

/* 从S中读取一个整型 */
static int LoadInt(LoadState* S) {
  int x;
  LoadVar(S,x);
  IF (x<0, "bad integer");
  return x;
}

/* 从S中读取lua数字型 */
static lua_Number LoadNumber(LoadState* S) {
  lua_Number x;
  LoadVar(S,x);
  return x;
}

/* 从字节码文件中读取字符串 */
static TString* LoadString(LoadState* S) {
  size_t size;
  LoadVar(S,size);                        /* 读取字符串长度 */
  if (size==0)
    return NULL;
  else {
    char* s=luaZ_openspace(S->L,S->b,size);
    LoadBlock(S,s,size);
    return luaS_newlstr(S->L,s,size-1);		/* 移除末尾的 '\0' 字符 */
  }
}

/* 读取代码 */
static void LoadCode(LoadState* S, Proto* f) {
  int n=LoadInt(S);       /* 读取指令的个数 */
  f->code=luaM_newvector(S->L,n,Instruction);
  f->sizecode=n;
  LoadVector(S,f->code,n,sizeof(Instruction));
}

static Proto* LoadFunction(LoadState* S, TString* p);

/* 从S中读取常量到f函数原型中 */
static void LoadConstants(LoadState* S, Proto* f) {
  int i,n;
  n=LoadInt(S);                               /* 读取常量个数 */
  f->k=luaM_newvector(S->L,n,TValue);         /* 创建新的常量表 */
  f->sizek=n;
  /* 初始化常量表 */
  for (i=0; i<n; i++) setnilvalue(&f->k[i]);
  
  /* 分别读取这些常数 */
  for (i=0; i<n; i++) {
    TValue* o=&f->k[i];
    int t=LoadChar(S);            /* 读取常量类型 */
    switch (t) {
      case LUA_TNIL:
        setnilvalue(o);
        break;
      case LUA_TBOOLEAN:
        setbvalue(o,LoadChar(S)!=0);
        break;
      case LUA_TNUMBER:
        setnvalue(o,LoadNumber(S));
        break;
      case LUA_TSTRING:
        setsvalue2n(S->L,o,LoadString(S));
        break;
      default:
        error(S,"bad constant");
        break;
    }
  }
  
  /* 下面读取
   */
  n=LoadInt(S);                 /* 读取当前函数子函数的个数 */
  f->p=luaM_newvector(S->L,n,Proto*);
  f->sizep=n;
  
  /* 加载所有子函数 */
  for (i=0; i<n; i++) f->p[i]=NULL;
  for (i=0; i<n; i++) f->p[i]=LoadFunction(S,f->source);
}

/* 加载调试信息 */
static void LoadDebug(LoadState* S, Proto* f) {
  int i,n;
  n=LoadInt(S);
  f->lineinfo=luaM_newvector(S->L,n,int);
  f->sizelineinfo=n;
  LoadVector(S,f->lineinfo,n,sizeof(int));
  n=LoadInt(S);
  f->locvars=luaM_newvector(S->L,n,LocVar);
  f->sizelocvars=n;
  for (i=0; i<n; i++) f->locvars[i].varname=NULL;
  for (i=0; i<n; i++) {
    f->locvars[i].varname=LoadString(S);
    f->locvars[i].startpc=LoadInt(S);
    f->locvars[i].endpc=LoadInt(S);
  }
  n=LoadInt(S);
  f->upvalues=luaM_newvector(S->L,n,TString*);
  f->sizeupvalues=n;
  for (i=0; i<n; i++) f->upvalues[i]=NULL;
  for (i=0; i<n; i++) f->upvalues[i]=LoadString(S);
}

/* 加载函数
 * S 加载状态结构指针
 * p 伪字符串
 */
static Proto* LoadFunction(LoadState* S, TString* p) {
  Proto* f;
  
  /* 调用lua函数栈太多了则出错 */
  if (++S->L->nCcalls > LUAI_MAXCCALLS) error(S,"code too deep");
  
  /* 创建一个新的函数原型 */
  f=luaF_newproto(S->L);
  
  /* 将新的函数原型放置到栈顶 */
  setptvalue2s(S->L,S->L->top,f); incr_top(S->L);
  
  /* 加载源代码 */
  f->source=LoadString(S); if (f->source==NULL) f->source=p;
  f->linedefined=LoadInt(S);              /* 行号定义 */
  f->lastlinedefined=LoadInt(S);          /* 最后的行号定义 */
  f->nups=LoadByte(S);                    /* 读取upvalue的数量 */
  f->numparams=LoadByte(S);               /* 这份代码的参数个数 */
  f->is_vararg=LoadByte(S);               /* 这份代码是否是多参数 */
  f->maxstacksize=LoadByte(S);            /* 最大的栈深度 */
  LoadCode(S,f);                          /* 读取代码 */
  LoadConstants(S,f);                     /* 读取常量 */
  LoadDebug(S,f);                         /* 读取调试信息 */
  
  /* 如果出错则退出 */
  IF (!luaG_checkcode(S->L, f), "bad code");
  
  /* 栈的恢复 */
  S->L->top--;
  S->L->nCcalls--;
  return f;
}

/* 加载字节码文件头 */
static void LoadHeader(LoadState* S) {
  char h[LUAC_HEADERSIZE];
  char s[LUAC_HEADERSIZE];
  
  /* 读取文件头与体 */
  luaU_header(h);
  LoadBlock(S,s,LUAC_HEADERSIZE);
  
  /* 如果文件头读取错误,则输出错误 */
  IF (memcmp(h,s,LUAC_HEADERSIZE)!=0, "bad header");
}

/* 加载预编译代码
 * L 线程状态指针
 * Z IO结构指针,用于读取要分析代码缓存
 * buff 用于scanner使用的缓存
 * name 这段代码的名称
 */
Proto* luaU_undump (lua_State* L, ZIO* Z, Mbuffer* buff, const char* name) {
  LoadState S;
  
  /* 设置不同的程序名称 */
  if (*name=='@' || *name=='=')
    S.name=name+1;
  else if (*name==LUA_SIGNATURE[0])
    S.name="binary string";
  else
    S.name=name;
  S.L=L;
  S.Z=Z;
  S.b=buff;
  
  /* 加载字节码文件头 */
  LoadHeader(&S);
  return LoadFunction(&S,luaS_newliteral(L,"=?"));
}

/* 读取文件头并存入缓存h */
void luaU_header (char* h) {
  int x=1;
  memcpy(h,LUA_SIGNATURE,sizeof(LUA_SIGNATURE)-1);
  h+=sizeof(LUA_SIGNATURE)-1;
  *h++=(char)LUAC_VERSION;              /* 版本 */
  *h++=(char)LUAC_FORMAT;               /* 格式 */
  *h++=(char)*(char*)&x;                /* 字节序 */
  *h++=(char)sizeof(int);               /* 一个int的长度 */
  *h++=(char)sizeof(size_t);            /* 一个size_t的长度 */
  *h++=(char)sizeof(Instruction);       /* 一条指令的长度 */
  *h++=(char)sizeof(lua_Number);        /* lua数字类型的长度 */
  *h++=(char)(((lua_Number)0.5)==0);		/* is lua_Number integral? */
}
