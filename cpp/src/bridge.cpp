#include "bridge.h"

#include <cbmpc/core/convert.h>
#include <cbmpc/core/error.h>
#include <cbmpc/crypto/base.h>
#include <cbmpc/crypto/base_ecc.h>
#include <cbmpc/crypto/base_pki.h>
#include <cbmpc/protocol/ecdsa_2p.h>

#include <cstdint>
#include <condition_variable>
#include <deque>
#include <functional>
#include <cstring>
#include <algorithm>
#include <mutex>
#include <optional>
#include <sstream>
#include <thread>
#include <utility>

namespace maany::bridge {

Error::Error(ErrorCode code, std::string message)
    : std::runtime_error(std::move(message)), code_(code) {}

namespace {

using coinbase::mem_t;
using coinbase::crypto::bn_t;
using coinbase::crypto::ecurve_t;
using coinbase::crypto::curve_secp256k1;
using coinbase::crypto::mpc_pid_t;
using coinbase::crypto::pid_from_name;
using coinbase::mpc::ecdsa2pc::dkg;
using coinbase::mpc::ecdsa2pc::key_t;
using coinbase::mpc::job_2p_t;
using coinbase::mpc::party_idx_t;
using coinbase::mpc::party_t;

constexpr uint32_t kKeyBlobMagic = 0x4D50434B;  // 'MPCK'
constexpr uint32_t kKeyBlobVersion = 1;

const mpc_pid_t& DevicePid() {
  static const mpc_pid_t pid = pid_from_name("maany-device");
  return pid;
}

const mpc_pid_t& ServerPid() {
  static const mpc_pid_t pid = pid_from_name("maany-server");
  return pid;
}

ecurve_t ToCbCurve(Curve curve) {
  switch (curve) {
    case Curve::Secp256k1:
      return curve_secp256k1;
    case Curve::Ed25519:
      throw Error(ErrorCode::Unsupported, "ed25519 not yet supported");
  }
  throw Error(ErrorCode::InvalidArgument, "unknown curve");
}

Curve FromCbCurve(const ecurve_t& cb_curve) {
  if (cb_curve == curve_secp256k1) return Curve::Secp256k1;
  throw Error(ErrorCode::Unsupported, "unsupported curve from cb-mpc");
}

party_t ToParty(ShareKind kind) {
  switch (kind) {
    case ShareKind::Device:
      return party_t::p1;
    case ShareKind::Server:
      return party_t::p2;
  }
  throw Error(ErrorCode::InvalidArgument, "unknown share kind");
}

ShareKind FromParty(party_t party) {
  return party == party_t::p1 ? ShareKind::Device : ShareKind::Server;
}

ErrorCode MapError(::error_t err) {
  if (err == SUCCESS) return ErrorCode::Ok;
  if (err == E_BADARG || err == E_FORMAT || err == E_RANGE)
    return ErrorCode::InvalidArgument;
  if (err == E_NOT_SUPPORTED) return ErrorCode::Unsupported;
  switch (ECATEGORY(err)) {
    case ECATEGORY_CRYPTO:
      return ErrorCode::Crypto;
    case ECATEGORY_NETWORK:
      return ErrorCode::Io;
    default:
      break;
  }
  return ErrorCode::General;
}

std::string FormatError(::error_t err, const char* where) {
  std::ostringstream oss;
  oss << where << " failed with 0x" << std::hex << err;
  return oss.str();
}

struct StoredError {
  ErrorCode code;
  std::string message;
};

struct KeyBlob {
  uint32_t magic = kKeyBlobMagic;
  uint32_t version = kKeyBlobVersion;
  uint32_t scheme = 0;
  uint32_t kind = 0;
  KeyId key_id;
  ecurve_t curve;
  coinbase::crypto::ecc_point_t Q;
  bn_t x_share;
  bn_t c_key;
  coinbase::crypto::paillier_t paillier;

