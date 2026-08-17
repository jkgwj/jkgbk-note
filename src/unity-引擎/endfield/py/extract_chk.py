#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
从 .chk 容器中按 .blc JSON 清单切割出文件（可选外解密）。

用法:
    python3 extract_chk.py <json> <chk>                # 基本切割
    python3 extract_chk.py <json> <chk> --decrypt      # 对 flag=1 文件做 XXE1 解密
    python3 extract_chk.py <json> <chk_dir>            # chk 为目录时批量匹配
    python3 extract_chk.py <json> <chk> --out ../out   # 指定输出目录

必填参数:
    json : decrypt_blc.py 生成的 .blc 清单 JSON
    chk  : 对应 .chk 文件；或含 .chk 的目录(逐个匹配清单 chunk)
           chunk.md5Name(小写hex) 与磁盘 .chk 文件名(大写hex) 大小写不敏感匹配

可选参数:
    --decrypt : 启用外解密。仅对 flag=1(带加密标识) 的文件做 XXE1(ChaCha20)
                解密；flag=0 的 .ab 原样切出(其 native 解密后续单独工具处理)
    --out     : 输出根目录(默认: 脚本所在目录, 保持 JSON 里相对路径结构)
    --force   : 覆盖已存在文件(默认跳过)

行为:
    - 每个 fileRecord 按 offset/len 从对应 .chk 切割, fileName 自带目录,
      自动 mkdir 后写入。
    - flag=0 明文直接可用; flag=1 默认仍写密文, 加 --decrypt 后解密输出。
    - 越界/路径穿越检查。

