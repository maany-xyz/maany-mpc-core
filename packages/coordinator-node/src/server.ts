import { EventEmitter } from 'node:events';
import { WebSocketServer, WebSocket, RawData } from 'ws';
import type { Coordinator } from './session/coordinator';
import { createCoordinator } from './session/coordinator';
import { WebSocketTransport } from './transport/websocket';
import type { Participant } from './transport';
import type { ShareStorage } from './storage';
import * as mpc from '@maany/mpc-node';

export interface CoordinatorServerOptions {
  port: number;
  storage: ShareStorage;
  onSessionReady?: (session: SessionReadyEvent) => void;
}

type SessionIntent =
  | { kind: 'dkg'; keyId?: string; sessionIdHint?: string }
  | { kind: 'sign'; keyId: string; sessionIdHint?: string }
  | { kind: 'refresh'; keyId: string; sessionIdHint?: string };

interface HelloPayload {
  type: 'hello';
  sessionId: string;
  role: Participant;
  token?: string;
  intent?: SessionIntent['kind'];
  keyId?: string;
  sessionIdHint?: string;
}

interface SessionState {
  transport: WebSocketTransport;
  coordinator: Coordinator;
  ctx: mpc.Ctx;
  socket?: WebSocket;
  intent: SessionIntent;
  token?: string;
}

export interface SessionReadyEvent {
  sessionId: string;
  token?: string;
  intent: SessionIntent;
  coordinator: Coordinator;
  ctx: mpc.Ctx;
  transport: WebSocketTransport;
  close: () => void;
}

const DEFAULT_INTENT: SessionIntent = { kind: 'dkg' };

export declare interface CoordinatorServer {
  on(event: 'session:ready', listener: (session: SessionReadyEvent) => void): this;
  emit(event: 'session:ready', session: SessionReadyEvent): boolean;
}

export class CoordinatorServer extends EventEmitter {
  private readonly wss: WebSocketServer;
  private readonly storage: ShareStorage;
  private readonly sessions = new Map<string, SessionState>();

  constructor(options: CoordinatorServerOptions) {
    super();
    this.storage = options.storage;
    this.wss = new WebSocketServer({ port: options.port });
    this.wss.on('connection', (socket) => this.handleConnection(socket));
    if (options.onSessionReady) {
      this.on('session:ready', options.onSessionReady);
    }
  }

  private handleConnection(socket: WebSocket) {
    const onMessage = (raw: RawData) => {
      let hello: HelloPayload;
      try {
        const buffer = Buffer.isBuffer(raw) ? raw : Buffer.from(raw as ArrayBuffer);
        hello = JSON.parse(buffer.toString('utf8'));
      } catch (err) {
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

  private registerParticipant(sessionId: string, socket: WebSocket, hello: HelloPayload) {
    let state = this.sessions.get(sessionId);
    if (!state) {
      const coordinator = createCoordinator({
        transport: new WebSocketTransport(),
        storage: this.storage,
      });
      state = {
        coordinator,
        transport: coordinator.options.transport as WebSocketTransport,
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

  private mapIntent(hello: HelloPayload): SessionIntent {
    switch (hello.intent) {
      case 'sign':
        if (!hello.keyId) throw new Error('sign intent missing keyId');
        return { kind: 'sign', keyId: hello.keyId, sessionIdHint: hello.sessionIdHint };
      case 'refresh':
        if (!hello.keyId) throw new Error('refresh intent missing keyId');
        return { kind: 'refresh', keyId: hello.keyId, sessionIdHint: hello.sessionIdHint };
      case 'dkg':
      case undefined:
      default:
        return { kind: 'dkg', keyId: hello.keyId, sessionIdHint: hello.sessionIdHint };
    }
  }

  private cleanupSession(sessionId: string) {
    const state = this.sessions.get(sessionId);
    if (!state) return;

    if (state.socket && state.socket.readyState === WebSocket.OPEN) {
      state.socket.close();
    }

    mpc.shutdown(state.ctx);
    this.sessions.delete(sessionId);
  }

  close(): Promise<void> {
    return new Promise((resolve) => {
      this.wss.close(() => {
        this.sessions.forEach((_, id) => this.cleanupSession(id));
        resolve();
      });
    });
  }
}
