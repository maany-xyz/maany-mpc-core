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
exports.CoordinatorServer = void 0;
const node_events_1 = require("node:events");
const ws_1 = require("ws");
const coordinator_1 = require("./session/coordinator");
const websocket_1 = require("./transport/websocket");
const mpc = __importStar(require("@maany/mpc-node"));
const DEFAULT_INTENT = { kind: 'dkg' };
class CoordinatorServer extends node_events_1.EventEmitter {
    constructor(options) {
        super();
        this.sessions = new Map();
        this.storage = options.storage;
        this.wss = new ws_1.WebSocketServer({ port: options.port });
        this.wss.on('connection', (socket) => this.handleConnection(socket));
        if (options.onSessionReady) {
            this.on('session:ready', options.onSessionReady);
        }
    }
    handleConnection(socket) {
        const onMessage = (raw) => {
            let hello;
            try {
                const buffer = Buffer.isBuffer(raw) ? raw : Buffer.from(raw);
                hello = JSON.parse(buffer.toString('utf8'));
            }
            catch (err) {
                socket.close(1002, 'invalid handshake payload');
                return;
            }
            if (hello.type !== 'hello' || !hello.sessionId || hello.role !== 'device') {
                socket.close(1002, 'invalid handshake contents');
                return;
            }
            socket.off('message', onMessage);
            this.registerParticipant(hello.sessionId, socket, hello);
        };
        socket.on('message', onMessage);
        socket.once('close', () => socket.off('message', onMessage));
    }
    registerParticipant(sessionId, socket, hello) {
        let state = this.sessions.get(sessionId);
        if (!state) {
            const coordinator = (0, coordinator_1.createCoordinator)({
                transport: new websocket_1.WebSocketTransport(),
                storage: this.storage,
            });
            state = {
                coordinator,
                transport: coordinator.options.transport,
                ctx: coordinator.initContext(),
                intent: this.mapIntent(hello),
                token: hello.token,
            };
            this.sessions.set(sessionId, state);
        }
        state.transport.setSocket('device', socket);
        state.socket = socket;
        state.intent = this.mapIntent(hello);
        state.token = hello.token;
        socket.once('close', () => {
            this.cleanupSession(sessionId);
        });
        this.emit('session:ready', {
            sessionId,
            token: state.token,
            intent: state.intent,
            coordinator: state.coordinator,
            ctx: state.ctx,
            transport: state.transport,
            close: () => this.cleanupSession(sessionId),
        });
    }
    mapIntent(hello) {
        switch (hello.intent) {
            case 'sign':
                if (!hello.keyId)
                    throw new Error('sign intent missing keyId');
                return { kind: 'sign', keyId: hello.keyId, sessionIdHint: hello.sessionIdHint };
            case 'refresh':
                if (!hello.keyId)
                    throw new Error('refresh intent missing keyId');
                return { kind: 'refresh', keyId: hello.keyId, sessionIdHint: hello.sessionIdHint };
            case 'dkg':
            case undefined:
            default:
                return { kind: 'dkg', keyId: hello.keyId, sessionIdHint: hello.sessionIdHint };
        }
    }
    cleanupSession(sessionId) {
        const state = this.sessions.get(sessionId);
        if (!state)
            return;
        if (state.socket && state.socket.readyState === ws_1.WebSocket.OPEN) {
            state.socket.close();
        }
        mpc.shutdown(state.ctx);
        this.sessions.delete(sessionId);
    }
    close() {
        return new Promise((resolve) => {
            this.wss.close(() => {
                this.sessions.forEach((_, id) => this.cleanupSession(id));
                resolve();
            });
        });
    }
}
exports.CoordinatorServer = CoordinatorServer;
