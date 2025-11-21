#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace maany::bridge {

enum class ErrorCode {
  Ok = 0,
  General,
  InvalidArgument,
  Unsupported,
  ProtocolState,
  Crypto,
  Rng,
  Io,
  Policy,
  Memory
};

class Error : public std::runtime_error {
 public:
  Error(ErrorCode code, std::string message);
  [[nodiscard]] ErrorCode code() const noexcept { return code_; }

 private:
  ErrorCode code_;
};

using RngCallback = std::function<int(uint8_t* out, size_t len)>;
using SecureZeroCallback = std::function<void(void* ptr, size_t len)>;
using MallocCallback = std::function<void*(size_t)>;
using FreeCallback = std::function<void(void*)>;
using LogCallback = std::function<void(int level, const std::string& msg)>;

struct InitOptions {
  RngCallback rng;
  SecureZeroCallback secure_zero;
  MallocCallback malloc_fn;
  FreeCallback free_fn;
  LogCallback logger;
};

enum class Curve {
  Secp256k1 = 0,
  Ed25519 = 1
};

enum class Scheme {
  Ecdsa2p = 0,
  EcdsaThresholdN = 1,
  Schnorr2p = 2
};

enum class ShareKind {
  Device = 0,
  Server = 1
};

enum class SigFormat {
  Der = 0,
  RawRs = 1
};

enum class StepState {
  Continue = 0,
  Done = 1
};

struct BufferOwner {
  std::vector<uint8_t> bytes;
};

struct PubKey {
  Curve curve;
  BufferOwner compressed;
};

struct KeyId {
  std::array<uint8_t, 32> bytes{};
};

struct DkgOptions {
  Curve curve{Curve::Secp256k1};
  Scheme scheme{Scheme::Ecdsa2p};
  ShareKind kind{ShareKind::Device};
  KeyId key_id{};
  BufferOwner session_id;  // optional; empty when unset
};

struct SignOptions {
  Scheme scheme{Scheme::Ecdsa2p};
  BufferOwner session_id;
  BufferOwner extra_aad;
};

struct RefreshOptions {
  BufferOwner session_id;
};

struct StepOutput {
  StepState state{StepState::Continue};
  std::optional<BufferOwner> outbound;
};

struct BackupCiphertext {
  ShareKind kind{ShareKind::Device};
  Scheme scheme{Scheme::Ecdsa2p};
  Curve curve{Curve::Secp256k1};
  KeyId key_id{};
  uint32_t threshold{0};
  uint32_t share_count{0};
  BufferOwner label;
  BufferOwner payload;  // nonce || tag || ciphertext
};

struct BackupShare {
  BufferOwner data;  // encoded pid||share
};

class Keypair;
class DkgSession;
class SignSession;

class Context {
 public:
  static std::unique_ptr<Context> Create(const InitOptions& opts);
  virtual ~Context();

  virtual std::unique_ptr<DkgSession> CreateDkg(const DkgOptions& opts) = 0;
  virtual std::unique_ptr<Keypair> ImportKey(const BufferOwner& blob) = 0;
  virtual BufferOwner ExportKey(const Keypair& kp) = 0;
  virtual PubKey GetPubKey(const Keypair& kp) = 0;
  virtual std::unique_ptr<SignSession> CreateSign(const Keypair& kp, const SignOptions& opts) = 0;
  virtual std::unique_ptr<DkgSession> CreateRefresh(const Keypair& kp, const RefreshOptions& opts) = 0;
  virtual void CreateBackup(
    const Keypair& kp,
    uint32_t threshold,
    size_t share_count,
    const BufferOwner& label,
    BackupCiphertext& out_ciphertext,
    std::vector<BackupShare>& out_shares) = 0;
  virtual std::unique_ptr<Keypair> RestoreBackup(
    const BackupCiphertext& ciphertext,
    const std::vector<BackupShare>& shares) = 0;
};

class Keypair {
 public:
  virtual ~Keypair();
  virtual ShareKind kind() const = 0;
  virtual Scheme scheme() const = 0;
  virtual Curve curve() const = 0;
  virtual KeyId key_id() const = 0;
};

class DkgSession {
 public:
  virtual ~DkgSession();
  virtual StepOutput Step(const std::optional<BufferOwner>& inbound) = 0;
  virtual std::unique_ptr<Keypair> Finalize() = 0;
};

class SignSession {
 public:
  virtual ~SignSession();
  virtual void SetMessage(const uint8_t* msg, size_t len) = 0;
  virtual StepOutput Step(const std::optional<BufferOwner>& inbound) = 0;
  virtual BufferOwner Finalize(SigFormat fmt) = 0;
};

}  // namespace maany::bridge