  void convert(coinbase::converter_t& conv) {
    conv.convert(magic);
    conv.convert(version);
    conv.convert(scheme);
    conv.convert(kind);
    conv.convert(key_id.bytes);
    conv.convert(curve);
    conv.convert(Q);
    conv.convert(x_share);
    conv.convert(c_key);
    conv.convert(paillier);
  }
};

std::vector<uint8_t> ToVector(mem_t mem) {
  return std::vector<uint8_t>(mem.data, mem.data + mem.size);
}

BufferOwner MakeBuffer(std::vector<uint8_t> bytes) {
  BufferOwner buf;
  buf.bytes = std::move(bytes);
  return buf;
}

class AsyncSession {
 public:
  AsyncSession() = default;
  AsyncSession(const AsyncSession&) = delete;
  AsyncSession& operator=(const AsyncSession&) = delete;
  virtual ~AsyncSession() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      aborted_ = true;
      cv_.notify_all();
    }
    if (worker_.joinable()) worker_.join();
  }

 protected:
  void StartWorker(std::function<void()> fn) {
    worker_ = std::thread([this, fn = std::move(fn)]() mutable {
      try {
        fn();
      } catch (const Error& err) {
        Fail(err.code(), err.what());
      } catch (const std::exception& ex) {
        Fail(ErrorCode::General, ex.what());
      } catch (...) {
        Fail(ErrorCode::General, "unknown exception");
      }
      {
        std::lock_guard<std::mutex> lock(mutex_);
        worker_done_ = true;
      }
      cv_.notify_all();
    });
  }

  ::error_t OnSend(mem_t msg) {
    std::vector<uint8_t> bytes(msg.data, msg.data + msg.size);
    {
      std::lock_guard<std::mutex> lock(mutex_);
      outbound_.emplace(std::move(bytes));
    }
    cv_.notify_all();
    return SUCCESS;
  }

  ::error_t OnReceive(mem_t& msg) {
    std::unique_lock<std::mutex> lock(mutex_);
    waiting_for_inbound_ = true;
    ++wait_request_id_;
    cv_.notify_all();
    cv_.wait(lock, [&] { return !inbound_queue_.empty() || aborted_ || fatal_.has_value(); });
    if (fatal_) return E_GENERAL;
    if (aborted_) return E_GENERAL;

    inbound_active_ = std::move(inbound_queue_.front());
    inbound_queue_.pop_front();
    waiting_for_inbound_ = false;

    msg = mem_t(inbound_active_.data(), static_cast<int>(inbound_active_.size()));
    return SUCCESS;
  }

  StepOutput AwaitStep(const std::optional<BufferOwner>& inbound) {
    const uint64_t wait_snapshot = wait_request_id_;

    if (inbound && !inbound->bytes.empty()) {
      std::lock_guard<std::mutex> lock(mutex_);
      inbound_queue_.push_back(inbound->bytes);
      cv_.notify_all();
    } else if (inbound && inbound->bytes.empty()) {
      // Accept empty message as legitimate round data
      std::lock_guard<std::mutex> lock(mutex_);
      inbound_queue_.emplace_back();
      cv_.notify_all();
    }

    std::unique_lock<std::mutex> lock(mutex_);
    for (;;) {
      if (fatal_) {
        auto err = *fatal_;
        lock.unlock();
        throw Error(err.code, err.message);
      }
      if (outbound_) {
        StepOutput out;
        std::vector<uint8_t> data = std::move(*outbound_);
        outbound_.reset();
        out.outbound = MakeBuffer(std::move(data));
        out.state = worker_done_ ? StepState::Done : StepState::Continue;
        return out;
      }
      if (worker_done_) {
        StepOutput out;
        out.state = StepState::Done;
        return out;
      }
      if (waiting_for_inbound_ && wait_request_id_ > wait_snapshot) {
        StepOutput out;
        out.state = StepState::Continue;
        return out;
      }
      cv_.wait(lock);
    }
  }

