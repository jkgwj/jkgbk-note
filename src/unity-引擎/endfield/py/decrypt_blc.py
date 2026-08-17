#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Decrypt Arknights:Endfield Beyond.VFS .blc (BlockGroupInfo) and dump manifest.

逆向依据 (用户提供的 il2cpp 反编译):
  - Beyond.VFS.VFSUtils.DecryptCreateBlockGroupInfo():
        version = data[0:4]                                     (协议版本, 参与 IV)
        iv      = data[0:12]   (BLOCK_HEAD_LEN=0xC)
        XXE1__ctor(stream, key, version, iv=data[0:12], ivLen=12, 1, 0)
        XXE1_WorkBytes: 原地解密 data[12:]                      -> ChaCha20(counter=1)
        然后 VFBlockMainInfo_ReadFromByteBuf(data, startOffset=12)
  - CRC: ReadFromByteBuf 开头
        其内部 crc 表算法返回 "无最终取反" 的结果 v16, 判据 Int == ~v16.
        等价于标准 zlib.crc32(payload) == LE(尾 4B), payload = out[12 : len-4].
        实测三样本全通过.

  - VFBlockMainInfo_ReadFromByteBuf(流从 a2=12 开始):
        codeVersion = ReadInt()                                (0x48 字段)
        if codeVersion >= 11: version = codeVersion; codeVersion = 3
        else:                  version = ReadInt()             (0x10 字段)
        groupCfgName        = ReadUTF8   (u16+utf8)
        groupCfgHashName    = ReadLong   (8)
        groupFileInfoNum    = ReadInt    (4)
        groupChunksLength   = ReadLong   (8)
        blockType           = ReadByte   (1)
        chunkCount          = ReadInt    (4)
        chunks...                                             (a1 = codeVersion = 4)
        若 codeVersion==3 且剩余>=1 再读一个 Int (本文件 codeVersion=4, 不读)

  - FVFBlockChunkInfo_ReadFromByteBuf(version=a1=4):
        md5Name(16) | contentMD5(16) | length(8)
        blockType(1) | [version>=4: mainTag u32] | fileCount(4)   # 9B 前缀

  - FVFBlockFileInfo_ReadFromByteBuf(version=a1=4):
        name(u16+utf8) | fileNameHash(8) | fileChunkMD5Name(16) | fileDataMD5(16,skip)
        offset(8) | len(8) | blockType(1) | flag(1)
        加密文件(flag=1, .bytes): ivSeed(4) + m_packedFlags(4) + fileTag(4)   -> record 70B
        未加密文件(flag=0, .pck): fileTag(4)                                  -> record 62B
        实测 record 大小: .bytes=70B, .pck=62B (按 groupFileInfoNum 自动选择)
        m_packedFlags 打包 blockType/loaderPosType/fileTag/flag 位
        (实测值 0 或 0xFFFFFFFF; fileTag 实测 None=0 / Audit=1)
        修正: 原实现把首 u32(每文件唯一随机=ivSeed) 误标成 fileTag, 末尾 0/1 误标成 m_packedFlags

  - 主清单尾部 (ReadFromByteBuf 未消费, 但文件里存在, 对应 dump.cs 字段):
        buildTime(u64) | buildNote(u16+utf8) | CRC32(4)
