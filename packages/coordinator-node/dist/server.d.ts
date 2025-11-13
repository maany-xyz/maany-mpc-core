import { EventEmitter } from 'node:events';
import type { Coordinator } from './session/coordinator';
import { WebSocketTransport } from './transport/websocket';
import type { ShareStorage } from './storage';
import * as mpc from '@maany/mpc-node';
export interface CoordinatorServerOptions {
    port: number;
    storage: ShareStorage;
    onSessionReady?: (session: SessionReadyEvent) => void;
}
type SessionIntent = {
    kind: 'dkg';
    keyId?: string;
    sessionIdHint?: string;
} | {
    kind: 'sign';
    keyId: string;
    sessionIdHint?: string;
} | {
    kind: 'refresh';
    keyId: string;
    sessionIdHint?: string;
};
export interface SessionReadyEvent {
    sessionId: string;
    token?: string;
    intent: SessionIntent;
    coordinator: Coordinator;
    ctx: mpc.Ctx;
    transport: WebSocketTransport;
    close: () => void;
}
export declare interface CoordinatorServer {
    on(event: 'session:ready', listener: (session: SessionReadyEvent) => void): this;
    emit(event: 'session:ready', session: SessionReadyEvent): boolean;
}
export declare class CoordinatorServer extends EventEmitter {
    private readonly wss;
    private readonly storage;
    private readonly sessions;
    constructor(options: CoordinatorServerOptions);
    private handleConnection;
    private registerParticipant;
    private mapIntent;
    private cleanupSession;
    close(): Promise<void>;
}
export {};
