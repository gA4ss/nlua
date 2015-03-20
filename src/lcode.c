/*
** $Id: lcode.c,v 2.25.1.5 2011/01/31 14:53:16 roberto Exp $
** Code generator for Lua
** See Copyright Notice in lua.h
*/


#include <stdlib.h>

#define lcode_c
#define LUA_CORE

#include "nlua.h"

#include "lcode.h"
#include "ldebug.h"
#include "ldo.h"
#include "lgc.h"
#include "llex.h"
#include "lmem.h"
#include "lobject.h"
#include "lopcodes.h"
#include "lparser.h"
#include "ltable.h"
#include "nopcodes.h"

/* 准备进行跳转 */
#define hasjumps(e)	((e)->t != (e)->f)

/* 表达式是一个实数 */
static int isnumeral(expdesc *e) {
  return (e->k == VKNUM && e->t == NO_JUMP && e->f == NO_JUMP);
}


void luaK_nil (FuncState *fs, int from, int n) {
  Instruction *previous;
  Proto *f = fs->f;
  
  if (fs->pc > fs->lasttarget) {  /* no jumps to current position? */
    if (fs->pc == 0) {  /* function start? */
      if (from >= fs->nactvar)
        return;  /* positions are already clean */
    }
    else {
      previous = &fs->f->code[fs->pc-1];
      if (GET_OPCODE(*previous) == P_OP(f,I_LOADNIL)) {
        int pfrom = GETARG_A(*previous);
        int pto = GETARG_B(*previous);
        if (pfrom <= from && from <= pto+1) {  /* can connect both? */
          if (from+n-1 > pto)
            SETARG_B(*previous, from+n-1);
          return;
        }
      }
    }
  }
  luaK_codeABC(fs, P_OP(f,I_LOADNIL), from, from+n-1, 0);  /* else no optimization */
}

/* 汇编jump指令 */
int luaK_jump (FuncState *fs) {
  int j;
  Proto *f = fs->f;
  int jpc = fs->jpc;  /* save list of jumps to here */
  fs->jpc = NO_JUMP;
  j = luaK_codeAsBx(fs, P_OP(f,I_JMP), 0, NO_JUMP);
  luaK_concat(fs, &j, jpc);  /* keep them on hold */
  return j;
}

void luaK_ret (FuncState *fs, int first, int nret) {
  Proto *f = fs->f;
  luaK_codeABC(fs, P_OP(f,I_RETURN), first, nret+1, 0);
}

static int condjump (FuncState *fs, OpCode op, int A, int B, int C) {
  luaK_codeABC(fs, op, A, B, C);
  return luaK_jump(fs);
}

static void fixjump (FuncState *fs, int pc, int dest) {
  Instruction *jmp = &fs->f->code[pc];
  int offset = dest-(pc+1);
  lua_assert(dest != NO_JUMP);
  if (abs(offset) > MAXARG_sBx)
    luaX_syntaxerror(fs->ls, "control structure too long");
  SETARG_sBx(*jmp, offset);
}


/*
** returns current `pc' and marks it as a jump target (to avoid wrong
** optimizations with consecutive instructions not in the same basic block).
*/
int luaK_getlabel (FuncState *fs) {
  fs->lasttarget = fs->pc;
  return fs->pc;
}


static int getjump (FuncState *fs, int pc) {
  int offset = GETARG_sBx(fs->f->code[pc]);
  if (offset == NO_JUMP)  /* point to itself represents end of list */
    return NO_JUMP;  /* end of list */
  else
    return (pc+1)+offset;  /* turn offset into absolute position */
}


static Instruction *getjumpcontrol (FuncState *fs, int pc) {
  Instruction *pi = &fs->f->code[pc];
  Proto *f = fs->f;
  if (pc >= 1 && nluaP_testtmode(fs->L, f, GET_OPCODE(*(pi-1))))
    return pi-1;
  else
    return pi;
}


/*
** check whether list has any jump that do not produce a value
** (or produce an inverted value)
*/
static int need_value (FuncState *fs, int list) {
  Proto *f = fs->f;
  for (; list != NO_JUMP; list = getjump(fs, list)) {
    Instruction i = *getjumpcontrol(fs, list);
    if (GET_OPCODE(i) != P_OP(f,I_TESTSET)) return 1;
  }
  return 0;  /* not found */
}

