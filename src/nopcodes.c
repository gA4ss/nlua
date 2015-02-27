//
//  nopcodes.c
//  nlua
//
//  Created by logic.yan on 15/2/26.
//  Copyright (c) 2015年 naga. All rights reserved.
//

#include "nlua.h"
#include "lstate.h"
#include "nopcodes.h"

void nluaP_opinit() {
  OP_MOVE=0;
  OP_LOADK=1;
  OP_LOADBOOL=2;
  OP_LOADNIL=3;
  OP_GETUPVAL=4;
  OP_GETGLOBAL=5;
  OP_GETTABLE=6;
  OP_SETGLOBAL=7;
  OP_SETUPVAL=8;
  OP_SETTABLE=9;
  OP_NEWTABLE=10;
  OP_SELF=11;
  OP_ADD=12;
  OP_SUB=13;
  OP_MUL=14;
  OP_DIV=15;
  OP_MOD=16;
  OP_POW=17;
  OP_UNM=18;
  OP_NOT=19;
  OP_LEN=20;
  OP_CONCAT=21;
  OP_JMP=22;
  OP_EQ=23;
  OP_LT=24;
  OP_LE=25;
  OP_TEST=26;
  OP_TESTSET=27;
  OP_CALL=28;
  OP_TAILCALL=29;
  OP_RETURN=30;
  OP_FORLOOP=31;
  OP_FORPREP=32;
  OP_TFORLOOP=33;
  OP_SETLIST=34;
  OP_CLOSE=35;
  OP_CLOSURE=36;
  OP_VARARG=37;
}

/* 用于重新编码opcode常量 */
void nluaP_oprecode(OpCode *tab) {
  OP_MOVE=tab[OP_MOVE];
  OP_LOADK=tab[OP_LOADK];
  OP_LOADBOOL=tab[OP_LOADBOOL];
  OP_LOADNIL=tab[OP_LOADNIL];
  OP_GETUPVAL=tab[OP_GETUPVAL];
  OP_GETGLOBAL=tab[OP_GETGLOBAL];
  OP_GETTABLE=tab[OP_GETTABLE];
  OP_SETGLOBAL=tab[OP_SETGLOBAL];
  OP_SETUPVAL=tab[OP_SETUPVAL];
  OP_SETTABLE=tab[OP_SETTABLE];
  OP_NEWTABLE=tab[OP_NEWTABLE];
  OP_SELF=tab[OP_SELF];
  OP_ADD=tab[OP_ADD];
  OP_SUB=tab[OP_SUB];
  OP_MUL=tab[OP_MUL];
  OP_DIV=tab[OP_DIV];
  OP_MOD=tab[OP_MOD];
  OP_POW=tab[OP_POW];
  OP_UNM=tab[OP_UNM];
  OP_NOT=tab[OP_NOT];
  OP_LEN=tab[OP_LEN];
  OP_CONCAT=tab[OP_CONCAT];
  OP_JMP=tab[OP_JMP];
  OP_EQ=tab[OP_EQ];
  OP_LT=tab[OP_LT];
  OP_LE=tab[OP_LE];
  OP_TEST=tab[OP_TEST];
  OP_TESTSET=tab[OP_TESTSET];
  OP_CALL=tab[OP_CALL];
  OP_TAILCALL=tab[OP_TAILCALL];
  OP_RETURN=tab[OP_RETURN];
  OP_FORLOOP=tab[OP_FORLOOP];
  OP_FORPREP=tab[OP_FORPREP];
  OP_TFORLOOP=tab[OP_TFORLOOP];
  OP_SETLIST=tab[OP_SETLIST];
  OP_CLOSE=tab[OP_CLOSE];
  OP_CLOSURE=tab[OP_CLOSURE];
  OP_VARARG=tab[OP_VARARG];
}

#define CREATE_ABC(o,a,b,c)	((cast(Instruction, o)<<POS_OP) \
| (cast(Instruction, a)<<POS_A) \
| (cast(Instruction, b)<<POS_B) \
| (cast(Instruction, c)<<POS_C))

#define CREATE_ABx(o,a,bc)	((cast(Instruction, o)<<POS_OP) \
| (cast(Instruction, a)<<POS_A) \
| (cast(Instruction, bc)<<POS_Bx))

Instruction nluaP_createABC(lua_State *L, OpCode o, int a, int b, int c) {
    Instruction i = CREATE_ABC(o,a,b,c);
    return i;
}

Instruction nluaP_createABx(lua_State *L, OpCode o, int a, unsigned int bc) {
    Instruction i = CREATE_ABx(o,a,bc);
    return i;
}

/*
Instruction xluaP_createAx(lua_State *L, OpCode o, int ax) {
    Instruction i = CREATE_Ax(o,ax);
    return i;
}
*/

/* 取得指令的操作模式
 * m OpCode
 */
lu_byte nluaP_getopmode(lua_State *L, OpCode m) {
    global_State *g = G(L);
    OpRun r = g->oprule.opmods[m];
    OpMode md = r & 3;
    
    return md;
}

/* 取得指令的B参数状态
 * m OpCode
 */
lu_byte nluaP_getbmode(lua_State *L, OpCode m) {
    global_State *g = G(L);
    OpRun r = g->oprule.opmods[m];
    OpArgMask oam = (r >> 4) & 3;
    
    return oam;
}

/* 取得指令的C参数状态
 * m OpCode
 */
lu_byte nluaP_getcmode(lua_State *L, OpCode m) {
    global_State *g = G(L);
    OpRun r = g->oprule.opmods[m];
    OpArgMask oam = (r >> 2) & 3;
    
    return oam;
}

/* 测试A是寄存器还是upvalue
 * m OpCode
 */
lu_byte nluaP_testamode(lua_State *L, OpCode m) {
    global_State *g = G(L);
    OpRun r = g->oprule.opmods[m];
    
    return (r & (1 << 6));
}

/* 测试当前指令是否是条件跳转指令
 * m OpCode
 */
lu_byte nluaP_testtmode(lua_State *L, OpCode m) {
    global_State *g = G(L);
    OpRun r = g->oprule.opmods[m];   /* 取出操作模式 */
    
    /* 最高位表示是否是条件跳转指令 */
    return (r & (1 << 7));
}