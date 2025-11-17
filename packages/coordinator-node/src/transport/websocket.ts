import { WebSocket, RawData } from 'ws';
import type { Participant, Transport, TransportMessage } from '../transport';

function normalizeBuffer(value: Buffer | ArrayBuffer): Buffer {
  return Buffer.isBuffer(value) ? value : Buffer.from(value);
}

function toUint8Array(data: RawData): Uint8Array {
  if (Array.isArray(data)) {
    const buffers = data.map((chunk) => normalizeBuffer(chunk as Buffer | ArrayBuffer));
    return new Uint8Array(Buffer.concat(buffers));
  }
  if (Buffer.isBuffer(data)) {
    return new Uint8Array(data);
  }
  return new Uint8Array(normalizeBuffer(data as ArrayBuffer));
}

export class WebSocketTransport implements Transport {
  private readonly queues: Record<Participant, Uint8Array[]> = {
    device: [],
    server: [],
  };

  private readonly sockets: Partial<Record<Participant, WebSocket>> = {};

  setSocket(participant: Participant, socket: WebSocket): void {
    this.sockets[participant] = socket;

    socket.on('message', (data) => {
      const target = participant === 'device' ? 'server' : 'device';
      const payload = toUint8Array(data);
      console.log(`[ws-transport] inbound from ${participant} -> enqueue for ${target} (${payload.length} bytes)`);
      this.queues[target].push(payload);
    });

    socket.once('close', () => {
      delete this.sockets[participant];
    });
  }

  async send(message: TransportMessage): Promise<void> {
    if (message.participant === 'server') {
      // Deliver to backend side by pushing onto the server queue directly.
      console.log(`[ws-transport] server enqueued ${message.payload.length} bytes`);
      this.queues.server.push(message.payload);
      return;
    }

    const socket = this.sockets.device;
    if (!socket || socket.readyState !== WebSocket.OPEN) {
      throw new Error('Socket for device is not ready');
    }
    console.log(`[ws-transport] sending ${message.payload.length} bytes to device over socket`);
    socket.send(message.payload);
  }

  async receive(participant: Participant): Promise<Uint8Array | null> {
    const queue = this.queues[participant];
    if (queue.length === 0) return null;
    const payload = queue.shift() ?? null;
    if (payload) {
      console.log(`[ws-transport] deliver ${payload.length} bytes to ${participant}`);
    }
    return payload;
  }
}
