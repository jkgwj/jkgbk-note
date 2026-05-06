import "frida-il2cpp-bridge";

Il2Cpp.perform(() => {

    const EncryptedJsonDataProvider = Il2Cpp.domain
        .assembly("Unity.ResourceManager") 
        .image.class("UnityEngine.ResourceManagement.ResourceProviders.EncryptedJsonDataProvider");


    
    //  获取AES偏移量(IV) 方法
    const getAESIVMethod = EncryptedJsonDataProvider.method("get_AES_IV");
    getAESIVMethod.implementation = function () {
        const result = getAESIVMethod.invoke();
        console.log("===获取AES偏移量(IV) 返回结果===:", result);
        return result;
    };

    //  获取AES密钥(Key) 方法
    const getAESKeyMethod = EncryptedJsonDataProvider.method("get_AES_Key");
    getAESKeyMethod.implementation = function () {
        const result = getAESKeyMethod.invoke();
        console.log("===获取AES密钥(Key) 返回结果===", result);
        return result;
    };

    //  密钥解码方法（盐值 + 混淆字节）
    const decodeKeyMethod = EncryptedJsonDataProvider.method("DecodeKey");
    decodeKeyMethod.implementation = function (saltBytes, scrambledBytes) {
        console.log("===解密密钥 -> 盐值参数===", saltBytes);
        console.log("===解密密钥 -> 混淆字节参数===", scrambledBytes);
        const result = decodeKeyMethod.invoke(saltBytes, scrambledBytes);
        console.log("===解密密钥 -> 最终返回结果===", result);
        return result;
    };

    console.log("已成功注入 Addressables 官方加密类 EncryptedJsonDataProvider");
});