/* 对测试寄存器指令进行修复 */
static int patchtestreg (FuncState *fs, int node, int reg) {
  Proto *f = fs->f;
  Instruction *i = getjumpcontrol(fs, node);
  if (GET_OPCODE(*i) != P_OP(f,I_TESTSET))
    return 0;  /* cannot patch other instructions */
  if (reg != NO_REG && reg != GETARG_B(*i))
    SETARG_A(*i, reg);
  else { /* no register to put value or register already has the value */
    *i = nluaP_createABC(fs->L, P_OP(f,I_TEST), GETARG_B(*i), 0, GETARG_C(*i));
  }

  return 1;
}


static void removevalues (FuncState *fs, int list) {
  for (; list != NO_JUMP; list = getjump(fs, list))
      patchtestreg(fs, list, NO_REG);
}


static void patchlistaux (FuncState *fs, int list, int vtarget, int reg,
                          int dtarget) {
  while (list != NO_JUMP) {
    int next = getjump(fs, list);
    if (patchtestreg(fs, list, reg))
      fixjump(fs, list, vtarget);
    else
      fixjump(fs, list, dtarget);  /* jump to default target */
    list = next;
  }
}


static void dischargejpc (FuncState *fs) {
  patchlistaux(fs, fs->jpc, fs->pc, NO_REG, fs->pc);
  fs->jpc = NO_JUMP;
}


void luaK_patchlist (FuncState *fs, int list, int target) {
  if (target == fs->pc)
    luaK_patchtohere(fs, list);
  else {
    lua_assert(target < fs->pc);
    patchlistaux(fs, list, target, NO_REG, target);
  }
}


void luaK_patchtohere (FuncState *fs, int list) {
  luaK_getlabel(fs);
  luaK_concat(fs, &fs->jpc, list);
}

/* 写入一条concat指令 */
void luaK_concat (FuncState *fs, int *l1, int l2) {
  if (l2 == NO_JUMP) return;
  else if (*l1 == NO_JUMP)
    *l1 = l2;
  else {
    int list = *l1;
    int next;
    while ((next = getjump(fs, list)) != NO_JUMP)  /* find last element */
      list = next;
    fixjump(fs, list, l2);
  }
}


void luaK_checkstack (FuncState *fs, int n) {
  int newstack = fs->freereg + n;
  if (newstack > fs->f->maxstacksize) {
    if (newstack >= MAXSTACK)
      luaX_syntaxerror(fs->ls, "function or expression too complex");
    fs->f->maxstacksize = cast_byte(newstack);
  }
}


void luaK_reserveregs (FuncState *fs, int n) {
  luaK_checkstack(fs, n);
  fs->freereg += n;
}


static void freereg (FuncState *fs, int reg) {
  if (!ISK(reg) && reg >= fs->nactvar) {
    fs->freereg--;
    lua_assert(reg == fs->freereg);
  }
}


static void freeexp (FuncState *fs, expdesc *e) {
  if (e->k == VNONRELOC)
    freereg(fs, e->u.s.info);
}


static int addk (FuncState *fs, TValue *k, TValue *v) {
  lua_State *L = fs->L;
  TValue *idx = luaH_set(L, fs->h, k);
  Proto *f = fs->f;
  int oldsize = f->sizek;
  if (ttisnumber(idx)) {
    lua_assert(luaO_rawequalObj(&fs->f->k[cast_int(nvalue(idx))], v));
    return cast_int(nvalue(idx));
  }
  else {  /* constant not found; create a new entry */
    setnvalue(idx, cast_num(fs->nk));
    luaM_growvector(L, f->k, fs->nk, f->sizek, TValue,
                    MAXARG_Bx, "constant table overflow");
    while (oldsize < f->sizek) setnilvalue(&f->k[oldsize++]);
    setobj(L, &f->k[fs->nk], v);
    luaC_barrier(L, f, v);
    return fs->nk++;
  }
}


int luaK_stringK (FuncState *fs, TString *s) {
  TValue o;
  setsvalue(fs->L, &o, s);
  return addk(fs, &o, &o);
}


int luaK_numberK (FuncState *fs, lua_Number r) {
  TValue o;
  setnvalue(&o, r);
  return addk(fs, &o, &o);
}


static int boolK (FuncState *fs, int b) {
  TValue o;
  setbvalue(&o, b);
  return addk(fs, &o, &o);
}


