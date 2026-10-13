import ida_hexrays
import ida_funcs
import json
import re

jt_targets = json.load(open(r"G:\DC4 new\dc4engine\work\jumptable.json"))
dis = open(r"G:\DC4 new\dc4engine\work\disasm_dispatcher.txt").read()
callees = sorted(set(re.findall(r'call\s+(sub_[0-9A-F]+)', dis)))
out = {}
errs = {}
for fn in callees:
    ea = int(fn[4:], 16)
    f = ida_funcs.get_func(ea)
    if not f:
        continue
    try:
        cf = ida_hexrays.decompile(ea)
        out[fn] = {"addr": hex(ea), "size": f.end_ea - f.start_ea, "code": str(cf)}
    except Exception as e:
        errs[fn] = str(e)
json.dump(out, open(r"G:\DC4 new\dc4engine\work\callees.json", "w"))
print("decompiled", len(out), "errors", len(errs))
