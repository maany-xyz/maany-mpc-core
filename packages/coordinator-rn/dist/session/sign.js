"use strict";
var __createBinding = (this && this.__createBinding) || (Object.create ? (function(o, m, k, k2) {
    if (k2 === undefined) k2 = k;
    var desc = Object.getOwnPropertyDescriptor(m, k);
    if (!desc || ("get" in desc ? !m.__esModule : desc.writable || desc.configurable)) {
      desc = { enumerable: true, get: function() { return m[k]; } };
    }
    Object.defineProperty(o, k2, desc);
}) : (function(o, m, k, k2) {
    if (k2 === undefined) k2 = k;
    o[k2] = m[k];
}));
var __setModuleDefault = (this && this.__setModuleDefault) || (Object.create ? (function(o, v) {
    Object.defineProperty(o, "default", { enumerable: true, value: v });
}) : function(o, v) {
    o["default"] = v;
});
var __importStar = (this && this.__importStar) || (function () {
    var ownKeys = function(o) {
        ownKeys = Object.getOwnPropertyNames || function (o) {
            var ar = [];
            for (var k in o) if (Object.prototype.hasOwnProperty.call(o, k)) ar[ar.length] = k;
            return ar;
        };
        return ownKeys(o);
    };
    return function (mod) {
        if (mod && mod.__esModule) return mod;
        var result = {};
        if (mod != null) for (var k = ownKeys(mod), i = 0; i < k.length; i++) if (k[i] !== "default") __createBinding(result, mod, k[i]);
        __setModuleDefault(result, mod);
        return result;
    };
})();
Object.defineProperty(exports, "__esModule", { value: true });
exports.runSign = runSign;
const mpc = __importStar(require("@maanyio/mpc-rn-bare"));
const bytes_1 = require("../utils/bytes");
async function runSign(ctx, device, server, opts) {
    const commonOpts = {};
    const sessionId = (0, bytes_1.optionalClone)(opts.sessionId);
    const extraAad = (0, bytes_1.optionalClone)(opts.extraAad);
    if (sessionId)
        commonOpts.sessionId = sessionId;
    if (extraAad)
        commonOpts.extraAad = extraAad;
    const signDevice = mpc.signNew(ctx, device, commonOpts);
    const signServer = mpc.signNew(ctx, server, commonOpts);
    const message = (0, bytes_1.cloneBytes)(opts.message);
    mpc.signSetMessage(ctx, signDevice, message);
    mpc.signSetMessage(ctx, signServer, message);
    let deviceDone = false;
    let serverDone = false;
    for (let i = 0; i < 128 && !(deviceDone && serverDone); ++i) {
        if (!serverDone) {
            const inbound = await opts.transport.receive('server');
            const res = await mpc.signStep(ctx, signServer, inbound ?? undefined);
            if (res.outMsg)
                await opts.transport.send({ participant: 'device', payload: res.outMsg });
            serverDone = res.done;
        }
        if (!deviceDone) {
            const inbound = await opts.transport.receive('device');
            const res = await mpc.signStep(ctx, signDevice, inbound ?? undefined);
            if (res.outMsg)
                await opts.transport.send({ participant: 'server', payload: res.outMsg });
            deviceDone = res.done;
        }
    }
    const format = opts.format ?? 'der';
    const signature = mpc.signFinalize(ctx, signDevice, format);
    mpc.signFree(signDevice);
    mpc.signFree(signServer);
    return signature;
}
