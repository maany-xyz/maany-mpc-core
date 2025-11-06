package com.maany.mpc.coordinator;

import android.content.Context;
import android.content.SharedPreferences;
import android.os.Build;
import android.security.keystore.KeyGenParameterSpec;
import android.security.keystore.KeyProperties;
import android.util.Base64;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.biometric.BiometricManager;
import androidx.biometric.BiometricPrompt;
import androidx.core.content.ContextCompat;
import androidx.fragment.app.FragmentActivity;

import com.facebook.react.bridge.Promise;
import com.facebook.react.bridge.ReactApplicationContext;
import com.facebook.react.bridge.ReactContextBaseJavaModule;
import com.facebook.react.bridge.ReactMethod;
import com.facebook.react.bridge.UiThreadUtil;

import java.security.GeneralSecurityException;
import java.security.KeyStore;
import java.security.NoSuchAlgorithmException;
import java.util.Arrays;
import java.util.concurrent.Executor;

import javax.crypto.Cipher;
import javax.crypto.KeyGenerator;
import javax.crypto.SecretKey;
import javax.crypto.spec.GCMParameterSpec;

public class SecureShareStorageModule extends ReactContextBaseJavaModule {
  private static final String MODULE_NAME = "MaanyMpcSecureStorage";
  private static final String KEYSTORE = "AndroidKeyStore";
  private static final String AES_MODE = "AES/GCM/NoPadding";
  private static final int IV_LENGTH = 12;
  private static final String PREFS_NAME = "maany_mpc_secure_storage";
  private static final String PREF_KEY_PREFIX = "share_";

  private final SharedPreferences preferences;
  private final ReactApplicationContext context;

  private Promise pendingPromise;

