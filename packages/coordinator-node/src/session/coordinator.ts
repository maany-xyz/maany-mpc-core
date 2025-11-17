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
  runDkg(
    ctx: mpc.Ctx,
    opts?: { keyId?: Uint8Array; sessionId?: Uint8Array; mode?: 'dual' | 'server-only' }
  ): ReturnType<typeof runDkg>;
  runSign(
    ctx: mpc.Ctx,
    device: mpc.Keypair,
    server: mpc.Keypair,
    opts: Omit<SignOptions, 'transport'>
  ): ReturnType<typeof runSign>;
}

export function createCoordinator(options: CoordinatorOptions): Coordinator {
  return {
    options,
    initContext() {
      return mpc.init();
    },
    runDkg(ctx, extraOpts = {}) {
      return runDkg(ctx, {
        transport: options.transport,
        storage: options.storage,
        keyId: extraOpts.keyId ? Buffer.from(extraOpts.keyId) : undefined,
        sessionId: extraOpts.sessionId ? Buffer.from(extraOpts.sessionId) : undefined,
        mode: extraOpts.mode,
      });
    },
    runSign(ctx, device, server, signOpts) {
      return runSign(ctx, device, server, {
        transport: options.transport,
        ...signOpts,
      });
    },
  };
}
