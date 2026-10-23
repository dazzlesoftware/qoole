# Lua 5.4.9

Unmodified upstream distribution from https://www.lua.org/ftp/lua-5.4.9.tar.gz.
SHA256: `2335b6c582a52654f94612bf10d2f4672805d05329aa6568b1d8cd9e5c6fb8e6`.
License and copyright: see lua-5.4.9/doc/readme.html and src/lua.h.
Genesis builds the library as C++ (upstream-supported exception unwinding),
excluding the standalone interpreter/compiler entry points. Public Lua headers
are private to the runtime adapter; do not expose Lua types in engine APIs.
