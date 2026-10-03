/*
** The client's bit library (`bit'), ported from the reference (FUN_00850cc0..FUN_00851010).
**
** Operands are numbers truncated toward zero to 64 bits and cut to their low 32 (the
** reference's _ftol2); results come back as unsigned 32-bit values, except `arshift' and `mod',
** which are signed.
*/

#define bit_c
#define LUA_LIB

#include "lua.h"

#include "lauxlib.h"
#include "lualib.h"


typedef unsigned int UBits;
typedef int SBits;


static UBits checkbits (lua_State *L, int narg) {
  return (UBits)(long long)luaL_checknumber(L, narg);
}


static void pushunsigned (lua_State *L, UBits b) {
  lua_pushnumber(L, (lua_Number)b);
}


/* ref: FUN_00850cc0 */
static int bit_bnot (lua_State *L) {
  pushunsigned(L, ~checkbits(L, 1));
  return 1;
}


/* ref: FUN_00850d00 */
static int bit_band (lua_State *L) {
  int n = lua_gettop(L);
  UBits b = checkbits(L, 1);
  int i;
  for (i = 2; i <= n; i++)
    b &= checkbits(L, i);
  pushunsigned(L, b);
  return 1;
}


/* ref: FUN_00850d80 */
static int bit_bor (lua_State *L) {
  int n = lua_gettop(L);
  UBits b = checkbits(L, 1);
  int i;
  for (i = 2; i <= n; i++)
    b |= checkbits(L, i);
  pushunsigned(L, b);
  return 1;
}


/* ref: FUN_00850e00 */
static int bit_bxor (lua_State *L) {
  int n = lua_gettop(L);
  UBits b = checkbits(L, 1);
  int i;
  for (i = 2; i <= n; i++)
    b ^= checkbits(L, i);
  pushunsigned(L, b);
  return 1;
}


/* ref: FUN_00850e80 */
static int bit_lshift (lua_State *L) {
  UBits n = checkbits(L, 2);
  UBits b = checkbits(L, 1);
  pushunsigned(L, b << (n & 31));
  return 1;
}


/* ref: FUN_00850ee0 */
static int bit_rshift (lua_State *L) {
  UBits n = checkbits(L, 2);
  UBits b = checkbits(L, 1);
  pushunsigned(L, b >> (n & 31));
  return 1;
}


/* ref: FUN_00850f40 */
static int bit_arshift (lua_State *L) {
  UBits n = checkbits(L, 2);
  SBits b = (SBits)checkbits(L, 1);
  lua_pushnumber(L, (lua_Number)(b >> (n & 31)));
  return 1;
}


/* ref: FUN_00850f90 */
static int bit_mod (lua_State *L) {
  SBits d = (SBits)checkbits(L, 2);
  if (d == 0) {
    /* a divisor that truncates to zero divides as a number instead */
    lua_pushnumber(L, 1 / lua_tonumber(L, 2));
    return 1;
  }
  d = (SBits)checkbits(L, 2);
  lua_pushnumber(L, (lua_Number)((SBits)checkbits(L, 1) % d));
  return 1;
}


static const struct luaL_Reg bitlib[] = {
  {"bnot", bit_bnot},
  {"band", bit_band},
  {"bor", bit_bor},
  {"bxor", bit_bxor},
  {"lshift", bit_lshift},
  {"rshift", bit_rshift},
  {"arshift", bit_arshift},
  {"mod", bit_mod},
  {NULL, NULL}
};


/* ref: FUN_00851010 */
LUALIB_API int luaopen_bit (lua_State *L) {
  luaL_openlib(L, LUA_BITLIBNAME, bitlib, 0);
  return 1;
}
