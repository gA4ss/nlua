//
//  nlua.c
//  nlua
//
//  Created by logic.yan on 15/2/26.
//  Copyright (c) 2015年 naga. All rights reserved.
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "nlua.h"

#include "xor.h"
#include "crc.h"
#include "ldebug.h"
#include "ldo.h"
#include "lfunc.h"
#include "lgc.h"
#include "lobject.h"
#include "nopcodes.h"
#include "lstate.h"
#include "lstring.h"
#include "ltable.h"
#include "ltm.h"
#include "lvm.h"
#include "nundump.h"

#define nlua_c
#define LUA_CORE

/* 指令处理表 */
LUAI_DATA nluaV_Instruction nluaV_opcodedisp[NUM_OPCODES];      /* 在lvm.c中定义 */

/* 初始化opcode编码表 */
#define init_optab() { \
  OpCode i; \
  for (i = 0; i < NUM_OPCODES; i++) r->optab[i] = i; \
  nluaP_opinit(); \
}

/* 获取随机值 */
static int get_rand() {
  int s = 0, i = 100;
  while (i > 0) {
    i--;
  }
  s = rand();
  s ^= rand();
  return s;
}

/* 测试表是否已经满了 */
static int tab_isfull(OpCode* tab) {
  OpCode i = 0;
  lu_byte count = 0;
  for (i = 0; i < NUM_OPCODES; i++) {
    if (tab[i] != cast(OpCode,NLUA_INVALID_OPCODE)) count++;
  }
  
  if (count==NUM_OPCODES) {
    return 1;
  }
  return 0;
}

/* 获取与i不一样的随机值 */
static unsigned char get_diffop(OpCode* tab) {
  OpCode i = 0, j = 0;
  
  /* 如果随机数与原数相同 */
_get_rand_lab:
  /* 在0-NUM_OPCODES范围内随机获取一个值 */
  j = (OpCode)get_rand() % NUM_OPCODES;
  for (i = 0; i < NUM_OPCODES; i++) {
    /* 如果当前表中的值不等于空 */
    if (tab[i] != cast(OpCode,NLUA_INVALID_OPCODE)) {
      /* 当前表中的值与随机数相等 */
      if (tab[i] == j) {
        goto _get_rand_lab;
      }
    }/* end if */
  }/* end for */
  
  /* 1.如果表中的项都为空，则直接返回
   * 2.如果表中某项不为空，则取出的j不能等于表中的取出项
   */
  return j;
}

/* 递归设置表
 * tab 要设置的表
 * i 要设置的索引
 */
static void set_roptab(OpCode *tab, OpCode i, OpCode j) {
  unsigned char m = 0;
  
  m = tab[j];
    
  /* 如果为空则交换*/
  if (m == NLUA_INVALID_OPCODE) {
    tab[j]=i;
    tab[i]=j;
  } else {
    /* 递归吧 */
    set_roptab(tab, i, m);
  }
}

/* 产生opcode随机表
 * tab 要产生的随机表
 */
//#define OP_RMASK (2^SIZE_OP)-1
static void generate_roptab(OpCode* tab) {
  OpCode i = 0, j = 0;
  
  /* 首先初始化 */
  memset(tab, NLUA_INVALID_OPCODE, sizeof(OpCode)*NUM_OPCODES);
  
  /* 遍历队列 */
  for (i = 0; i < NUM_OPCODES; i++) {
    
    /* 测试表是否已经满了 */
    if (tab_isfull(tab)) {
      break;
    }
    
    j = get_diffop(tab);    	/* 随机选一个位置 */
    /* 如果为空则设置 */
    if (tab[i] == cast(OpCode,NLUA_INVALID_OPCODE)) {
      set_roptab(tab, i, j);
    }
  }
}

/* 从新的编码表中,重新排列派遣函数的顺序
 * od opcode派遣函数表
 * tab opcode编码表
 */
static void generate_opdtab(nluaV_Instruction* od, OpCode* tab) {
  OpCode i = 0;
  for (i = 0; i < NUM_OPCODES; i++) {
    od[i] = nluaV_opcodedisp[tab[i]];
  }
}

/* 从新的编码表中,重新排列操作表顺序
 * od opcode派遣函数表
 * tab opcode编码表
 */
static void generate_opmodes(OpRun* om, OpCode* tab) {
  OpCode i = 0;
  for (i = 0; i < NUM_OPCODES; i++) {
    om[i] = luaP_opmodes[tab[i]];
  }
}

/* 从新的编码表中,重新排列opcode名称表顺序
 * od opcode派遣函数表
 * tab opcode名称表
 */
static void generate_opnames(const char** on, OpCode* tab) {
  OpCode i = 0;
  for (i = 0; i < NUM_OPCODES; i++) {
    on[i] = luaP_opnames[tab[i]];
  }
}

