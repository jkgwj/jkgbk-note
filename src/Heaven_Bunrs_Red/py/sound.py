import os
import hashlib
import json

def compute_sha1(file_path):
    """计算文件的SHA-1哈希"""
    hash_sha1 = hashlib.sha1()
    with open(file_path, "rb") as f:
        # 分块读取，避免大文件内存溢出
        for chunk in iter(lambda: f.read(4096), b""):
            hash_sha1.update(chunk)
    return hash_sha1.hexdigest()

def match_files(json_path, sound_dir):
    """根据JSON里的哈希匹配文件，重命名为原始名称"""
    with open(json_path, "r", encoding="utf-8") as f:
        sound_data = json.load(f)

    # 遍历所有acb/awb文件
    for root, _, files in os.walk(sound_dir):
        for file in files:
            if file.endswith((".acb", ".awb")):
                file_path = os.path.join(root, file)
                file_hash = compute_sha1(file_path)

                # 匹配JSON里的条目
                for entry in sound_data:
                    if (entry.get("acbFileHash") == file_hash or 
                        entry.get("awbFileHash") == file_hash):
                        # 获取原始文件名
                        original_name = (entry.get("acbAddress") or 
                                        entry.get("awbAddress")).split("/")[-1]
                        new_path = os.path.join(root, original_name)
                        os.rename(file_path, new_path)
                        print(f"✅ 匹配成功: {file} → {original_name}")
                        break

if __name__ == "__main__":
    # 替换成你的JSON文件路径和音频文件目录
    match_files("LilyLotusSoundKeyTable.json", "./Sound")