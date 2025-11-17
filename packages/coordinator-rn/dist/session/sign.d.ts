import * as mpc from '@maanyio/mpc-rn-bare';
import type { Transport } from '../transport';
export interface SignOptions {
    transport: Transport;
    message: Uint8Array;
    sessionId?: Uint8Array;
    extraAad?: Uint8Array;
    format?: mpc.SignatureFormat;
}
export declare function runSign(ctx: mpc.Ctx, device: mpc.Keypair, server: mpc.Keypair, opts: SignOptions): Promise<Uint8Array>;
