/*
** $Id: print.c,v 1.55a 2006/05/31 13:30:05 lhf Exp $
** print bytecodes
** See Copyright Notice in lua.h
*/

#include <ctype.h>
#include <stdio.h>

#define nluac_c
#define LUA_CORE

#include "nlua.h"

#include "ldebug.h"
#include "lobject.h"
#include "lopcodes.h"
#include "lundump.h"
#include "nopcodes.h"
#include "nundump.h"

#define PrintFunction	luaU_print

#define Sizeof(x)	((int)sizeof(x))
#define VOID(p)		((const void*)(p))

/* 打印字符串 */
static void PrintString(const TString* ts) {
  const char* s=getstr(ts);
  size_t i,n=ts->tsv.len;
  putchar('"');
  
  /* 分析字符串的转译字符 */
  for (i=0; i<n; i++) {
    int c=s[i];
    switch (c) {
      case '"': printf("\\\""); break;
      case '\\': printf("\\\\"); break;
      case '\a': printf("\\a"); break;
      case '\b': printf("\\b"); break;
      case '\f': printf("\\f"); break;
      case '\n': printf("\\n"); break;
      case '\r': printf("\\r"); break;
      case '\t': printf("\\t"); break;
      case '\v': printf("\\v"); break;
      default:	if (isprint((unsigned char)c))
        putchar(c);
      else
        printf("\\%03u",(unsigned char)c);
    }
  }
  putchar('"');
}

/* 打印单个常量,i为常量索引号 */
static void PrintConstant(const Proto* f, int i) {
  const TValue* o=&f->k[i];
  switch (ttype(o)) {
    case LUA_TNIL:
      printf("nil");
      break;
    case LUA_TBOOLEAN:
      printf(bvalue(o) ? "true" : "false");
      break;
    case LUA_TNUMBER:
      printf(LUA_NUMBER_FMT,nvalue(o));
      break;
    case LUA_TSTRING:
      PrintString(rawtsvalue(o));
      break;
    default:				/* cannot happen */
      printf("? type=%d",ttype(o));
      break;
  }
}

static void PrintCode(lua_State* L, const Proto* f) {
  OPR* opr = &(f->rule.oprule);
  unsigned int opt = f->rule.nopt;
  const Instruction* code=f->code;
  int pc,n=f->sizecode;
  nluaV_DeInstruction deins = G(L)->ideins;
  nluaV_DeInstructionData deidata = G(L)->ideidata;
  
  /* 遍历指令 */
  for (pc=0; pc<n; pc++) {
    Instruction i;
    OpCode o;
    int a,b,c,bx,sbx;
    unsigned int key;
    int line;
    
    i=code[pc];
    
    if (nlo_opt_ei(opt)) {
      if (pc==0) {
        key=f->rule.ekey;
      } else {
        key=crc32((unsigned char*)&code[pc-1], sizeof(Instruction));
      }
      deins(L, &i, key);
    }
    
    if (nlo_opt_eid(opt)) {
      deidata(L,&i);
    }
  
    o=GET_OPCODE(i);
    a=GETARG_A(i);
    b=GETARG_B(i);
    c=GETARG_C(i);
    bx=GETARG_Bx(i);
    sbx=GETARG_sBx(i);
    
    /* 获取当前指令对应的源代码行数 */
    line=getlinenm(f,pc);
    printf("\t%d\t",pc+1);
    if (line>0) printf("[%d]\t",line); else printf("[-]\t");
    
    /* 打印opcode名称 */
    printf("%-9s\t",opr->opnames[o]);
    
    /* 判断操作模式 */
    switch (nluaP_getopmode(L, f, o)) {
      case iABC:
        printf("%d",a);
        if (nluaP_getbmode(L, f, o)!=OpArgN) printf(" %d",ISK(b) ? (-1-INDEXK(b)) : b);
        if (nluaP_getcmode(L, f, o)!=OpArgN) printf(" %d",ISK(c) ? (-1-INDEXK(c)) : c);
        break;
      case iABx:
        if (nluaP_getbmode(L, f, o)==OpArgK) printf("%d %d",a,-1-bx); else printf("%d %d",a,bx);
        break;
      case iAsBx:
        if (o==P_OP(f, I_JMP)) printf("%d",sbx); else printf("%d %d",a,sbx);
        break;
    }
    
    /* 打印指令的数据部分 */
    if (o==P_OP(f,I_LOADK)) {
      printf("\t; "); PrintConstant(f,bx);
    } else if ((o==P_OP(f,I_GETUPVAL)) || (o==P_OP(f,I_SETUPVAL))) {
      printf("\t; %s", (f->sizeupvalues>0) ? getstr(f->upvalues[b]) : "-");
    } else if ((o==P_OP(f,I_GETGLOBAL)) || (o==P_OP(f,I_SETGLOBAL))) {
      printf("\t; %s",svalue(&f->k[bx]));
    } else if ((o==P_OP(f,I_GETTABLE)) || (o==P_OP(f,I_SELF))) {
      if (ISK(c)) { printf("\t; "); PrintConstant(f,INDEXK(c)); }
    } else if ((o==P_OP(f,I_SETTABLE)) || (o==P_OP(f,I_ADD)) || (o==P_OP(f,I_SUB)) || (o==P_OP(f,I_MUL)) ||
               (o==P_OP(f,I_DIV)) || (o==P_OP(f,I_POW)) || (o==P_OP(f,I_EQ)) || (o==P_OP(f,I_LT)) || (o==P_OP(f,I_LE))) {
      if (ISK(b) || ISK(c)) {
        printf("\t; ");
        if (ISK(b)) PrintConstant(f,INDEXK(b)); else printf("-");
        printf(" ");
        if (ISK(c)) PrintConstant(f,INDEXK(c)); else printf("-");
      }
    } else if ((o==P_OP(f,I_JMP)) || (o==P_OP(f,I_FORLOOP)) || (o==P_OP(f,I_FORPREP))) {
      printf("\t; to %d",sbx+pc+2);
    } else if (o==P_OP(f,I_CLOSURE)) {
      printf("\t; %p",VOID(f->p[bx]));
    } else if (o==P_OP(f,I_SETLIST)) {
      if (c==0) printf("\t; %d",(int)code[++pc]);
      else printf("\t; %d",c);
    }
    printf("\n");
  }
}

