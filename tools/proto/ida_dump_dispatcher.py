import idc
import idautils
import ida_hexrays
import ida_nalt
import ida_bytes
import json

# 1) full disassembly of the dispatcher
lines = []
ea = 0x428640
end = 0x431690
while ea < end and ea != idc.BADADDR:
    lines.append("%08X  %s" % (ea, idc.GetDisasm(ea)))
    ea = idc.next_head(ea, end)
open(r"G:\DC4 new\dc4engine\work\disasm_dispatcher.txt", "w").write("\n".join(lines))

# 2) full pseudocode
try:
    cf = ida_hexrays.decompile(0x428640)
    open(r"G:\DC4 new\dc4engine\work\pseudo_dispatcher.c", "w").write(str(cf))
except Exception as e:
    open(r"G:\DC4 new\dc4engine\work\pseudo_dispatcher.c", "w").write("// error: %s" % e)

# 3) jump table with targets
si = ida_nalt.get_switch_info(0x428DBC)
targets = [ida_bytes.get_dword(si.jumps + i * 4) for i in range(si.ncases)]
json.dump({"jumptable": hex(si.jumps), "ncases": si.ncases,
           "targets": [hex(t) for t in targets]},
          open(r"G:\DC4 new\dc4engine\work\jumptable.json", "w"))

# 4) also dump pseudocode of the other 8 sub-switch functions/tables context is same function
print("done", len(lines))
