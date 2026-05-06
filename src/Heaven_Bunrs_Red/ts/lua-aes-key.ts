import "frida-il2cpp-bridge";

Il2Cpp.perform(() => {
    // 定位Lua加密类
    const LuaEncryption = Il2Cpp.domain.assembly("LuaEncryption").image.class("LuaEncryption");

    // 获取AES偏移量(IV) 方法
    const getAESIVMethod = LuaEncryption.method("get_AES_IV");
    getAESIVMethod.implementation = function () {
        const result = getAESIVMethod.invoke();
        console.log("===获取AES偏移量(IV) 返回结果===:", result);
        return result;
    };

    // 获取AES密钥(Key) 方法
    const getAESKeyMethod = LuaEncryption.method("get_AES_Key");
    getAESKeyMethod.implementation = function () {
        const result = getAESKeyMethod.invoke();
        console.log("===获取AES密钥(Key) 返回结果===", result);
        return result;
    };

    // 解密密钥 方法
    const decodeKeyMethod = LuaEncryption.method("DecodeKey");
    decodeKeyMethod.implementation = function (saltBytes, scrambledBytes) {
        console.log("===解密密钥 -> 盐值参数===", saltBytes);
        console.log("===解密密钥 -> 混淆字节参数===", scrambledBytes);
        const result = decodeKeyMethod.invoke(saltBytes, scrambledBytes);
        console.log("===解密密钥 -> 最终返回结果===", result);
        return result;
    };
});