"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.InMemoryShareStorage = void 0;
class InMemoryShareStorage {
    constructor() {
        this.state = new Map();
    }
    async save(record) {
        this.state.set(record.keyId, new Uint8Array(record.blob));
    }
    async load(keyId) {
        const blob = this.state.get(keyId);
        if (!blob)
            return null;
        return { keyId, blob: new Uint8Array(blob) };
    }
    async remove(keyId) {
        this.state.delete(keyId);
    }
}
exports.InMemoryShareStorage = InMemoryShareStorage;
