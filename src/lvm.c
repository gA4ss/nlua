/*
** $Id: lvm.c,v 2.63.1.5 2011/08/17 20:43:11 roberto Exp $
** Lua virtual machine
** See Copyright Notice in lua.h
*/


#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define lvm_c
#define LUA_CORE

#include "nlua.h"

#include "ldebug.h"
#include "ldo.h"
#include "lfunc.h"
#include "lgc.h"
#include "lobject.h"
#include "lopcodes.h"
#include "lstate.h"
#include "lstring.h"
#include "ltable.h"
#include "ltm.h"
#include "lvm.h"
#include "nopcodes.h"
#include "nundump.h"



/* limit for table tag-method chains (to avoid loops) */
#define MAXTAGLOOP	100


const TValue *luaV_tonumber (const TValue *obj, TValue *n) {
  lua_Number num;
  if (ttisnumber(obj)) return obj;
  if (ttisstring(obj) && luaO_str2d(svalue(obj), &num)) {
    setnvalue(n, num);
    return n;
  }
  else
    return NULL;
}


int luaV_tostring (lua_State *L, StkId obj) {
  if (!ttisnumber(obj))
    return 0;
  else {
    char s[LUAI_MAXNUMBER2STR];
    lua_Number n = nvalue(obj);
    lua_number2str(s, n);
    setsvalue2s(L, obj, luaS_new(L, s));
    return 1;
  }
}


static void traceexec (lua_State *L, const Instruction *pc) {
  lu_byte mask = L->hookmask;
  const Instruction *oldpc = L->savedpc;
  L->savedpc = pc;
  if ((mask & LUA_MASKCOUNT) && L->hookcount == 0) {
    resethookcount(L);
    luaD_callhook(L, LUA_HOOKCOUNT, -1);
  }
  if (mask & LUA_MASKLINE) {
    Proto *p = ci_func(L->ci)->l.p;
    int npc = pcRel(pc, p);
    int newline = getlinenm(p, npc);
    /* call linehook when enter a new function, when jump back (loop),
       or when enter a new line */
    if (npc == 0 || pc <= oldpc || newline != getlinenm(p, pcRel(oldpc, p)))
      luaD_callhook(L, LUA_HOOKLINE, newline);
  }
}


static void callTMres (lua_State *L, StkId res, const TValue *f,
                        const TValue *p1, const TValue *p2) {
  ptrdiff_t result = savestack(L, res);
  setobj2s(L, L->top, f);  /* push function */
  setobj2s(L, L->top+1, p1);  /* 1st argument */
  setobj2s(L, L->top+2, p2);  /* 2nd argument */
  luaD_checkstack(L, 3);
  L->top += 3;
  luaD_call(L, L->top - 3, 1);
  res = restorestack(L, result);
  L->top--;
  setobjs2s(L, res, L->top);
}



static void callTM (lua_State *L, const TValue *f, const TValue *p1,
                    const TValue *p2, const TValue *p3) {
  setobj2s(L, L->top, f);  /* push function */
  setobj2s(L, L->top+1, p1);  /* 1st argument */
  setobj2s(L, L->top+2, p2);  /* 2nd argument */
  setobj2s(L, L->top+3, p3);  /* 3th argument */
  luaD_checkstack(L, 4);
  L->top += 4;
  luaD_call(L, L->top - 4, 0);
}


void luaV_gettable (lua_State *L, const TValue *t, TValue *key, StkId val) {
  int loop;
  for (loop = 0; loop < MAXTAGLOOP; loop++) {
    const TValue *tm;
    if (ttistable(t)) {  /* `t' is a table? */
      Table *h = hvalue(t);
      const TValue *res = luaH_get(h, key); /* do a primitive get */
      if (!ttisnil(res) ||  /* result is no nil? */
          (tm = fasttm(L, h->metatable, TM_INDEX)) == NULL) { /* or no TM? */
        setobj2s(L, val, res);
        return;
      }
      /* else will try the tag method */
    }
    else if (ttisnil(tm = luaT_gettmbyobj(L, t, TM_INDEX)))
      luaG_typeerror(L, t, "index");
    if (ttisfunction(tm)) {
      callTMres(L, val, tm, t, key);
      return;
    }
    t = tm;  /* else repeat with `tm' */ 
  }
  luaG_runerror(L, "loop in gettable");
}

void luaV_settable (lua_State *L, const TValue *t, TValue *key, StkId val) {
  int loop;
  TValue temp;
  for (loop = 0; loop < MAXTAGLOOP; loop++) {
    const TValue *tm;
    if (ttistable(t)) {  /* `t' is a table? */
      Table *h = hvalue(t);
      TValue *oldval = luaH_set(L, h, key); /* do a primitive set */
      if (!ttisnil(oldval) ||  /* result is no nil? */
          (tm = fasttm(L, h->metatable, TM_NEWINDEX)) == NULL) { /* or no TM? */
        setobj2t(L, oldval, val);
        h->flags = 0;
        luaC_barriert(L, h, val);
        return;
      }
      /* else will try the tag method */
    }
    else if (ttisnil(tm = luaT_gettmbyobj(L, t, TM_NEWINDEX)))
      luaG_typeerror(L, t, "index");
    if (ttisfunction(tm)) {
      callTM(L, tm, t, key, val);
      return;
    }
    /* else repeat with `tm' */
    setobj(L, &temp, tm);  /* avoid pointing inside table (may rehash) */
    t = &temp;
  }
  luaG_runerror(L, "loop in settable");
}


