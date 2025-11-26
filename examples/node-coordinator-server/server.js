const { PrismaClient } = require('@maany/mpc-coordinator-node/node_modules/@prisma/client');
const Redis = require('ioredis');
const {
  CoordinatorServer,
  PostgresRedisStorage,
  createEnvKeyEncryptor,
  PlaintextKeyEncryptor,
  makeSignBytes,
  sha256,
} = require('@maany/mpc-coordinator-node');
const mpc = require('@maany/mpc-node');

const PORT = process.env.PORT ? Number(process.env.PORT) : 8080;

console.log(`Starting coordinator server on ws://localhost:${PORT}`);

const DATABASE_URL =
  process.env.DATABASE_URL ?? 'postgresql://postgres:maany@localhost:5432/postgres?schema=public';
process.env.DATABASE_URL = DATABASE_URL;

const prisma = new PrismaClient();
const redis = new Redis(process.env.REDIS_URL ?? 'redis://localhost:6379');

const encryptor = process.env.MAANY_COORDINATOR_MASTER_KEY
  ? createEnvKeyEncryptor()
  : new PlaintextKeyEncryptor();

const server = new CoordinatorServer({
  port: PORT,
  storage: new PostgresRedisStorage(prisma, redis),
  encryptor,
  onSessionReady: async (session) => {
    console.log(
      `Session ready: id=${session.sessionId} intent=${session.intent.kind} token=${session.token ?? 'n/a'}`
    );
    try {
      switch (session.intent.kind) {
        case 'dkg': {
          const dkgOpts = {
            mode: 'server-only',
            keyId: session.intent.keyId ? Buffer.from(session.intent.keyId, 'hex') : undefined,
            sessionId: session.intent.sessionIdHint
              ? Buffer.from(session.intent.sessionIdHint, 'hex')
              : undefined,
            metadata: session.intent.keyId ? { walletId: session.intent.keyId } : undefined,
          };
          console.log('runDkg opts:', {
            mode: dkgOpts.mode,
            keyId: dkgOpts.keyId?.toString('hex'),
            sessionId: dkgOpts.sessionId?.toString('hex'),
          });
          await session.coordinator.runDkg(session.ctx, dkgOpts);
          await sendDoneSignal(session, 'dkg');
          console.log(`DKG complete for session ${session.sessionId}`);
          break;
        }
        case 'sign': {
          if (!session.intent.keyId) {
            throw new Error('sign intent requires keyId');
          }
          const serverRecord = await session.coordinator.options.storage.getWalletShare(session.intent.keyId);
          if (!serverRecord) {
            throw new Error(`Missing server share for keyId ${session.intent.keyId}`);
          }
          const ctx = session.ctx;
          const serverBlob = await session.coordinator.options.encryptor.decryptShare(
            serverRecord.encryptedServerShare
          );
          const serverKp = mpc.kpImport(ctx, serverBlob);
          const digest = sha256(makeSignBytes({
            chainId: 'cosmoshub-4',
            accountNumber: '0',
            sequence: '0',
            bodyBytes: Buffer.alloc(32, 0x42),
            authInfoBytes: Buffer.alloc(32, 0x24),
          }));
          await session.coordinator.runSign(ctx, null, serverKp, { message: digest, mode: 'server-only' });
          await sendDoneSignal(session, 'sign');
          console.log(`Sign complete for key ${session.intent.keyId}`);
          break;
        }
        default:
          console.warn(`Unhandled intent ${session.intent.kind}`);
      }
    } catch (err) {
      console.error(`Session ${session.sessionId} failed:`, err);
    } finally {
      if (session.intent.kind === 'dkg') {
        await waitForBackupWindow(session);
      }
      session.close();
    }
  },
});

const DONE_SIGNAL = Buffer.from([0xff, 0x00, 0xff, 0x00]);

async function sendDoneSignal(session, intent) {
  try {
    await session.transport.send({ participant: 'device', payload: DONE_SIGNAL });
    console.log(`[session ${session.sessionId}] sent completion signal for ${intent}`);
  } catch (error) {
    console.warn(`[session ${session.sessionId}] failed to send completion signal:`, error);
  }
}

const BACKUP_GRACE_MS = Number(process.env.BACKUP_GRACE_MS ?? 15000);

async function waitForBackupWindow(session) {
  if (!session.socket) {
    await new Promise((resolve) => setTimeout(resolve, 1000));
    return;
  }
  console.log(
    `[session ${session.sessionId}] waiting up to ${BACKUP_GRACE_MS}ms for backup fragment`
  );
  await new Promise((resolve) => {
    let resolved = false;
    const timer = setTimeout(() => {
      if (resolved) return;
      resolved = true;
      session.socket?.off('close', onClose);
      resolve();
    }, BACKUP_GRACE_MS);

    const onClose = () => {
      if (resolved) return;
      resolved = true;
      clearTimeout(timer);
      resolve();
    };

    session.socket.once('close', onClose);
  });
}

process.on('SIGINT', async () => {
  console.log('Shutting down coordinator server...');
  await server.close();
  process.exit(0);
});
