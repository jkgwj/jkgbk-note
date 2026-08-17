#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""从 global-metadata_fix.dat + libil2cpp_fixed.so 生成 IDA rename 脚本

针对厂商魔改 il2cpp（metadata version 伪 29 / typeDef 92 字节 / codeRegistration 前移 2 指针）
直接从元数据 methodDefs(<token>) + codeGenModules.methodPointers 提取 RVA，生成:
    ida_rename.py    IDAPython 脚本（对当前 IDA 库批量 set_name）
    method_map.tsv   列名: rva fullname
    types.tsv        列名: typeindex fullname
用法: python3 gen_ida_rename.py [--so 路径] [--meta 路径] [--out 目录] [--codereg 0x1a8a2568]
"""
import argparse
import struct
import sys
import os

MINUS1 = 0xFFFFFFFFFFFFFFFF


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--so", default="libil2cpp_fixed.so")
    ap.add_argument("--meta", default="global-metadata_fix.dat")
    ap.add_argument("--out", default=".")
    ap.add_argument("--codereg", default="0x1a8a2568")
    args = ap.parse_args()

    so = open(args.so, "rb").read()
    g = open(args.meta, "rb").read()

    def u64(o):
        return struct.unpack_from("<Q", so, o)[0]

    def i64(o):
        return struct.unpack_from("<q", so, o)[0]

    def u32(o):
        return struct.unpack_from("<I", g, o)[0]

    def i32(o):
        return struct.unpack_from("<i", g, o)[0]

    def u16(o):
        return struct.unpack_from("<H", g, o)[0]

    # ---------- 1. 校验 metadata 头（v29 标准 uint 布局） ----------
    assert u32(0) == 0xFAB11BAF, "metadata magic"
    ver = i32(4)
    print("metadata version=%d  typeDefsOff=0x%x  stringsOff=0x%x" % (
        ver, u32(0xa0), u32(0x18)))

    strOff = u32(0x18)
    tdOff = u32(0xa0)
    tdSize = i32(0xa4)
    methodsOff = u32(0x30)
    methodsSize = i32(0x34)
    imgOff = u32(0xa8)
    imgSize = i32(0xac)

    TD_SZ = 92
    nTypeDefs = tdSize // TD_SZ
    nMethods = methodsSize // 32
    nImages = imgSize // 40
    print("typeDefs=%d methods=%d images=%d" % (nTypeDefs, nMethods, nImages))

    def gstr(idx):
        if idx < 0:
            return ""
        end = g.find(b"\x00", strOff + idx, strOff + idx + 512)
        if end < 0:
            return "?"
        return g[strOff + idx:end].decode("utf-8", "replace")

    # ---------- 2. typeDefs（92B，字段经实测: 见上方锚点推导） ----------
    # 0 name, 4 ns, 8 byval,12 extra,16 parent,20 byval2,24 gci,28 flags,
    # 32 ?(declaring-Type-><>c), 36 methodStart,40 eventStart,44 propertyStart,
    # 48 nestedStart,52 interfacesStart,56 vtableStart,60 ioStart,64 fieldStart,
    # 68..84 counts(8*u16),84 bitfield,88 token
    def typeName(i):
        o = tdOff + i * TD_SZ
        nm = gstr(u32(o))
        ns = gstr(u32(o + 4))
        return (ns + "." + nm) if ns else nm

    # ---------- 3. images ----------
    imgs = []  # (name, typeStart, typeCount, index)
    for i in range(nImages):
        o = imgOff + i * 40
        nm = gstr(u32(o))
        ts = i32(o + 8)
        tc = u32(o + 12)
        imgs.append((nm, ts, tc, i))

    def img_of_type(ti):
        for nm, ts, tc, _ in imgs:
            if ts <= ti < ts + tc:
                return nm
        return None

    # ---------- 4. 运行库镜像基址（ELF delta=0, RVA = VA - MB） ----------
    # 从 metadataRegistration.types (runtime ptr) 与 types 数组已知 file 偏移 0x1b3fd2a0 反推
    META_REG = 0x1b17e590
    typesRuntime = u64(META_REG + 56)  # types 字段
    TYPES_ARRAY_FILE = 0x1b3fd2a0
    MB = typesRuntime - TYPES_ARRAY_FILE
    print("runtime image base MB = 0x%x" % MB)

    def rva_of(va):
        return va - MB

    def instr_ok(off):
        return 0 <= off < len(so)

    # ---------- 5. codeRegistration 的 codeGenModules ----------
    codeReg = int(args.codereg, 16)
    cgm_count = struct.unpack_from("<q", so, codeReg + 0x78)[0]
    cgm_ptr_runtime = u64(codeReg + 0x80)
    cgm_arr = rva_of(cgm_ptr_runtime)
    modules = {}
    for i in range(cgm_count):
        p = u64(cgm_arr + i * 8)
        q = rva_of(p)
        nmr = u64(q)
        mpc = struct.unpack_from("<q", so, q + 8)[0]
        mpp = u64(q + 16)
        nq = rva_of(nmr)
        end = so.find(b"\x00", nq, nq + 512)
        name = so[nq:end].decode("utf-8", "replace")
        modules[name] = (mpc, rva_of(mpp))
    print("codeGenModules=%d" % len(modules))

    # ---------- 6. methods -> (rva, fullname) ----------
    # Il2CppMethodDefinition(v29, 32B): 0 name,4 declaringType,8 returnType,
    # 12 parameterStart,16 gci,20 token,24 flags,26 iflags,28 slot,30 paramCount
    byaddr = {}       # addr -> fullname
    cnt = {"ok": 0, "noimg": 0, "nomod": 0, "oob": 0, "zero": 0}
    for m in range(nMethods):
        o = methodsOff + m * 32
        mname = gstr(u32(o))
        decl = i32(o + 4)
        tok = u32(o + 20)
        if not (0 <= decl < nTypeDefs):
            cnt["noimg"] += 1
            continue
        img = img_of_type(decl)
        if img is None:
            cnt["noimg"] += 1
            continue
        mod = modules.get(img)
        if mod is None:
            cnt["nomod"] += 1
            continue
        mpc, mpp = mod
        idx = (tok & 0xFFFFFF) - 1
        if idx < 0 or idx >= mpc:
            cnt["oob"] += 1
            continue
        va = u64(mpp + idx * 8)
        ra = rva_of(va)
        if va == 0:
            cnt["zero"] += 1
            continue
        if not instr_ok(ra):
            cnt["oob"] += 1
            continue
        full = "%s.%s" % (typeName(decl), mname)
        if ra not in byaddr:
            byaddr[ra] = full
        cnt["ok"] += 1
    print("method resolve:", cnt)

    # 同地址去重（保留第一个名字）
    # ---------- 7. 输出 ----------
    os.makedirs(args.out, exist_ok=True)

    with open(os.path.join(args.out, "method_map.tsv"), "w", encoding="utf-8") as f:
        f.write("# rva\tsignature\n")
        for ra in sorted(byaddr):
            f.write("0x%08x\t%s\n" % (ra, byaddr[ra]))

    with open(os.path.join(args.out, "types.tsv"), "w", encoding="utf-8") as f:
        f.write("# typeindex\tfullname\n")
        for i in range(nTypeDefs):
            f.write("%d\t%s\n" % (i, typeName(i)))

    with open(os.path.join(args.out, "ida_rename.py"), "w", encoding="utf-8") as f:
        f.write("# -*- coding: utf-8 -*-\n")
        f.write("# Generated by gen_ida_rename.py - apply inside IDA on libil2cpp_fixed.so\n")
        f.write("# ELF segments loaded at RVA (p_vaddr==p_offset, delta 0); addresses below are RVAs.\n")
        f.write("import idaapi, idc, idautils\n")
        f.write("import ida_name\n")
        f.write("_names = {\n")
        for ra in sorted(byaddr):
            nm = byaddr[ra].replace('"', "'")
            f.write("    0x%x: %r,\n" % (ra, nm))
        f.write("}\n")
        f.write("def _setname(_ea, _name):\n")
        f.write("    _sn = 0\n")
        f.write("    for _c in ('SN_NOWARN', 'SN_NOCHECK', 'SN_FORCE'):\n")
        f.write("        _m = getattr(idc, _c, None)\n")
        f.write("        if _m is None:\n")
        f.write("            _m = getattr(ida_name, _c, None)\n")
        f.write("        if _m is not None:\n")
        f.write("            _sn |= _m\n")
        f.write("    return ida_name.set_name(_ea, _name, _sn)\n")
        f.write("_applied = 0\n")
        f.write("for _ea in idautils.Functions():\n")
        f.write("    _name = _names.get(_ea)\n")
        f.write("    if _name and _setname(_ea, _name):\n")
        f.write("        _applied += 1\n")
        f.write("print('renamed %d functions' % _applied)\n")

    print("written: ida_rename.py, method_map.tsv, types.tsv -> %s" % args.out)


if __name__ == "__main__":
    main()