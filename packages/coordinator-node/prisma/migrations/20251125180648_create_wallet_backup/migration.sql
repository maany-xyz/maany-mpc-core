-- CreateTable
CREATE TABLE "WalletBackup" (
    "walletId" TEXT NOT NULL,
    "ciphertextKind" TEXT NOT NULL,
    "ciphertextCurve" TEXT NOT NULL,
    "ciphertextScheme" TEXT NOT NULL,
    "ciphertextKeyId" TEXT NOT NULL,
    "ciphertextThreshold" INTEGER NOT NULL,
    "ciphertextShareCount" INTEGER NOT NULL,
    "ciphertextLabel" BYTEA NOT NULL,
    "ciphertextBlob" BYTEA NOT NULL,
    "coordinatorFragment" BYTEA NOT NULL,
    "createdAt" TIMESTAMP(3) NOT NULL DEFAULT CURRENT_TIMESTAMP,
    "updatedAt" TIMESTAMP(3) NOT NULL DEFAULT CURRENT_TIMESTAMP,
    CONSTRAINT "WalletBackup_pkey" PRIMARY KEY ("walletId"),
    CONSTRAINT "WalletBackup_walletId_fkey" FOREIGN KEY ("walletId") REFERENCES "WalletShare"("walletId") ON DELETE RESTRICT ON UPDATE CASCADE
);

CREATE OR REPLACE FUNCTION set_wallet_backup_updated_at()
RETURNS TRIGGER AS $$
BEGIN
    NEW."updatedAt" = NOW();
    RETURN NEW;
END;
$$ LANGUAGE plpgsql;

CREATE TRIGGER wallet_backup_set_updated_at
BEFORE UPDATE ON "WalletBackup"
FOR EACH ROW
EXECUTE FUNCTION set_wallet_backup_updated_at();
