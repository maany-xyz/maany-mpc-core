# Node Coordinator Example

This example exposes the `@maany/mpc-coordinator-node` reference server over
WebSockets. It accepts a device connection, drives DKG/signing rounds, and saves
the coordinator share using whichever storage/encryption implementation you
inject. By default it uses in-memory storage so you can smoke test the flow, but
you can point it at Postgres + Redis to exercise the real persistence layer.

## Quick Start (in-memory)

```bash
cd examples/node-coordinator-server
npm install
npm start
```

This boots a coordinator on `ws://localhost:8080` that:

1. Waits for a device WebSocket and intent handshake.
2. Runs the DKG protocol (server-only mode) and encrypts its share with the
   provided key encryptor (plaintext by default).
3. Stores the encrypted share using the configured storage (in-memory by
   default).
4. Executes a dummy signing round and logs when each session completes.

Use this path for local integration tests or to understand the coordinator API
surface without provisioning infrastructure.

## Running with Postgres + Redis

If you want to test the persistence layer that production will use:

1. **Start services**
   ```bash
   docker run --rm -e POSTGRES_PASSWORD=maany -p 5432:5432 postgres:15
   docker run --rm -p 6379:6379 redis:7
   ```
2. **Set env vars** before `npm start`:
   ```bash
   export DATABASE_URL="postgresql://postgres:maany@localhost:5432/postgres?schema=public"
   export REDIS_URL="redis://localhost:6379"
   export MAANY_COORDINATOR_MASTER_KEY=$(openssl rand -hex 32)
   ```
3. **Apply the Prisma schema** (run once):
   ```bash
   cd packages/coordinator-node
   npx prisma migrate dev --schema prisma/schema.prisma
   cd ../../examples/node-coordinator-server
   ```
4. **Edit `server.js`** to use the persistent adapter:
   ```js
   const { PrismaClient } = require('@prisma/client');
   const Redis = require('ioredis');
   const {
     PostgresRedisStorage,
     createEnvKeyEncryptor,
     CoordinatorServer,
   } = require('@maany/mpc-coordinator-node');

   const prisma = new PrismaClient();
   const redis = new Redis(process.env.REDIS_URL);

   const server = new CoordinatorServer({
     port: PORT,
     storage: new PostgresRedisStorage(prisma, redis),
     encryptor: createEnvKeyEncryptor(),
     // ...
   });
   ```

When you now run `npm start`, completed DKG sessions will write encrypted
`walletShare` rows to Postgres and keep session/nonce state in Redis, matching
the architecture described in `CODEX.md`.
