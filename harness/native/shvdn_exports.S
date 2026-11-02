// ScriptHookV's functions under their Windows C++ (MSVC) names, which ScriptHookV .NET imports by exact name.
// Each is a one-instruction jump to the real function.
.text
.macro STUB name, target
  .globl "\name"
  .p2align 2
"\name":
  b \target
.endm
STUB "_?nativeInit@@YAX_K@Z", _shv_nativeInit
STUB "_?nativePush64@@YAX_K@Z", _shv_nativePush64
STUB "_?nativeCall@@YAPEA_KXZ", _shv_nativeCall
STUB "_?createTexture@@YAHPEBD@Z", _shv_createTexture
STUB "_?drawTexture@@YAXHHHHMMMMMMMMMMMM@Z", _shv_drawTexture
STUB "_?getGameVersion@@YA?AW4eGameVersion@@XZ", _shv_getGameVersion
STUB "_?getGlobalPtr@@YAPEA_KH@Z", _shv_getGlobalPtr

// Stands in for PC game functions that don't exist on iOS: does nothing, returns 0 (integer and float).
.globl _shv_null_stub
.p2align 2
_shv_null_stub:
  mov x0, #0
  movi d0, #0
  ret
