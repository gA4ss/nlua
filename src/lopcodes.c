/*
** $Id: lopcodes.c,v 1.37.1.1 2007/12/27 13:02:25 roberto Exp $
** See Copyright Notice in lua.h
*/


#define lopcodes_c
#define LUA_CORE


#include "lopcodes.h"


/*----------------------------------------------------------------------
 name		args	description
 ------------------------------------------------------------------------*/
OpCode OP_MOVE=0;/*	A B	R(A) := R(B)					*/
OpCode OP_LOADK=1;/*	A Bx	R(A) := Kst(Bx)					*/
OpCode OP_LOADBOOL=2;/*	A B C	R(A) := (Bool)B; if (C) pc++			*/
OpCode OP_LOADNIL=3;/*	A B	R(A) := ... := R(B) := nil			*/
OpCode OP_GETUPVAL=4;/*	A B	R(A) := UpValue[B]				*/
OpCode OP_GETGLOBAL=5;/*	A Bx	R(A) := Gbl[Kst(Bx)]				*/
OpCode OP_GETTABLE=6;/*	A B C	R(A) := R(B)[RK(C)]				*/
OpCode OP_SETGLOBAL=7;/*	A Bx	Gbl[Kst(Bx)] := R(A)				*/
OpCode OP_SETUPVAL=8;/*	A B	UpValue[B] := R(A)				*/
OpCode OP_SETTABLE=9;/*	A B C	R(A)[RK(B)] := RK(C)				*/
OpCode OP_NEWTABLE=10;/*	A B C	R(A) := {} (size = B,C)				*/
OpCode OP_SELF=11;/*	A B C	R(A+1) := R(B); R(A) := R(B)[RK(C)]		*/
OpCode OP_ADD=12;/*	A B C	R(A) := RK(B) + RK(C)				*/
OpCode OP_SUB=13;/*	A B C	R(A) := RK(B) - RK(C)				*/
OpCode OP_MUL=14;/*	A B C	R(A) := RK(B) * RK(C)				*/
OpCode OP_DIV=15;/*	A B C	R(A) := RK(B) / RK(C)				*/
OpCode OP_MOD=16;/*	A B C	R(A) := RK(B) % RK(C)				*/
OpCode OP_POW=17;/*	A B C	R(A) := RK(B) ^ RK(C)				*/
OpCode OP_UNM=18;/*	A B	R(A) := -R(B)					*/
OpCode OP_NOT=19;/*	A B	R(A) := not R(B)				*/
OpCode OP_LEN=20;/*	A B	R(A) := length of R(B)				*/
OpCode OP_CONCAT=21;/*	A B C	R(A) := R(B).. ... ..R(C)			*/
OpCode OP_JMP=22;/*	sBx	pc+=sBx					*/
OpCode OP_EQ=23;/*	A B C	if ((RK(B) == RK(C)) ~= A) then pc++		*/
OpCode OP_LT=24;/*	A B C	if ((RK(B) <  RK(C)) ~= A) then pc++  		*/
OpCode OP_LE=25;/*	A B C	if ((RK(B) <= RK(C)) ~= A) then pc++  		*/
OpCode OP_TEST=26;/*	A C	if not (R(A) <=> C) then pc++			*/
OpCode OP_TESTSET=27;/*	A B C	if (R(B) <=> C) then R(A) := R(B) else pc++	*/
OpCode OP_CALL=28;/*	A B C	R(A), ... ,R(A+C-2) := R(A)(R(A+1), ... ,R(A+B-1)) */
OpCode OP_TAILCALL=29;/*	A B C	return R(A)(R(A+1), ... ,R(A+B-1))		*/
OpCode OP_RETURN=30;/*	A B	return R(A), ... ,R(A+B-2)	(see note)	*/
OpCode OP_FORLOOP=31;/*	A sBx	R(A)+=R(A+2);if R(A) <?= R(A+1) then { pc+=sBx; R(A+3)=R(A) }*/
OpCode OP_FORPREP=32;/*	A sBx	R(A)-=R(A+2); pc+=sBx				*/
OpCode OP_TFORLOOP=33;/*	A C	R(A+3), ... ,R(A+2+C) := R(A)(R(A+1), R(A+2));if R(A+3) ~= nil then R(A+2)=R(A+3) else pc++	*/
OpCode OP_SETLIST=34;/*	A B C	R(A)[(C-1)*FPF+i] := R(A+i), 1 <= i <= B	*/
OpCode OP_CLOSE=35;/*	A 	close all variables in the stack up to (>=) R(A)*/
OpCode OP_CLOSURE=36;/*	A Bx	R(A) := closure(KPROTO[Bx], R(A), ... ,R(A+n))	*/
OpCode OP_VARARG=37;/*	A B	R(A), R(A+1), ..., R(A+B-1) = vararg		*/

