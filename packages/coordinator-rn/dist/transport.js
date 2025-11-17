"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.InMemoryTransport = void 0;
class InMemoryTransport {
    constructor() {
        this.queues = {
            device: [],
            server: [],
        };
    }
    async send(message) {
        const target = message.participant === 'device' ? 'device' : 'server';
        this.queues[target].push(new Uint8Array(message.payload));
    }
    async receive(participant) {
        const queue = this.queues[participant];
        if (queue.length === 0)
            return null;
        const payload = queue.shift();
        return payload ? new Uint8Array(payload) : null;
    }
}
exports.InMemoryTransport = InMemoryTransport;