  void Fail(ErrorCode code, std::string message) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!fatal_) fatal_ = StoredError{code, std::move(message)};
    aborted_ = true;
    cv_.notify_all();
  }

  bool IsDone() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return worker_done_ && !fatal_.has_value();
  }

  [[nodiscard]] bool HasFailure() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return fatal_.has_value();
  }

  [[nodiscard]] StoredError GetFailure() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return *fatal_;
  }

  void PushInbound(std::vector<uint8_t> bytes) {
    std::lock_guard<std::mutex> lock(mutex_);
    inbound_queue_.push_back(std::move(bytes));
    cv_.notify_all();
  }

  void EnsureWorkerFinished() {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [&] { return worker_done_ || fatal_.has_value(); });
    if (fatal_) {
      auto err = *fatal_;
      lock.unlock();
      throw Error(err.code, err.message);
    }
  }

  bool waiting_for_inbound() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return waiting_for_inbound_;
  }

 protected:
  friend class FiberJob;
  mutable std::mutex mutex_;
  std::condition_variable cv_;
  std::thread worker_;
  bool worker_done_ = false;
  bool aborted_ = false;
  bool waiting_for_inbound_ = false;
  std::deque<std::vector<uint8_t>> inbound_queue_;
  std::vector<uint8_t> inbound_active_;
  std::optional<std::vector<uint8_t>> outbound_;
  std::optional<StoredError> fatal_;
  uint64_t wait_request_id_ = 0;
};

class FiberJob final : public job_2p_t {
 public:
  FiberJob(party_t party, AsyncSession& session)
      : job_2p_t(party, DevicePid(), ServerPid()), session_(session) {}

 protected:
  ::error_t send_impl(party_idx_t, mem_t msg) override { return session_.OnSend(msg); }
  ::error_t receive_impl(party_idx_t, mem_t& msg) override { return session_.OnReceive(msg); }

 private:
  AsyncSession& session_;
};

class KeypairImpl final : public Keypair {
 public:
  KeypairImpl(ShareKind kind, Scheme scheme, Curve curve, KeyId id, key_t key)
      : kind_(kind), scheme_(scheme), curve_(curve), key_id_(id), key_(std::move(key)) {}

  ~KeypairImpl() override = default;

  ShareKind kind() const override { return kind_; }
  Scheme scheme() const override { return scheme_; }
  Curve curve() const override { return curve_; }
  KeyId key_id() const override { return key_id_; }

  key_t& key() { return key_; }
  const key_t& key() const { return key_; }

 private:
  ShareKind kind_;
  Scheme scheme_;
  Curve curve_;
  KeyId key_id_;
  key_t key_;
};

class ContextImpl;

class DkgSessionImpl final : public DkgSession, private AsyncSession {
 public:
  DkgSessionImpl(const DkgOptions& opts)
      : opts_(opts),
        curve_(ToCbCurve(opts.curve)),
        party_(ToParty(opts.kind)),
        job_(std::make_unique<FiberJob>(party_, static_cast<AsyncSession&>(*this))) {
    if (opts.scheme != Scheme::Ecdsa2p) throw Error(ErrorCode::Unsupported, "only ECDSA 2p supported");
    StartWorker([this]() { Worker(); });
  }

  ~DkgSessionImpl() override = default;

  StepOutput Step(const std::optional<BufferOwner>& inbound) override { return AwaitStep(inbound); }

  std::unique_ptr<Keypair> Finalize() override {
    EnsureWorkerFinished();
    if (!key_ready_) throw Error(ErrorCode::ProtocolState, "DKG not complete");
    key_ready_ = false;
    return std::make_unique<KeypairImpl>(opts_.kind, opts_.scheme, opts_.curve, opts_.key_id, key_);
  }

 private:
  void Worker() {
    key_t tmp;
    tmp.role = party_;
    tmp.curve = curve_;
    auto rv = dkg(*job_, curve_, tmp);
    if (rv != SUCCESS) {
      Fail(MapError(rv), FormatError(rv, "ecdsa2pc::dkg"));
      return;
    }

    {
      std::lock_guard<std::mutex> lock(mutex_);
      key_ = std::move(tmp);
      key_ready_ = true;
    }
    cv_.notify_all();
  }

  DkgOptions opts_;
  ecurve_t curve_;
  party_t party_;
  std::unique_ptr<FiberJob> job_;
  key_t key_{};
  bool key_ready_ = false;
};

