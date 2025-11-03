export type Participant = 'device' | 'server';

export interface TransportMessage {
  participant: Participant;
  payload: Uint8Array;
}

export interface Transport {
  send(message: TransportMessage): Promise<void>;
  receive(participant: Participant): Promise<Uint8Array | null>;
}

export class InMemoryTransport implements Transport {
  private readonly queues: Record<Participant, Uint8Array[]> = {
    device: [],
    server: [],
  };

  async send(message: TransportMessage): Promise<void> {
    const target = message.participant === 'device' ? 'device' : 'server';
    this.queues[target].push(message.payload);
  }

  async receive(participant: Participant): Promise<Uint8Array | null> {
    const queue = this.queues[participant];
    if (queue.length === 0) return null;
    return queue.shift() ?? null;
  }
}
