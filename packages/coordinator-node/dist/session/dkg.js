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
exports.runDkg = runDkg;
const mpc = __importStar(require("@maany/mpc-node"));
async function runDkg(ctx, opts) {
    const dkgDevice = mpc.dkgNew(ctx, {
        role: 'device',
        keyId: opts.keyId ? Buffer.from(opts.keyId) : undefined,
        sessionId: opts.sessionId ? Buffer.from(opts.sessionId) : undefined,
    });
    const dkgServer = mpc.dkgNew(ctx, {
        role: 'server',
        keyId: opts.keyId ? Buffer.from(opts.keyId) : undefined,
        sessionId: opts.sessionId ? Buffer.from(opts.sessionId) : undefined,
    });
    async function step(participant, handle, inbound) {
        const res = await mpc.dkgStep(ctx, handle, inbound ?? undefined);
        if (res.outMsg) {
            await opts.transport.send({ participant: participant === 'device' ? 'server' : 'device', payload: res.outMsg });
        }
        return res.done;
    }
    let deviceDone = false;
    let serverDone = false;
    for (let i = 0; i < 128 && !(deviceDone && serverDone); ++i) {
        if (!deviceDone) {
            const inbound = await opts.transport.receive('device');
            deviceDone = await step('device', dkgDevice, inbound);
        }
        if (!serverDone) {
            const inbound = await opts.transport.receive('server');
            serverDone = await step('server', dkgServer, inbound);
        }
    }
    const deviceKeypair = mpc.dkgFinalize(ctx, dkgDevice);
    const serverKeypair = mpc.dkgFinalize(ctx, dkgServer);
    const deviceBlob = mpc.kpExport(ctx, deviceKeypair);
    const serverBlob = mpc.kpExport(ctx, serverKeypair);
    const keyId = Buffer.from(deviceBlob).toString('hex');
    await opts.storage.save({ keyId, blob: deviceBlob });
    await opts.storage.save({ keyId: `${keyId}:server`, blob: serverBlob });
    return { deviceKeypair, serverKeypair };
}