依赖: pycryptodome(x) 仅 --decrypt 时需要。
"""
import os
import sys
import json
import glob
import struct
import argparse

# 与 decrypt_blc.py 一致: VirtualFileSystem.GetCommonChachaKeyBs 设备实测密钥
KEY = bytes.fromhex("e95b317ac4f828569d23a86bf271dcb53e846fa75c924d671dba8e38f4ca52e1")


def _chacha():
    for name in ("Cryptodome", "Crypto"):
        try:
            return __import__(name + ".Cipher.ChaCha20", fromlist=["new"])
        except ImportError:
            continue
    raise RuntimeError("需要 pycryptodome(x): apt install python3-pycryptodome")


def decrypt_xxe1(data, iv_seed):
    """XXE1(ChaCha20) 文件级解密, 与 blc 头部同构: nonce=ivSeed(4)+补零(8), counter=1."""
    nonce = struct.pack("<I", iv_seed & 0xffffffff) + b"\x00" * 8
    c = _chacha().new(key=KEY, nonce=nonce)
    c.seek(64)                     # counter=1
    return c.encrypt(data)


def find_chk(md5_name, chk_target, json_path):
    """md5Name(小写) 匹配 .chk 文件(任意大小写), 返回绝对路径或 None."""
    base = md5_name.lower()
    if os.path.isfile(chk_target):
        return os.path.abspath(chk_target)
    cand_dirs = [os.path.abspath(chk_target),
                 os.path.dirname(os.path.abspath(json_path)),
                 os.getcwd()]
    seen = set()
    for d in cand_dirs:
        if d in seen or not os.path.isdir(d):
            continue
        seen.add(d)
        for name in os.listdir(d):
            if not name.lower().endswith(".chk"):
                continue
            if name.lower().startswith(base) and len(name) - len(".chk") == len(base):
                return os.path.join(d, name)
    return None


def chk_md5_of_file(chk_path):
    """从 .chk 文件名提取小写 md5Name."""
    return os.path.splitext(os.path.basename(chk_path))[0].lower()


def process_json(json_path, chk_target, out_root, force, decrypt):
    with open(json_path, "rb") as f:
        data = json.load(f)

    group = data.get("groupCfgName", "?")
    block = data.get("blockType", "?")
    chunks = data.get("chunks", [])
    print("== %s : groupCfgName=%s blockType=%s chunks=%d" %
          (os.path.basename(json_path), group, block, len(chunks)))

    stats = {"files": 0, "encrypted": 0, "skipped": 0, "missing": 0, "bytes": 0}

    if os.path.isfile(chk_target):
        # 明确指定单个 chk: 在 JSON 里反查对应 md5Name, 找不到就报错
        want = chk_md5_of_file(chk_target)
        match = [c for c in chunks if c.get("md5Name", "").lower() == want]
        if not match:
            print("!! 错误: JSON 中找不到与 .chk 对应的哈希值")
            print("   .chk 文件名     : %s" % os.path.basename(chk_target))
            print("   提取的 md5Name  : %s" % want)
            print("   请检查 JSON 是否正确、chk 是否属于该清单, 或 JSON 是否已过期")
            raise SystemExit(2)
        chunks = match

    for ci, chunk in enumerate(chunks):
        md5 = chunk.get("md5Name", "")
        chk_path = find_chk(md5, chk_target, json_path)
        if not chk_path:
            if os.path.isfile(chk_target):
                continue  # 指定单个 chk, 只处理匹配的 chunk
            print("  [chunk %d] 未找到 .chk: %s (跳过 %d 个文件)" %
                  (ci, md5, len(chunk.get("files", []))))
            stats["missing"] += len(chunk.get("files", []))
            continue

        chk_size = os.path.getsize(chk_path)
        cname = os.path.basename(chk_path)
        print("  [chunk %d] 容器: %s (%d 字节, %d 文件)" %
              (ci, cname, chk_size, len(chunk.get("files", []))))
        for fi, f in enumerate(chunk.get("files", [])):
            fname = f["fileName"]
            off = f["offset"]
            ln = f["len"]
            flag = f.get("flag", 0)

            if off + ln > chk_size:
                print("  [%s] 越界: offset+len=%d > chk大小 %d, 跳过" %
                      (fname, off + ln, chk_size))
                stats["missing"] += 1
                continue

            out_path = os.path.join(out_root, fname)
            if os.path.commonpath([os.path.abspath(out_root),
                                   os.path.abspath(out_path)]) != os.path.abspath(out_root):
                print("  [%s] 路径越界, 跳过" % fname)
                stats["missing"] += 1
                continue

            if os.path.exists(out_path) and not force:
                stats["skipped"] += 1
                continue

            os.makedirs(os.path.dirname(out_path), exist_ok=True)
            with open(chk_path, "rb") as cf:
                cf.seek(off)
                blob = cf.read(ln)

            do_dec = decrypt and flag == 1 and f.get("ivSeed") is not None
            if do_dec:
                try:
                    blob = decrypt_xxe1(blob, f["ivSeed"])
                except Exception as ex:
                    print("  [%s] XXE1 解密失败: %s (写入密文)" % (fname, ex))
                    blob = cf and blob  # keep raw

            with open(out_path, "wb") as of:
                of.write(blob)

            stats["files"] += 1
            stats["bytes"] += ln
            if do_dec:
                stats["encrypted"] += 1
                print("  [%s] (XXE1解密) off=%d len=%d ivSeed=%u" % (fname, off, ln, f["ivSeed"]))
            elif flag == 1:
                print("  [%s] (加密/密文, 未解密) off=%d len=%d" % (fname, off, ln))
            else:
                print("  [%s] off=%d len=%d" % (fname, off, ln))

    print("  完成: 写入 %d 个文件 (%d XXE1解密), 跳过 %d, 缺失 %d, 共 %d 字节" %
          (stats["files"], stats["encrypted"], stats["skipped"],
           stats["missing"], stats["bytes"]))
    return stats


def main():
    ap = argparse.ArgumentParser(description="按 .blc JSON 从 .chk 切割文件(可选 XXE1 外解密)")
    ap.add_argument("json", help="decrypt_blc.py 生成的 JSON 清单")
    ap.add_argument("chk", help="对应 .chk 文件或含 .chk 的目录")
    ap.add_argument("--decrypt", action="store_true",
                    help="对 flag=1 文件做 XXE1 解密(需要 pycryptodome)")
    ap.add_argument("--out", default=None, help="输出根目录(默认脚本所在目录)")
    ap.add_argument("--force", action="store_true", help="覆盖已存在文件")
    args = ap.parse_args()

    if not os.path.isfile(args.json):
        print("JSON 不存在: %s" % args.json)
        return 1
    if not os.path.exists(args.chk):
        print("chk 不存在: %s" % args.chk)
        return 1

    out_root = os.path.abspath(args.out) if args.out \
        else os.path.dirname(os.path.abspath(__file__))
    os.makedirs(out_root, exist_ok=True)
    print("输出根目录: %s" % out_root)

    st = process_json(args.json, args.chk, out_root, args.force, args.decrypt)
    print("\n== 汇总: 写入 %d 文件(%d XXE1解密), 跳过 %d, 缺失 %d, 共 %.2f MB" %
          (st["files"], st["encrypted"], st["skipped"], st["missing"],
           st["bytes"] / 1048576.0))
    return 0


if __name__ == "__main__":
    sys.exit(main())