static int call_binTM (lua_State *L, const TValue *p1, const TValue *p2,
                       StkId res, TMS event) {
  const TValue *tm = luaT_gettmbyobj(L, p1, event);  /* try first operand */
  if (ttisnil(tm))
    tm = luaT_gettmbyobj(L, p2, event);  /* try second operand */
  if (ttisnil(tm)) return 0;
  callTMres(L, res, tm, p1, p2);
  return 1;
}


static const TValue *get_compTM (lua_State *L, Table *mt1, Table *mt2,
                                  TMS event) {
  const TValue *tm1 = fasttm(L, mt1, event);
  const TValue *tm2;
  if (tm1 == NULL) return NULL;  /* no metamethod */
  if (mt1 == mt2) return tm1;  /* same metatables => same metamethods */
  tm2 = fasttm(L, mt2, event);
  if (tm2 == NULL) return NULL;  /* no metamethod */
  if (luaO_rawequalObj(tm1, tm2))  /* same metamethods? */
    return tm1;
  return NULL;
}


static int call_orderTM (lua_State *L, const TValue *p1, const TValue *p2,
                         TMS event) {
  const TValue *tm1 = luaT_gettmbyobj(L, p1, event);
  const TValue *tm2;
  if (ttisnil(tm1)) return -1;  /* no metamethod? */
  tm2 = luaT_gettmbyobj(L, p2, event);
  if (!luaO_rawequalObj(tm1, tm2))  /* different metamethods? */
    return -1;
  callTMres(L, L->top, tm1, p1, p2);
  return !l_isfalse(L->top);
}


static int l_strcmp (const TString *ls, const TString *rs) {
  const char *l = getstr(ls);
  size_t ll = ls->tsv.len;
  const char *r = getstr(rs);
  size_t lr = rs->tsv.len;
  for (;;) {
    int temp = strcoll(l, r);
    if (temp != 0) return temp;
    else {  /* strings are equal up to a `\0' */
      size_t len = strlen(l);  /* index of first `\0' in both strings */
      if (len == lr)  /* r is finished? */
        return (len == ll) ? 0 : 1;
      else if (len == ll)  /* l is finished? */
        return -1;  /* l is smaller than r (because r is not finished) */
      /* both strings longer than `len'; go on comparing (after the `\0') */
      len++;
      l += len; ll -= len; r += len; lr -= len;
    }
  }
}


int luaV_lessthan (lua_State *L, const TValue *l, const TValue *r) {
  int res;
  if (ttype(l) != ttype(r))
    return luaG_ordererror(L, l, r);
  else if (ttisnumber(l))
    return luai_numlt(nvalue(l), nvalue(r));
  else if (ttisstring(l))
    return l_strcmp(rawtsvalue(l), rawtsvalue(r)) < 0;
  else if ((res = call_orderTM(L, l, r, TM_LT)) != -1)
    return res;
  return luaG_ordererror(L, l, r);
}


static int lessequal (lua_State *L, const TValue *l, const TValue *r) {
  int res;
  if (ttype(l) != ttype(r))
    return luaG_ordererror(L, l, r);
  else if (ttisnumber(l))
    return luai_numle(nvalue(l), nvalue(r));
  else if (ttisstring(l))
    return l_strcmp(rawtsvalue(l), rawtsvalue(r)) <= 0;
  else if ((res = call_orderTM(L, l, r, TM_LE)) != -1)  /* first try `le' */
    return res;
  else if ((res = call_orderTM(L, r, l, TM_LT)) != -1)  /* else try `lt' */
    return !res;
  return luaG_ordererror(L, l, r);
}


int luaV_equalval (lua_State *L, const TValue *t1, const TValue *t2) {
  const TValue *tm;
  lua_assert(ttype(t1) == ttype(t2));
  switch (ttype(t1)) {
    case LUA_TNIL: return 1;
    case LUA_TNUMBER: return luai_numeq(nvalue(t1), nvalue(t2));
    case LUA_TBOOLEAN: return bvalue(t1) == bvalue(t2);  /* true must be 1 !! */
    case LUA_TLIGHTUSERDATA: return pvalue(t1) == pvalue(t2);
    case LUA_TUSERDATA: {
      if (uvalue(t1) == uvalue(t2)) return 1;
      tm = get_compTM(L, uvalue(t1)->metatable, uvalue(t2)->metatable,
                         TM_EQ);
      break;  /* will try TM */
    }
    case LUA_TTABLE: {
      if (hvalue(t1) == hvalue(t2)) return 1;
      tm = get_compTM(L, hvalue(t1)->metatable, hvalue(t2)->metatable, TM_EQ);
      break;  /* will try TM */
    }
    default: return gcvalue(t1) == gcvalue(t2);
  }
  if (tm == NULL) return 0;  /* no TM? */
  callTMres(L, L->top, tm, t1, t2);  /* call TM */
  return !l_isfalse(L->top);
}


