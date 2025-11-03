import * as mpc from '@maany/mpc-rn';
import type { Transport } from '../transport';
import { cloneBytes, optionalClone } from '../utils/bytes';

export interface SignOptions {
  transport: Transport;
  message: Uint8Array;
  sessionId?: Uint8Array;
  extraAad?: Uint8Array;
  format?: mpc.SignatureFormat;
}

export async function runSign(
  ctx: mpc.Ctx,
  device: mpc.Keypair,
  server: mpc.Keypair,
  opts: SignOptions
): Promise<Uint8Array> {
  const commonOpts: mpc.SignOptions = {};
  const sessionId = optionalClone(opts.sessionId);
  const extraAad = optionalClone(opts.extraAad);
  if (sessionId) commonOpts.sessionId = sessionId;
  if (extraAad) commonOpts.extraAad = extraAad;

  const signDevice = mpc.signNew(ctx, device, commonOpts);
  const signServer = mpc.signNew(ctx, server, commonOpts);

  const message = cloneBytes(opts.message);
  mpc.signSetMessage(ctx, signDevice, message);
  mpc.signSetMessage(ctx, signServer, message);

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
