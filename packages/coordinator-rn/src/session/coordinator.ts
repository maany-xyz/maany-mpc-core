import type { Transport } from '../transport';
import type { ShareStorage } from '../storage';
import * as mpc from '@maany/mpc-rn';
import { runDkg } from './dkg';
import { runSign, SignOptions } from './sign';
import { optionalClone } from '../utils/bytes';

export interface CoordinatorOptions {
  transport: Transport;
  storage: ShareStorage;
}

export interface Coordinator {
  readonly options: CoordinatorOptions;
  initContext(): mpc.Ctx;
  runDkg(ctx: mpc.Ctx, opts?: { keyId?: Uint8Array; sessionId?: Uint8Array }): ReturnType<typeof runDkg>;
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
        keyId: optionalClone(extraOpts.keyId),
        sessionId: optionalClone(extraOpts.sessionId),
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
