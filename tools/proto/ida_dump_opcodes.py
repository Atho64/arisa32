import ida_nalt
import ida_bytes
import ida_hexrays
import ida_funcs
import idc
import json

si = ida_nalt.get_switch_info(0x428DBC)
jt = si.jumps
out = {}
for i in range(si.ncases):
    tgt = ida_bytes.get_dword(jt + i * 4)
    f = ida_funcs.get_func(tgt)
    entry = {"op": i, "handler": hex(tgt), "size": (f.end_ea - f.start_ea) if f else 0}
    if tgt == 0x430EDA:
        entry["valid"] = False
    else:
        entry["valid"] = True
        try:
            cf = ida_hexrays.decompile(tgt)
            entry["code"] = str(cf)
        except Exception as e:
            entry["code_err"] = str(e)
    out[i] = entry

with open(r"G:\DC4 new\dc4engine\work\opcodes.json", "w") as fp:
    json.dump(out, fp)
print("dumped", len(out))
