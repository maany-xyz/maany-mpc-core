-- CreateTable
CREATE TABLE "WalletShare" (
    "walletId" TEXT NOT NULL,
    "appId" TEXT,
    "userId" TEXT,
    "deviceId" TEXT,
    "publicKey" TEXT,
    "encryptedServerShare" BYTEA NOT NULL,
    "createdAt" TIMESTAMP(3) NOT NULL DEFAULT CURRENT_TIMESTAMP,
    "updatedAt" TIMESTAMP(3) NOT NULL,

    CONSTRAINT "WalletShare_pkey" PRIMARY KEY ("walletId")
);

-- CreateTable
CREATE TABLE "WalletNonce" (
    "walletId" TEXT NOT NULL,
    "value" INTEGER NOT NULL,
    "createdAt" TIMESTAMP(3) NOT NULL DEFAULT CURRENT_TIMESTAMP,
    "updatedAt" TIMESTAMP(3) NOT NULL,

    CONSTRAINT "WalletNonce_pkey" PRIMARY KEY ("walletId")
);
