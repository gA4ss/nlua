/*
** $Id: lstate.c,v 2.36.1.2 2008/01/03 15:20:39 roberto Exp $
** Global State
** See Copyright Notice in lua.h
*/


#include <stddef.h>

#define lstate_c
#define LUA_CORE

#include "nlua.h"

#include "ldebug.h"
#include "ldo.h"
#include "lfunc.h"
#include "lgc.h"
#include "llex.h"
#include "lmem.h"
#include "lstate.h"
#include "lstring.h"
#include "ltable.h"
#include "ltm.h"
#include "nundump.h"

/* 计算整个LG的长度 */
#define state_size(x)	(sizeof(x) + LUAI_EXTRASPACE)
/* 返回LG整体的指针 */
#define fromstate(l)	(cast(lu_byte *, (l)) - LUAI_EXTRASPACE)
/* 返回lua_State指针 */
#define tostate(l)   (cast(lua_State *, cast(lu_byte *, l) + LUAI_EXTRASPACE))

/* 
 * 主线程包括一个线程状态与一个全局状态 
 */
typedef struct LG {
  lua_State l;        /* 线程状态 */
  global_State g;     /* 全局状态 */
} LG;
  
/* 初始化栈状态 */
static void stack_init (lua_State *L1, lua_State *L) {
  /* 初始化CallInfo队列 */
  L1->base_ci = luaM_newvector(L, BASIC_CI_SIZE, CallInfo);
  L1->ci = L1->base_ci;
  L1->size_ci = BASIC_CI_SIZE;
  L1->end_ci = L1->base_ci + L1->size_ci - 1;
  /* 初始化对战队列 */
  L1->stack = luaM_newvector(L, BASIC_STACK_SIZE + EXTRA_STACK, TValue);
  L1->stacksize = BASIC_STACK_SIZE + EXTRA_STACK;
  L1->top = L1->stack;
  L1->stack_last = L1->stack+(L1->stacksize - EXTRA_STACK)-1;
  /* 初始化第一个ci */
  L1->ci->func = L1->top;
  setnilvalue(L1->top++);  /* `function' entry for this `ci' */
  L1->base = L1->ci->base = L1->top;
  L1->ci->top = L1->top + LUA_MINSTACK;
}

/* 释放栈空间 */
static void freestack (lua_State *L, lua_State *L1) {
  luaM_freearray(L, L1->base_ci, L1->size_ci, CallInfo);
  luaM_freearray(L, L1->stack, L1->stacksize, TValue);
}


/*
** open parts that may cause memory-allocation errors
*/
static void f_luaopen (lua_State *L, void *ud) {
  /* 获取全局状态指针 */
  global_State *g = G(L);
  UNUSED(ud);
  /* 栈初始化 */
  stack_init(L, L);  /* init stack */
  /* 初始化全局哈希表 */
  sethvalue(L, gt(L), luaH_new(L, 0, 2));  /* table of globals */
  /* 初始化寄存器 */
  sethvalue(L, registry(L), luaH_new(L, 0, 2));  /* registry */
  /* 初始化字符串表 */
  luaS_resize(L, MINSTRTABSIZE);  /* initial size of string table */
  luaT_init(L);
  luaX_init(L);
  luaS_fix(luaS_newliteral(L, MEMERRMSG));
  g->GCthreshold = 4*g->totalbytes;
}

/* 初始化线程状态 */
static void preinit_state (lua_State *L, global_State *g) {
  G(L) = g;
  L->stack = NULL;
  L->stacksize = 0;
  L->errorJmp = NULL;
  L->hook = NULL;
  L->hookmask = 0;
  L->basehookcount = 0;
  L->allowhook = 1;
  resethookcount(L);
  L->openupval = NULL;
  L->size_ci = 0;
  L->nCcalls = L->baseCcalls = 0;
  L->status = 0;
  L->base_ci = L->ci = NULL;
  L->savedpc = NULL;
  L->errfunc = 0;
  setnilvalue(gt(L));
}

/* 关闭本地线程 */
static void close_state (lua_State *L) {
  global_State *g = G(L);
  /* 关闭这条线程上所有的upvalues */
  luaF_close(L, L->stack);  /* close all upvalues for this thread */
  /* 回收所有的对象内存 */
  luaC_freeall(L);  /* collect all objects */
  lua_assert(g->rootgc == obj2gco(L));
  lua_assert(g->strt.nuse == 0);
  /* 释放全局字符串哈希表 */
  luaM_freearray(L, G(L)->strt.hash, G(L)->strt.size, TString *);
  /* 释放临时缓存 */
  luaZ_freebuffer(L, &g->buff);
  /* 释放堆栈 */
  freestack(L, L);
  lua_assert(g->totalbytes == sizeof(LG));
  /* 释放整体内存 */
  (*g->frealloc)(g->ud, fromstate(L), state_size(LG), 0);
}

/* 创建新的线程 */
lua_State *luaE_newthread (lua_State *L) {
  /* 创建一个新的线程状态 */
  lua_State *L1 = tostate(luaM_malloc(L, state_size(lua_State)));
  luaC_link(L, obj2gco(L1), LUA_TTHREAD);
  preinit_state(L1, G(L));
  stack_init(L1, L);  /* init stack */
  setobj2n(L, gt(L1), gt(L));  /* share table of globals */
  L1->hookmask = L->hookmask;
  L1->basehookcount = L->basehookcount;
  L1->hook = L->hook;
  resethookcount(L1);
  lua_assert(iswhite(obj2gco(L1)));
  return L1;
}

