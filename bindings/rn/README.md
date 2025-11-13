
## Installation (Expo / React Native)

1. Install packages:
   ```bash
   npm install @maany/mpc-rn @maany/mpc-coordinator-rn
   ```
2. Generate the iOS project once (if using Expo):
   ```bash
   npx expo prebuild ios
   # or the first
   npx expo run:ios
   ```
3. Add the OpenSSL embed hook in `ios/Podfile`:
   ```ruby
   post_install do |installer|
     installer.aggregate_targets.each do |aggregate|
       next unless aggregate.pod_targets.any? { |t| t.name == 'MaanyMpc' }

       aggregate.user_targets.each do |user_target|
         next if user_target.shell_script_build_phases.any? { |p| p.name == '[Maany] Embed OpenSSL' }

         phase = user_target.new_shell_script_build_phase('[Maany] Embed OpenSSL')
         phase.shell_path = '/bin/sh'
         phase.shell_script = <<~'SH'
           set -euo pipefail
           if [ -z "${FRAMEWORKS_FOLDER_PATH:-}" ]; then
             exit 0
           fi
           MAANY_ROOT="${PODS_ROOT}/../../node_modules/@maany/mpc-rn/ios"
           if [[ "${PLATFORM_NAME}" == "iphonesimulator" ]]; then
             SLICE="ios-arm64_x86_64-simulator"
           else
             SLICE="ios-arm64_arm64e"
           fi
           FRAMEWORK_SRC="${MAANY_ROOT}/dist/openssl.xcframework/${SLICE}/openssl.framework"
           FRAMEWORK_DEST="${TARGET_BUILD_DIR}/${FRAMEWORKS_FOLDER_PATH}/openssl.framework"
           rm -rf "${FRAMEWORK_DEST}"
           mkdir -p "$(dirname "${FRAMEWORK_DEST}")"
           cp -R "${FRAMEWORK_SRC}" "${FRAMEWORK_DEST}"
         SH
       end
     end

     react_native_post_install(installer)
   end
   ```
4. Reinstall Pods once:
   ```bash
   rm -rf ios/Pods ios/Podfile.lock
   npx expo run:ios
   # or
   cd ios && pod install
   ```
   Expo’s `run:ios` already runs `pod install`, so future builds reuse the hook automatically.
5. Build your app:
   ```bash
   npx expo run:ios
   ```
   The OpenSSL framework is now embedded automatically and the Maany coordinator/binding modules load without extra steps.

### Android

The npm package ships prebuilt OpenSSL slices under `bindings/rn/android/third-party/openssl`, so consuming apps do not need any Gradle properties or extra scripts—`npx expo run:android` works out of the box.

#### Refreshing the bundled OpenSSL (maintainers)

If you update OpenSSL or add another ABI, rebuild the archives before publishing:

```bash
ANDROID_NDK_ROOT=/path/to/android-ndk-r26d \
  ABI=arm64-v8a \
  ./scripts/build_android_openssl.sh
```

The script writes into `bindings/rn/android/third-party/openssl/<abi>/{include,libcrypto.a}`. Re-run per ABI, commit the updated artifacts, and the Gradle build will pick them up automatically.

# EAS build logic
For a managed EAS Build we need three pieces:

  1. Custom config plugin (e.g., plugins/withMaanyOpenSSL.js)

     const { withDangerousMod } = require('@expo/config-plugins');
     const fs = require('fs');

     module.exports = function withMaanyOpenSSL(config) {
       return withDangerousMod(config, ['ios', (cfg) => {
         const podfile = `${cfg.modRequest.projectRoot}/ios/Podfile`;
         if (!fs.existsSync(podfile)) return cfg;

         const script = `

  installer.aggregate_targets.each do |aggregate|
  next unless aggregate.pod_targets.any? { |t| t.name == 'MaanyMpc' }

      aggregate.user_targets.each do |user_target|
        next if user_target.shell_script_build_phases.any? { |p| p.name == '[Maany] Embed OpenSSL' }

        phase = user_target.new_shell_script_build_phase('[Maany] Embed OpenSSL')
        phase.shell_path = '/bin/sh'
        phase.shell_script = <<~'SH'
          set -euo pipefail
          if [ -z "${FRAMEWORKS_FOLDER_PATH:-}" ]; then
            exit 0
          end
          MAANY_ROOT="${PODS_ROOT}/../../node_modules/@maany/mpc-rn/ios"
          if [[ "${PLATFORM_NAME}" == "iphonesimulator" ]]; then
            SLICE="ios-arm64_x86_64-simulator"
          else
            SLICE="ios-arm64_arm64e"
          fi
          FRAMEWORK_SRC="${MAANY_ROOT}/dist/openssl.xcframework/${SLICE}/openssl.framework"
          FRAMEWORK_DEST="${TARGET_BUILD_DIR}/${FRAMEWORKS_FOLDER_PATH}/openssl.framework"
          rm -rf "${FRAMEWORK_DEST}"
          mkdir -p "$(dirname "${FRAMEWORK_DEST}")"
          cp -R "${FRAMEWORK_SRC}" "${FRAMEWORK_DEST}"
        SH
      end

  end
  `;
  let contents = fs.readFileSync(podfile, 'utf8');
  if (!contents.includes('[Maany] Embed OpenSSL')) {
  contents = contents.replace(/post_install do |installer|[\s\S]*?end/,
  (match) => match.replace(/(post_install do |installer|)/,
  };


  2. **`app.json` / `app.config.js`**
  ```json
  {
    "expo": {
      ...
      "plugins": [
        "./plugins/withMaanyOpenSSL"
      ]
    }
  }

  3. EAS Build config
     Normal managed eas build --platform ios. During prebuild, Expo runs plugin, which patches the generated Podfile before pod install.

  With that setup, the build server always injects the embed phase automatically, so the OpenSSL framework gets copied and EAS builds succeed without checking ios/ into
  the repo.
