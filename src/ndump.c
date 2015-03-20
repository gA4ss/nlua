/*
** $Id: ldump.c,v 2.8.1.1 2007/12/27 13:02:25 roberto Exp $
** save precompiled Lua chunks
** See Copyright Notice in lua.h
*/

#include <stdlib.h>
#include <stddef.h>

#define ndump_c
#define LUA_CORE

#include "nlua.h"

#include "lobject.h"
#include "lstate.h"
#include "nundump.h"

/* 预编译状态 */
typedef struct {
  lua_State* L;           /* 线程状态结构 */
  lua_Writer writer;      /* 写入接口函数 */
  void* data;             /* 要写入的数据 */
  int strip;              /* 是否剔除调试信息 */
  int status;
  unsigned int key;       /* 加密时使用的密码 */
  unsigned int dkey;      /* 加密数据时使用的密码 */
  NagaLuaOpt* opt;        /* 操作选项 */
} DumpState;

#define DumpMem(b,n,size,D)	DumpBlock(b,(n)*(size),D)
#define DumpVar(x,D)        DumpMem(&x,1,sizeof(x),D)

static void DumpBlock(const void* b, size_t size, DumpState* D) {
  global_State* g=G(D->L);
  NagaLuaOpt* nopt=D->opt;
  
  if (D->status==0) {
    lua_unlock(D->L);
    
    if (nlo_ef(nopt)) {
      char* tmp=malloc(size+1);
      if (tmp==NULL) {
        // 这里的出错处理以后在完善
        // printf("malloc failed\n");
        exit(-1);
      }
      g->enbuf(D->L, D->key, (unsigned char*)b,
               (unsigned char*)tmp, (unsigned int)size);
      D->status=(*D->writer)(D->L,tmp,size,D->data);
      free(tmp);
    }
    else {
      D->status=(*D->writer)(D->L,b,size,D->data);
    }
    lua_lock(D->L);
  }
}

static void DumpChar(int y, DumpState* D) {
  char x=(char)y;
  DumpVar(x,D);
}

static void DumpInt(int x, DumpState* D) {
  DumpVar(x,D);
}

static void DumpNumber(lua_Number x, DumpState* D) {
  DumpVar(x,D);
}

static void DumpVector(const void* b, int n, int size, DumpState* D) {
  DumpInt(n,D);
  DumpMem(b,n,size,D);
}

static void DumpString(const TString* s, DumpState* D) {
  global_State* g=G(D->L);
  NagaLuaOpt* nopt=D->opt;
  if (s==NULL || getstr(s)==NULL) {
    int size=0;
    DumpVar(size,D);
  } else {
    int size=(int)(s->tsv.len)+1;		/* 加上一个末尾的 '\0' */
    DumpVar(size,D);
    
    /* 加密字符串 */
    if (nlo_ed(nopt)) {
      char* tmp=malloc(size);
      if (tmp==NULL) {
        // 这里的出错处理以后在完善
        //printf("malloc failed\n");
        exit(-1);
      }
      memset(tmp, 0, size);
      g->enbuf(D->L, D->dkey, (unsigned char*)getstr(s),
               (unsigned char*)tmp, (unsigned int)size);
      DumpBlock(tmp,size,D);
      free(tmp);
    } else
      DumpBlock(getstr(s),size,D);
  }
}

#define DumpCode2(f,D)	 DumpVector(f->code,f->sizecode,sizeof(Instruction),D)
static void DumpCode(const Proto* f,DumpState* D) {
  int i;
  global_State* g=G(D->L);
  NagaLuaOpt* nopt=D->opt;
  if (nlo_eid(nopt) || nlo_ei(nopt)) {
    /* 是否加密指令数据 */
    if (nlo_eid(nopt)) {
      for (i=0; i<f->sizecode; i++) g->ienidata(D->L,&(f->code[i]));
    }
  }/* end if */
  
  /* 是否加密指令 */
  if (nlo_ei(nopt)) {
    nluaV_enproc(D->L,f);
  }
  
  /* 刷入文件 */
  DumpCode2(f,D);
}

static void DumpFunction(const Proto* f, const TString* p, DumpState* D);

