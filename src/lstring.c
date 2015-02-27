/*
** $Id: lstring.c,v 2.8.1.1 2007/12/27 13:02:25 roberto Exp $
** String table (keeps all strings handled by Lua)
** See Copyright Notice in lua.h
*/


#include <string.h>

#define lstring_c
#define LUA_CORE

#include "nlua.h"

#include "lmem.h"
#include "lobject.h"
#include "lstate.h"
#include "lstring.h"

/* 重新设置字符串hash表长度
 * L 线程状态指针
 * newsize hash表的新长度
 */
void luaS_resize (lua_State *L, int newsize) {
  GCObject **newhash;
  stringtable *tb;
  int i;
  
  /* 如果垃圾回收正在清理字符串缓存 */
  if (G(L)->gcstate == GCSsweepstring)
    return;  /* 不能重新设置在GC回收期间 */
  newhash = luaM_newvector(L, newsize, GCObject *);
  tb = &G(L)->strt;
  for (i=0; i<newsize; i++) newhash[i] = NULL;
  /* rehash */
  for (i=0; i<tb->size; i++) {
    GCObject *p = tb->hash[i];
    while (p) {  /* for each node in the list */
      GCObject *next = p->gch.next;  /* save next */
      unsigned int h = gco2ts(p)->hash;
      int h1 = lmod(h, newsize);  /* new position */
      lua_assert(cast_int(h%newsize) == lmod(h, newsize));
      p->gch.next = newhash[h1];  /* chain it */
      newhash[h1] = p;
      p = next;
    }
  }
  luaM_freearray(L, tb->hash, tb->size, TString *);
  tb->size = newsize;
  tb->hash = newhash;
}

/* 插入新的字符串
 * L 线程状态指针
 * str 要插入的字符串
 * l 字符串的长度
 * h 字符串的hash值
 */
static TString *newlstr (lua_State *L, const char *str, size_t l,
                                       unsigned int h) {
  TString *ts;
  stringtable *tb;
  
  /* 如果当前字符串长度过大则出错 */
  if (l+1 > (MAX_SIZET - sizeof(TString))/sizeof(char))
    luaM_toobig(L);
  
  /* 分配一个TString结构
   * 在TString之后跟的真正的字符串
   */
  ts = cast(TString *, luaM_malloc(L, (l+1)*sizeof(char)+sizeof(TString)));
  ts->tsv.len = l;
  ts->tsv.hash = h;
  ts->tsv.marked = luaC_white(G(L));
  ts->tsv.tt = LUA_TSTRING;
  ts->tsv.reserved = 0;
  
  /* 复制字符串内容 */
  memcpy(ts+1, str, l*sizeof(char));
  ((char *)(ts+1))[l] = '\0';  /* ending 0 */
  
  /* 字符串hash表 */
  tb = &G(L)->strt;
  h = lmod(h, tb->size);
  
  /* 重新连接hash表分支节点 */
  ts->tsv.next = tb->hash[h];  /* chain new entry */
  tb->hash[h] = obj2gco(ts);
  tb->nuse++;
  
  /* 重新规划字符串hash表的大小 */
  if (tb->nuse > cast(lu_int32, tb->size) && tb->size <= MAX_INT/2)
    luaS_resize(L, tb->size*2);  /* too crowded */
  return ts;
}

/* 创建新字符串
 * L 线程状态指针
 * str 字符串缓存指针
 * l 字符串长度
 */
TString *luaS_newlstr (lua_State *L, const char *str, size_t l) {
  GCObject *o;
  unsigned int h = cast(unsigned int, l);         /* 使用字符串长度作为hash种子 */
  size_t step = (l>>5)+1;                         /* 如果字符串不太长，不要hash它的所有字符 */
  size_t l1;
  for (l1=l; l1>=step; l1-=step)                  /* 计算hash值 */
    h = h ^ ((h<<5)+(h>>2)+cast(unsigned char, str[l1-1]));
  
  /* 遍历hash树上拥有同样hash值的分支，寻找当前要插入的字符串
   * 直到为空则退出循环
   * 找到则直接退出
   */
  for (o = G(L)->strt.hash[lmod(h, G(L)->strt.size)];
       o != NULL;
       o = o->gch.next) {
    TString *ts = rawgco2ts(o);     /* 从对象中取出字符串 */
    
    /* 如果要插入的字符串相同则直接返回 */
    if (ts->tsv.len == l && (memcmp(str, getstr(ts), l) == 0)) {
      /* 关于内存回收
       * string may be dead
       */
      if (isdead(G(L), o)) changewhite(o);
      return ts;
    }
  }
  
  /* 直接插入新字符串 */
  return newlstr(L, str, l, h);  /* not found */
}


Udata *luaS_newudata (lua_State *L, size_t s, Table *e) {
  Udata *u;
  if (s > MAX_SIZET - sizeof(Udata))
    luaM_toobig(L);
  u = cast(Udata *, luaM_malloc(L, s + sizeof(Udata)));
  u->uv.marked = luaC_white(G(L));  /* is not finalized */
  u->uv.tt = LUA_TUSERDATA;
  u->uv.len = s;
  u->uv.metatable = NULL;
  u->uv.env = e;
  /* chain it on udata list (after main thread) */
  u->uv.next = G(L)->mainthread->next;
  G(L)->mainthread->next = obj2gco(u);
  return u;
}

