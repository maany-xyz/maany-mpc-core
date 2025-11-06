import { NativeModules, Platform } from 'react-native';
import type { ShareRecord, ShareStorage } from '../storage';
import { cloneBytes, fromBase64, toBase64 } from '../utils/bytes';

type NativeSecureStorage = {
  saveShare(keyId: string, base64: string): Promise<void>;
  loadShare(keyId: string, prompt?: string): Promise<string | null>;
  removeShare(keyId: string): Promise<void>;
};

const MODULE_NAME = 'MaanyMpcSecureStorage';

function getNativeModule(): NativeSecureStorage {
  const native = (NativeModules as Record<string, unknown>)[MODULE_NAME] as NativeSecureStorage | undefined;
  if (!native) {
    throw new Error(`${MODULE_NAME} native module not available. Ensure the coordinator package is linked.`);
  }
  return native;
}

export interface SecureShareStorageOptions {
  promptMessage?: string;
}

export class SecureShareStorage implements ShareStorage {
  private readonly native: NativeSecureStorage;
  private readonly promptMessage?: string;

  constructor(options: SecureShareStorageOptions = {}) {
    if (Platform.OS !== 'ios' && Platform.OS !== 'android') {
      throw new Error('SecureShareStorage is only supported on iOS and Android');
    }
    this.native = getNativeModule();
    this.promptMessage = options.promptMessage;
  }

  async save(record: ShareRecord): Promise<void> {
    const base64 = toBase64(cloneBytes(record.blob));
    await this.native.saveShare(record.keyId, base64);
  }

  async load(keyId: string): Promise<ShareRecord | null> {
    const base64 = await this.native.loadShare(keyId, this.promptMessage);
    if (!base64) {
      return null;
    }
    return { keyId, blob: fromBase64(base64) };
  }

  async remove(keyId: string): Promise<void> {
    await this.native.removeShare(keyId);
  }
}