static void DumpConstants(const Proto* f, DumpState* D) {
  int i,n=f->sizek;
  DumpInt(n,D);
  for (i=0; i<n; i++) {
    const TValue* o=&f->k[i];
    DumpChar(ttype(o),D);
    switch (ttype(o)) {
      case LUA_TNIL:
        break;
      case LUA_TBOOLEAN:
        DumpChar(bvalue(o),D);
        break;
      case LUA_TNUMBER:
        DumpNumber(nvalue(o),D);
        break;
      case LUA_TSTRING:
        DumpString(rawtsvalue(o),D);
        break;
      default:
        lua_assert(0);			/* cannot happen */
        break;
    }
  }
  n=f->sizep;
  DumpInt(n,D);
  for (i=0; i<n; i++) DumpFunction(f->p[i],f->source,D);
}

static void DumpDebug(const Proto* f, DumpState* D) {
  int i,n;
  n= (D->strip) ? 0 : f->sizelineinfo;
  DumpVector(f->lineinfo,n,sizeof(int),D);
  n= (D->strip) ? 0 : f->sizelocvars;
  DumpInt(n,D);
  for (i=0; i<n; i++) {
    DumpString(f->locvars[i].varname,D);
    DumpInt(f->locvars[i].startpc,D);
    DumpInt(f->locvars[i].endpc,D);
  }
  n= (D->strip) ? 0 : f->sizeupvalues;
  DumpInt(n,D);
  for (i=0; i<n; i++) DumpString(f->upvalues[i],D);
}

static void DumpFunction(const Proto* f, const TString* p, DumpState* D) {
  DumpString((f->source==p || D->strip) ? NULL : f->source,D);
  DumpInt(f->linedefined,D);
  DumpInt(f->lastlinedefined,D);
  DumpChar(f->nups,D);
  DumpChar(f->numparams,D);
  DumpChar(f->is_vararg,D);
  DumpChar(f->maxstacksize,D);
  DumpCode(f,D);
  DumpConstants(f,D);
  DumpDebug(f,D);
}

static void DumpBlockForce(const void* b, size_t size, DumpState* D) {
  if (D->status==0) {
    lua_unlock(D->L);
    D->status=(*D->writer)(D->L,b,size,D->data);
    lua_lock(D->L);
  }
}

static void DumpHeader(DumpState* D) {
  char h[NLUAC_HEADERSIZE];
  nluaU_header(h);
  DumpBlockForce(h,NLUAC_HEADERSIZE,D);
}

static void DumpOptions(DumpState* D) {
  int ops;
  NagaLuaOpt* nopt;
  
  nopt=D->opt;
  ops = sizeofnlo(nopt);
  DumpBlockForce(nopt,ops,D);
}

static void DumpOpcodeTable(lua_State* L, DumpState* D) {
  OpCode tab[NUM_OPCODES];
  nluaV_oprwrite(L,R(L),tab);
  DumpBlock(tab,sizeof(tab),D);
}

/* dump接口 */
int nluaU_dump (lua_State* L, const Proto* f, lua_Writer w,
                void* data, int strip, NagaLuaOpt* nopt,
                unsigned int ekey) {
  DumpState D;
  D.L=L;
  D.writer=w;
  D.data=data;
  D.strip=strip;
  D.status=0;
  D.opt=nopt;
  D.key=ekey;
  if (nlo_ed(nopt) || (nlo_ei(nopt))) {
    D.dkey=crc32((unsigned char*)&ekey, 4);
  } else {
    D.dkey=ekey;
  }
  
  /* 加密NagaOpt密钥 */
  if (nlo_ef(nopt) || (nlo_ed(nopt))) {
    char h[NLUAC_HEADERSIZE];
    unsigned char* ks;
    unsigned int k;
    nluaU_header(h);
    k=crc32((unsigned char*)&h, NLUAC_HEADERSIZE);
    ks=nlo_get_key(nopt);
    XorArray(k, ks, ks, nlo_ks(nopt));
  }
  
  DumpHeader(&D);
  DumpOptions(&D);
  DumpOpcodeTable(L,&D);
  DumpFunction(f,NULL,&D);
  return D.status;
}