const char *const luaP_opnames[NUM_OPCODES+1] = {
  "MOVE",
  "LOADK",
  "LOADBOOL",
  "LOADNIL",
  "GETUPVAL",
  "GETGLOBAL",
  "GETTABLE",
  "SETGLOBAL",
  "SETUPVAL",
  "SETTABLE",
  "NEWTABLE",
  "SELF",
  "ADD",
  "SUB",
  "MUL",
  "DIV",
  "MOD",
  "POW",
  "UNM",
  "NOT",
  "LEN",
  "CONCAT",
  "JMP",
  "EQ",
  "LT",
  "LE",
  "TEST",
  "TESTSET",
  "CALL",
  "TAILCALL",
  "RETURN",
  "FORLOOP",
  "FORPREP",
  "TFORLOOP",
  "SETLIST",
  "CLOSE",
  "CLOSURE",
  "VARARG",
  NULL
};


#define opmode(t,a,b,c,m) (((t)<<7) | ((a)<<6) | ((b)<<4) | ((c)<<2) | (m))

const OpRun luaP_opmodes[NUM_OPCODES] = {
/*       T  A    B       C     mode		   opcode	*/
  opmode(0, 1, OpArgR, OpArgN, iABC) 		/* OP_MOVE */
 ,opmode(0, 1, OpArgK, OpArgN, iABx)		/* OP_LOADK */
 ,opmode(0, 1, OpArgU, OpArgU, iABC)		/* OP_LOADBOOL */
 ,opmode(0, 1, OpArgR, OpArgN, iABC)		/* OP_LOADNIL */
 ,opmode(0, 1, OpArgU, OpArgN, iABC)		/* OP_GETUPVAL */
 ,opmode(0, 1, OpArgK, OpArgN, iABx)		/* OP_GETGLOBAL */
 ,opmode(0, 1, OpArgR, OpArgK, iABC)		/* OP_GETTABLE */
 ,opmode(0, 0, OpArgK, OpArgN, iABx)		/* OP_SETGLOBAL */
 ,opmode(0, 0, OpArgU, OpArgN, iABC)		/* OP_SETUPVAL */
 ,opmode(0, 0, OpArgK, OpArgK, iABC)		/* OP_SETTABLE */
 ,opmode(0, 1, OpArgU, OpArgU, iABC)		/* OP_NEWTABLE */
 ,opmode(0, 1, OpArgR, OpArgK, iABC)		/* OP_SELF */
 ,opmode(0, 1, OpArgK, OpArgK, iABC)		/* OP_ADD */
 ,opmode(0, 1, OpArgK, OpArgK, iABC)		/* OP_SUB */
 ,opmode(0, 1, OpArgK, OpArgK, iABC)		/* OP_MUL */
 ,opmode(0, 1, OpArgK, OpArgK, iABC)		/* OP_DIV */
 ,opmode(0, 1, OpArgK, OpArgK, iABC)		/* OP_MOD */
 ,opmode(0, 1, OpArgK, OpArgK, iABC)		/* OP_POW */
 ,opmode(0, 1, OpArgR, OpArgN, iABC)		/* OP_UNM */
 ,opmode(0, 1, OpArgR, OpArgN, iABC)		/* OP_NOT */
 ,opmode(0, 1, OpArgR, OpArgN, iABC)		/* OP_LEN */
 ,opmode(0, 1, OpArgR, OpArgR, iABC)		/* OP_CONCAT */
 ,opmode(0, 0, OpArgR, OpArgN, iAsBx)		/* OP_JMP */
 ,opmode(1, 0, OpArgK, OpArgK, iABC)		/* OP_EQ */
 ,opmode(1, 0, OpArgK, OpArgK, iABC)		/* OP_LT */
 ,opmode(1, 0, OpArgK, OpArgK, iABC)		/* OP_LE */
 ,opmode(1, 1, OpArgR, OpArgU, iABC)		/* OP_TEST */
 ,opmode(1, 1, OpArgR, OpArgU, iABC)		/* OP_TESTSET */
 ,opmode(0, 1, OpArgU, OpArgU, iABC)		/* OP_CALL */
 ,opmode(0, 1, OpArgU, OpArgU, iABC)		/* OP_TAILCALL */
 ,opmode(0, 0, OpArgU, OpArgN, iABC)		/* OP_RETURN */
 ,opmode(0, 1, OpArgR, OpArgN, iAsBx)		/* OP_FORLOOP */
 ,opmode(0, 1, OpArgR, OpArgN, iAsBx)		/* OP_FORPREP */
 ,opmode(1, 0, OpArgN, OpArgU, iABC)		/* OP_TFORLOOP */
 ,opmode(0, 0, OpArgU, OpArgU, iABC)		/* OP_SETLIST */
 ,opmode(0, 0, OpArgN, OpArgN, iABC)		/* OP_CLOSE */
 ,opmode(0, 1, OpArgU, OpArgN, iABx)		/* OP_CLOSURE */
 ,opmode(0, 1, OpArgU, OpArgN, iABC)		/* OP_VARARG */
};