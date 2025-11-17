"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.WebSocketTransport = void 0;
const bytes_1 = require("../utils/bytes");
const READY_STATE_OPEN = 1;
function getWebSocketConstructor() {
    const ctor = globalThis
        .WebSocket;
    if (!ctor) {
        throw new Error('WebSocket constructor is not available in this environment');
    }
    return ctor;
}
function toUint8Array(data) {
    if (data instanceof Uint8Array) {
        return data;
    }
    if (data instanceof ArrayBuffer) {
        return new Uint8Array(data);
    }
    if (typeof data === 'string') {
        return (0, bytes_1.fromUtf8)(data);
    }
    if (Array.isArray(data)) {
        return new Uint8Array(data);
    }
    if (data && typeof data === 'object' && 'data' in data) {
        const value = data.data;
        return toUint8Array(value);
    }
    return new Uint8Array(0);
}
function toArrayBuffer(view) {
    if (view.byteLength === 0) {
        return new ArrayBuffer(0);
    }
    const { buffer, byteOffset, byteLength } = view;
    if (buffer instanceof ArrayBuffer && byteOffset === 0 && byteLength === buffer.byteLength) {
        return buffer;
    }
    const copy = view.slice();
    return copy.buffer;
}
class WebSocketTransport {
    constructor() {
        this.queues = {
            device: [],
            server: [],
        };
        this.sockets = {};
    }
    /**
     * Establishes a WebSocket connection for the given participant and attaches the
     * transport listeners. Resolves once the socket reaches OPEN state.
     */
    async connect(options) {
        const participant = options.participant ?? 'device';
        const WebSocketCtor = getWebSocketConstructor();
        const socket = options.protocols
            ? new WebSocketCtor(options.url, options.protocols)
            : new WebSocketCtor(options.url);
        if (typeof socket.binaryType !== 'undefined') {
            socket.binaryType = 'arraybuffer';
        }
        await new Promise((resolve, reject) => {
            let cleanup;
            const handleOpen = () => {
                cleanup();
                resolve();
            };
            const handleError = (event) => {
                cleanup();
                const message = (event && typeof event === 'object' && 'message' in event)
                    ? String(event.message)
                    : 'WebSocket connection failed';
                reject(new Error(message));
            };
            cleanup = () => {
                detachListener(socket, 'open', handleOpen);
                detachListener(socket, 'error', handleError);
            };
            attachListener(socket, 'open', handleOpen);
            attachListener(socket, 'error', handleError);
        });
        this.attach(participant, socket);
    }
    /**
     * Attaches an existing WebSocket instance to the transport for the provided participant.
     */
    attach(participant, socket) {
        this.detach(participant);
        const onMessage = (event) => {
            const target = participant === 'device' ? 'server' : 'device';
            const payload = 'data' in event ? toUint8Array(event.data) : toUint8Array(event);
            this.queues[target].push(payload);
        };
        const onClose = () => {
            this.detach(participant);
        };
        attachListener(socket, 'message', onMessage);
        attachListener(socket, 'close', onClose);
        const bound = {
            socket,
            onMessage,
            onClose,
            originalOnMessage: socket.onmessage ?? undefined,
            originalOnClose: socket.onclose ?? undefined,
        };
        // Ensure we don't overwrite existing handlers unexpectedly
        socket.onmessage = undefined;
        socket.onclose = undefined;
        this.sockets[participant] = bound;
    }
    async send(message) {
        const bound = this.sockets[message.participant];
        if (!bound) {
            throw new Error(`No WebSocket attached for participant ${message.participant}`);
        }
        if (bound.socket.readyState !== READY_STATE_OPEN) {
            throw new Error(`Socket for ${message.participant} is not open`);
        }
        const data = toArrayBuffer(message.payload);
        bound.socket.send(data);
    }
    async receive(participant) {
        const queue = this.queues[participant];
        if (queue.length === 0)
            return null;
        return queue.shift() ?? null;
    }
    detach(participant) {
        const bound = this.sockets[participant];
        if (!bound)
            return;
        detachListener(bound.socket, 'message', bound.onMessage);
        detachListener(bound.socket, 'close', bound.onClose);
        if (typeof bound.originalOnMessage !== 'undefined') {
            bound.socket.onmessage = bound.originalOnMessage;
        }
        if (typeof bound.originalOnClose !== 'undefined') {
            bound.socket.onclose = bound.originalOnClose;
        }
        delete this.sockets[participant];
    }
    close(participant) {
        if (participant) {
            const bound = this.sockets[participant];
            if (bound) {
                bound.socket.close();
            }
            return;
        }
        Object.keys(this.sockets).forEach((key) => {
            const bound = this.sockets[key];
            if (bound)
                bound.socket.close();
        });
    }
}
exports.WebSocketTransport = WebSocketTransport;
function attachListener(target, type, listener) {
    if (typeof target.addEventListener === 'function') {
        target.addEventListener(type, listener);
    }
    else if (type === 'message') {
        target.onmessage = listener;
    }
    else if (type === 'close') {
        target.onclose = listener;
    }
    else if (type === 'open') {
        target.onopen = listener;
    }
    else if (type === 'error') {
        target.onerror = listener;
    }
}
function detachListener(target, type, listener) {
    if (typeof target.removeEventListener === 'function') {
        target.removeEventListener(type, listener);
    }
    else if (type === 'message' && target.onmessage === listener) {
        target.onmessage = undefined;
    }
    else if (type === 'close' && target.onclose === listener) {
        target.onclose = undefined;
    }
    else if (type === 'open' && target.onopen === listener) {
        target.onopen = undefined;
    }
    else if (type === 'error' && target.onerror === listener) {
        target.onerror = undefined;
    }
}
