import base64
import sys
import os
from Crypto.Cipher import AES

KEY = b"giNArbHRlWBDIggF"
IV = b"jC34fOybW3zEh0Kl"

def encrypt(input_path, output_path):
    # 读取明文文件
    with open(input_path, 'rb') as f:
        plain_bytes = f.read()
    
    # 如果明文文件自带 BOM 头
    if plain_bytes.startswith(b'\xef\xbb\xbf'):
        plain_bytes = plain_bytes[3:]

    # 零填充到 16 字节倍数
    block_size = 16
    pad_len = (block_size - len(plain_bytes) % block_size) % block_size
    padded = plain_bytes + b'\x00' * pad_len

    # AES-CBC 加密
    cipher = AES.new(KEY, AES.MODE_CBC, IV)
    encrypted = cipher.encrypt(padded)

    # Base64 编码
    b64_encoded = base64.b64encode(encrypted).decode('ascii')

    # 游戏原文件有 UTF-8 BOM
    output_data = b'\xef\xbb\xbf' + b64_encoded.encode('utf-8')

    # 写入文件
    with open(output_path, 'wb') as f:
        f.write(output_data)

    print(f"加密完成，输出: {output_path}")

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("用法: encrypt.py 输入文件 [输出文件]")
        sys.exit()

    input_file = sys.argv[1]
    output_file = sys.argv[2] if len(sys.argv) > 2 else f"{os.path.splitext(input_file)[0]}_encrypted.dat"
    
    encrypt(input_file, output_file)