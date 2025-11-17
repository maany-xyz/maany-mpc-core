import * as mpc from '@maany/mpc-node';
import type { Transport } from '../transport';

export interface SignOptions {
  transport: Transport;
  message: Uint8Array;
  sessionId?: Uint8Array;
  extraAad?: Uint8Array;
  format?: mpc.SignatureFormat;
  mode?: 'dual' | 'server-only';
}

export async function runSign(
  ctx: mpc.Ctx,
  device: mpc.Keypair | null,
  server: mpc.Keypair,
  opts: SignOptions
): Promise<Uint8Array | null> {
  const simulateDevice = opts.mode !== 'server-only';
  if (simulateDevice && !device) {
    throw new Error('device keypair is required unless mode="server-only"');
  }
  const commonOpts: mpc.SignOptions = {};
  if (opts.sessionId) commonOpts.sessionId = Buffer.from(opts.sessionId);
  if (opts.extraAad) commonOpts.extraAad = Buffer.from(opts.extraAad);

  const signServer = mpc.signNew(ctx, server, commonOpts);

  const message = Buffer.from(opts.message);
  mpc.signSetMessage(ctx, signServer, message);

  if (simulateDevice && device) {
    const signDevice = mpc.signNew(ctx, device, commonOpts);
    mpc.signSetMessage(ctx, signDevice, message);

    let deviceDone = false;
    let serverDone = false;

    for (let i = 0; i < 128 && !(deviceDone && serverDone); ++i) {
      if (!serverDone) {
        const inbound = await opts.transport.receive('server');
        const res = await mpc.signStep(ctx, signServer, inbound ?? undefined);
        if (res.outMsg) await opts.transport.send({ participant: 'device', payload: res.outMsg });
        serverDone = res.done;
      }
      if (!deviceDone) {
        const inbound = await opts.transport.receive('device');
        const res = await mpc.signStep(ctx, signDevice, inbound ?? undefined);
        if (res.outMsg) await opts.transport.send({ participant: 'server', payload: res.outMsg });
        deviceDone = res.done;
      }
    }

    const format = opts.format ?? 'der';
    const signature = mpc.signFinalize(ctx, signDevice, format);
    mpc.signFree(signDevice);
    mpc.signFree(signServer);
    return signature;
  }

  const waitForDeviceMessage = async (): Promise<Uint8Array> => {
    while (true) {
      const next = await opts.transport.receive('server');
      if (next) {
        return next;
      }
      await new Promise((resolve) => setTimeout(resolve, 5));
    }
  };

  let inbound: Uint8Array | undefined = await waitForDeviceMessage();
  for (let i = 0; i < 256; ++i) {
    console.log(`[server-only sign] round=${i} inbound=${inbound ? inbound.length : 0}`);
    const res = await mpc.signStep(ctx, signServer, inbound);
    inbound = undefined;
    if (res.outMsg) await opts.transport.send({ participant: 'device', payload: res.outMsg });
    if (res.done) {
      console.log('[server-only sign] done');
      break;
    }
    inbound = await waitForDeviceMessage();
  }

  mpc.signFree(signServer);
  return null;
}
