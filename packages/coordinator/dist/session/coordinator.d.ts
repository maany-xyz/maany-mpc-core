import type { Transport } from '../transport';
import type { ShareStorage } from '../storage';
import * as mpc from '@maany/mpc-node';
import { runDkg } from './dkg';
import { runSign, SignOptions } from './sign';
export interface CoordinatorOptions {
    transport: Transport;
    storage: ShareStorage;
}
export interface Coordinator {
    readonly options: CoordinatorOptions;
    initContext(): mpc.Ctx;
    runDkg(ctx: mpc.Ctx, opts?: {
        keyId?: Uint8Array;
        sessionId?: Uint8Array;
    }): ReturnType<typeof runDkg>;
    runSign(ctx: mpc.Ctx, device: mpc.Keypair, server: mpc.Keypair, opts: Omit<SignOptions, 'transport'>): ReturnType<typeof runSign>;
}
export declare function createCoordinator(options: CoordinatorOptions): Coordinator;