static int nilK (FuncState *fs) {
  TValue k, v;
  setnilvalue(&v);
  /* cannot use nil as key; instead use table itself to represent nil */
  sethvalue(fs->L, &k, fs->h);
  return addk(fs, &k, &v);
}


void luaK_setreturns (FuncState *fs, expdesc *e, int nresults) {
  if (e->k == VCALL) {  /* expression is an open function call? */
    SETARG_C(getcode(fs, e), nresults+1);
  }
  else if (e->k == VVARARG) {
    SETARG_B(getcode(fs, e), nresults+1);
    SETARG_A(getcode(fs, e), fs->freereg);
    luaK_reserveregs(fs, 1);
  }
}


void luaK_setoneret (FuncState *fs, expdesc *e) {
  if (e->k == VCALL) {  /* expression is an open function call? */
    e->k = VNONRELOC;
    e->u.s.info = GETARG_A(getcode(fs, e));
  }
  else if (e->k == VVARARG) {
    SETARG_B(getcode(fs, e), 2);
    e->k = VRELOCABLE;  /* can relocate its simple result */
  }
}


void luaK_dischargevars (FuncState *fs, expdesc *e) {
  Proto *f = fs->f;
  switch (e->k) {
    case VLOCAL: {
      e->k = VNONRELOC;
      break;
    }
    case VUPVAL: {
      e->u.s.info = luaK_codeABC(fs, P_OP(f,I_GETUPVAL), 0, e->u.s.info, 0);
      e->k = VRELOCABLE;
      break;
    }
    case VGLOBAL: {
      e->u.s.info = luaK_codeABx(fs, P_OP(f,I_GETGLOBAL), 0, e->u.s.info);
      e->k = VRELOCABLE;
      break;
    }
    case VINDEXED: {
      freereg(fs, e->u.s.aux);
      freereg(fs, e->u.s.info);
      e->u.s.info = luaK_codeABC(fs, P_OP(f,I_GETTABLE), 0, e->u.s.info, e->u.s.aux);
      e->k = VRELOCABLE;
      break;
    }
    case VVARARG:
    case VCALL: {
      luaK_setoneret(fs, e);
      break;
    }
    default: break;  /* there is one value available (somewhere) */
  }
}


static int code_label (FuncState *fs, int A, int b, int jump) {
  Proto *f = fs->f;
  luaK_getlabel(fs);  /* those instructions may be jump targets */
  return luaK_codeABC(fs, P_OP(f,I_LOADBOOL), A, b, jump);
}

static void discharge2reg (FuncState *fs, expdesc *e, int reg) {
  Proto *f = fs->f;
  luaK_dischargevars(fs, e);
  switch (e->k) {
    case VNIL: {
      luaK_nil(fs, reg, 1);
      break;
    }
    case VFALSE:  case VTRUE: {
      luaK_codeABC(fs, P_OP(f,I_LOADBOOL), reg, e->k == VTRUE, 0);
      break;
    }
    case VK: {
      luaK_codeABx(fs, P_OP(f,I_LOADK), reg, e->u.s.info);
      break;
    }
    case VKNUM: {
      luaK_codeABx(fs, P_OP(f,I_LOADK), reg, luaK_numberK(fs, e->u.nval));
      break;
    }
    case VRELOCABLE: {
      Instruction *pc = &getcode(fs, e);
      SETARG_A(*pc, reg);
      break;
    }
    case VNONRELOC: {
      if (reg != e->u.s.info)
        luaK_codeABC(fs, P_OP(f,I_MOVE), reg, e->u.s.info, 0);
      break;
    }
    default: {
      lua_assert(e->k == VVOID || e->k == VJMP);
      return;  /* nothing to do... */
    }
  }
  e->u.s.info = reg;
  e->k = VNONRELOC;
}


static void discharge2anyreg (FuncState *fs, expdesc *e) {
  if (e->k != VNONRELOC) {
    luaK_reserveregs(fs, 1);
    discharge2reg(fs, e, fs->freereg-1);
  }
}


