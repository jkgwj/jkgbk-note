import base64
import sys
import os
from Crypto.Cipher import AES

KEY = b"giNArbHRlWBDIggF"
IV = b"jC34fOybW3zEh0Kl"

def decrypt(input_path, output_path):
    # 二进制读取文件
    with open(input_path, 'rb') as f:
        data = f.read()
    
    # 去除UTF8 BOM 
    if data.startswith(b'\xef\xbb\xbf'):
        data = data[3:]
    data = data.strip()
    
    # Base64解码 + AES解密
    encrypted = base64.b64decode(data)
    cipher = AES.new(KEY, AES.MODE_CBC, IV)
    decrypted = cipher.decrypt(encrypted).rstrip(b'\0')
    
    # 写入结果
    with open(output_path, 'wb') as f:
        f.write(decrypted)
    
    print("完成")

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("用法: decrypt.py 输入文件 [输出文件]")
        sys.exit()

    input_file = sys.argv[1]
    output_file = sys.argv[2] if len(sys.argv) > 2 else f"{os.path.splitext(input_file)[0]}_decrypted.json"
    
    decrypt(input_file, output_file)