/* 控制英文单复数情况，符合语法输出，做的真细致 */
#define SS(x)	(x==1)?"":"s"
#define S(x)	x,SS(x)

static void PrintHeader(const Proto* f) {
  const char* s=getstr(f->source);
  if (*s=='@' || *s=='=')
    s++;
  else if ((*s==LUA_SIGNATURE[0]) || (*s==LUA_SIGNATURE[0]))
    s="(bstring)";
  else
    s="(string)";
  printf("\n%s <%s:%d,%d> (%d instruction%s, %d bytes at %p)\n",
         (f->linedefined==0)?"main":"function",s,
         f->linedefined,f->lastlinedefined,
         S(f->sizecode),f->sizecode*Sizeof(Instruction),VOID(f));
  printf("%d%s param%s, %d slot%s, %d upvalue%s, ",
         f->numparams,f->is_vararg?"+":"",SS(f->numparams),
         S(f->maxstacksize),S(f->nups));
  printf("%d local%s, %d constant%s, %d function%s\n",
         S(f->sizelocvars),S(f->sizek),S(f->sizep));
}

/* 打印一个函数的所有常量 */
static void PrintConstants(const Proto* f) {
  int i,n=f->sizek;
  printf("constants (%d) for %p:\n",n,VOID(f));
  for (i=0; i<n; i++) {
    printf("\t%d\t",i+1);
    /* 打印一个常量 */
    PrintConstant(f,i);
    printf("\n");
  }
}

/* 打印一个函数的局部变量 */
static void PrintLocals(const Proto* f) {
  int i,n=f->sizelocvars;
  printf("locals (%d) for %p:\n",n,VOID(f));
  for (i=0; i<n; i++) {
    printf("\t%d\t%s\t%d\t%d\n",
           i,getstr(f->locvars[i].varname),f->locvars[i].startpc+1,f->locvars[i].endpc+1);
 }
}

/* 打印一个函数的upvalue */
static void PrintUpvalues(const Proto* f) {
  int i,n=f->sizeupvalues;
  printf("upvalues (%d) for %p:\n",n,VOID(f));
  if (f->upvalues==NULL) return;
  for (i=0; i<n; i++) {
    printf("\t%d\t%s\n",i,getstr(f->upvalues[i]));
  }
}

/* 输出一个函数的详细信息，full参数表示输出的更加相信 */
void PrintFunction(lua_State* L, const Proto* f, int full) {
  int i,n=f->sizep;
  PrintHeader(f);
  PrintCode(L, f);
  if (full) {
    PrintConstants(f);
    PrintLocals(f);
    PrintUpvalues(f);
  }
  for (i=0; i<n; i++) PrintFunction(L, f->p[i],full);
}
