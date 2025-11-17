import type { Participant, Transport, TransportMessage } from '../transport';
type Listener = (event: any) => void;
type WebSocketLike = {
    readyState: number;
    binaryType?: string;
    send(data: ArrayBuffer | Uint8Array | string): void;
    close(code?: number, reason?: string): void;
    addEventListener?: (type: string, listener: Listener) => void;
    removeEventListener?: (type: string, listener: Listener) => void;
    onmessage?: (event: {
        data: unknown;
    }) => void;
    onclose?: (event: unknown) => void;
};
export interface WebSocketConnectOptions {
    url: string;
    participant?: Participant;
    protocols?: string | string[];
}
export declare class WebSocketTransport implements Transport {
    private readonly queues;
    private readonly sockets;
    /**
     * Establishes a WebSocket connection for the given participant and attaches the
     * transport listeners. Resolves once the socket reaches OPEN state.
     */
    connect(options: WebSocketConnectOptions): Promise<void>;
    /**
     * Attaches an existing WebSocket instance to the transport for the provided participant.
     */
    attach(participant: Participant, socket: WebSocketLike): void;
    send(message: TransportMessage): Promise<void>;
    receive(participant: Participant): Promise<Uint8Array | null>;
    detach(participant: Participant): void;
    close(participant?: Participant): void;
}
export {};
