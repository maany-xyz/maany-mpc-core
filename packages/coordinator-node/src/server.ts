import { EventEmitter } from 'node:events';
import { WebSocketServer, WebSocket, RawData } from 'ws';
import type { Coordinator } from './session/coordinator';
import { createCoordinator } from './session/coordinator';
import { WebSocketTransport } from './transport/websocket';
import type { Participant } from './transport';
import type { CoordinatorStorage } from './storage';
import type { KeyEncryptor } from './crypto/key-encryptor';
import * as mpc from '@maany/mpc-node';

export interface CoordinatorServerOptions {
  port: number;
  storage: CoordinatorStorage;
  encryptor: KeyEncryptor;
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
  serverSocket?: WebSocket;
  intent: SessionIntent;
  token?: string;
}

interface BackupSharePayload {
  type: 'backup-share';
  keyId?: string;
  walletId?: string;
  ciphertext: BackupShareCiphertextPayload;
  fragment?: string;
  share?: string;
  fragmentEncoding?: 'hex' | 'base64';
}

interface BackupShareCiphertextPayload {
  kind: string;
  curve: string;
  scheme: string;
  keyId: string;
  threshold: number;
  shareCount: number;
  label: string;
  blob: string;
  labelEncoding?: 'hex' | 'base64';
  blobEncoding?: 'hex' | 'base64';
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
  private readonly storage: CoordinatorStorage;
  private readonly encryptor: KeyEncryptor;
  private readonly sessions = new Map<string, SessionState>();

  constructor(options: CoordinatorServerOptions) {
    super();
    this.storage = options.storage;
    this.encryptor = options.encryptor;
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
      const transport = new WebSocketTransport((event) =>
        this.handleControlMessage(sessionId, event)
      );
      const coordinator = createCoordinator({
        transport,
        storage: this.storage,
        encryptor: this.encryptor,
      });
      state = {
        coordinator,
        transport,
        ctx: coordinator.initContext(),
        intent: this.mapIntent(hello),
        token: hello.token,
      };
      this.sessions.set(sessionId, state);
    }

    if (hello.role === 'device') {
      state.transport.setSocket('device', socket);
      state.socket = socket;
    } else {
      state.transport.setSocket('server', socket);
      state.serverSocket = socket;
    }
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

  private async handleControlMessage(
    sessionId: string,
    event: { participant: Participant; data: RawData; socket: WebSocket }
  ): Promise<boolean> {
    if (event.participant !== 'device') {
      return false;
    }

    const text = this.extractText(event.data);
    if (!text) return false;

    let payload: BackupSharePayload | undefined;
    try {
      payload = JSON.parse(text);
    } catch {
      return false;
    }
    if (!payload || payload.type !== 'backup-share') {
      return false;
    }

    try {
      await this.processBackupShare(sessionId, payload);
      await this.sendBackupAck(event.socket, payload, { status: 'ok' });
    } catch (error) {
      console.error('[backup-share] failed:', error);
      await this.sendBackupAck(event.socket, payload, {
        status: 'error',
        message: error instanceof Error ? error.message : 'unknown error',
      });
    }

    return true;
  }

  private extractText(data: RawData): string | null {
    if (typeof data === 'string') return data;
    if (Buffer.isBuffer(data)) return data.toString('utf8');
    return null;
  }

  private async processBackupShare(sessionId: string, payload: BackupSharePayload): Promise<void> {
    const state = this.sessions.get(sessionId);
    if (!state) throw new Error('session not found');

    const walletId = (payload.walletId ?? payload.keyId ?? state.intent.keyId)?.trim();
    if (!walletId) {
      throw new Error('backup-share missing wallet identifier');
    }

    const fragmentSource = payload.fragment ?? payload.share;
    if (!fragmentSource) {
      throw new Error('backup-share missing fragment payload');
    }

    const ciphertextPayload = payload.ciphertext;
    if (!ciphertextPayload) {
      throw new Error('backup-share missing ciphertext');
    }

    const fragment = decodeBinary(fragmentSource, payload.fragmentEncoding, 'fragment');
    const ciphertext = parseCiphertext(ciphertextPayload);
    const encryptedFragment = await this.encryptor.encryptShare(new Uint8Array(fragment));

    await this.storage.upsertWalletBackup({
      walletId,
      ciphertextKind: ciphertext.kind,
      ciphertextCurve: ciphertext.curve,
      ciphertextScheme: ciphertext.scheme,
      ciphertextKeyId: ciphertext.keyId,
      ciphertextThreshold: ciphertext.threshold,
      ciphertextShareCount: ciphertext.shareCount,
      ciphertextLabel: ciphertext.label.toString('base64'),
      ciphertextBlob: ciphertext.blob.toString('base64'),
      encryptedCoordinatorFragment: encryptedFragment,
    });

    console.log(`[backup-share] stored fragment for wallet ${walletId}`);
  }

  private async sendBackupAck(
    socket: WebSocket,
    payload: BackupSharePayload,
    body: { status: 'ok' } | { status: 'error'; message: string }
  ): Promise<void> {
    const response = {
      type: 'backup-share:ack',
      keyId: payload.keyId ?? payload.walletId ?? null,
      ...body,
    };
    try {
      socket.send(JSON.stringify(response));
    } catch (error) {
      console.warn('[backup-share] failed to send ack', error);
    }
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

function decodeBinary(value: string | undefined, encoding: 'hex' | 'base64' | undefined, field: string): Buffer {
  if (typeof value !== 'string') {
    throw new Error(`backup-share ${field} missing`);
  }
  if (value.length === 0) {
    return Buffer.alloc(0);
  }
  const trimmed = value.trim();
  if (trimmed.length === 0) {
    return Buffer.alloc(0);
  }
  const format: 'hex' | 'base64' = encoding ?? inferEncoding(trimmed);
  try {
    return Buffer.from(trimmed, format);
  } catch (error) {
    throw new Error(`backup-share ${field} invalid ${format}`);
  }
}

function inferEncoding(value: string): 'hex' | 'base64' {
  const isHex = /^[0-9a-fA-F]+$/.test(value) && value.length % 2 === 0;
  return isHex ? 'hex' : 'base64';
}

function parseCiphertext(payload: BackupShareCiphertextPayload) {
  const {
    kind,
    curve,
    scheme,
    keyId,
    threshold,
    shareCount,
    label,
    blob,
    labelEncoding,
    blobEncoding,
  } = payload;

  if (!kind || !curve || !scheme || !keyId) {
    throw new Error('backup-share ciphertext missing metadata');
  }
  if (typeof threshold !== 'number' || typeof shareCount !== 'number') {
    throw new Error('backup-share ciphertext missing threshold/shareCount');
  }

  const labelBuffer = decodeBinary(label, labelEncoding, 'ciphertext.label');
  const blobBuffer = decodeBinary(blob, blobEncoding, 'ciphertext.blob');

  return {
    kind,
    curve,
    scheme,
    keyId,
    threshold,
    shareCount,
    label: labelBuffer,
    blob: blobBuffer,
  };
}
