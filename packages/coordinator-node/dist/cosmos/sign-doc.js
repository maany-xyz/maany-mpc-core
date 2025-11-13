"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.makeSignBytes = makeSignBytes;
exports.sha256 = sha256;
const node_crypto_1 = require("node:crypto");
function makeSignBytes(doc) {
    return Buffer.concat([
        Buffer.from(doc.bodyBytes),
        Buffer.from(doc.authInfoBytes),
        Buffer.from(doc.chainId, 'utf8'),
        Buffer.from(doc.accountNumber),
        Buffer.from(doc.sequence),
    ]);
}
function sha256(data) {
    return (0, node_crypto_1.createHash)('sha256').update(data).digest();
}