class SignSessionImpl final : public SignSession, private AsyncSession {
 public:
  SignSessionImpl(const KeypairImpl& kp, const SignOptions& opts)
      : opts_(opts),
        curve_(kp.key().curve),
        party_(ToParty(kp.kind())),
        key_(kp.key()),
        job_(std::make_unique<FiberJob>(party_, static_cast<AsyncSession&>(*this))) {
    if (opts.scheme != Scheme::Ecdsa2p) throw Error(ErrorCode::Unsupported, "only ECDSA 2p sign supported");
    StartWorker([this]() { Worker(); });
  }

  ~SignSessionImpl() override {
    std::fill(signature_der_.bytes.begin(), signature_der_.bytes.end(), 0);
    std::fill(signature_raw_.bytes.begin(), signature_raw_.bytes.end(), 0);
  }

  void SetMessage(const uint8_t* msg, size_t len) override {
    if (!msg || len == 0) throw Error(ErrorCode::InvalidArgument, "message required");
    std::lock_guard<std::mutex> lock(message_mutex_);
    if (message_ready_) throw Error(ErrorCode::ProtocolState, "message already set");
    message_.assign(msg, msg + len);
    message_ready_ = true;
    message_cv_.notify_all();
  }

  StepOutput Step(const std::optional<BufferOwner>& inbound) override { return AwaitStep(inbound); }

  BufferOwner Finalize(SigFormat fmt) override {
    EnsureWorkerFinished();
    if (party_ != party_t::p1) throw Error(ErrorCode::ProtocolState, "signature finalize not available for this share");
    BufferOwner out;
    {
      std::lock_guard<std::mutex> guard(result_mutex_);
      if (!signature_ready_) throw Error(ErrorCode::ProtocolState, "signature not ready");
      const std::vector<uint8_t>& src = (fmt == SigFormat::Der) ? signature_der_.bytes : signature_raw_.bytes;
      if (src.empty()) throw Error(ErrorCode::ProtocolState, "requested signature format unavailable");
      out.bytes = src;
    }
    return out;
  }

 private:
  void Worker() {
    std::unique_lock<std::mutex> lock(message_mutex_);
    message_cv_.wait(lock, [&] { return message_ready_ || fatal_.has_value() || aborted_; });
    if (!message_ready_) return;
    std::vector<uint8_t> msg = std::move(message_);
    message_.clear();
    message_ready_ = false;
    lock.unlock();

    coinbase::mem_t msg_mem(msg.data(), static_cast<int>(msg.size()));

    coinbase::buf_t sid_buf;
    if (!opts_.session_id.bytes.empty()) {
      sid_buf = coinbase::buf_t(static_cast<int>(opts_.session_id.bytes.size()));
      std::memcpy(sid_buf.data(), opts_.session_id.bytes.data(), opts_.session_id.bytes.size());
    }

    coinbase::buf_t sig_buf;
    auto rv = coinbase::mpc::ecdsa2pc::sign(*job_, sid_buf, key_, msg_mem, sig_buf);
    if (rv != SUCCESS) {
      Fail(MapError(rv), FormatError(rv, "ecdsa2pc::sign"));
      return;
    }

    if (sig_buf.size() == 0) {
      sig_buf.secure_bzero();
      std::fill(msg.begin(), msg.end(), 0);
      cv_.notify_all();
      return;
    }

    signature_der_.bytes.assign(sig_buf.data(), sig_buf.data() + sig_buf.size());
    sig_buf.secure_bzero();

    coinbase::crypto::ecdsa_signature_t parsed;
    rv = parsed.from_der(curve_, coinbase::mem_t(signature_der_.bytes.data(),
                                                static_cast<int>(signature_der_.bytes.size())));
    if (rv) {
      Fail(MapError(rv), FormatError(rv, "ecdsa_signature_t::from_der"));
      return;
    }

    const auto order = curve_.order();
    int coord_size = order.get_bin_size();
    coinbase::buf_t r_bin = parsed.get_r().to_bin(coord_size);
    coinbase::buf_t s_bin = parsed.get_s().to_bin(coord_size);
    signature_raw_.bytes.resize(static_cast<size_t>(coord_size) * 2);
    std::memcpy(signature_raw_.bytes.data(), r_bin.data(), coord_size);
    std::memcpy(signature_raw_.bytes.data() + coord_size, s_bin.data(), coord_size);
    r_bin.secure_bzero();
    s_bin.secure_bzero();

    std::fill(msg.begin(), msg.end(), 0);

    {
      std::lock_guard<std::mutex> guard(result_mutex_);
      signature_ready_ = true;
    }
    cv_.notify_all();
  }

