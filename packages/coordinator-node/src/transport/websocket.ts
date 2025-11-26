import { WebSocket, RawData } from 'ws';
import type { Participant, Transport, TransportMessage } from '../transport';

type ControlHandler = (message: {
  participant: Participant;
  data: RawData;
  socket: WebSocket;
}) => boolean | Promise<boolean>;

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
  private readonly controlHandler?: ControlHandler;

  constructor(controlHandler?: ControlHandler) {
    this.controlHandler = controlHandler;
  }

  setSocket(participant: Participant, socket: WebSocket): void {
    this.sockets[participant] = socket;

    socket.on('message', async (data) => {
      if (this.controlHandler) {
        try {
          const handled = await this.controlHandler({ participant, data, socket });
          if (handled) {
            return;
          }
        } catch (err) {
          console.warn('[ws-transport] control handler error:', err);
        }
      }
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
    const socket = this.sockets[message.participant];
    if (socket && socket.readyState === WebSocket.OPEN) {
      console.log(
        `[ws-transport] sending ${message.payload.length} bytes to ${message.participant} over socket`
      );
      socket.send(message.payload);
      return;
    }

    console.log(
      `[ws-transport] enqueued ${message.payload.length} bytes for ${message.participant} (no socket)`
    );
    this.queues[message.participant].push(message.payload);
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