static void exp2reg (FuncState *fs, expdesc *e, int reg) {
  discharge2reg(fs, e, reg);
  if (e->k == VJMP)
    luaK_concat(fs, &e->t, e->u.s.info);  /* put this jump in `t' list */
  if (hasjumps(e)) {
    int final;  /* position after whole expression */
    int p_f = NO_JUMP;  /* position of an eventual LOAD false */
    int p_t = NO_JUMP;  /* position of an eventual LOAD true */
    if (need_value(fs, e->t) || need_value(fs, e->f)) {
      int fj = (e->k == VJMP) ? NO_JUMP : luaK_jump(fs);
      p_f = code_label(fs, reg, 0, 1);
      p_t = code_label(fs, reg, 1, 0);
      luaK_patchtohere(fs, fj);
    }
    final = luaK_getlabel(fs);
    patchlistaux(fs, e->f, final, reg, p_f);
    patchlistaux(fs, e->t, final, reg, p_t);
  }
  e->f = e->t = NO_JUMP;
  e->u.s.info = reg;
  e->k = VNONRELOC;
}


void luaK_exp2nextreg (FuncState *fs, expdesc *e) {
  luaK_dischargevars(fs, e);
  freeexp(fs, e);
  luaK_reserveregs(fs, 1);
  exp2reg(fs, e, fs->freereg - 1);
}


int luaK_exp2anyreg (FuncState *fs, expdesc *e) {
  luaK_dischargevars(fs, e);
  if (e->k == VNONRELOC) {
    if (!hasjumps(e)) return e->u.s.info;  /* exp is already in a register */
    if (e->u.s.info >= fs->nactvar) {  /* reg. is not a local? */
      exp2reg(fs, e, e->u.s.info);  /* put value on it */
      return e->u.s.info;
    }
  }
  luaK_exp2nextreg(fs, e);  /* default */
  return e->u.s.info;
}


void luaK_exp2val (FuncState *fs, expdesc *e) {
  if (hasjumps(e))
    luaK_exp2anyreg(fs, e);
  else
    luaK_dischargevars(fs, e);
}


int luaK_exp2RK (FuncState *fs, expdesc *e) {
  luaK_exp2val(fs, e);
  switch (e->k) {
    case VKNUM:
    case VTRUE:
    case VFALSE:
    case VNIL: {
      if (fs->nk <= MAXINDEXRK) {  /* constant fit in RK operand? */
        e->u.s.info = (e->k == VNIL)  ? nilK(fs) :
                      (e->k == VKNUM) ? luaK_numberK(fs, e->u.nval) :
                                        boolK(fs, (e->k == VTRUE));
        e->k = VK;
        return RKASK(e->u.s.info);
      }
      else break;
    }
    case VK: {
      if (e->u.s.info <= MAXINDEXRK)  /* constant fit in argC? */
        return RKASK(e->u.s.info);
      else break;
    }
    default: break;
  }
  /* not a constant in the right range: put it in a register */
  return luaK_exp2anyreg(fs, e);
}


void luaK_storevar (FuncState *fs, expdesc *var, expdesc *ex) {
  Proto *f = fs->f;
  switch (var->k) {
    case VLOCAL: {
      freeexp(fs, ex);
      exp2reg(fs, ex, var->u.s.info);
      return;
    }
    case VUPVAL: {
      int e = luaK_exp2anyreg(fs, ex);
      luaK_codeABC(fs, P_OP(f,I_SETUPVAL), e, var->u.s.info, 0);
      break;
    }
    case VGLOBAL: {
      int e = luaK_exp2anyreg(fs, ex);
      luaK_codeABx(fs, P_OP(f,I_SETGLOBAL), e, var->u.s.info);
      break;
    }
    case VINDEXED: {
      int e = luaK_exp2RK(fs, ex);
      luaK_codeABC(fs, P_OP(f,I_SETTABLE), var->u.s.info, var->u.s.aux, e);
      break;
    }
    default: {
      lua_assert(0);  /* invalid var kind to store */
      break;
    }
  }
  freeexp(fs, ex);
}


void luaK_self (FuncState *fs, expdesc *e, expdesc *key) {
  int func;
  Proto *f = fs->f;
  luaK_exp2anyreg(fs, e);
  freeexp(fs, e);
  func = fs->freereg;
  luaK_reserveregs(fs, 2);
  luaK_codeABC(fs, P_OP(f,I_SELF), func, e->u.s.info, luaK_exp2RK(fs, key));
  freeexp(fs, key);
  e->u.s.info = func;
  e->k = VNONRELOC;
}


