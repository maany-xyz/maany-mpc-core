#pragma once
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*============================*
 *  Versioning & ABI
 *============================*/
#define MAANY_MPC_API_VERSION_MAJOR 1
#define MAANY_MPC_API_VERSION_MINOR 0
#define MAANY_MPC_API_VERSION_PATCH 0

typedef struct {
  uint32_t major, minor, patch;
} maany_mpc_version_t;

typedef enum {
  MAANY_MPC_OK = 0,
  MAANY_MPC_ERR_GENERAL = 1,
  MAANY_MPC_ERR_INVALID_ARG = 2,
  MAANY_MPC_ERR_UNSUPPORTED = 3,
  MAANY_MPC_ERR_PROTO_STATE = 4,
  MAANY_MPC_ERR_CRYPTO = 5,
  MAANY_MPC_ERR_RNG = 6,
  MAANY_MPC_ERR_IO = 7,
  MAANY_MPC_ERR_POLICY = 8,
  MAANY_MPC_ERR_MEMORY = 9
} maany_mpc_error_t;

/*============================*
 *  Opaque handles
 *============================*/
typedef struct maany_mpc_ctx_s     maany_mpc_ctx_t;       /* library/global ctx */
typedef struct maany_mpc_kp_s      maany_mpc_keypair_t;   /* device/server share */
typedef struct maany_mpc_dkg_s     maany_mpc_dkg_t;       /* DKG session */
typedef struct maany_mpc_sign_s    maany_mpc_sign_t;      /* Sign session */

/*============================*
 *  Curves & Schemes
 *============================*/
typedef enum {
  MAANY_MPC_CURVE_SECP256K1 = 0,
  MAANY_MPC_CURVE_ED25519   = 1
} maany_mpc_curve_t;

typedef enum {
  MAANY_MPC_SCHEME_ECDSA_2P = 0,   /* 2-of-2 ECDSA */
  MAANY_MPC_SCHEME_ECDSA_TN = 1,   /* t-of-n ECDSA (future) */
  MAANY_MPC_SCHEME_SCHNORR_2P = 2  /* optional */
} maany_mpc_scheme_t;

/*============================*
 *  RNG & Allocators (optional)
 *============================*/
typedef int (*maany_mpc_rng_cb)(uint8_t* out, size_t out_len); /* return 0 on success */

typedef void* (*maany_mpc_malloc_fn)(size_t);
typedef void  (*maany_mpc_free_fn)(void*);
typedef void  (*maany_mpc_secure_zero_fn)(void* p, size_t n);

/*============================*
 *  Buffers
 *============================*/
typedef struct {
  uint8_t* data;
  size_t   len;
} maany_mpc_buf_t;

/* Caller retains ownership unless explicitly told otherwise. For outputs,
 * the library allocates and caller frees via maany_mpc_free().
 */

/*============================*
 *  Logging (optional)
 *============================*/
typedef enum {
  MAANY_MPC_LOG_ERROR = 0,
  MAANY_MPC_LOG_WARN  = 1,
  MAANY_MPC_LOG_INFO  = 2,
  MAANY_MPC_LOG_DEBUG = 3
} maany_mpc_log_level_t;

typedef void (*maany_mpc_log_cb)(maany_mpc_log_level_t level, const char* msg);

/*============================*
 *  Key Identifiers & Public Key
 *============================*/
typedef struct {
  maany_mpc_curve_t curve;
  /* 33 bytes for secp256k1 compressed; 32 for ed25519; generic buffer for forward-compat */
  maany_mpc_buf_t   pubkey;       /* out: allocated by lib */
} maany_mpc_pubkey_t;

typedef struct {
  /* Application-defined 32B key ID (e.g., coordinator-provided). */
  uint8_t bytes[32];
} maany_mpc_key_id_t;

/*============================*
 *  Initialization & teardown
 *============================*/
