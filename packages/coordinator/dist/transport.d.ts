export type Participant = 'device' | 'server';
export interface TransportMessage {
    participant: Participant;
    payload: Uint8Array;
}
export interface Transport {
    send(message: TransportMessage): Promise<void>;
    receive(participant: Participant): Promise<Uint8Array | null>;
}
export declare class InMemoryTransport implements Transport {
    private readonly queues;
    send(message: TransportMessage): Promise<void>;
    receive(participant: Participant): Promise<Uint8Array | null>;
}