  SignOptions opts_;
  coinbase::crypto::ecurve_t curve_;
  party_t party_;
  key_t key_;
  std::unique_ptr<FiberJob> job_;

  std::mutex message_mutex_;
  std::condition_variable message_cv_;
  std::vector<uint8_t> message_;
  bool message_ready_ = false;

  std::mutex result_mutex_;
  bool signature_ready_ = false;
  BufferOwner signature_der_;
  BufferOwner signature_raw_;
};

class ContextImpl final : public Context {
 public:
  explicit ContextImpl(const InitOptions& opts) : opts_(opts) {}

  std::unique_ptr<DkgSession> CreateDkg(const DkgOptions& opts) override {
    return std::make_unique<DkgSessionImpl>(opts);
  }

  std::unique_ptr<Keypair> ImportKey(const BufferOwner& blob) override {
    mem_t mem(blob.bytes.data(), static_cast<int>(blob.bytes.size()));
    coinbase::converter_t conv(mem);
    KeyBlob stored;
    stored.convert(conv);
    if (conv.get_rv() != SUCCESS)
      throw Error(ErrorCode::InvalidArgument, "invalid key blob");
    if (stored.magic != kKeyBlobMagic || stored.version != kKeyBlobVersion)
      throw Error(ErrorCode::InvalidArgument, "unsupported key blob version");

    auto kind = static_cast<ShareKind>(stored.kind);
    auto scheme = static_cast<Scheme>(stored.scheme);
    auto curve = FromCbCurve(stored.curve);

    key_t key;
    key.role = ToParty(kind);
    key.curve = stored.curve;
    key.Q = stored.Q;
    key.x_share = stored.x_share;
    key.c_key = stored.c_key;
    key.paillier = stored.paillier;

    return std::make_unique<KeypairImpl>(kind, scheme, curve, stored.key_id, std::move(key));
  }

  BufferOwner ExportKey(const Keypair& kp_base) override {
    auto& kp = dynamic_cast<const KeypairImpl&>(kp_base);
    KeyBlob blob;
    blob.scheme = static_cast<uint32_t>(kp.scheme());
    blob.kind = static_cast<uint32_t>(kp.kind());
    blob.key_id = kp.key_id();
    blob.curve = kp.key().curve;
    blob.Q = kp.key().Q;
    blob.x_share = kp.key().x_share;
    blob.c_key = kp.key().c_key;
    blob.paillier = kp.key().paillier;

    coinbase::converter_t calc(true);
    blob.convert(calc);
    std::vector<uint8_t> out(calc.get_offset());
    coinbase::converter_t writer(out.data());
    blob.convert(writer);
    if (writer.get_rv() != SUCCESS)
      throw Error(ErrorCode::General, "failed to serialize key");
    return MakeBuffer(std::move(out));
  }

  PubKey GetPubKey(const Keypair& kp_base) override {
    auto& kp = dynamic_cast<const KeypairImpl&>(kp_base);
    auto compressed = kp.key().Q.to_compressed_bin();
    PubKey pub;
    pub.curve = kp.curve();
    pub.compressed.bytes.assign(compressed.data(), compressed.data() + compressed.size());
    return pub;
  }

  std::unique_ptr<SignSession> CreateSign(const Keypair& kp_base, const SignOptions& opts) override {
    auto& kp = dynamic_cast<const KeypairImpl&>(kp_base);
    return std::make_unique<SignSessionImpl>(kp, opts);
  }

 private:
  InitOptions opts_;
};

}  // namespace

std::unique_ptr<Context> Context::Create(const InitOptions& opts) {
  return std::make_unique<ContextImpl>(opts);
}

Context::~Context() = default;
Keypair::~Keypair() = default;
DkgSession::~DkgSession() = default;
SignSession::~SignSession() = default;

}  // namespace maany::bridge
