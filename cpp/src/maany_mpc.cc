#include "maany_mpc.h"

#include "bridge.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

struct maany_mpc_ctx_s {
  std::unique_ptr<maany::bridge::Context> bridge;
  maany_mpc_malloc_fn malloc_fn;
  maany_mpc_free_fn free_fn;
  maany_mpc_secure_zero_fn secure_zero_fn;
};

struct maany_mpc_dkg_s {
  std::unique_ptr<maany::bridge::DkgSession> session;
  maany_mpc_ctx_t* owner;
};

struct maany_mpc_kp_s {
  std::unique_ptr<maany::bridge::Keypair> keypair;
  maany_mpc_ctx_t* owner;
};

struct maany_mpc_sign_s {
  std::unique_ptr<maany::bridge::SignSession> session;
  maany_mpc_ctx_t* owner;
};

namespace {

using maany::bridge::BufferOwner;
using maany::bridge::Context;
using maany::bridge::DkgOptions;
using maany::bridge::DkgSession;
using maany::bridge::Error;
using maany::bridge::ErrorCode;
using maany::bridge::KeyId;
using maany::bridge::Keypair;
using maany::bridge::PubKey;
using maany::bridge::ShareKind;
using maany::bridge::SignOptions;
using maany::bridge::SignSession;
using maany::bridge::SigFormat;
using maany::bridge::StepOutput;
using maany::bridge::StepState;
using maany::bridge::Scheme;
using maany::bridge::Curve;

void* DefaultMalloc(size_t n) {
  return std::malloc(n);
}

void DefaultFree(void* p) {
  std::free(p);
}

void DefaultSecureZero(void* p, size_t n) {
  if (!p) return;
  volatile unsigned char* vp = static_cast<volatile unsigned char*>(p);
  while (n--) *vp++ = 0;
}

maany_mpc_error_t MapBridgeErrorCode(ErrorCode code) {
  switch (code) {
    case ErrorCode::Ok:
      return MAANY_MPC_OK;
    case ErrorCode::InvalidArgument:
      return MAANY_MPC_ERR_INVALID_ARG;
    case ErrorCode::Unsupported:
      return MAANY_MPC_ERR_UNSUPPORTED;
    case ErrorCode::ProtocolState:
      return MAANY_MPC_ERR_PROTO_STATE;
    case ErrorCode::Crypto:
      return MAANY_MPC_ERR_CRYPTO;
    case ErrorCode::Rng:
      return MAANY_MPC_ERR_RNG;
    case ErrorCode::Io:
      return MAANY_MPC_ERR_IO;
    case ErrorCode::Policy:
      return MAANY_MPC_ERR_POLICY;
    case ErrorCode::Memory:
      return MAANY_MPC_ERR_MEMORY;
    case ErrorCode::General:
    default:
      return MAANY_MPC_ERR_GENERAL;
  }
}

maany_mpc_error_t TranslateException() {
  try {
    throw;
  } catch (const Error& err) {
    return MapBridgeErrorCode(err.code());
  } catch (const std::bad_alloc&) {
    return MAANY_MPC_ERR_MEMORY;
  } catch (...) {
    return MAANY_MPC_ERR_GENERAL;
  }
}

maany_mpc_error_t CopyOutBuffer(maany_mpc_ctx_t* ctx, const std::vector<uint8_t>& src, maany_mpc_buf_t* dst) {
  if (!dst) return MAANY_MPC_OK;
  dst->data = nullptr;
  dst->len = 0;
  if (src.empty()) return MAANY_MPC_OK;

  auto alloc = ctx->malloc_fn ? ctx->malloc_fn : DefaultMalloc;
  uint8_t* ptr = static_cast<uint8_t*>(alloc(src.size()));
  if (!ptr) return MAANY_MPC_ERR_MEMORY;
  std::memcpy(ptr, src.data(), src.size());
  dst->data = ptr;
  dst->len = src.size();
  return MAANY_MPC_OK;
}

std::vector<uint8_t> CopyInBuffer(const maany_mpc_buf_t* buf) {
  if (!buf || buf->len == 0) return {};
  if (!buf->data) throw std::invalid_argument("null buffer data");
  const auto* bytes = static_cast<const uint8_t*>(buf->data);
  return std::vector<uint8_t>(bytes, bytes + buf->len);
}

DkgOptions ConvertDkgOptions(const maany_mpc_dkg_opts_t& opts) {
  DkgOptions o;
  o.curve = static_cast<Curve>(opts.curve);
  o.scheme = static_cast<Scheme>(opts.scheme);
  o.kind = static_cast<ShareKind>(opts.kind);
  std::memcpy(o.key_id.bytes.data(), opts.key_id_hint.bytes, sizeof(opts.key_id_hint.bytes));
  if (opts.session_id.data && opts.session_id.len) {
    o.session_id.bytes.assign(static_cast<const uint8_t*>(opts.session_id.data),
                              static_cast<const uint8_t*>(opts.session_id.data) + opts.session_id.len);
  }
  return o;
}

SignOptions ConvertSignOptions(const maany_mpc_sign_opts_t* opts) {
  SignOptions o;
  if (!opts) return o;
  o.scheme = static_cast<Scheme>(opts->scheme);
  if (opts->session_id.data && opts->session_id.len) {
    o.session_id.bytes.assign(static_cast<const uint8_t*>(opts->session_id.data),
                              static_cast<const uint8_t*>(opts->session_id.data) + opts->session_id.len);
  }
  if (opts->extra_aad.data && opts->extra_aad.len) {
    o.extra_aad.bytes.assign(static_cast<const uint8_t*>(opts->extra_aad.data),
                             static_cast<const uint8_t*>(opts->extra_aad.data) + opts->extra_aad.len);
  }
  return o;
}

maany_mpc_error_t FillMeta(const Keypair& kp, maany_mpc_kp_meta_t* out_meta) {
  if (!out_meta) return MAANY_MPC_ERR_INVALID_ARG;
  out_meta->kind = static_cast<maany_mpc_share_kind_t>(kp.kind());
  out_meta->scheme = static_cast<maany_mpc_scheme_t>(kp.scheme());
  out_meta->curve = static_cast<maany_mpc_curve_t>(kp.curve());
  auto id = kp.key_id();
  std::memcpy(out_meta->key_id.bytes, id.bytes.data(), id.bytes.size());
  return MAANY_MPC_OK;
}

}  // namespace