typedef struct {
  maany_mpc_rng_cb         rng;           /* optional; use internal if NULL */
  maany_mpc_malloc_fn      malloc_fn;     /* optional; default malloc */
  maany_mpc_free_fn        free_fn;       /* optional; default free */
  maany_mpc_secure_zero_fn secure_zero;   /* optional; internal if NULL */
  maany_mpc_log_cb         logger;        /* optional */
} maany_mpc_init_opts_t;

maany_mpc_ctx_t* maany_mpc_init(const maany_mpc_init_opts_t* opts);
void              maany_mpc_shutdown(maany_mpc_ctx_t* ctx);
maany_mpc_version_t maany_mpc_version(void);
const char*       maany_mpc_error_string(maany_mpc_error_t err);

/*============================*
 *  Device/Server share storage
 *============================*/
/* The keypair handle represents the *local share* (device or server).
 * You persist it encrypted outside the library (SDK’s job). On load, you pass
 * the ciphertext to import; the library gives you a handle.
 */

typedef enum {
  MAANY_MPC_SHARE_DEVICE = 0,
  MAANY_MPC_SHARE_SERVER = 1
} maany_mpc_share_kind_t;

typedef struct {
  maany_mpc_share_kind_t kind;
  maany_mpc_curve_t      curve;
  maany_mpc_scheme_t     scheme;
  maany_mpc_key_id_t     key_id;     /* optional; 0ed if unknown */
} maany_mpc_kp_meta_t;

/* Serialize / deserialize local share (opaque bytes) */
maany_mpc_error_t maany_mpc_kp_export(
  maany_mpc_ctx_t* ctx,
  const maany_mpc_keypair_t* kp,
  maany_mpc_buf_t* out_ciphertext /* lib-alloc, caller frees */);

maany_mpc_error_t maany_mpc_kp_import(
  maany_mpc_ctx_t* ctx,
  const maany_mpc_buf_t* in_ciphertext,
  maany_mpc_keypair_t** out_kp /* out handle */);

void maany_mpc_kp_free(maany_mpc_keypair_t* kp);
maany_mpc_error_t maany_mpc_kp_meta(
  maany_mpc_ctx_t* ctx,
  const maany_mpc_keypair_t* kp,
  maany_mpc_kp_meta_t* out_meta);

maany_mpc_error_t maany_mpc_kp_pubkey(
  maany_mpc_ctx_t* ctx,
  const maany_mpc_keypair_t* kp,
  maany_mpc_pubkey_t* out_pub /* pubkey.data allocated by lib */);

void maany_mpc_buf_free(maany_mpc_ctx_t* ctx, maany_mpc_buf_t* buf);

/*============================*
 *  DKG (2-of-2 example)
 *============================*/
/* The DKG is a multi-round message exchange between *this* share and a remote peer.
 * You feed incoming peer messages; the API yields outbound messages until done.
 */

typedef enum {
  MAANY_MPC_STEP_CONTINUE = 0,
  MAANY_MPC_STEP_DONE     = 1
} maany_mpc_step_result_t;

typedef struct {
  maany_mpc_curve_t  curve;
  maany_mpc_scheme_t scheme;      /* MAANY_MPC_SCHEME_ECDSA_2P typically */
  maany_mpc_share_kind_t kind;    /* DEVICE or SERVER */
  maany_mpc_key_id_t key_id_hint; /* optional: coordinator-provided */
  maany_mpc_buf_t    session_id;  /* optional stable SID (e.g., 32B) */
} maany_mpc_dkg_opts_t;

/* Create a DKG session */
maany_mpc_error_t maany_mpc_dkg_new(
  maany_mpc_ctx_t* ctx,
  const maany_mpc_dkg_opts_t* opts,
  maany_mpc_dkg_t** out_dkg);

/* Advance with an optional inbound peer message; produce an outbound message (if any).
 * - in_peer_msg: NULL for first local step (if protocol starts locally)
 * - out_msg: lib-alloc; caller sends to peer; caller frees with maany_mpc_buf_free()
 * - result: CONTINUE until finalization
 */