void luaV_concat (lua_State *L, int total, int last) {
  do {
    StkId top = L->base + last + 1;
    int n = 2;  /* number of elements handled in this pass (at least 2) */
    if (!(ttisstring(top-2) || ttisnumber(top-2)) || !tostring(L, top-1)) {
      if (!call_binTM(L, top-2, top-1, top-2, TM_CONCAT))
        luaG_concaterror(L, top-2, top-1);
    } else if (tsvalue(top-1)->len == 0)  /* second op is empty? */
      (void)tostring(L, top - 2);  /* result is first op (as string) */
    else {
      /* at least two string values; get as many as possible */
      size_t tl = tsvalue(top-1)->len;
      char *buffer;
      int i;
      /* collect total length */
      for (n = 1; n < total && tostring(L, top-n-1); n++) {
        size_t l = tsvalue(top-n-1)->len;
        if (l >= MAX_SIZET - tl) luaG_runerror(L, "string length overflow");
        tl += l;
      }
      buffer = luaZ_openspace(L, &G(L)->buff, tl);
      tl = 0;
      for (i=n; i>0; i--) {  /* concat all strings */
        size_t l = tsvalue(top-i)->len;
        memcpy(buffer+tl, svalue(top-i), l);
        tl += l;
      }
      setsvalue2s(L, top-n, luaS_newlstr(L, buffer, tl));
    }
    total -= n-1;  /* got `n' strings to create 1 new */
    last -= n-1;
  } while (total > 1);  /* repeat until only 1 result left */
}


static void Arith (lua_State *L, StkId ra, const TValue *rb,
                   const TValue *rc, TMS op) {
  TValue tempb, tempc;
  const TValue *b, *c;
  if ((b = luaV_tonumber(rb, &tempb)) != NULL &&
      (c = luaV_tonumber(rc, &tempc)) != NULL) {
    lua_Number nb = nvalue(b), nc = nvalue(c);
    switch (op) {
      case TM_ADD: setnvalue(ra, luai_numadd(nb, nc)); break;
      case TM_SUB: setnvalue(ra, luai_numsub(nb, nc)); break;
      case TM_MUL: setnvalue(ra, luai_nummul(nb, nc)); break;
      case TM_DIV: setnvalue(ra, luai_numdiv(nb, nc)); break;
      case TM_MOD: setnvalue(ra, luai_nummod(nb, nc)); break;
      case TM_POW: setnvalue(ra, luai_numpow(nb, nc)); break;
      case TM_UNM: setnvalue(ra, luai_numunm(nb)); break;
      default: lua_assert(0); break;
    }
  }
  else if (!call_binTM(L, rb, rc, ra, op))
    luaG_aritherror(L, rb, rc);
}

/* 在`luaV_execute'调用中的一些通用宏
 */
#define runtime_check(L, c)	{ if (!(c)) return OPCODE_DISPATCH_CONTINUE; }

/* 获取在栈上的寄存器A */
#define RA(i)	((*base)+GETARG_A(i))

/* 使用后可能会导致栈重新分配 */
#define RB(i)	check_exp(nluaP_getbmode(L,GET_OPCODE(i)) == OpArgR, (*base)+GETARG_B(i))
#define RC(i)	check_exp(nluaP_getcmode(L,GET_OPCODE(i)) == OpArgR, (*base)+GETARG_C(i))
/* 如果B是常量索引则取常量，否则从B为栈索引 */
#define RKB(i) check_exp(nluaP_getbmode(L,GET_OPCODE(i)) == OpArgK, \
  ISK(GETARG_B(i)) ? k+INDEXK(GETARG_B(i)) : (*base)+GETARG_B(i))
/* 如果C是常量索引则取常量，否则从C为栈索引 */
#define RKC(i) check_exp(nluaP_getcmode(L,GET_OPCODE(i)) == OpArgK, \
  ISK(GETARG_C(i)) ? k+INDEXK(GETARG_C(i)) : (*base)+GETARG_C(i))
/* Bx是常量索引，获取常量 */
#define KBx(i) check_exp(nluaP_getbmode(L,GET_OPCODE(i)) == OpArgK, k+GETARG_Bx(i))

/* 执行跳转指令,pc:当前地址，i:要跳转的偏移 */
#define dojump(L,pc,i)	{(pc) += (i); luai_threadyield(L);}
/* 保护执行 */
#define Protect(x)	{ L->savedpc = (*pc); {x;}; (*base) = L->base; }

/* 算数指令 */
#define arith_op(op,tm) { \
  TValue *rb = RKB(ins); \
  TValue *rc = RKC(ins); \
  if (ttisnumber(rb) && ttisnumber(rc)) { \
    lua_Number nb = nvalue(rb), nc = nvalue(rc); \
    setnvalue(ra, op(nb, nc)); \
  } else Protect(Arith(L, ra, rb, rc, tm)); \
}

/* 调用执行前后指令 */
#define do_op_start(ins) nret = G((L))->istart((L), &(ins));
#define do_op_end(ins,r) nret = G((L))->iend((L), &(ins)); return (r);

/* Opcode分派函数的返回值 */
typedef enum {
  OPCODE_DISPATCH_CONTINUE = 0,
  OPCODE_DISPATCH_NEWFRAME = 1,
  OPCODE_DISPATCH_RETURN = 2
} OpCodeRet;

/* opcode处理通用宏 */
#define op_common() \
  int nret=0; \
  StkId ra; \
  do_op_start(ins); \
  ra=RA(ins); \
  TValue *k=cl->p->k; \
  lua_assert((*base) == L->base && L->base == L->ci->base); \
  lua_assert((*base) <= L->top && L->top <= L->stack + L->stacksize); \
  lua_assert(L->top == L->ci->top || luaG_checkopenop(ins));