extern "C" {

maany_mpc_ctx_t* maany_mpc_init(const maany_mpc_init_opts_t* opts) {
  maany_mpc_malloc_fn malloc_fn = (opts && opts->malloc_fn) ? opts->malloc_fn : DefaultMalloc;
  maany_mpc_free_fn free_fn = (opts && opts->free_fn) ? opts->free_fn : DefaultFree;
  maany_mpc_secure_zero_fn zero_fn = (opts && opts->secure_zero) ? opts->secure_zero : DefaultSecureZero;

  void* raw = malloc_fn(sizeof(maany_mpc_ctx_t));
  if (!raw) return nullptr;
  maany_mpc_ctx_t* ctx = new (raw) maany_mpc_ctx_t();
  ctx->bridge = nullptr;
  ctx->malloc_fn = malloc_fn;
  ctx->free_fn = free_fn;
  ctx->secure_zero_fn = zero_fn;

  maany::bridge::InitOptions bridge_opts;
  if (opts && opts->rng) {
    bridge_opts.rng = [cb = opts->rng](uint8_t* out, size_t len) { return cb(out, len); };
  }
  if (opts && opts->secure_zero) {
    bridge_opts.secure_zero = [cb = opts->secure_zero](void* p, size_t n) { cb(p, n); };
  }
  if (opts && opts->malloc_fn) {
    bridge_opts.malloc_fn = [cb = opts->malloc_fn](size_t n) { return cb(n); };
  }
  if (opts && opts->free_fn) {
    bridge_opts.free_fn = [cb = opts->free_fn](void* p) { cb(p); };
  }
  if (opts && opts->logger) {
    bridge_opts.logger = [cb = opts->logger](int level, const std::string& msg) {
      cb(static_cast<maany_mpc_log_level_t>(level), msg.c_str());
    };
  }

  try {
    ctx->bridge = Context::Create(bridge_opts);
  } catch (...) {
    ctx->~maany_mpc_ctx_s();
    free_fn(raw);
    return nullptr;
  }
  return ctx;
}

void maany_mpc_shutdown(maany_mpc_ctx_t* ctx) {
  if (!ctx) return;
  maany_mpc_free_fn free_fn = ctx->free_fn ? ctx->free_fn : DefaultFree;
  ctx->bridge.reset();
  ctx->~maany_mpc_ctx_s();
  free_fn(ctx);
}

maany_mpc_version_t maany_mpc_version(void) {
  maany_mpc_version_t v = {MAANY_MPC_API_VERSION_MAJOR, MAANY_MPC_API_VERSION_MINOR,
                           MAANY_MPC_API_VERSION_PATCH};
  return v;
}

const char* maany_mpc_error_string(maany_mpc_error_t err) {
  switch (err) {
    case MAANY_MPC_OK:
      return "ok";
    case MAANY_MPC_ERR_GENERAL:
      return "general error";
    case MAANY_MPC_ERR_INVALID_ARG:
      return "invalid argument";
    case MAANY_MPC_ERR_UNSUPPORTED:
      return "unsupported";
    case MAANY_MPC_ERR_PROTO_STATE:
      return "protocol state";
    case MAANY_MPC_ERR_CRYPTO:
      return "crypto error";
    case MAANY_MPC_ERR_RNG:
      return "rng failure";
    case MAANY_MPC_ERR_IO:
      return "io";
    case MAANY_MPC_ERR_POLICY:
      return "policy";
    case MAANY_MPC_ERR_MEMORY:
      return "out of memory";
    default:
      return "unknown";
  }
}

maany_mpc_error_t maany_mpc_kp_export(
  maany_mpc_ctx_t* ctx,
  const maany_mpc_keypair_t* kp,
  maany_mpc_buf_t* out_ciphertext) {
  if (!ctx || !ctx->bridge || !kp || !kp->keypair) return MAANY_MPC_ERR_INVALID_ARG;
  if (!out_ciphertext) return MAANY_MPC_ERR_INVALID_ARG;

  try {
    BufferOwner blob = ctx->bridge->ExportKey(*kp->keypair);
    return CopyOutBuffer(ctx, blob.bytes, out_ciphertext);
  } catch (...) {
    return TranslateException();
  }
}

maany_mpc_error_t maany_mpc_kp_import(
  maany_mpc_ctx_t* ctx,
  const maany_mpc_buf_t* in_ciphertext,
  maany_mpc_keypair_t** out_kp) {
  if (!ctx || !ctx->bridge || !out_kp || !in_ciphertext) return MAANY_MPC_ERR_INVALID_ARG;

  std::vector<uint8_t> blob;
  try {
    blob = CopyInBuffer(in_ciphertext);
  } catch (...) {
    return MAANY_MPC_ERR_INVALID_ARG;
  }

  try {
    auto key = ctx->bridge->ImportKey(BufferOwner{std::move(blob)});

    void* raw = ctx->malloc_fn(sizeof(maany_mpc_kp_s));
    if (!raw) return MAANY_MPC_ERR_MEMORY;
    auto* handle = new (raw) maany_mpc_kp_s();
    handle->owner = ctx;
    handle->keypair = std::move(key);
    *out_kp = handle;
    return MAANY_MPC_OK;
  } catch (...) {
    return TranslateException();
  }
}

void maany_mpc_kp_free(maany_mpc_keypair_t* kp) {
  if (!kp) return;
  maany_mpc_ctx_t* owner = kp->owner;
  maany_mpc_free_fn free_fn = owner && owner->free_fn ? owner->free_fn : DefaultFree;
  kp->keypair.reset();
  kp->~maany_mpc_kp_s();
  free_fn(kp);
}

maany_mpc_error_t maany_mpc_kp_meta(
  maany_mpc_ctx_t* ctx,
  const maany_mpc_keypair_t* kp,
  maany_mpc_kp_meta_t* out_meta) {
  if (!ctx || !kp || !kp->keypair || !out_meta) return MAANY_MPC_ERR_INVALID_ARG;
  try {
    return FillMeta(*kp->keypair, out_meta);
  } catch (...) {
    return TranslateException();
  }
}

maany_mpc_error_t maany_mpc_kp_pubkey(
  maany_mpc_ctx_t* ctx,
  const maany_mpc_keypair_t* kp,
  maany_mpc_pubkey_t* out_pub) {
  if (!ctx || !ctx->bridge || !kp || !kp->keypair || !out_pub) return MAANY_MPC_ERR_INVALID_ARG;

  try {
    PubKey pub = ctx->bridge->GetPubKey(*kp->keypair);
    out_pub->curve = static_cast<maany_mpc_curve_t>(pub.curve);
    return CopyOutBuffer(ctx, pub.compressed.bytes, &out_pub->pubkey);
  } catch (...) {
    return TranslateException();
  }
}

void maany_mpc_buf_free(maany_mpc_ctx_t* ctx, maany_mpc_buf_t* buf) {
  if (!ctx || !buf || !buf->data) return;
  auto zero = ctx->secure_zero_fn ? ctx->secure_zero_fn : DefaultSecureZero;
  zero(buf->data, buf->len);
  auto free_fn = ctx->free_fn ? ctx->free_fn : DefaultFree;
  free_fn(buf->data);
  buf->data = nullptr;
  buf->len = 0;
}

maany_mpc_error_t maany_mpc_dkg_new(
  maany_mpc_ctx_t* ctx,
  const maany_mpc_dkg_opts_t* opts,
  maany_mpc_dkg_t** out_dkg) {
  if (!ctx || !ctx->bridge || !opts || !out_dkg) return MAANY_MPC_ERR_INVALID_ARG;

  try {
    DkgOptions bridge_opts = ConvertDkgOptions(*opts);
    auto session = ctx->bridge->CreateDkg(bridge_opts);

    void* raw = ctx->malloc_fn(sizeof(maany_mpc_dkg_t));
    if (!raw) return MAANY_MPC_ERR_MEMORY;
    maany_mpc_dkg_t* handle = new (raw) maany_mpc_dkg_t();
    handle->owner = ctx;
    handle->session = std::move(session);
    *out_dkg = handle;
    return MAANY_MPC_OK;
  } catch (...) {
    return TranslateException();
  }
}

maany_mpc_error_t maany_mpc_dkg_step(
  maany_mpc_ctx_t* ctx,
  maany_mpc_dkg_t* dkg,
  const maany_mpc_buf_t* in_peer_msg,
  maany_mpc_buf_t* out_msg,
  maany_mpc_step_result_t* result) {
  if (!ctx || !dkg || !dkg->session) return MAANY_MPC_ERR_INVALID_ARG;
  if (out_msg) {
    out_msg->data = nullptr;
    out_msg->len = 0;
  }
  if (result) *result = MAANY_MPC_STEP_CONTINUE;

  try {
    std::optional<BufferOwner> inbound;
    if (in_peer_msg && in_peer_msg->len) {
      try {
        inbound = BufferOwner{CopyInBuffer(in_peer_msg)};
      } catch (...) {
        return MAANY_MPC_ERR_INVALID_ARG;
      }
    } else if (in_peer_msg && in_peer_msg->len == 0) {
      inbound = BufferOwner{};
    }

    StepOutput output = dkg->session->Step(inbound);
    if (output.outbound && !out_msg) return MAANY_MPC_ERR_INVALID_ARG;
    if (out_msg && output.outbound) {
      maany_mpc_error_t err = CopyOutBuffer(ctx, output.outbound->bytes, out_msg);
      if (err != MAANY_MPC_OK) return err;
    }
    if (result) *result = static_cast<maany_mpc_step_result_t>(output.state);
    return MAANY_MPC_OK;
  } catch (...) {
    return TranslateException();
  }
}

maany_mpc_error_t maany_mpc_dkg_finalize(
  maany_mpc_ctx_t* ctx,
  maany_mpc_dkg_t* dkg,
  maany_mpc_keypair_t** out_local_share) {
  if (!ctx || !dkg || !dkg->session || !out_local_share) return MAANY_MPC_ERR_INVALID_ARG;

  try {
    auto key = dkg->session->Finalize();
    dkg->session.reset();

    void* raw = ctx->malloc_fn(sizeof(maany_mpc_kp_s));
    if (!raw) return MAANY_MPC_ERR_MEMORY;
    auto* handle = new (raw) maany_mpc_kp_s();
    handle->owner = ctx;
    handle->keypair = std::move(key);
    *out_local_share = handle;
    return MAANY_MPC_OK;
  } catch (...) {
    return TranslateException();
  }
}

void maany_mpc_dkg_free(maany_mpc_dkg_t* dkg) {
  if (!dkg) return;
  maany_mpc_ctx_t* owner = dkg->owner;
  maany_mpc_free_fn free_fn = owner && owner->free_fn ? owner->free_fn : DefaultFree;
  dkg->session.reset();
  dkg->~maany_mpc_dkg_s();
  free_fn(dkg);
}

maany_mpc_error_t maany_mpc_sign_new(
  maany_mpc_ctx_t* ctx,
  const maany_mpc_keypair_t* kp,
  const maany_mpc_sign_opts_t* opts,
  maany_mpc_sign_t** out_sign) {
  if (!ctx || !ctx->bridge || !kp || !kp->keypair || !out_sign) return MAANY_MPC_ERR_INVALID_ARG;

  try {
    SignOptions bridge_opts = ConvertSignOptions(opts);
    auto session = ctx->bridge->CreateSign(*kp->keypair, bridge_opts);

    void* raw = ctx->malloc_fn(sizeof(maany_mpc_sign_s));
    if (!raw) return MAANY_MPC_ERR_MEMORY;
    auto* handle = new (raw) maany_mpc_sign_s();
    handle->owner = ctx;
    handle->session = std::move(session);
    *out_sign = handle;
    return MAANY_MPC_OK;
  } catch (...) {
    return TranslateException();
  }
}

maany_mpc_error_t maany_mpc_sign_set_message(
  maany_mpc_ctx_t* ctx,
  maany_mpc_sign_t* sign,
  const uint8_t* msg,
  size_t msg_len) {
  if (!ctx || !sign || !sign->session || !msg || msg_len == 0) return MAANY_MPC_ERR_INVALID_ARG;

  try {
    sign->session->SetMessage(msg, msg_len);
    return MAANY_MPC_OK;
  } catch (...) {
    return TranslateException();
  }
}

maany_mpc_error_t maany_mpc_sign_step(
  maany_mpc_ctx_t* ctx,
  maany_mpc_sign_t* sign,
  const maany_mpc_buf_t* in_peer_msg,
  maany_mpc_buf_t* out_msg,
  maany_mpc_step_result_t* result) {
  if (!ctx || !sign || !sign->session) return MAANY_MPC_ERR_INVALID_ARG;
  if (out_msg) {
    out_msg->data = nullptr;
    out_msg->len = 0;
  }
  if (result) *result = MAANY_MPC_STEP_CONTINUE;

  try {
    std::optional<BufferOwner> inbound;
    if (in_peer_msg && in_peer_msg->len) {
      try {
        inbound = BufferOwner{CopyInBuffer(in_peer_msg)};
      } catch (...) {
        return MAANY_MPC_ERR_INVALID_ARG;
      }
    } else if (in_peer_msg && in_peer_msg->len == 0) {
      inbound = BufferOwner{};
    }

    StepOutput output = sign->session->Step(inbound);
    if (output.outbound && !out_msg) return MAANY_MPC_ERR_INVALID_ARG;
    if (out_msg && output.outbound) {
      maany_mpc_error_t err = CopyOutBuffer(ctx, output.outbound->bytes, out_msg);
      if (err != MAANY_MPC_OK) return err;
    }
    if (result) *result = static_cast<maany_mpc_step_result_t>(output.state);
    return MAANY_MPC_OK;
  } catch (...) {
    return TranslateException();
  }
}

maany_mpc_error_t maany_mpc_sign_finalize(
  maany_mpc_ctx_t* ctx,
  maany_mpc_sign_t* sign,
  maany_mpc_sig_format_t fmt,
  maany_mpc_buf_t* out_signature) {
  if (!ctx || !sign || !sign->session || !out_signature) return MAANY_MPC_ERR_INVALID_ARG;

  try {
    BufferOwner sig = sign->session->Finalize(static_cast<SigFormat>(fmt));
    maany_mpc_error_t err = CopyOutBuffer(ctx, sig.bytes, out_signature);
    if (err != MAANY_MPC_OK) return err;
    std::fill(sig.bytes.begin(), sig.bytes.end(), 0);
    return MAANY_MPC_OK;
  } catch (...) {
    return TranslateException();
  }
}

void maany_mpc_sign_free(maany_mpc_sign_t* sign) {
  if (!sign) return;
  maany_mpc_ctx_t* owner = sign->owner;
  maany_mpc_free_fn free_fn = owner && owner->free_fn ? owner->free_fn : DefaultFree;
  sign->session.reset();
  sign->~maany_mpc_sign_s();
  free_fn(sign);
}

maany_mpc_error_t maany_mpc_refresh_new(
  maany_mpc_ctx_t* ctx,
  const maany_mpc_keypair_t* kp,
  const maany_mpc_refresh_opts_t* opts,
  maany_mpc_dkg_t** out_refresh) {
  (void)ctx;
  (void)kp;
  (void)opts;
  if (out_refresh) *out_refresh = nullptr;
  return MAANY_MPC_ERR_UNSUPPORTED;
}

void maany_mpc_free(void* p) {
  DefaultFree(p);
}

void maany_mpc_secure_zero(void* p, size_t n) {
  DefaultSecureZero(p, n);
}

}  // extern "C"
