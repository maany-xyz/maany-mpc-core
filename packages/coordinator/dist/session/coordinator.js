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
exports.createCoordinator = createCoordinator;
const mpc = __importStar(require("@maany/mpc-node"));
const dkg_1 = require("./dkg");
const sign_1 = require("./sign");
function createCoordinator(options) {
    return {
        options,
        initContext() {
            return mpc.init();
        },
        runDkg(ctx, extraOpts = {}) {
            return (0, dkg_1.runDkg)(ctx, {
                transport: options.transport,
                storage: options.storage,
                keyId: extraOpts.keyId ? Buffer.from(extraOpts.keyId) : undefined,
                sessionId: extraOpts.sessionId ? Buffer.from(extraOpts.sessionId) : undefined,
            });
        },
        runSign(ctx, device, server, signOpts) {
            return (0, sign_1.runSign)(ctx, device, server, {
                transport: options.transport,
                ...signOpts,
            });
        },
    };
}