/* 解密指令 */
static Instruction deins(lua_State *L, Instruction ins,
                         const LClosure *cl, const Instruction *pc) {
  global_State* g = G(L);
  unsigned int opt = g->nopt;
  
  /* 是否解密指令 */
  if (nlo_opt_ei(opt)) {
    nluaV_DeInstruction ideins = G(L)->ideins;
    
    /* 如果是当前函数的第一条指令
     * 判断地址是否相等
     */
    if (&(cl->p->code[0]) == pc) {
      nluaE_setkey(L, NLUA_DEF_KEY);
    } else {
      Instruction pins = *(pc-1);
      unsigned int key = crc32((unsigned char*)&pins, sizeof(Instruction));
      nluaE_setkey(L, key);
    }
    ideins(L, &ins);
  }
  
  return ins;
}

/* 读取pc值的内容 */
static Instruction readpc(lua_State*L, const LClosure *cl, const Instruction *pc) {
  global_State* g = G(L);
  unsigned int opt = g->nopt;
  Instruction ins = *pc;
  
  /* 是否解密指令 */
  if (nlo_opt_ei(opt)) {
    ins = deins(L,ins,cl,pc);
  }
  
  /* 是否解密指令数据 */
  if (nlo_opt_eid(opt)) {
    nluaV_DeInstructionData deidata = g->ideidata;
    deidata(L, &ins);
  }
  
  return ins;
}

static int op_move(lua_State* L, Instruction ins, StkId* base, LClosure* cl,
                   const Instruction** pc, int* pnexeccalls) {
  op_common();
  setobjs2s(L, ra, RB(ins));
  do_op_end(ins,OPCODE_DISPATCH_CONTINUE);
}

static int op_loadk(lua_State* L, Instruction ins, StkId* base, LClosure* cl,
                    const Instruction** pc, int* pnexeccalls) {
  op_common();
  setobj2s(L, ra, KBx(ins));
  do_op_end(ins,OPCODE_DISPATCH_CONTINUE);
}

static int op_loadbool(lua_State* L, Instruction ins, StkId* base, LClosure* cl,
                       const Instruction** pc, int* pnexeccalls) {
  op_common();
  /* 如果C为真，则跳过下一条指令 */
  if (GETARG_C(ins)) {
    (*pc)++;
  }
  do_op_end(ins,OPCODE_DISPATCH_CONTINUE);
}

static int op_loadnil(lua_State* L, Instruction ins, StkId* base, LClosure* cl,
                      const Instruction** pc, int* pnexeccalls) {
  TValue *rb;
  op_common();
  rb=RB(ins);
  do {
    setnilvalue(rb--);
  } while (rb >= ra);
  do_op_end(ins,OPCODE_DISPATCH_CONTINUE);
}

static int op_getupval(lua_State* L, Instruction ins, StkId* base, LClosure* cl,
                       const Instruction** pc, int* pnexeccalls) {
  int b;
  op_common();
  b = GETARG_B(ins);
  setobj2s(L, ra, cl->upvals[b]->v);
  do_op_end(ins,OPCODE_DISPATCH_CONTINUE);
}

static int op_getglobal(lua_State* L, Instruction ins, StkId* base, LClosure* cl,
                        const Instruction** pc, int* pnexeccalls) {
  TValue g;
  TValue *rb;
  op_common();
  rb = KBx(ins);
  sethvalue(L, &g, cl->env);
  lua_assert(ttisstring(rb));
  Protect(luaV_gettable(L, &g, rb, ra));
  
  do_op_end(ins,OPCODE_DISPATCH_CONTINUE);
}

static int op_gettable(lua_State* L, Instruction ins, StkId* base, LClosure* cl,
                       const Instruction** pc, int* pnexeccalls) {
  op_common();
  Protect(luaV_gettable(L, RB(ins), RKC(ins), ra));
  
  do_op_end(ins,OPCODE_DISPATCH_CONTINUE);
}

static int op_setglobal(lua_State* L, Instruction ins, StkId* base, LClosure* cl,
                        const Instruction** pc, int* pnexeccalls) {
  TValue g;
  op_common();
  
  sethvalue(L, &g, cl->env);
  lua_assert(ttisstring(KBx(ins)));
  Protect(luaV_settable(L, &g, KBx(ins), ra));
  
  do_op_end(ins,OPCODE_DISPATCH_CONTINUE);
}

static int op_setupval(lua_State* L, Instruction ins, StkId* base, LClosure* cl,
                       const Instruction** pc, int* pnexeccalls) {
  UpVal *uv;
  op_common();
  
  uv = cl->upvals[GETARG_B(ins)];
  setobj(L, uv->v, ra);
  luaC_barrier(L, uv, ra);
  
  do_op_end(ins,OPCODE_DISPATCH_CONTINUE);
}

static int op_settable(lua_State* L, Instruction ins, StkId* base, LClosure* cl,
                       const Instruction** pc, int* pnexeccalls) {
  op_common();
  
  Protect(luaV_settable(L, ra, RKB(ins), RKC(ins)));
  
  do_op_end(ins,OPCODE_DISPATCH_CONTINUE);
}

