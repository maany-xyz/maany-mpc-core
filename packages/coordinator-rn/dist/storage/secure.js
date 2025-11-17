"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.SecureShareStorage = void 0;
const react_native_1 = require("react-native");
const bytes_1 = require("../utils/bytes");
const MODULE_NAME = 'MaanyMpcSecureStorage';
function getNativeModule() {
    const native = react_native_1.NativeModules[MODULE_NAME];
    if (!native) {
        throw new Error(`${MODULE_NAME} native module not available. Ensure the coordinator package is linked.`);
    }
    return native;
}
class SecureShareStorage {
    constructor(options = {}) {
        if (react_native_1.Platform.OS !== 'ios' && react_native_1.Platform.OS !== 'android') {
            throw new Error('SecureShareStorage is only supported on iOS and Android');
        }
        this.native = getNativeModule();
        this.promptMessage = options.promptMessage;
    }
    async save(record) {
        const base64 = (0, bytes_1.toBase64)((0, bytes_1.cloneBytes)(record.blob));
        await this.native.saveShare(record.keyId, base64);
    }
    async load(keyId) {
        const base64 = await this.native.loadShare(keyId, this.promptMessage);
        if (!base64) {
            return null;
        }
        return { keyId, blob: (0, bytes_1.fromBase64)(base64) };
    }
    async remove(keyId) {
        await this.native.removeShare(keyId);
    }
}
exports.SecureShareStorage = SecureShareStorage;
