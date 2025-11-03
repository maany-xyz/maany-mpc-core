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
      this.queues[target].push(toUint8Array(data));
    });

    socket.once('close', () => {
      delete this.sockets[participant];
    });
  }

  async send(message: TransportMessage): Promise<void> {
    const socket = this.sockets[message.participant];
    if (!socket || socket.readyState !== WebSocket.OPEN) {
      throw new Error(`Socket for ${message.participant} is not ready`);
    }
    socket.send(message.payload);
  }

  async receive(participant: Participant): Promise<Uint8Array | null> {
    const queue = this.queues[participant];
    if (queue.length === 0) return null;
    return queue.shift() ?? null;
  }
}
