import type { Participant, Transport, TransportMessage } from '../transport';
import { fromUtf8 } from '../utils/bytes';

type Listener = (event: any) => void;

type WebSocketLike = {
  readyState: number;
  binaryType?: string;
  send(data: ArrayBuffer | Uint8Array | string): void;
  close(code?: number, reason?: string): void;
  addEventListener?: (type: string, listener: Listener) => void;
  removeEventListener?: (type: string, listener: Listener) => void;
  onmessage?: (event: { data: unknown }) => void;
  onclose?: (event: unknown) => void;
};

type EventSource = {
  addEventListener?: (type: string, listener: Listener) => void;
  removeEventListener?: (type: string, listener: Listener) => void;
  onmessage?: Listener;
  onclose?: Listener;
};

interface BoundSocket {
  socket: WebSocketLike;
  onMessage: Listener;
  onClose: Listener;
  originalOnMessage?: Listener;
  originalOnClose?: Listener;
}

const READY_STATE_OPEN = 1;

function getWebSocketConstructor(): (new (url: string, protocols?: string | string[]) => WebSocketLike) {
  const ctor = (globalThis as unknown as { WebSocket?: new (url: string, protocols?: string | string[]) => WebSocketLike })
    .WebSocket;
  if (!ctor) {
    throw new Error('WebSocket constructor is not available in this environment');
  }
  return ctor;
}

function toUint8Array(data: unknown): Uint8Array {
  if (data instanceof Uint8Array) {
    return data;
  }
  if (data instanceof ArrayBuffer) {
    return new Uint8Array(data);
  }
  if (typeof data === 'string') {
    return fromUtf8(data);
  }
  if (Array.isArray(data)) {
    return new Uint8Array(data as number[]);
  }
  if (data && typeof data === 'object' && 'data' in (data as Record<string, unknown>)) {
    const value = (data as Record<string, unknown>).data;
    return toUint8Array(value);
  }
  return new Uint8Array(0);
}

function toArrayBuffer(view: Uint8Array): ArrayBuffer {
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

export interface WebSocketConnectOptions {
  url: string;
  participant?: Participant;
  protocols?: string | string[];
}

export class WebSocketTransport implements Transport {
  private readonly queues: Record<Participant, Uint8Array[]> = {
    device: [],
    server: [],
  };

  private readonly sockets: Partial<Record<Participant, BoundSocket>> = {};

  /**
   * Establishes a WebSocket connection for the given participant and attaches the
   * transport listeners. Resolves once the socket reaches OPEN state.
   */
  async connect(options: WebSocketConnectOptions): Promise<void> {
    const participant = options.participant ?? 'device';
    const WebSocketCtor = getWebSocketConstructor();
    const socket = options.protocols
      ? new WebSocketCtor(options.url, options.protocols)
      : new WebSocketCtor(options.url);
    if (typeof socket.binaryType !== 'undefined') {
      socket.binaryType = 'arraybuffer';
    }

    await new Promise<void>((resolve, reject) => {
      let cleanup: () => void;

      const handleOpen: Listener = () => {
        cleanup();
        resolve();
      };
      const handleError: Listener = (event) => {
        cleanup();
        const message = (event && typeof event === 'object' && 'message' in event)
          ? String((event as { message?: unknown }).message)
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
  attach(participant: Participant, socket: WebSocketLike): void {
    this.detach(participant);

    const onMessage: Listener = (event) => {
      const target: Participant = participant === 'device' ? 'server' : 'device';
      const payload = 'data' in event ? toUint8Array((event as { data: unknown }).data) : toUint8Array(event);
      this.queues[target].push(payload);
    };

  const onClose: Listener = () => {
    this.detach(participant);
  };

  attachListener(socket, 'message', onMessage);
  attachListener(socket, 'close', onClose);

  const bound: BoundSocket = {
    socket,
    onMessage,
    onClose,
      originalOnMessage: (socket as EventSource).onmessage ?? undefined,
      originalOnClose: (socket as EventSource).onclose ?? undefined,
  };

  // Ensure we don't overwrite existing handlers unexpectedly
    (socket as EventSource).onmessage = undefined;
    (socket as EventSource).onclose = undefined;

  this.sockets[participant] = bound;
}

  async send(message: TransportMessage): Promise<void> {
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

  async receive(participant: Participant): Promise<Uint8Array | null> {
    const queue = this.queues[participant];
    if (queue.length === 0) return null;
    return queue.shift() ?? null;
  }

  detach(participant: Participant): void {
    const bound = this.sockets[participant];
    if (!bound) return;

    detachListener(bound.socket, 'message', bound.onMessage);
    detachListener(bound.socket, 'close', bound.onClose);

    if (typeof bound.originalOnMessage !== 'undefined') {
      (bound.socket as EventSource).onmessage = bound.originalOnMessage;
    }
    if (typeof bound.originalOnClose !== 'undefined') {
      (bound.socket as EventSource).onclose = bound.originalOnClose;
    }

    delete this.sockets[participant];
  }

  close(participant?: Participant): void {
    if (participant) {
      const bound = this.sockets[participant];
      if (bound) {
        bound.socket.close();
      }
      return;
    }
    (Object.keys(this.sockets) as Participant[]).forEach((key) => {
      const bound = this.sockets[key];
      if (bound) bound.socket.close();
    });
  }
}

function attachListener(target: EventSource, type: string, listener: Listener) {
  if (typeof target.addEventListener === 'function') {
    target.addEventListener(type, listener);
  } else if (type === 'message') {
    target.onmessage = listener;
  } else if (type === 'close') {
    target.onclose = listener as ((event: unknown) => void);
  } else if (type === 'open') {
    (target as unknown as { onopen?: Listener }).onopen = listener;
  } else if (type === 'error') {
    (target as unknown as { onerror?: Listener }).onerror = listener;
  }
}

function detachListener(target: EventSource, type: string, listener: Listener) {
  if (typeof target.removeEventListener === 'function') {
    target.removeEventListener(type, listener);
  } else if (type === 'message' && target.onmessage === listener) {
    target.onmessage = undefined;
  } else if (type === 'close' && target.onclose === listener) {
    target.onclose = undefined;
  } else if (type === 'open' && (target as unknown as { onopen?: Listener }).onopen === listener) {
    (target as unknown as { onopen?: Listener }).onopen = undefined;
  } else if (type === 'error' && (target as unknown as { onerror?: Listener }).onerror === listener) {
    (target as unknown as { onerror?: Listener }).onerror = undefined;
  }
}
