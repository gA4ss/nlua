//
//  nlua.h
//  nlua
//
//  Created by logic.yan on 15/2/23.
//  Copyright (c) 2015年 naga. All rights reserved.
//

#ifndef nlua_nlua_h
#define nlua_nlua_h

#include "lua.h"
#include "lopcodes.h"
#include "lcode.h"
#include "lstate.h"
#include "lobject.h"
#include "nversion.h"
#include "nglobal.h"

/* 预编译字节码文件头 "<vt>YWB"*/
#define	NLUA_SIGNATURE      "\013YWB"

/* 运算符号个数 */
#define NUM_OPTPS OPR_NOBINOPR
/* 无效指令编码 */
#define NLUA_INVALID_OPCODE       0xFF

/* 获取opcode规则 */
#define R(l) &(G((l))->oprule)

LUAI_FUNC void nluaV_oprinit (lua_State* L, OPR *opr);
LUAI_FUNC void nluaV_oprrand(lua_State* L, OPR *opr);
LUAI_FUNC void nluaV_oprrand_global(lua_State* L);
LUAI_FUNC void nluaV_oprread (lua_State* L, OPR *opr, OpCode* tab);
LUAI_FUNC void nluaV_oprwrite (lua_State* L, OPR *opr, OpCode* tab);
LUAI_FUNC Instruction nluaV_remap(lua_State* L, Instruction ins, OpCode *tabf, OpCode *tabt);
LUAI_FUNC Instruction nluaV_remap_onnow(lua_State* L, Instruction ins, OpCode *tabf);

LUAI_FUNC int nluaV_insstart (lua_State* L, Instruction* pins);
LUAI_FUNC int nluaV_insend (lua_State* L, Instruction* pins);
LUAI_FUNC int nluaV_enidata (lua_State* L, Instruction* pins);
LUAI_FUNC int nluaV_deidata (lua_State* L, Instruction* pins);
LUAI_FUNC int nluaV_enbuf (lua_State* L, lu_int32 key, lu_byte *p1, lu_byte*p2, int bsize);
LUAI_FUNC int nluaV_debuf (lua_State* L, lu_int32 key, lu_byte *p1, lu_byte*p2, int bsize);
LUAI_FUNC int nluaV_enins(lua_State* L, Instruction* pins, unsigned int key);
LUAI_FUNC int nluaV_deins(lua_State* L, Instruction* pins, unsigned int key);
LUAI_FUNC int nluaV_enproc(lua_State* L, const Proto* f);
LUAI_FUNC int nluaV_deproc(lua_State* L, const Proto* f);
LUAI_FUNC lu_int32 nluaV_fkmake (lua_State* L, const char* path);/* 出错返回0 */

#endif