/* opcode随机初始化
 * L 虚拟机状态指针
 */
void nluaV_oprinit(lua_State* L, OPR *opr) {
  OPR *r = opr;
  UNUSED(L);
  /* 初始化编码表 */
  init_optab();
  
  /* 根据编码表对派遣函数表以及操作模式表进行初始化 */
  nluaP_oprecode(r->optab);
  generate_opdtab(r->opcodedisp, r->optab);
  generate_opmodes(r->opmods, r->optab);
  generate_opnames(r->opnames, r->optab);
}

/* 随机opcode
 */
void nluaV_oprrand(lua_State* L, OPR *opr) {
  OPR *r = opr;
  
  /* 初始化编码表 */
  init_optab();
  
  generate_roptab(r->optab);
  
  /* 根据编码表对派遣函数表以及操作模式表进行初始化 */
  nluaP_oprecode(r->optab);
  generate_opdtab(r->opcodedisp, r->optab);
  generate_opmodes(r->opmods, r->optab);
  generate_opnames(r->opnames, r->optab);
}

/* 随机opcode
 */
void nluaV_oprrand_global(lua_State* L) {
  nluaV_oprrand(L, &(G(L)->oprule));
}

/* opcode规则表到虚拟机状态
 * L 虚拟机状态指针
 * tab 要读取的opcode表
 */
void nluaV_oprread (lua_State* L, OPR *opr, OpCode* tab) {
  OPR *r = opr;
  UNUSED(L);
  /* 对表进行读取 */
  memcpy(&r->optab[0], tab, sizeof(r->optab));
  nluaP_opinit();
  
  /* 根据编码表对派遣函数表以及操作模式表进行随机化 */
  nluaP_oprecode(r->optab);
  generate_opdtab(r->opcodedisp, r->optab);
  generate_opmodes(r->opmods, r->optab);
  generate_opnames(r->opnames, r->optab);
}

/* opcode规则表到虚拟机状态
 * L 虚拟机状态指针
 * tab 要填充的opcode表
 */
void nluaV_oprwrite (lua_State* L, OPR *opr, OpCode* tab) {
  UNUSED(L);
  memcpy(tab, opr->optab, sizeof(opr->optab));//sizeof(OpCode)*NUM_OPCODES
}

/* 指令opcode转换
 * 将一条指令经过随机话的指令按照另外一张表重现编码
 */
Instruction nluaV_remap(lua_State* L, Instruction ins, OpCode *tabf, OpCode *tabt) {
  OpCode o, z;
  int i;
  
  UNUSED(L);
  
  /* 取出指令的opcode */
  o = GET_OPCODE(ins);
  /* 通过它的编码表找回原始的值 */
  for (i=0; i<NUM_OPCODES; i++) {
    if (o == tabf[i]) {
      z=(OpCode)i;
      break;
    }
  }
  /* 重新进行编码 
   * FIXME: 如果上边找不到则会出现错误
   */
  o = tabt[z];
  SET_OPCODE(ins, o);
  
  return ins;
}

/* 指令opcode转换
 * 将一条指令经过随机话的指令按照当前编码表
 */
Instruction nluaV_remap_onnow(lua_State* L, Instruction ins, OpCode *tabf) {
  OpCode *tabt;
  tabt = &(G(L)->oprule.optab[0]);
  return nluaV_remap(L, ins, tabf, tabt);
}

/* 指令开始执行前作的动作
 * L 虚拟机状态指针
 * pins 当前要执行指令的指针
 */
int nluaV_insstart(lua_State* L, Instruction* pins) {
  Proto *p;
  int opt;
  lua_assert(isLua(L->ci));       /* 必须是lua函数 */
  p = clvalue(L->ci->func)->l.p;
  opt = p->rule.nopt;
  
  /* 是否解密指令数据 */
  if (nlo_opt_eid(opt)) {
    nluaV_DeInstructionData deidata = G(L)->ideidata;
    deidata(L, pins);
  }
  
  return 0;
}

/* 指令开始执行后的动作
 * L 虚拟机状态指针
 * pins 当前要执行指令的指针
 */
int nluaV_insend(lua_State *L, Instruction* pins) {
  Proto *p;
  int opt;
  //lua_assert(isLua(L->ci));       /* 必须是lua函数 */
  //p = clvalue(L->ci->func)->l.p;
  //opt = p->rule.nopt;
  
  UNUSED(p);
  UNUSED(opt);
  return 0;
}

/* 加密指令部分
 * L 虚拟机状态指针
 * pins 当前要执行指令的指针
 */
int nluaV_enins(lua_State* L, Instruction* pins, unsigned int key) {
  return G(L)->enbuf(L, key, (unsigned char*)pins,
                     (unsigned char*)pins, sizeof(Instruction));
}