static void invertjump (FuncState *fs, expdesc *e) {
  Proto *f = fs->f;
  Instruction *pc = getjumpcontrol(fs, e->u.s.info);
  lua_assert(testTMode(GET_OPCODE(*pc)) && GET_OPCODE(*pc) != P_OP(f,I_TESTSET) &&
                                           GET_OPCODE(*pc) != P_OP(f,I_TEST));
  SETARG_A(*pc, !(GETARG_A(*pc)));
}


static int jumponcond (FuncState *fs, expdesc *e, int cond) {
  Proto *f = fs->f;
  if (e->k == VRELOCABLE) {
    Instruction ie = getcode(fs, e);
    if (GET_OPCODE(ie) == P_OP(f,I_NOT)) {
      fs->pc--;  /* remove previous I_NOT */
      return condjump(fs, P_OP(f,I_TEST), GETARG_B(ie), 0, !cond);
    }
    /* else go through */
  }
  discharge2anyreg(fs, e);
  freeexp(fs, e);
  return condjump(fs, P_OP(f,I_TESTSET), NO_REG, e->u.s.info, cond);
}

void luaK_goiftrue (FuncState *fs, expdesc *e) {
  int pc;  /* pc of last jump */
  luaK_dischargevars(fs, e);
  switch (e->k) {
    case VK: case VKNUM: case VTRUE: {
      pc = NO_JUMP;  /* always true; do nothing */
      break;
    }
    case VJMP: {
      invertjump(fs, e);
      pc = e->u.s.info;
      break;
    }
    default: {
      pc = jumponcond(fs, e, 0);
      break;
    }
  }
  luaK_concat(fs, &e->f, pc);  /* insert last jump in `f' list */
  luaK_patchtohere(fs, e->t);
  e->t = NO_JUMP;
}


static void luaK_goiffalse (FuncState *fs, expdesc *e) {
  int pc;  /* pc of last jump */
  luaK_dischargevars(fs, e);
  switch (e->k) {
    case VNIL: case VFALSE: {
      pc = NO_JUMP;  /* always false; do nothing */
      break;
    }
    case VJMP: {
      pc = e->u.s.info;
      break;
    }
    default: {
      pc = jumponcond(fs, e, 1);
      break;
    }
  }
  luaK_concat(fs, &e->t, pc);  /* insert last jump in `t' list */
  luaK_patchtohere(fs, e->f);
  e->f = NO_JUMP;
}


static void codenot (FuncState *fs, expdesc *e) {
  Proto *f = fs->f;
  luaK_dischargevars(fs, e);
  switch (e->k) {
    case VNIL: case VFALSE: {
      e->k = VTRUE;
      break;
    }
    case VK: case VKNUM: case VTRUE: {
      e->k = VFALSE;
      break;
    }
    case VJMP: {
      invertjump(fs, e);
      break;
    }
    case VRELOCABLE:
    case VNONRELOC: {
      discharge2anyreg(fs, e);
      freeexp(fs, e);
      e->u.s.info = luaK_codeABC(fs, P_OP(f, I_NOT), 0, e->u.s.info, 0);
      e->k = VRELOCABLE;
      break;
    }
    default: {
      lua_assert(0);  /* cannot happen */
      break;
    }
  }
  /* interchange true and false lists */
  { int temp = e->f; e->f = e->t; e->t = temp; }
  removevalues(fs, e->f);
  removevalues(fs, e->t);
}


void luaK_indexed (FuncState *fs, expdesc *t, expdesc *k) {
  t->u.s.aux = luaK_exp2RK(fs, k);
  t->k = VINDEXED;
}

/* 合并表达式的值
 * op 操作码
 * e1 表达式1的指针
 * e2 表达式2的指针
 */
static int constfolding (FuncState *fs, OpCode op, expdesc *e1, expdesc *e2) {
  Proto *f = fs->f;
  lua_Number v1, v2, r;
  if (!isnumeral(e1) || !isnumeral(e2)) return 0;
  v1 = e1->u.nval;
  v2 = e2->u.nval;
    
  if (op==P_OP(f,I_ADD)) {
    r = luai_numadd(v1, v2);
  } else if (op==P_OP(f,I_SUB)) {
    r = luai_numsub(v1, v2);
  } else if (op==P_OP(f,I_MUL)) {
    r = luai_nummul(v1, v2);
  } else if (op==P_OP(f,I_DIV)) {
    if (v2 == 0) return 0;      /* 除数不能为0 */
    r = luai_numdiv(v1, v2);
  } else if (op==P_OP(f,I_MOD)) {
    if (v2 == 0) return 0;      /* 除数不能为0 */
    r = luai_nummod(v1, v2);
  } else if (op==P_OP(f,I_POW)) {
    r = luai_numpow(v1, v2);
  } else if (op==P_OP(f,I_UNM)) {
    r = luai_numunm(v1);
  } else if (op==P_OP(f,I_LEN)) {
    return 0;                   /* 常数没有len操作 */
  } else {
    lua_assert(0); r = 0;
  }
  
  if (luai_numisnan(r)) return 0;  /* 计算结果不能为 NaN */
  e1->u.nval = r;
  return 1;
}


