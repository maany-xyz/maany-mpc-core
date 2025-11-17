# @maany/mpc-rn-bare

React Native bindings for the Maany MPC core library that download their native payload during installation. The JavaScript/TypeScript surface stays tiny while the iOS static libraries, OpenSSL XCFramework, and Android static libraries are fetched on-demand from release archives.

## Installation

1. Install the packages that depend on the bindings:
   ```bash
   npm install @maany/mpc-rn-bare @maany/mpc-coordinator-rn
   ```
2. Run `npm install` / `yarn install` as usual. The `postinstall` hook executes `npm run fetch:native` which downloads the prebuilt archives described below. Set the environment variables first if you host the artifacts yourself.
3. Generate the iOS project (Expo prebuild or your usual React Native workflow) and run `pod install` once so CocoaPods can see the downloaded binaries.
4. Build your app as usual (`npx expo run:ios`, `npx react-native run-android`, etc.).

## Native artifacts download

The package ships without `ios/dist` or `android/native-libs`. During `postinstall` we run `scripts/fetch-native-artifacts.cjs`, which reads `native-artifacts.json` and downloads archives into the package root.

### Default URLs

If you do not provide overrides, the script downloads from
```
https://github.com/maany-xyz/maany-mpc-core/releases/download/v${version}/maany-mpc-ios-${version}.tar.gz
https://github.com/maany-xyz/maany-mpc-core/releases/download/v${version}/maany-mpc-android-${version}.tar.gz
```
where `${version}` is the version from `package.json`.

Each archive must unpack directly into the package root and contain:
- `ios/dist` with `dist/device`, `dist/simulator`, and `dist/openssl.xcframework`
- `android/native-libs/<abi>` containing `libmaany_mpc_core.a`, `libcbmpc.a`, and `libcrypto.a`

### Environment variables

| Variable | Purpose |
| --- | --- |
| `MAANY_MPC_ARTIFACTS_BASE_URL` | Override the base GitHub releases URL for all artifacts. |
| `MAANY_MPC_IOS_ARCHIVE` | Full URL or filesystem path to the iOS `.tar.gz` archive. Takes precedence over the base URL. |
| `MAANY_MPC_ANDROID_ARCHIVE` | Full URL or path to the Android `.tar.gz` archive. |
| `MAANY_MPC_FORCE_DOWNLOAD=1` | Ignore cached artifacts and redownload everything. |

Archives can be generated locally for testing via:
```bash
(cd bindings/rn && tar -czf /tmp/maany-mpc-ios.tar.gz ios/dist)
(cd bindings/rn && tar -czf /tmp/maany-mpc-android.tar.gz android/native-libs)
MAANY_MPC_IOS_ARCHIVE=/tmp/maany-mpc-ios.tar.gz \
MAANY_MPC_ANDROID_ARCHIVE=/tmp/maany-mpc-android.tar.gz \
npm run fetch:native
```

## iOS configuration

The `MaanyMpc.podspec` loads the static libraries from `ios/dist` and exposes them as vendored libs/frameworks. Add the OpenSSL embed hook once (for plain React Native or managed Expo projects):

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
        MAANY_ROOT="${PODS_ROOT}/../../node_modules/@maany/mpc-rn-bare/ios"
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

Reinstall Pods once (`cd ios && pod install` or `npx expo run:ios`) so the script runs and embeds the framework. Afterwards the build system reuses the generated phase automatically.

## Android configuration

The Gradle module expects the native archives under `android/native-libs/<abi>` (populated by `fetch-native-artifacts.cjs`). The `preBuild` tasks verify that `libmaany_mpc_core.a`, `libcbmpc.a`, and `libcrypto.a` exist for every ABI listed in `supportedAbis`. When present, the CMake file links them into the `maany_mpc_rn` shared library together with fbjni/jsi.

No additional Gradle configuration is required for consumers—`npx react-native run-android` or `npx expo run:android` triggers the check automatically.

## Managed EAS build notes

Managed EAS Build workflows still need a config plugin that injects the Podfile hook above before `pod install`. A minimal plugin looks like this:

```js
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
        fi
        MAANY_ROOT="${PODS_ROOT}/../../node_modules/@maany/mpc-rn-bare/ios"
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
      contents = contents.replace(/post_install do \|installer\|[\s\S]*?end/, (match) => {
        return match.replace(/(post_install do \|installer\|)/, `$1\n${script}`);
      });
      fs.writeFileSync(podfile, contents);
    }
    return cfg;
  }]);
};
```

Add the plugin to `app.json` / `app.config.js`:

```json
{
  "expo": {
    "plugins": [
      "./plugins/withMaanyOpenSSL"
    ]
  }
}
```

With that hook in place EAS downloads the archives during `npm install`, patches the Podfile, and embeds OpenSSL without having to check `ios/` into source control.
