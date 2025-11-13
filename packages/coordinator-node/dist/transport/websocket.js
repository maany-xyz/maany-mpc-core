"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.WebSocketTransport = void 0;
const ws_1 = require("ws");
function normalizeBuffer(value) {
    return Buffer.isBuffer(value) ? value : Buffer.from(value);
}
function toUint8Array(data) {
    if (Array.isArray(data)) {
        const buffers = data.map((chunk) => normalizeBuffer(chunk));
        return new Uint8Array(Buffer.concat(buffers));
    }
    if (Buffer.isBuffer(data)) {
        return new Uint8Array(data);
    }
    return new Uint8Array(normalizeBuffer(data));
}
class WebSocketTransport {
    constructor() {
        this.queues = {
            device: [],
            server: [],
        };
        this.sockets = {};
    }
    setSocket(participant, socket) {
        this.sockets[participant] = socket;
        socket.on('message', (data) => {
            const target = participant === 'device' ? 'server' : 'device';
            this.queues[target].push(toUint8Array(data));
        });
        socket.once('close', () => {
            delete this.sockets[participant];
        });
    }
    async send(message) {
        const socket = this.sockets[message.participant];
        if (!socket || socket.readyState !== ws_1.WebSocket.OPEN) {
            throw new Error(`Socket for ${message.participant} is not ready`);
        }
        socket.send(message.payload);
    }
    async receive(participant) {
        const queue = this.queues[participant];
        if (queue.length === 0)
            return null;
        return queue.shift() ?? null;
    }
}
exports.WebSocketTransport = WebSocketTransport;