"""
import struct
import sys
import os
import json
import glob
import zlib

KEY = bytes.fromhex("e95b317ac4f828569d23a86bf271dcb53e846fa75c924d671dba8e38f4ca52e1")
PROTO_VERSION = 0x00000003
BLOCK_HEAD_LEN = 0xC


# --------------------------------------------------------------------------
#  ChaCha20 仅用第三方库 (pycryptodomex 或 pycryptodome), 不留手写实现
# --------------------------------------------------------------------------
def _chacha():
    for name in ("Cryptodome", "Crypto"):  # pycryptodomex / pycryptodome
        try:
            return __import__(name + ".Cipher.ChaCha20", fromlist=["new"])
        except ImportError:
            continue
    raise RuntimeError("需要 pycryptodome(x): apt install python3-pycryptodome")

def decrypt_blc(data):
    """返回 data[0:12] + 解密后的 data[12:], 与 XXE1_WorkBytes(data,12,...) 等价."""
    if len(data) <= BLOCK_HEAD_LEN:
        raise ValueError("too short")
    iv = data[:BLOCK_HEAD_LEN]
    ct = data[BLOCK_HEAD_LEN:]
    c = _chacha().new(key=KEY, nonce=iv)
    c.seek(64)                     # counter=1
    return data[:BLOCK_HEAD_LEN] + c.encrypt(ct)


# --------------------------------------------------------------------------
#  二进制读取
# --------------------------------------------------------------------------
class Reader:
    def __init__(self, buf, pos=0):
        self.b = buf
        self.o = pos

    def u8(self):
        v = self.b[self.o]
        self.o += 1
        return v

    def u16(self):
        v = struct.unpack_from("<H", self.b, self.o)[0]
        self.o += 2
        return v

    def u32(self):
        v = struct.unpack_from("<I", self.b, self.o)[0]
        self.o += 4
        return v

    def u64(self):
        v = struct.unpack_from("<Q", self.b, self.o)[0]
        self.o += 8
        return v

    def raw(self, n):
        v = self.b[self.o:self.o + n]
        self.o += n
        return v

    def skip(self, n):
        self.o += n

    def vfs_str(self):
        n = self.u16()
        if n <= 0 or self.o + n > len(self.b):
            return ""
        s = self.b[self.o:self.o + n]
        self.o += n
        return s.decode("utf-8", "replace")


# --------------------------------------------------------------------------
#  主清单 (流从 a2=12 开始, 与 ReadFromByteBuf(a1, 12) 对齐)
# --------------------------------------------------------------------------
def parse_main(out):
    r = Reader(out)
    node = {}
    node["headVersion"] = r.u32()          # out[0:4] = 3, 协议版本(仅用于解密/IV)
    r.skip(BLOCK_HEAD_LEN - 4)             # out[4:12], IV 余段
    code_version = r.u32()                 # out[12:16] = 4  -> 0x48 codeVersion 字段
    node["codeVersion"] = code_version
    if code_version >= 11:                 # 新格式: 版本合一, codeVersion 固定 3
        node["version"] = code_version
        node["codeVersion"] = 3
    else:
        node["version"] = r.u32()          # out[16:20] -> 0x10 version 字段
    node["serializeVersion"] = code_version
    node["groupCfgName"] = r.vfs_str()
    node["groupCfgHashName"] = r.u64()
    node["groupFileInfoNum"] = r.u32()
    node["groupChunksLength"] = r.u64()
    node["blockType"] = r.u8()
    node["chunkCount"] = r.u32()
    return node, r.o


# --------------------------------------------------------------------------
#  chunk + file 解析. 返回 (chunks, files_count, endpos) 或抛出异常
#  chunk 前缀 9B: blockType(1) + mainTag(u32, version>=4) + fileCount(4)
# --------------------------------------------------------------------------
def parse_chunks(out, start, chunk_count, file_tail):
    r = Reader(out, start)
    end = len(out) - 4                    # 尾部 4B 是 CRC, 不属于结构
    chunks = []
    files_count = 0
    for _ in range(chunk_count):
        if r.o + 49 > end:
            raise ValueError("chunk header truncated")
        md5_name = r.raw(16)
        content_md5 = r.raw(16)
        chunk_len = r.u64()
        block_type = r.u8()
        # version>=4 时读取 mainTag(u32); 本文件 serializeVersion=4, 必读
        main_tag = r.u32()
        file_count = r.u32()
        cnode = {"md5Name": md5_name.hex(), "contentMD5": content_md5.hex(),
                 "length": chunk_len, "blockType": block_type,
                 "mainTag": main_tag, "fileCount": file_count, "files": []}
        inner = 0
        while r.o + 2 <= end:
            nl = struct.unpack_from("<H", out, r.o)[0]
            if not (1 <= nl < 512) or r.o + 2 + nl + (file_tail - 0) > end:
                break
            name_bytes = out[r.o + 2:r.o + 2 + nl]
            if not name_bytes or any(b < 0x20 or b > 0x7E for b in name_bytes):
                break
            r.skip(2 + nl)
            fh = r.u64()
            chunk_md5 = r.raw(16)
            data_md5 = r.raw(16)
            offset = r.u64()
            length = r.u64()
            btype = r.u8()
            flag = r.u8()
            iv_seed = None
            packed = None
            # 磁盘布局 (version>=4):
            #   加密文件(flag=1, .bytes):  blockType(1) flag(1) ivSeed(4) m_packedFlags(4) fileTag(4)
            #   未加密文件(flag=0, .pck):  blockType(1) flag(1) fileTag(4)
            if file_tail - 58 >= 12:      # 加密文件带 ivSeed + m_packedFlags + fileTag
                iv_seed = r.u32()
                packed = r.u32()
                file_tag = r.u32()
            else:                          # 未加密文件只有 fileTag
                file_tag = r.u32()
            cnode["files"].append({
                "fileName": name_bytes.decode("utf-8", "replace"),
                "fileNameHash": fh,
                "fileChunkMD5Name": chunk_md5.hex(),
                "fileDataMD5": data_md5.hex(),
                "offset": offset,
                "len": length,
                "blockType": btype,
                "flag": flag,
                "fileTag": file_tag,
                "ivSeed": iv_seed,
                "m_packedFlags": packed,
            })
            files_count += 1
            inner += 1
        if inner > 0:
            cnode["fileCount"] = inner
        chunks.append(cnode)
    return chunks, files_count, r.o


def pick_file_tail(out, start, chunk_count, expect_files):
    """按哪个 tail 能把文件数解析到 == groupFileInfoNum 来自动选择 (70: .bytes / 62: .pck)."""
    for tail in (70, 62, 78, 54, 66, 58):
        try:
            _, n, _ = parse_chunks(out, start, chunk_count, tail)
            if n == expect_files:
                return tail, n
        except Exception:
            continue
    best = (0, 0)
    for tail in (70, 62, 78, 54, 66, 58):
        try:
            _, n, _ = parse_chunks(out, start, chunk_count, tail)
            if n > best[0]:
                best = (n, tail)
        except Exception:
            continue
    return best[1], best[0]


# --------------------------------------------------------------------------
#  入口
# --------------------------------------------------------------------------
def decode_blc(data):
    out = decrypt_blc(data)
    main, start = parse_main(out)

    payload = out[BLOCK_HEAD_LEN:-4]
    crc_stored = struct.unpack_from("<I", out, len(out) - 4)[0]
    crc_calc = zlib.crc32(payload) & 0xffffffff
    main["crcOk"] = crc_calc == crc_stored
    main["crc"] = "0x%08x" % crc_stored

    tail, files = pick_file_tail(out, start, main["chunkCount"], main["groupFileInfoNum"])
    chunks, _, endpos = parse_chunks(out, start, main["chunkCount"], tail)

    # 尾部构建信息 (ReadFromByteBuf 未消费; 对应 dump.cs 的 buildTime/buildNote)
    trailer = out[endpos:-4]
    if len(trailer) >= 10:
        bt = struct.unpack_from("<Q", trailer, 0)[0]
        nl = struct.unpack_from("<H", trailer, 8)[0]
        note = trailer[10:10 + nl] if nl else b""
        main["buildTime"] = bt
        main["buildNote"] = note.decode("utf-8", "replace")
        main["trailerLen"] = len(trailer)
    else:
        main["buildTime"] = None
        main["buildNote"] = ""
        main["trailerLen"] = len(trailer)

    main["fileRecordTailBytes"] = tail
    main["chunks"] = chunks
    main["parseEnd"] = endpos
    main["bufferLen"] = len(out)
    main["leftoverBytes"] = len(out) - endpos
    main["leftoverHex"] = out[endpos:].hex() if endpos < len(out) else ""
    return main


def process_file(path, out_dir=None):
    base = os.path.basename(path)
    if not os.path.isfile(path):
        print("[%s] 不存在或不是文件" % base)
        return False
    try:
        with open(path, "rb") as f:
            data = f.read()
    except Exception as ex:
        print("[%s] 读取失败: %s" % (base, ex))
        return False
    if data[:4].hex() != "03000000":
        print("[%s] warn: version 头不是 03 00 00 00, 仍尝试解密" % base)
    try:
        info = decode_blc(data)
    except Exception as ex:
        print("[%s] 解密/解析失败: %s" % (base, ex))
        return False
    if not info["crcOk"]:
        print("[%s] warn: CRC 校验失败" % base)
    if not out_dir:
        out_dir = os.path.dirname(os.path.abspath(__file__))
    os.makedirs(out_dir, exist_ok=True)
    out_path = os.path.join(out_dir, os.path.splitext(base)[0] + ".json")
    with open(out_path, "w", encoding="utf-8") as f:
        json.dump(info, f, ensure_ascii=False, indent=2)
    print("[%s] groupCfgName=%s ver=%s codeVer=%s chunkCount=%d files=%d tail=%dB crc=%s buildTime=%s -> %s"
          % (base, info["groupCfgName"], info.get("version"), info.get("codeVersion"),
             info["chunkCount"], info["groupFileInfoNum"], info["fileRecordTailBytes"],
             info["crc"], info.get("buildTime"), out_path))
    return True


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        print("usage: python3 decrypt_blc.py <file.blc> [more.blc ...]")
        print("       解包后 JSON 输出到本脚本所在目录")
        return 1
    out_dir = os.path.dirname(os.path.abspath(__file__))  # 固定在脚本目录
    ok = True
    for target in sys.argv[1:]:
        if os.path.isdir(target):
            blcs = sorted(glob.glob(os.path.join(target, "*.blc")))
            if not blcs:
                print("no .blc found in", target)
                ok = False
            for b in blcs:
                if not process_file(b, out_dir):
                    ok = False
        else:
            if not process_file(target, out_dir):
                ok = False
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())