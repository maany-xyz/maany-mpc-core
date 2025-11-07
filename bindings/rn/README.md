
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
