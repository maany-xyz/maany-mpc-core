import type { Transport } from '../transport';
import type { ShareStorage } from '../storage';
import * as mpc from '@maany/mpc-node';
export interface DkgResult {
    deviceKeypair: mpc.Keypair;
    serverKeypair: mpc.Keypair;
}
export interface DkgOptions {
    transport: Transport;
    storage: ShareStorage;
    keyId?: Uint8Array | Buffer;
    sessionId?: Uint8Array | Buffer;
}
export declare function runDkg(ctx: mpc.Ctx, opts: DkgOptions): Promise<DkgResult>;
