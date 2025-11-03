export interface ShareRecord {
    keyId: string;
    blob: Uint8Array;
}
export interface ShareStorage {
    save(record: ShareRecord): Promise<void>;
    load(keyId: string): Promise<ShareRecord | null>;
    remove(keyId: string): Promise<void>;
}
export declare class InMemoryShareStorage implements ShareStorage {
    private readonly state;
    save(record: ShareRecord): Promise<void>;
    load(keyId: string): Promise<ShareRecord | null>;
    remove(keyId: string): Promise<void>;
}