static int op_newtable(lua_State* L, Instruction ins, StkId* base, LClosure* cl,
                       const Instruction** pc, int* pnexeccalls) {
  int b, c;
  op_common();
  
  b = GETARG_B(ins);
  c = GETARG_C(ins);
  sethvalue(L, ra, luaH_new(L, luaO_fb2int(b), luaO_fb2int(c)));
  Protect(luaC_checkGC(L));
  
  do_op_end(ins,OPCODE_DISPATCH_CONTINUE);
}

static int op_self(lua_State* L, Instruction ins, StkId* base, LClosure* cl,
                   const Instruction** pc, int* pnexeccalls) {
  StkId rb;
  op_common();
  
  rb = RB(ins);
  setobjs2s(L, ra+1, rb);
  Protect(luaV_gettable(L, rb, RKC(ins), ra));
  
  do_op_end(ins,OPCODE_DISPATCH_CONTINUE);
}

static int op_add(lua_State* L, Instruction ins, StkId* base, LClosure* cl,
                  const Instruction** pc, int* pnexeccalls) {
  op_common();
  
  arith_op(luai_numadd, TM_ADD);
  
  do_op_end(ins,OPCODE_DISPATCH_CONTINUE);
}

static int op_sub(lua_State* L, Instruction ins, StkId* base, LClosure* cl,
                  const Instruction** pc, int* pnexeccalls) {
  op_common();
  
  arith_op(luai_numsub, TM_SUB);
  
  do_op_end(ins,OPCODE_DISPATCH_CONTINUE);
}

static int op_mul(lua_State* L, Instruction ins, StkId* base, LClosure* cl,
                  const Instruction** pc, int* pnexeccalls) {
  op_common();
  
  arith_op(luai_nummul, TM_MUL);
  
  do_op_end(ins,OPCODE_DISPATCH_CONTINUE);
}

static int op_div(lua_State* L, Instruction ins, StkId* base, LClosure* cl,
                  const Instruction** pc, int* pnexeccalls) {
  op_common();
  
  arith_op(luai_numdiv, TM_DIV);
  
  do_op_end(ins,OPCODE_DISPATCH_CONTINUE);
}

static int op_mod(lua_State* L, Instruction ins, StkId* base, LClosure* cl,
                  const Instruction** pc, int* pnexeccalls) {
  op_common();
  
  arith_op(luai_nummod, TM_MOD);
  
  do_op_end(ins,OPCODE_DISPATCH_CONTINUE);
}

static int op_pow(lua_State* L, Instruction ins, StkId* base, LClosure* cl,
                  const Instruction** pc, int* pnexeccalls) {
  op_common();
  
  arith_op(luai_numpow, TM_POW);
  
  do_op_end(ins,OPCODE_DISPATCH_CONTINUE);
}

static int op_unm(lua_State* L, Instruction ins, StkId* base, LClosure* cl,
                  const Instruction** pc, int* pnexeccalls) {
  TValue *rb;
  op_common();
  
  rb = RB(ins);
  if (ttisnumber(rb)) {
    lua_Number nb = nvalue(rb);
    setnvalue(ra, luai_numunm(nb));
  }
  else {
    Protect(Arith(L, ra, rb, rb, TM_UNM));
  }
  
  do_op_end(ins,OPCODE_DISPATCH_CONTINUE);
}

static int op_not(lua_State* L, Instruction ins, StkId* base, LClosure* cl,
                  const Instruction** pc, int* pnexeccalls) {
  int res;
  op_common();
  
  res = l_isfalse(RB(ins));  /* next assignment may change this value */
  setbvalue(ra, res);
  
  do_op_end(ins,OPCODE_DISPATCH_CONTINUE);
}

static int op_len(lua_State* L, Instruction ins, StkId* base, LClosure* cl,
                  const Instruction** pc, int* pnexeccalls) {
  const TValue *rb;
  op_common();
  
  rb = RB(ins);
  switch (ttype(rb)) {
    case LUA_TTABLE: {
      setnvalue(ra, cast_num(luaH_getn(hvalue(rb))));
      break;
    }
    case LUA_TSTRING: {
      setnvalue(ra, cast_num(tsvalue(rb)->len));
      break;
    }
    default: {  /* 尝试元操作 */
      Protect(
              if (!call_binTM(L, rb, luaO_nilobject, ra, TM_LEN))
              luaG_typeerror(L, rb, "get length of");
              )
    }
  }
  
  do_op_end(ins,OPCODE_DISPATCH_CONTINUE);
}

static int op_concat(lua_State* L, Instruction ins, StkId* base, LClosure* cl,
                     const Instruction** pc, int* pnexeccalls) {
  int b;
  int c;
  op_common();
  
  b = GETARG_B(ins);
  c = GETARG_C(ins);
  Protect(luaV_concat(L, c-b+1, c); luaC_checkGC(L));
  setobjs2s(L, RA(ins), (*base)+b);
  
  do_op_end(ins,OPCODE_DISPATCH_CONTINUE);
}

static int op_jmp(lua_State* L, Instruction ins, StkId* base, LClosure* cl,
                  const Instruction** pc, int* pnexeccalls) {
  op_common();
  
  dojump(L, (*pc), GETARG_sBx(ins));
  
  do_op_end(ins,OPCODE_DISPATCH_CONTINUE);
}