static void codearith (FuncState *fs, OpCode op, expdesc *e1, expdesc *e2) {
  Proto *f = fs->f;
  if (constfolding(fs, op, e1, e2))
    return;
  else {
    int o2 = (op != P_OP(f,I_UNM) && op != P_OP(f,I_LEN)) ? luaK_exp2RK(fs, e2) : 0;
    int o1 = luaK_exp2RK(fs, e1);
    if (o1 > o2) {
      freeexp(fs, e1);
      freeexp(fs, e2);
    }
    else {
      freeexp(fs, e2);
      freeexp(fs, e1);
    }
    e1->u.s.info = luaK_codeABC(fs, op, 0, o1, o2);
    e1->k = VRELOCABLE;
  }
}


static void codecomp (FuncState *fs, OpCode op, int cond, expdesc *e1,
                      expdesc *e2) {
  Proto *f = fs->f;
  int o1 = luaK_exp2RK(fs, e1);
  int o2 = luaK_exp2RK(fs, e2);
  freeexp(fs, e2);
  freeexp(fs, e1);
  if (cond == 0 && op != P_OP(f,I_EQ)) {
    int temp;  /* exchange args to replace by `<' or `<=' */
    temp = o1; o1 = o2; o2 = temp;  /* o1 <==> o2 */
    cond = 1;
  }
  e1->u.s.info = condjump(fs, op, cond, o1, o2);
  e1->k = VJMP;
}


void luaK_prefix (FuncState *fs, UnOpr op, expdesc *e) {
  expdesc e2;
  Proto *f = fs->f;
  e2.t = e2.f = NO_JUMP; e2.k = VKNUM; e2.u.nval = 0;
  switch (op) {
    case OPR_MINUS: {
      if (!isnumeral(e))
        luaK_exp2anyreg(fs, e);  /* cannot operate on non-numeric constants */
      codearith(fs, P_OP(f,I_UNM), e, &e2);
      break;
    }
    case OPR_NOT: codenot(fs, e); break;
    case OPR_LEN: {
      luaK_exp2anyreg(fs, e);  /* cannot operate on constants */
      codearith(fs, P_OP(f,I_LEN), e, &e2);
      break;
    }
    default: lua_assert(0);
  }
}

/* fs 当前函数状态
 * op 当前二元运算符号
 * v 当前表达式描述
 */
void luaK_infix (FuncState *fs, BinOpr op, expdesc *v) {
  switch (op) {
    case OPR_AND: {
      luaK_goiftrue(fs, v);
      break;
    }
    case OPR_OR: {
      luaK_goiffalse(fs, v);
      break;
    }
    case OPR_CONCAT: {
      luaK_exp2nextreg(fs, v);  /* operand must be on the `stack' */
      break;
    }
    case OPR_ADD: case OPR_SUB: case OPR_MUL: case OPR_DIV:
    case OPR_MOD: case OPR_POW: {
      if (!isnumeral(v)) luaK_exp2RK(fs, v);
      break;
    }
    default: {
      luaK_exp2RK(fs, v);
      break;
    }
  }
}


