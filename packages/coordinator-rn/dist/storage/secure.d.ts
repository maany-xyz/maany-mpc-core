import type { ShareRecord, ShareStorage } from '../storage';
export interface SecureShareStorageOptions {
    promptMessage?: string;
}
export declare class SecureShareStorage implements ShareStorage {
    private readonly native;
    private readonly promptMessage?;
    constructor(options?: SecureShareStorageOptions);
    save(record: ShareRecord): Promise<void>;
    load(keyId: string): Promise<ShareRecord | null>;
    remove(keyId: string): Promise<void>;
}