static int op_eq(lua_State* L, Instruction ins, StkId* base, LClosure* cl,
                 const Instruction** pc, int* pnexeccalls) {
  TValue *rb;
  TValue *rc;
  op_common();
  
  rb = RKB(ins);
  rc = RKC(ins);
  Protect(
          if (equalobj(L, rb, rc) == GETARG_A(ins))
          dojump(L, (*pc), GETARG_sBx(readpc(L,cl,*pc)));
          )
  (*pc)++;
  do_op_end(ins,OPCODE_DISPATCH_CONTINUE);
}

static int op_lt(lua_State* L, Instruction ins, StkId* base, LClosure* cl,
                 const Instruction** pc, int* pnexeccalls) {
  op_common();
  
  Protect(
          if (luaV_lessthan(L, RKB(ins), RKC(ins)) == GETARG_A(ins))
          dojump(L, (*pc), GETARG_sBx(readpc(L,cl,*pc)));
          )
  (*pc)++;
  do_op_end(ins,OPCODE_DISPATCH_CONTINUE);
}

static int op_le(lua_State* L, Instruction ins, StkId* base, LClosure* cl,
                 const Instruction** pc, int* pnexeccalls) {
  op_common();
  
  Protect(
          if (lessequal(L, RKB(ins), RKC(ins)) == GETARG_A(ins))
          dojump(L, (*pc), GETARG_sBx(readpc(L,cl,*pc)));
          )
  (*pc)++;
  do_op_end(ins,OPCODE_DISPATCH_CONTINUE);
}

static int op_test(lua_State* L, Instruction ins, StkId* base, LClosure* cl,
                   const Instruction** pc, int* pnexeccalls) {
  op_common();
  
  if (l_isfalse(ra) != GETARG_C(ins))
    dojump(L, (*pc), GETARG_sBx(readpc(L,cl,*pc)));
  (*pc)++;
  do_op_end(ins,OPCODE_DISPATCH_CONTINUE);
}

static int op_testset(lua_State* L, Instruction ins, StkId* base, LClosure* cl,
                      const Instruction** pc, int* pnexeccalls) {
  TValue *rb;
  op_common();
  
  rb = RB(ins);
  if (l_isfalse(rb) != GETARG_C(ins)) {
    setobjs2s(L, ra, rb);
    dojump(L, (*pc), GETARG_sBx(readpc(L,cl,*pc)));
  }
  (*pc)++;
  do_op_end(ins,OPCODE_DISPATCH_CONTINUE);
}

static int op_call(lua_State* L, Instruction ins, StkId* base, LClosure* cl,
                   const Instruction** pc, int* pnexeccalls) {
  int b;
  int nresults;
  op_common();
  
  b = GETARG_B(ins);
  nresults = GETARG_C(ins) - 1;
  
  if (b != 0) L->top = ra+b;  /* else previous instruction set top */
  L->savedpc = *pc;
  switch (luaD_precall(L, ra, nresults)) {
    case PCRLUA: {
      (*pnexeccalls)++;
      do_op_end(ins,OPCODE_DISPATCH_NEWFRAME);  /* restart luaV_execute over new Lua function */
    }
    case PCRC: {
      /* it was a C function (`precall' called it); adjust results */
      if (nresults >= 0) L->top = L->ci->top;
      (*base)=L->base;
      do_op_end(ins,OPCODE_DISPATCH_CONTINUE);
    }
    default: {
      do_op_end(ins,OPCODE_DISPATCH_RETURN);    /* yield */
    }
  }
  
  do_op_end(ins,OPCODE_DISPATCH_CONTINUE);
}

static int op_tailcall(lua_State* L, Instruction ins, StkId* base, LClosure* cl,
                       const Instruction** pc, int* pnexeccalls) {
  int b;
  op_common();
  
  b = GETARG_B(ins);
  if (b != 0) L->top = ra+b;  /* else previous instruction set top */
  L->savedpc = *pc;
  lua_assert(GETARG_C(ins) - 1 == LUA_MULTRET);
  switch (luaD_precall(L, ra, LUA_MULTRET)) {
    case PCRLUA: {
      /* tail call: put new frame in place of previous one */
      CallInfo *ci = L->ci - 1;  /* previous frame */
      int aux;
      StkId func = ci->func;
      StkId pfunc = (ci+1)->func;  /* previous function index */
      if (L->openupval) luaF_close(L, ci->base);
      L->base = ci->base = ci->func + ((ci+1)->base - pfunc);
      for (aux = 0; pfunc+aux < L->top; aux++)  /* move frame down */
        setobjs2s(L, func+aux, pfunc+aux);
      ci->top = L->top = func+aux;  /* correct top */
      lua_assert(L->top == L->base + clvalue(func)->l.p->maxstacksize);
      ci->savedpc = L->savedpc;
      ci->tailcalls++;  /* one more call lost */
      L->ci--;  /* remove new frame */
      do_op_end(ins,OPCODE_DISPATCH_NEWFRAME);
    }
    case PCRC: {  /* it was a C function (`precall' called it) */
      (*base) = L->base;
      do_op_end(ins,OPCODE_DISPATCH_CONTINUE);
    }
    default: {
      do_op_end(ins,OPCODE_DISPATCH_RETURN);  /* yield */
    }
  }
  
  do_op_end(ins,OPCODE_DISPATCH_CONTINUE);
}