void luaK_posfix (FuncState *fs, BinOpr op, expdesc *e1, expdesc *e2) {
  Proto *f = fs->f;
  switch (op) {
    case OPR_AND: {
      lua_assert(e1->t == NO_JUMP);  /* list must be closed */
      luaK_dischargevars(fs, e2);
      luaK_concat(fs, &e2->f, e1->f);
      *e1 = *e2;
      break;
    }
    case OPR_OR: {
      lua_assert(e1->f == NO_JUMP);  /* list must be closed */
      luaK_dischargevars(fs, e2);
      luaK_concat(fs, &e2->t, e1->t);
      *e1 = *e2;
      break;
    }
    case OPR_CONCAT: {
      luaK_exp2val(fs, e2);
      if (e2->k == VRELOCABLE && GET_OPCODE(getcode(fs, e2)) == P_OP(f,I_CONCAT)) {
        lua_assert(e1->u.s.info == GETARG_B(getcode(fs, e2))-1);
        freeexp(fs, e1);
        SETARG_B(getcode(fs, e2), e1->u.s.info);
        e1->k = VRELOCABLE; e1->u.s.info = e2->u.s.info;
      }
      else {
        luaK_exp2nextreg(fs, e2);  /* operand must be on the 'stack' */
        codearith(fs, P_OP(f,I_CONCAT), e1, e2);
      }
      break;
    }
    case OPR_ADD: codearith(fs, P_OP(f,I_ADD), e1, e2); break;
    case OPR_SUB: codearith(fs, P_OP(f,I_SUB), e1, e2); break;
    case OPR_MUL: codearith(fs, P_OP(f,I_MUL), e1, e2); break;
    case OPR_DIV: codearith(fs, P_OP(f,I_DIV), e1, e2); break;
    case OPR_MOD: codearith(fs, P_OP(f,I_MOD), e1, e2); break;
    case OPR_POW: codearith(fs, P_OP(f,I_POW), e1, e2); break;
    case OPR_EQ: codecomp(fs, P_OP(f,I_EQ), 1, e1, e2); break;
    case OPR_NE: codecomp(fs, P_OP(f,I_EQ), 0, e1, e2); break;
    case OPR_LT: codecomp(fs, P_OP(f,I_LT), 1, e1, e2); break;
    case OPR_LE: codecomp(fs, P_OP(f,I_LE), 1, e1, e2); break;
    case OPR_GT: codecomp(fs, P_OP(f,I_LT), 0, e1, e2); break;
    case OPR_GE: codecomp(fs, P_OP(f,I_LE), 0, e1, e2); break;
    default: lua_assert(0);
  }
}


void luaK_fixline (FuncState *fs, int line) {
  fs->f->lineinfo[fs->pc - 1] = line;
}


static int luaK_code (FuncState *fs, Instruction i, int line) {
  Proto *f = fs->f;
  dischargejpc(fs);  /* `pc' will change */
  /* put new instruction in code array */
  luaM_growvector(fs->L, f->code, fs->pc, f->sizecode, Instruction,
                  MAX_INT, "code size overflow");
  f->code[fs->pc] = i;
  /* save corresponding line information */
  luaM_growvector(fs->L, f->lineinfo, fs->pc, f->sizelineinfo, int,
                  MAX_INT, "code size overflow");
  f->lineinfo[fs->pc] = line;
  return fs->pc++;
}

int luaK_codeABC (FuncState *fs, OpCode o, int a, int b, int c) {
  Proto *f = fs->f;
  lua_assert(nluaP_getopmode(fs->L,f,o) == iABC);
  lua_assert(nluaP_getbmode(fs->L,f,o) != OpArgN || b == 0);
  lua_assert(nluaP_getcmode(fs->L,f,o) != OpArgN || c == 0);
  return luaK_code(fs, nluaP_createABC(fs->L, o, a, b, c), fs->ls->lastline);
}


int luaK_codeABx (FuncState *fs, OpCode o, int a, unsigned int bc) {
  Proto *f = fs->f;
  lua_assert(nluaP_getopmode(fs->L,f,o) == iABx || nluaP_getopmode(fs->L,f,o) == iAsBx);
  lua_assert(nluaP_getcmode(fs->L,f,o) == OpArgN);
  return luaK_code(fs, nluaP_createABx(fs->L, o, a, bc), fs->ls->lastline);
}

void luaK_setlist (FuncState *fs, int base, int nelems, int tostore) {
  Proto *f = fs->f;
  int c =  (nelems - 1)/LFIELDS_PER_FLUSH + 1;
  int b = (tostore == LUA_MULTRET) ? 0 : tostore;
  lua_assert(tostore != 0);
  if (c <= MAXARG_C)
    luaK_codeABC(fs, P_OP(f,I_SETLIST), base, b, c);
  else {
    luaK_codeABC(fs, P_OP(f,I_SETLIST), base, b, 0);
    luaK_code(fs, cast(Instruction, c), fs->ls->lastline);
  }
  fs->freereg = base + 1;  /* free registers with list values */
}