maany_mpc_error_t maany_mpc_dkg_step(
  maany_mpc_ctx_t* ctx,
  maany_mpc_dkg_t* dkg,
  const maany_mpc_buf_t* in_peer_msg,   /* nullable */
  maany_mpc_buf_t* out_msg,             /* nullable if no msg to send */
  maany_mpc_step_result_t* result);

/* Finalize: materialize the local share handle. */
maany_mpc_error_t maany_mpc_dkg_finalize(
  maany_mpc_ctx_t* ctx,
  maany_mpc_dkg_t* dkg,
  maany_mpc_keypair_t** out_local_share);

void maany_mpc_dkg_free(maany_mpc_dkg_t* dkg);

/*============================*
 *  Signing (2-of-2)
 *============================*/
/* Deterministic sign state machine (nonce derivation inside engine).
 * The message to sign is the canonical sign-bytes prepared by the SDK.
 */

typedef struct {
  maany_mpc_scheme_t scheme;       /* ECDSA_2P typically */
  maany_mpc_buf_t    session_id;   /* optional; bind policy/session */
  maany_mpc_buf_t    extra_aad;    /* optional additional associated data */
} maany_mpc_sign_opts_t;

/* Begin a signing session for a given local share */
maany_mpc_error_t maany_mpc_sign_new(
  maany_mpc_ctx_t* ctx,
  const maany_mpc_keypair_t* kp,
  const maany_mpc_sign_opts_t* opts,
  maany_mpc_sign_t** out_sign);

/* Provide message to sign (entire canonical sign bytes) */
maany_mpc_error_t maany_mpc_sign_set_message(
  maany_mpc_ctx_t* ctx,
  maany_mpc_sign_t* sign,
  const uint8_t* msg,
  size_t msg_len);

/* Advance round with optional inbound peer message; produce outbound */
maany_mpc_error_t maany_mpc_sign_step(
  maany_mpc_ctx_t* ctx,
  maany_mpc_sign_t* sign,
  const maany_mpc_buf_t* in_peer_msg,   /* nullable */
  maany_mpc_buf_t* out_msg,             /* nullable if no msg to send */
  maany_mpc_step_result_t* result);

/* Finalize: recover final signature bytes (DER for ECDSA, 64B for raw if desired) */
typedef enum {
  MAANY_MPC_SIG_FORMAT_DER = 0,
  MAANY_MPC_SIG_FORMAT_RAW_RS = 1  /* r||s 64B for ECDSA */
} maany_mpc_sig_format_t;

maany_mpc_error_t maany_mpc_sign_finalize(
  maany_mpc_ctx_t* ctx,
  maany_mpc_sign_t* sign,
  maany_mpc_sig_format_t fmt,
  maany_mpc_buf_t* out_signature);   /* lib-alloc */

void maany_mpc_sign_free(maany_mpc_sign_t* sign);

/*============================*
 *  Share Refresh (optional)
 *============================*/
typedef struct {
  maany_mpc_buf_t session_id;   /* optional */
} maany_mpc_refresh_opts_t;

/* Similar round-based API; returns an updated local share handle */
maany_mpc_error_t maany_mpc_refresh_new(
  maany_mpc_ctx_t* ctx,
  const maany_mpc_keypair_t* kp,
  const maany_mpc_refresh_opts_t* opts,
  maany_mpc_dkg_t** out_refresh /* reuse dkg struct for refresh flows */);

/* Use dkg_step/dkg_finalize to complete refresh; finalize returns new kp handle. */

/*============================*
 *  Utilities
 *============================*/
void maany_mpc_free(void* p); /* convenience: uses configured free_fn */
void maany_mpc_secure_zero(void* p, size_t n); /* uses configured secure_zero */

#ifdef __cplusplus
} /* extern "C" */
#endif