static int op_return(lua_State* L, Instruction ins, StkId* base, LClosure* cl,
                     const Instruction** pc, int* pnexeccalls) {
  int b;
  op_common();
  
  b = GETARG_B(ins);
  if (b != 0) L->top = ra+b-1;
  if (L->openupval) luaF_close(L, (*base));
  L->savedpc = *pc;
  b = luaD_poscall(L, ra);
  if (--(*pnexeccalls) == 0) {  /* was previous function running `here'? */
    do_op_end(ins,OPCODE_DISPATCH_RETURN); /* no: return */
  } else {  /* yes: continue its execution */
    if (b) L->top = L->ci->top;
    lua_assert(isLua(L->ci));
    lua_assert(GET_OPCODE(*((L->ci)->savedpc - 1)) == OP_CALL);/* 这里要做转编码判断 */
    do_op_end(ins,OPCODE_DISPATCH_NEWFRAME);
  }
  
  do_op_end(ins,OPCODE_DISPATCH_CONTINUE);
}

static int op_forloop(lua_State* L, Instruction ins, StkId* base, LClosure* cl,
                      const Instruction** pc, int* pnexeccalls) {
  lua_Number step;
  lua_Number idx;
  lua_Number limit;
  op_common();
  
  step = nvalue(ra+2);
  idx = luai_numadd(nvalue(ra), step); /* increment index */
  limit = nvalue(ra+1);
  if (luai_numlt(0, step) ? luai_numle(idx, limit)
      : luai_numle(limit, idx)) {
    dojump(L, *pc, GETARG_sBx(ins));  /* jump back */
    setnvalue(ra, idx);  /* update internal index... */
    setnvalue(ra+3, idx);  /* ...and external index */
  }
  
  do_op_end(ins,OPCODE_DISPATCH_CONTINUE);
}

static int op_forprep(lua_State* L, Instruction ins, StkId* base, LClosure* cl,
                      const Instruction** pc, int* pnexeccalls) {
  const TValue *init;
  const TValue *plimit;
  const TValue *pstep;
  op_common();
  
  init = ra;
  plimit = ra+1;
  pstep = ra+2;
  L->savedpc = *pc;  /* next steps may throw errors */
  if (!tonumber(init, ra))
    luaG_runerror(L, LUA_QL("for") " initial value must be a number");
  else if (!tonumber(plimit, ra+1))
    luaG_runerror(L, LUA_QL("for") " limit must be a number");
  else if (!tonumber(pstep, ra+2))
    luaG_runerror(L, LUA_QL("for") " step must be a number");
  setnvalue(ra, luai_numsub(nvalue(ra), nvalue(pstep)));
  dojump(L, *pc, GETARG_sBx(ins));

  do_op_end(ins,OPCODE_DISPATCH_CONTINUE);
}

static int op_tforloop(lua_State* L, Instruction ins, StkId* base, LClosure* cl,
                       const Instruction** pc, int* pnexeccalls) {
  StkId cb;
  op_common();
  
  cb = ra + 3;  /* call base */
  setobjs2s(L, cb+2, ra+2);
  setobjs2s(L, cb+1, ra+1);
  setobjs2s(L, cb, ra);
  L->top = cb+3;  /* func. + 2 args (state and index) */
  Protect(luaD_call(L, cb, GETARG_C(ins)));
  L->top = L->ci->top;
  cb = RA(ins) + 3;  /* previous call may change the stack */
  if (!ttisnil(cb)) {  /* continue loop? */
    setobjs2s(L, cb-1, cb);  /* save control variable */
    dojump(L, *pc, GETARG_sBx(readpc(L,cl,*pc)));  /* jump back */
  }
  (*pc)++;
  do_op_end(ins,OPCODE_DISPATCH_CONTINUE);
}

static int op_setlist(lua_State* L, Instruction ins, StkId* base, LClosure* cl,
                      const Instruction** pc, int* pnexeccalls) {
  int n;
  int c;
  int last;
  Table *h;
  op_common();
  
  n = GETARG_B(ins);
  c = GETARG_C(ins);

  if (n == 0) {
    n = cast_int(L->top - ra) - 1;
    L->top = L->ci->top;
  }
  /* 如果寄存器c为0，则 */
  if (c == 0) {
    //c = cast_int(*(*pc)++);
    c = cast_int(readpc(L,cl,*pc));
    (*pc)++;
  }
  runtime_check(L, ttistable(ra));
  h = hvalue(ra);
  last = ((c-1)*LFIELDS_PER_FLUSH) + n;
  if (last > h->sizearray)  /* needs more space? */
    luaH_resizearray(L, h, last);  /* pre-alloc it at once */
  for (; n > 0; n--) {
    TValue *val = ra+n;
    setobj2t(L, luaH_setnum(L, h, last--), val);
    luaC_barriert(L, h, val);
  }

  do_op_end(ins,OPCODE_DISPATCH_CONTINUE);
}

static int op_close(lua_State* L, Instruction ins, StkId* base, LClosure* cl,
                    const Instruction** pc, int* pnexeccalls) {
  op_common();
  
  luaF_close(L, ra);
  
  do_op_end(ins,OPCODE_DISPATCH_CONTINUE);
}

