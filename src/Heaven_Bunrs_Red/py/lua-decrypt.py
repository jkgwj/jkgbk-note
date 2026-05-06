# -*- coding: utf-8 -*-
import os
import base64
from Crypto.Cipher import AES
from Crypto.Util.Padding import unpad

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
AES_KEY = bytes([81,103,105,88,50,97,105,33,65,35,110,98,103,58,73,111])
AES_IV  = bytes([119,124,81,113,74,48,65,82,117,77,84,37,115,85,112,114])

def main():
    path = input("路径: ").strip().strip('"')
    if not os.path.isfile(path):
        print("文件不存在")
        return
    
    with open(path, "rb") as f:
        raw = f.read()
    base64_str = raw.decode("ascii", errors="ignore").strip()

    # Base64解码
    try:
        decoded = base64.b64decode(base64_str, validate=False)
    except:
        print("Base64解码失败")
        return

    out_b64 = os.path.join(SCRIPT_DIR, "Base64解码后.bin")
    with open(out_b64, "wb") as f:
        f.write(decoded)
    print(f"已保存: {out_b64} | 解码后大小: {len(decoded)} 字节")

    # AES解密
    try:
        cipher = AES.new(AES_KEY, AES.MODE_CBC, AES_IV)
        decrypted = cipher.decrypt(decoded)
        final = unpad(decrypted, AES.block_size)
        
        out_aes = os.path.join(SCRIPT_DIR, "AES解密成功.lua")
        with open(out_aes, "wb") as f:
            f.write(final)
        print(f"已保存: {out_aes}")
        print("完成")
    except:
        print("AES解密失败，仅保留Base64解码文件")

if __name__ == "__main__":
    main()
    input("\n按回车退出")