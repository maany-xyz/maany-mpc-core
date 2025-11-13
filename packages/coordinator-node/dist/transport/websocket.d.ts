import { WebSocket } from 'ws';
import type { Participant, Transport, TransportMessage } from '../transport';
export declare class WebSocketTransport implements Transport {
    private readonly queues;
    private readonly sockets;
    setSocket(participant: Participant, socket: WebSocket): void;
    send(message: TransportMessage): Promise<void>;
    receive(participant: Participant): Promise<Uint8Array | null>;
}