static int op_closure(lua_State* L, Instruction ins, StkId* base, LClosure* cl,
                      const Instruction** pc, int* pnexeccalls) {
  Proto *p;
  Closure *ncl;
  int nup, j;
  op_common();
  
  p = cl->p->p[GETARG_Bx(ins)];
  nup = p->nups;
  ncl = luaF_newLclosure(L, nup, cl->env);
  ncl->l.p = p;
  for (j=0; j<nup; j++, (*pc)++) {
    if (GET_OPCODE(readpc(L,cl,*pc)) == OP_GETUPVAL)
      ncl->l.upvals[j] = cl->upvals[GETARG_B(readpc(L,cl,*pc))];
    else {
      lua_assert(GET_OPCODE(readpc(L,**pc)) == OP_MOVE);
      ncl->l.upvals[j] = luaF_findupval(L, *base + GETARG_B(readpc(L,cl,*pc)));
    }
  }
  setclvalue(L, ra, ncl);
  Protect(luaC_checkGC(L));
  
  do_op_end(ins,OPCODE_DISPATCH_CONTINUE);
}

static int op_vararg(lua_State* L, Instruction ins, StkId* base, LClosure* cl,
                     const Instruction** pc, int* pnexeccalls) {
  int b;
  int j;
  int n;
  CallInfo *ci;
  op_common();
  
  b = GETARG_B(ins) - 1;
  ci = L->ci;
  n = cast_int(ci->base - ci->func) - cl->p->numparams - 1;
  if (b == LUA_MULTRET) {
    Protect(luaD_checkstack(L, n));
    ra = RA(ins);  /* previous call may change the stack */
    b = n;
    L->top = ra + n;
  }
  for (j = 0; j < b; j++) {
    if (j < n) {
      setobjs2s(L, ra + j, ci->base - n + j);
    }
    else {
      setnilvalue(ra + j);
    }
  }
  
  do_op_end(ins,OPCODE_DISPATCH_CONTINUE);
}

/* Opcode分派函数表 */
nluaV_Instruction nluaV_opcodedisp[NUM_OPCODES] = {
  op_move
  ,op_loadk
  ,op_loadbool
  ,op_loadnil
  ,op_getupval
  ,op_getglobal
  ,op_gettable
  ,op_setglobal
  ,op_setupval
  ,op_settable
  ,op_newtable
  ,op_self
  ,op_add
  ,op_sub
  ,op_mul
  ,op_div
  ,op_mod
  ,op_pow
  ,op_unm
  ,op_not
  ,op_len
  ,op_concat
  ,op_jmp
  ,op_eq
  ,op_lt
  ,op_le
  ,op_test
  ,op_testset
  ,op_call
  ,op_tailcall
  ,op_return
  ,op_forloop
  ,op_forprep
  ,op_tforloop
  ,op_setlist
  ,op_close
  ,op_closure
  ,op_vararg
};

/* 虚拟执行 */
void luaV_execute (lua_State *L, int nexeccalls) {
  LClosure *cl;
  StkId base;
  //TValue *k;
  const Instruction *pc;
  nluaV_Instruction disp;
    
reentry:  /* 重新进入点 */
  lua_assert(isLua(L->ci));       /* 必须是lua函数 */
  pc = L->savedpc;                /* 获取当前的pc值 */
  cl = &clvalue(L->ci->func)->l;  /* 获取当前的函数结构指针 */
  base = L->base;                 /* 获取当前的栈基 */
  //k = cl->p->k;                 /* 获取当前的常量队列 */
  
#if 0
  /* 是否加密指令 */
  if (nlo_ei(nopt)) {
    unsigned int key = crc32(&(f->code[0]), (f->sizecode)*sizeof(Instruction));
    /* 加密第一条代码 */
    nluaE_setkey(L, key);
    g->ienins(D->L,&(f->code[0]));
    
    /* 加密其余指令 */
    for (i=1; i<f->sizecode; i++) {
      /* 使用其他指令的密文hash作为key */
      key = crc32(&(f->code[i-1]), sizeof(Instruction));
      nluaE_setkey(L, key);
      g->ienins(D->L,&(f->code[i]));
    }
  }
#endif
  
  /* 解释主循环 */
  for (;;) {
    Instruction i;
    StkId ra;
    OpCode o;
    int ret;
    
    /* 解密指令 */
    i = deins(L,*pc,cl,pc);
    pc++;
    
    /* 是否进入hook */
    if ((L->hookmask & (LUA_MASKLINE | LUA_MASKCOUNT)) &&
        (--L->hookcount == 0 || L->hookmask & LUA_MASKLINE)) {
      traceexec(L, pc);
      if (L->status == LUA_YIELD) {  /* hook函数挂起 */
        L->savedpc = pc - 1;
        return;
      }
      base = L->base;
    }
    
    /* 警告!! 一些调用会引起栈的重新分配以及使得`ra`寄存器无效 */
    ra = base+GETARG_A(i);
    lua_assert(base == L->base && L->base == L->ci->base);
    lua_assert(base <= L->top && L->top <= L->stack + L->stacksize);
    lua_assert(L->top == L->ci->top || luaG_checkopenop(i));
    o = GET_OPCODE(i);
    
    /* 调用派遣函数 */
    disp = G(L)->oprule.opcodedisp[o];
    ret = disp(L, i, &base, cl, &pc, &nexeccalls);
    switch (ret) {
      case OPCODE_DISPATCH_CONTINUE:
        continue;
      case OPCODE_DISPATCH_NEWFRAME:
        goto reentry;
      case OPCODE_DISPATCH_RETURN:
        return;
      default:
        continue;
        break;
    }
  }/* end for */
}

