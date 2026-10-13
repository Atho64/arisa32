import ida_hexrays
cf = ida_hexrays.decompile(0x433FE0)
open(r"G:\DC4 new\dc4engine\work\title_state.c", "w").write(str(cf))
print("dumped", len(str(cf)))