/* 释放线程 */
void luaE_freethread (lua_State *L, lua_State *L1) {
  luaF_close(L1, L1->stack);  /* close all upvalues for this thread */
  lua_assert(L1->openupval == NULL);
  luai_userstatefree(L1);
  freestack(L, L1);
  luaM_freemem(L, fromstate(L1), state_size(lua_State));
}

/* 初始化nlua */
static void init_nlua(global_State *g) {
  
  if (g->nboot) {
    return;
  }
  g->nboot=1;
  
  /* 文件类型初始化 */
  g->is_nlua = 0;
  g->nopt = 0;
  memset(g->fkeyp, 0, MAX_KEY_PATH);
  /* 设置指令前后函数 */
  g->istart = nluaV_insstart;
  g->iend = nluaV_insend;
  
  /* 设置指令数据加解密函数 */
  g->ienidata = nluaV_enidata;
  g->ideidata = nluaV_deidata;
  
  /* 设置指令加解密函数 */
  g->ienins = nluaV_enins;
  g->ideins = nluaV_deins;
  
  /* 设置文件加解密函数 */
  g->enbuf = nluaV_enbuf;
  g->debuf = nluaV_debuf;
  g->fkmake = nluaV_fkmake;
  
  /* 进行opcode规则的重新编码 */
  nluaV_oprinit(g);
}

/* 分配新的状态 */
LUA_API lua_State *lua_newstate (lua_Alloc f, void *ud) {
  int i;
  lua_State *L;
  global_State *g;
  
  /* 分配一个主线程结构状态 */
  void *l = (*f)(ud, NULL, 0, state_size(LG));
  if (l == NULL) return NULL;
  memset(l,0,state_size(LG));  /* 清空刚分配的内存 */
  
  /* 返回lua_State */
  L = tostate(l);
  g = &((LG *)L)->g;
  L->next = NULL;
  L->tt = LUA_TTHREAD;
  g->currentwhite = bit2mask(WHITE0BIT, FIXEDBIT);
  L->marked = luaC_white(g);
  set2bits(L->marked, FIXEDBIT, SFIXEDBIT);
  preinit_state(L, g);
  g->frealloc = f;
  g->ud = ud;
  g->mainthread = L;
  g->uvhead.u.l.prev = &g->uvhead;
  g->uvhead.u.l.next = &g->uvhead;
  g->GCthreshold = 0;  /* mark it as unfinished state */
  g->strt.size = 0;
  g->strt.nuse = 0;
  g->strt.hash = NULL;
  setnilvalue(registry(L));
  /* 初始化临时缓存 */
  luaZ_initbuffer(L, &g->buff);
  g->panic = NULL;
  g->gcstate = GCSpause;
  g->rootgc = obj2gco(L);
  g->sweepstrgc = 0;
  g->sweepgc = &g->rootgc;
  g->gray = NULL;
  g->grayagain = NULL;
  g->weak = NULL;
  g->tmudata = NULL;
  g->totalbytes = sizeof(LG);
  g->gcpause = LUAI_GCPAUSE;
  g->gcstepmul = LUAI_GCMUL;
  g->gcdept = 0;
  /* 初始化元操作 */
  for (i=0; i<NUM_TAGS; i++) g->mt[i] = NULL;
  
  /* 初始化nlua */
  init_nlua(g);
  
  /* 打开这条线程状态 */
  if (luaD_rawrunprotected(L, f_luaopen, NULL) != 0) {
    /* memory allocation error: free partial state */
    close_state(L);
    L = NULL;
  }
  else
    luai_userstateopen(L);
  return L;
}

/* 调用 GC 元方法为所有的用户数据 */
static void callallgcTM (lua_State *L, void *ud) {
  UNUSED(ud);
  luaC_callGCTM(L);  /* call GC metamethods for all udata */
}

/* 关闭一个lua线程 */
LUA_API void lua_close (lua_State *L) {
  L = G(L)->mainthread;  /* only the main thread can be closed */
  lua_lock(L);
  luaF_close(L, L->stack);  /* close all upvalues for this thread */
  luaC_separateudata(L, 1);  /* separate udata that have GC metamethods */
  L->errfunc = 0;  /* no error function during GC metamethods */
  do {  /* repeat until no more errors */
    L->ci = L->base_ci;
    L->base = L->top = L->ci->base;
    L->nCcalls = L->baseCcalls = 0;
  } while (luaD_rawrunprotected(L, callallgcTM, NULL) != 0);
  lua_assert(G(L)->tmudata == NULL);
  luai_userstateclose(L);
  close_state(L);
}

/*
 * nlua
 */

LUAI_FUNC void nluaE_setopt (lua_State *L, unsigned int opt) {
  G(L)->nopt = opt;
}

LUAI_FUNC void nluaE_setnlua (lua_State *L, int is_nlua) {
  G(L)->is_nlua = is_nlua;
}

LUAI_FUNC void nluaE_setkey (lua_State *L, unsigned int key) {
  G(L)->ekey=key;
}

LUAI_FUNC void nluaE_setfkey (lua_State *L, const char* key) {
  strcpy(G(L)->fkeyp,key);
}
