"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.makeSignBytes = makeSignBytes;
exports.sha256 = sha256;
const sha256_1 = require("@noble/hashes/sha256");
const bytes_1 = require("../utils/bytes");
function makeSignBytes(doc) {
    return (0, bytes_1.concatBytes)([
        (0, bytes_1.cloneBytes)(doc.bodyBytes),
        (0, bytes_1.cloneBytes)(doc.authInfoBytes),
        (0, bytes_1.fromUtf8)(doc.chainId),
        (0, bytes_1.fromUtf8)(doc.accountNumber),
        (0, bytes_1.fromUtf8)(doc.sequence),
    ]);
}
function sha256(data) {
    return (0, sha256_1.sha256)(data);
}
