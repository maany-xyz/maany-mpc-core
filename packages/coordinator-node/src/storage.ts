export interface ShareRecord {
  keyId: string;
  blob: Uint8Array;
}

export interface ShareStorage {
  save(record: ShareRecord): Promise<void>;
  load(keyId: string): Promise<ShareRecord | null>;
  remove(keyId: string): Promise<void>;
}

export class InMemoryShareStorage implements ShareStorage {
  private readonly state = new Map<string, Uint8Array>();

  async save(record: ShareRecord): Promise<void> {
    this.state.set(record.keyId, new Uint8Array(record.blob));
  }

  async load(keyId: string): Promise<ShareRecord | null> {
    const blob = this.state.get(keyId);
    if (!blob) return null;
    return { keyId, blob: new Uint8Array(blob) };
  }

  async remove(keyId: string): Promise<void> {
    this.state.delete(keyId);
  }
}
