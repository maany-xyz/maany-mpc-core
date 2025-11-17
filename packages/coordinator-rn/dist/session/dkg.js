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
const mpc = __importStar(require("@maanyio/mpc-rn-bare"));
const bytes_1 = require("../utils/bytes");
async function runDkg(ctx, opts) {
    const runServerLocal = opts.mode !== 'device-only';
    const dkgOpts = {
        role: 'device',
        keyId: (0, bytes_1.optionalClone)(opts.keyId),
        sessionId: (0, bytes_1.optionalClone)(opts.sessionId),
    };
    const dkgDevice = mpc.dkgNew(ctx, dkgOpts);
    const dkgServer = runServerLocal ? mpc.dkgNew(ctx, { ...dkgOpts, role: 'server' }) : null;
    async function step(participant, handle, inbound) {
        const res = await mpc.dkgStep(ctx, handle, inbound ?? undefined);
        if (res.outMsg) {
            await opts.transport.send({ participant: participant === 'device' ? 'server' : 'device', payload: res.outMsg });
        }
        return res.done;
    }
    const waitForMessage = async (participant) => {
        while (true) {
            const message = await opts.transport.receive(participant);
            if (message) {
                return message;
            }
            await new Promise((resolve) => setTimeout(resolve, 5));
        }
    };
    if (runServerLocal && dkgServer) {
        let deviceDone = false;
        let serverDone = false;
        for (let i = 0; i < 128 && !(deviceDone && serverDone); ++i) {
            if (!deviceDone) {
                const inbound = await waitForMessage('device');
                deviceDone = await step('device', dkgDevice, inbound);
            }
            if (!serverDone) {
                const inbound = await waitForMessage('server');
                serverDone = await step('server', dkgServer, inbound);
            }
        }
    }
    else {
        let inbound = null;
        for (let i = 0; i < 512; ++i) {
            const done = await step('device', dkgDevice, inbound);
            inbound = null;
            if (done)
                break;
            inbound = await waitForMessage('device');
        }
    }
    const deviceKeypair = mpc.dkgFinalize(ctx, dkgDevice);
    let serverKeypair = null;
    if (runServerLocal && dkgServer) {
        serverKeypair = mpc.dkgFinalize(ctx, dkgServer);
    }
    if (opts.storage) {
        const deviceBlob = mpc.kpExport(ctx, deviceKeypair);
        const keyId = (0, bytes_1.toHex)(deviceBlob);
        await opts.storage.save({ keyId, blob: (0, bytes_1.cloneBytes)(deviceBlob) });
        if (serverKeypair) {
            const serverBlob = mpc.kpExport(ctx, serverKeypair);
            await opts.storage.save({ keyId: `${keyId}:server`, blob: (0, bytes_1.cloneBytes)(serverBlob) });
        }
    }
    return { deviceKeypair, serverKeypair };
}
