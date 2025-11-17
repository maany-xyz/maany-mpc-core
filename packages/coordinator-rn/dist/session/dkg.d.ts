import type { Transport } from '../transport';
import type { ShareStorage } from '../storage';
import * as mpc from '@maanyio/mpc-rn-bare';
export interface DkgResult {
    deviceKeypair: mpc.Keypair;
    serverKeypair: mpc.Keypair | null;
}
export interface DkgOptions {
    transport: Transport;
    storage: ShareStorage;
    keyId?: Uint8Array;
    sessionId?: Uint8Array;
    mode?: 'dual' | 'device-only';
}
export declare function runDkg(ctx: mpc.Ctx, opts: DkgOptions): Promise<DkgResult>;