/* 解密指令部分
 * L 虚拟机状态指针
 * pins 当前要执行指令的指针
 */
int nluaV_deins(lua_State* L, Instruction* pins, unsigned int key) {
  return G(L)->debuf(L, key, (unsigned char*)pins,
                     (unsigned char*)pins, sizeof(Instruction));
}

/* 加密缓存
 * L 虚拟机状态指针
 * key 密钥
 * p1 要加密的缓存
 * p2 加密后存放的缓存
 * bsize 要加密的缓存长度
 * 返回加密后的长度
 */
int nluaV_enbuf(lua_State* L, lu_int32 key, lu_byte *p1, lu_byte*p2, int bsize) {
  XorArray(key, p1, p2, bsize);
  return bsize;
}

/* 解密缓存
 * L 虚拟机状态指针
 * key 密钥
 * p1 要解密的缓存
 * p2 解密后存放的缓存
 * 返回解密后的长度
 * bsize 要解密的缓存长度
 */
int nluaV_debuf(lua_State* L, lu_int32 key, lu_byte *p1, lu_byte*p2, int bsize) {
  XorArray(key, p1, p2, bsize);
  return bsize;
}

lu_int32 nluaV_fkmake(lua_State* L, const char* path) {
  FILE* fp=NULL;
  unsigned int fkey=0;
  long fsize=0;
  size_t r=0;
  void* fbuf=NULL;
  
  fp = fopen(path, "rb");
  if (fp == NULL) {
    return 0;
  }
  
  /* 获取文件长度 */
  fseek(fp, 0, SEEK_END);
  fsize = ftell(fp);
  fseek(fp, 0, SEEK_SET);
  
  /* 读取文件 */
  fbuf=malloc(fsize);
  if (fbuf == NULL) {
    return 0;
  }
  r = fread(fbuf, 1, fsize, fp);
  if (r != fsize) {
    return 0;
  }
  
  fkey=(unsigned int)naga_crc32((unsigned char*)fbuf, (unsigned int)fsize);
  free(fbuf);
  fclose(fp);
  
  return fkey;
}

/* 加密指令数据部分
 * L 虚拟机状态指针
 * pins 当前要执行指令的指针
 */
int nluaV_enidata (lua_State* L, Instruction* pins) {
  lu_byte op = GET_OPCODE(*pins);
  lu_byte *d = ((lu_byte*)pins) + 1;
  int i;
  for (i = 0; i < 3; i++) {
    d[i] ^= op;
  }
  return 0;
}

/* 解密指令数据部分
 * L 虚拟机状态指针
 * pins 当前要执行指令的指针
 */
int nluaV_deidata (lua_State* L, Instruction* pins) {
  lu_byte op = GET_OPCODE(*pins);
  lu_byte *d = ((lu_byte*)pins) + 1;
  int i;
  for (i = 0; i < 3; i++) {
    d[i] ^= op;
  }
  return 0;
}

int nluaV_enproc(lua_State* L, const Proto* f) {
  int i;

  /* 计算这个key，可以关联其他保密数据 */
  //unsigned int key = naga_crc32((unsigned char*)&(f->code[0]), (f->sizecode)*sizeof(Instruction));
  unsigned int key = f->rule.ekey;
  /* 加密第一条代码 */
  G(L)->ienins(L,&(f->code[0]), key);
  
  /* 加密其余指令 */
  for (i=1; i<f->sizecode; i++) {
    /* 使用其他指令的密文hash作为key */
    //key = naga_crc32((unsigned char*)&(f->code[i-1]), sizeof(Instruction));
    key = (unsigned int)(f->code[i-1]);
    G(L)->ienins(L,&(f->code[i]), key);
  }
  
#if 0
  for (i=0; i<f->sizecode; i++) {
    G(L)->ienins(L,&(f->code[i]), 0);
  }
#endif
  
  return f->sizecode;
}

LUAI_FUNC int nluaV_deproc(lua_State* L, const Proto* f) {
  int i;
  
  /* 计算这个key，可以关联其他保密数据 */
  //unsigned int key = naga_crc32((unsigned char*)&(f->code[0]), (f->sizecode)*sizeof(Instruction));
  unsigned int key = f->rule.ekey;
  /* 加密第一条代码 */
  G(L)->ideins(L,&(f->code[0]), key);
  
  /* 加密其余指令 */
  for (i=1; i<f->sizecode; i++) {
    /* 使用其他指令的密文hash作为key */
    //key = naga_crc32((unsigned char*)&(f->code[i-1]), sizeof(Instruction));
    key = (unsigned int)(f->code[i-1]);
    G(L)->ideins(L,&(f->code[i]), key);
  }
  
#if 0
  for (i=0; i<f->sizecode; i++) {
    G(L)->ideins(L,&(f->code[i]), 0);
  }
#endif
  
  return f->sizecode;
}
