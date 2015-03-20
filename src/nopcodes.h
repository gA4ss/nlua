//
//  nopcodes.h
//  nlua
//
//  Created by logic.yan on 15/2/26.
//  Copyright (c) 2015年 naga. All rights reserved.
//

#ifndef nlua_nopcodes_h
#define nlua_nopcodes_h

#include <stdio.h>

#include "lopcodes.h"
#include "lobject.h"

LUAI_FUNC void nluaP_opinit();
LUAI_FUNC void nluaP_oprecode(OpCode *tab);
LUAI_FUNC Instruction nluaP_createABC(lua_State *L, OpCode o, int a, int b, int c);
LUAI_FUNC Instruction nluaP_createABx(lua_State *L, OpCode o, int a, unsigned int bc);
//LUAI_FUNC Instruction nluaP_createAx(lua_State *L, OpCode o, int ax);
LUAI_FUNC lu_byte nluaP_getopmode(lua_State *L, Proto *p, OpCode m);
LUAI_FUNC lu_byte nluaP_getbmode(lua_State *L, Proto *p, OpCode m);
LUAI_FUNC lu_byte nluaP_getcmode(lua_State *L, Proto *p, OpCode m);
LUAI_FUNC lu_byte nluaP_testamode(lua_State *L, Proto *p, OpCode m);
LUAI_FUNC lu_byte nluaP_testtmode(lua_State *L, Proto *p, OpCode m);

#endif