  public SecureShareStorageModule(ReactApplicationContext reactContext) {
    super(reactContext);
    this.context = reactContext;
    this.preferences = reactContext.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE);
  }

  @NonNull
  @Override
  public String getName() {
    return MODULE_NAME;
  }

  @ReactMethod
  public void saveShare(String keyId, String base64, Promise promise) {
    try {
      byte[] value = Base64.decode(base64, Base64.DEFAULT);
      SecretKey key = getOrCreateKey(aliasFor(keyId));
      Cipher cipher = Cipher.getInstance(AES_MODE);
      cipher.init(Cipher.ENCRYPT_MODE, key);
      byte[] iv = cipher.getIV();
      byte[] ciphertext = cipher.doFinal(value);

      byte[] payload = new byte[IV_LENGTH + ciphertext.length];
      System.arraycopy(iv, 0, payload, 0, IV_LENGTH);
      System.arraycopy(ciphertext, 0, payload, IV_LENGTH, ciphertext.length);

      String encoded = Base64.encodeToString(payload, Base64.NO_WRAP);
      preferences.edit().putString(prefKey(keyId), encoded).apply();
      promise.resolve(null);
    } catch (Exception e) {
      promise.reject("ERR_SAVE_SHARE", "Failed to encrypt share", e);
    }
  }

  @ReactMethod
  public void loadShare(final String keyId, @Nullable final String promptMessage, final Promise promise) {
    final String stored = preferences.getString(prefKey(keyId), null);
    if (stored == null) {
      promise.resolve(null);
      return;
    }
    if (pendingPromise != null) {
      promise.reject("ERR_BUSY", "Another authentication request is already in progress");
      return;
    }
    final FragmentActivity activity = getCurrentActivityAsFragment();
    if (activity == null) {
      promise.reject("ERR_NO_ACTIVITY", "Unable to access current activity");
      return;
    }

    final SecretKey key;
    try {
      key = getExistingKey(aliasFor(keyId));
      if (key == null) {
        promise.resolve(null);
        return;
      }
    } catch (Exception e) {
      promise.reject("ERR_KEYSTORE", "Failed to access keystore", e);
      return;
    }

    final byte[] payload = Base64.decode(stored, Base64.DEFAULT);
    if (payload.length < IV_LENGTH + 1) {
      promise.reject("ERR_INVALID_PAYLOAD", "Encrypted payload too short");
      return;
    }
    final byte[] iv = Arrays.copyOfRange(payload, 0, IV_LENGTH);
    final byte[] encrypted = Arrays.copyOfRange(payload, IV_LENGTH, payload.length);

    final Cipher cipher;
    try {
      cipher = Cipher.getInstance(AES_MODE);
      GCMParameterSpec spec = new GCMParameterSpec(128, iv);
      cipher.init(Cipher.DECRYPT_MODE, key, spec);
    } catch (Exception e) {
      promise.reject("ERR_CIPHER", "Failed to prepare cipher", e);
      return;
    }

    pendingPromise = promise;

    UiThreadUtil.runOnUiThread(new Runnable() {
      @Override
      public void run() {
        Executor executor = ContextCompat.getMainExecutor(context);
        BiometricPrompt.AuthenticationCallback callback = new BiometricPrompt.AuthenticationCallback() {
          @Override
          public void onAuthenticationError(int errorCode, @NonNull CharSequence errString) {
            Promise current = pendingPromise;
            pendingPromise = null;
            if (current != null) {
              current.reject("ERR_AUTH", errString.toString());
            }
          }

          @Override
          public void onAuthenticationSucceeded(@NonNull BiometricPrompt.AuthenticationResult result) {
            Promise current = pendingPromise;
            pendingPromise = null;
            Cipher authCipher = result.getCryptoObject() != null ? result.getCryptoObject().getCipher() : null;
            if (authCipher == null) {
              if (current != null) {
                current.reject("ERR_AUTH", "Missing cryptographic object");
              }
              return;
            }
            try {
              byte[] decrypted = authCipher.doFinal(encrypted);
              String output = Base64.encodeToString(decrypted, Base64.NO_WRAP);
              if (current != null) {
                current.resolve(output);
              }
            } catch (Exception e) {
              if (current != null) {
                current.reject("ERR_DECRYPT", "Failed to decrypt share", e);
              }
            }
          }

          @Override
          public void onAuthenticationFailed() {
            // Let the biometric prompt continue listening.
          }
        };

        BiometricPrompt prompt = new BiometricPrompt(activity, executor, callback);
        BiometricPrompt.PromptInfo.Builder builder = new BiometricPrompt.PromptInfo.Builder()
            .setTitle(promptMessage != null ? promptMessage : "Unlock key share")
            .setSubtitle("Authenticate to continue");
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
          builder.setAllowedAuthenticators(
              BiometricManager.Authenticators.BIOMETRIC_STRONG
                  | BiometricManager.Authenticators.DEVICE_CREDENTIAL);
        } else {
          builder.setNegativeButtonText("Cancel");
          // Deprecated on API 30+, but required for compatibility with older versions.
          setDeviceCredentialAllowedLegacy(builder);
        }
        try {
          prompt.authenticate(builder.build(), new BiometricPrompt.CryptoObject(cipher));
        } catch (Exception e) {
          Promise current = pendingPromise;
          pendingPromise = null;
          if (current != null) {
            current.reject("ERR_PROMPT", "Failed to launch biometric prompt", e);
          }
        }
      }
    });
  }

  @ReactMethod
  public void removeShare(String keyId, Promise promise) {
    preferences.edit().remove(prefKey(keyId)).apply();
    try {
      deleteKey(aliasFor(keyId));
    } catch (Exception e) {
      // ignore to avoid blocking removal if key missing
    }
    promise.resolve(null);
  }

  private String prefKey(String keyId) {
    return PREF_KEY_PREFIX + sanitizeKey(keyId);
  }

  private String aliasFor(String keyId) {
    return "maany_mpc_share_" + sanitizeKey(keyId);
  }

  private String sanitizeKey(String keyId) {
    return keyId.replaceAll("[^A-Za-z0-9_]", "_");
  }

  private SecretKey getOrCreateKey(String alias) throws GeneralSecurityException {
    KeyStore keyStore = KeyStore.getInstance(KEYSTORE);
    try {
      keyStore.load(null);
      KeyStore.Entry entry = keyStore.getEntry(alias, null);
      if (entry instanceof KeyStore.SecretKeyEntry) {
        return ((KeyStore.SecretKeyEntry) entry).getSecretKey();
      }
    } catch (Exception ignored) {
    }

    KeyGenerator keyGenerator = KeyGenerator.getInstance(KeyProperties.KEY_ALGORITHM_AES, KEYSTORE);
    KeyGenParameterSpec.Builder builder = new KeyGenParameterSpec.Builder(
        alias,
        KeyProperties.PURPOSE_ENCRYPT | KeyProperties.PURPOSE_DECRYPT)
        .setBlockModes(KeyProperties.BLOCK_MODE_GCM)
        .setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE)
        .setRandomizedEncryptionRequired(true)
        .setUserAuthenticationRequired(true)
        .setUnlockedDeviceRequired(true);

    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
      builder.setUserAuthenticationParameters(0, KeyProperties.AUTH_BIOMETRIC_STRONG | KeyProperties.AUTH_DEVICE_CREDENTIAL);
    } else {
      builder.setUserAuthenticationValidityDurationSeconds(-1);
    }
    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.N) {
      builder.setInvalidatedByBiometricEnrollment(true);
    }

    keyGenerator.init(builder.build());
    return keyGenerator.generateKey();
  }

  @Nullable
  private SecretKey getExistingKey(String alias) throws GeneralSecurityException {
    KeyStore keyStore = KeyStore.getInstance(KEYSTORE);
    try {
      keyStore.load(null);
      KeyStore.Entry entry = keyStore.getEntry(alias, null);
      if (entry instanceof KeyStore.SecretKeyEntry) {
        return ((KeyStore.SecretKeyEntry) entry).getSecretKey();
      }
    } catch (Exception ignored) {
    }
    return null;
  }

  private void deleteKey(String alias) throws GeneralSecurityException {
    KeyStore keyStore = KeyStore.getInstance(KEYSTORE);
    try {
      keyStore.load(null);
      if (keyStore.containsAlias(alias)) {
        keyStore.deleteEntry(alias);
      }
    } catch (Exception ignored) {
    }
  }

  @SuppressWarnings("deprecation")
  private void setDeviceCredentialAllowedLegacy(BiometricPrompt.PromptInfo.Builder builder) {
    builder.setDeviceCredentialAllowed(true);
  }

  @Nullable
  private FragmentActivity getCurrentActivityAsFragment() {
    if (getCurrentActivity() instanceof FragmentActivity) {
      return (FragmentActivity) getCurrentActivity();
    }
    return null;
  }
}
