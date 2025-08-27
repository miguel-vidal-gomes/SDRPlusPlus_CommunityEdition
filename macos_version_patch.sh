#!/bin/bash

# This script updates the macOS jobs in the GitHub Actions workflow to properly inject version information

# First, update the checkout action to fetch full history with tags
sed -i '' 's/    build_macos_intel:\n        runs-on: macos-13\n\n        steps:\n        - uses: actions\/checkout@v4/    build_macos_intel:\n        runs-on: macos-13\n\n        steps:\n        - uses: actions\/checkout@v4\n          with:\n            fetch-depth: 0\n            fetch-tags: true/g' .github/workflows/build_all.yml
sed -i '' 's/    build_macos_arm:\n        runs-on: macos-14\n\n        steps:\n        - uses: actions\/checkout@v4/    build_macos_arm:\n        runs-on: macos-14\n\n        steps:\n        - uses: actions\/checkout@v4\n          with:\n            fetch-depth: 0\n            fetch-tags: true/g' .github/workflows/build_all.yml

# Replace the version computation in macOS Intel job
sed -i '' 's/        - name: Update Version Information\n          run: |/        - name: Compute nightly build version\n          shell: bash\n          run: |/g' .github/workflows/build_all.yml

# Add date and SHA to version string for macOS Intel job
sed -i '' 's/              # Master branch builds get -nightly suffix\n              VERSION_STR="${VERSION_BASE}-${VERSION_SUFFIX}-nightly"/              # Master branch builds get nightly+date+sha suffix\n              DATE_UTC=$(date -u +%Y-%m-%d)\n              SHORT_SHA=${GITHUB_SHA::7}\n              VERSION_STR="${VERSION_BASE}-${VERSION_SUFFIX}-nightly+${DATE_UTC}+${SHORT_SHA}"\n              BUILD_VERSION="v${VERSION_STR}"/g' .github/workflows/build_all.yml

# Add BUILD_VERSION for other branch types in macOS Intel job
sed -i '' 's/              VERSION_STR="${GITHUB_REF#refs\/tags\/}"/              VERSION_STR="${GITHUB_REF#refs\/tags\/}"\n              BUILD_VERSION="${VERSION_STR}"/g' .github/workflows/build_all.yml
sed -i '' 's/              VERSION_STR="${VERSION_BASE}-${VERSION_SUFFIX}-pr.${PR_NUM}"/              VERSION_STR="${VERSION_BASE}-${VERSION_SUFFIX}-pr.${PR_NUM}+${DATE_UTC}+${SHORT_SHA}"\n              BUILD_VERSION="v${VERSION_STR}"/g' .github/workflows/build_all.yml
sed -i '' 's/              VERSION_STR="${VERSION_BASE}-${VERSION_SUFFIX}-branch.${BRANCH}"/              VERSION_STR="${VERSION_BASE}-${VERSION_SUFFIX}-branch.${BRANCH}+${DATE_UTC}+${SHORT_SHA}"\n              BUILD_VERSION="v${VERSION_STR}"/g' .github/workflows/build_all.yml

# Add BUILD_VERSION to environment in macOS Intel job
sed -i '' 's/            echo "VERSION_STR=$VERSION_STR" >> $GITHUB_ENV\n            echo "Setting version to: $VERSION_STR"/            echo "VERSION_STR=$VERSION_STR" >> $GITHUB_ENV\n            echo "BUILD_VERSION=$BUILD_VERSION" >> $GITHUB_ENV\n            echo "Setting version to: $VERSION_STR"\n            echo "Setting build version to: $BUILD_VERSION"/g' .github/workflows/build_all.yml

# Remove version.h update in macOS Intel job
sed -i '' 's/            # Update version.h\n            sed -i '"'"'"'"'"' "s\/^#define VERSION_STR.*\/#define VERSION_STR \\"$VERSION_STR\\"\//" core\/src\/version.h\n            //g' .github/workflows/build_all.yml

# Add BUILD_VERSION to CMake configure step in macOS Intel job
sed -i '' 's/          run: cmake -DCMAKE_OSX_DEPLOYMENT_TARGET=10.15 $GITHUB_WORKSPACE -DOPT_BUILD_PLUTOSDR_SOURCE=ON -DOPT_BUILD_BLADERF_SOURCE=ON -DOPT_BUILD_SDRPLAY_SOURCE=ON -DOPT_BUILD_LIMESDR_SOURCE=ON -DOPT_BUILD_AUDIO_SINK=ON -DOPT_BUILD_PORTAUDIO_SINK=OFF -DOPT_BUILD_NEW_PORTAUDIO_SINK=OFF -DOPT_BUILD_M17_DECODER=ON -DOPT_BUILD_PERSEUS_SOURCE=ON -DOPT_BUILD_AUDIO_SOURCE=OFF -DOPT_BUILD_RFNM_SOURCE=ON -DOPT_BUILD_FOBOSSDR_SOURCE=ON -DUSE_BUNDLE_DEFAULTS=ON -DCMAKE_BUILD_TYPE=Release/          run: cmake -DCMAKE_OSX_DEPLOYMENT_TARGET=10.15 $GITHUB_WORKSPACE -DOPT_BUILD_PLUTOSDR_SOURCE=ON -DOPT_BUILD_BLADERF_SOURCE=ON -DOPT_BUILD_SDRPLAY_SOURCE=ON -DOPT_BUILD_LIMESDR_SOURCE=ON -DOPT_BUILD_AUDIO_SINK=ON -DOPT_BUILD_PORTAUDIO_SINK=OFF -DOPT_BUILD_NEW_PORTAUDIO_SINK=OFF -DOPT_BUILD_M17_DECODER=ON -DOPT_BUILD_PERSEUS_SOURCE=ON -DOPT_BUILD_AUDIO_SOURCE=OFF -DOPT_BUILD_RFNM_SOURCE=ON -DOPT_BUILD_FOBOSSDR_SOURCE=ON -DUSE_BUNDLE_DEFAULTS=ON -DCMAKE_BUILD_TYPE=Release\n          env:\n            BUILD_VERSION: ${{ env.BUILD_VERSION }}/g' .github/workflows/build_all.yml

# Add Info.plist version stamping after bundle creation in macOS Intel job
sed -i '' 's/          run: cd $GITHUB_WORKSPACE && sh make_macos_bundle.sh ${{runner.workspace}}\/build .\/SDR++CE.app/          run: cd $GITHUB_WORKSPACE && sh make_macos_bundle.sh ${{runner.workspace}}\/build .\/SDR++CE.app\n\n        - name: Stamp Info.plist with version\n          run: |\n            plutil -replace CFBundleShortVersionString -string "${{ env.VERSION_STR }}" "SDR++CE.app\/Contents\/Info.plist"\n            plutil -replace CFBundleVersion           -string "${{ env.VERSION_STR }}" "SDR++CE.app\/Contents\/Info.plist"/g' .github/workflows/build_all.yml

# Do the same for the macOS ARM job
# Replace the version computation in macOS ARM job
sed -i '' 's/        - name: Update Version Information\n          run: |/        - name: Compute nightly build version\n          shell: bash\n          run: |/g' .github/workflows/build_all.yml

# Add BUILD_VERSION to CMake configure step in macOS ARM job
sed -i '' 's/          run: cmake -DCMAKE_OSX_DEPLOYMENT_TARGET=10.15 $GITHUB_WORKSPACE -DOPT_BUILD_PLUTOSDR_SOURCE=ON -DOPT_BUILD_BLADERF_SOURCE=ON -DOPT_BUILD_SDRPLAY_SOURCE=ON -DOPT_BUILD_LIMESDR_SOURCE=ON -DOPT_BUILD_AUDIO_SINK=ON -DOPT_BUILD_PORTAUDIO_SINK=OFF -DOPT_BUILD_NEW_PORTAUDIO_SINK=OFF -DOPT_BUILD_M17_DECODER=OFF -DOPT_BUILD_PERSEUS_SOURCE=OFF -DOPT_BUILD_AUDIO_SOURCE=OFF -DOPT_BUILD_RFNM_SOURCE=ON -DOPT_BUILD_FOBOSSDR_SOURCE=ON -DUSE_BUNDLE_DEFAULTS=ON -DCMAKE_BUILD_TYPE=Release/          run: cmake -DCMAKE_OSX_DEPLOYMENT_TARGET=10.15 $GITHUB_WORKSPACE -DOPT_BUILD_PLUTOSDR_SOURCE=ON -DOPT_BUILD_BLADERF_SOURCE=ON -DOPT_BUILD_SDRPLAY_SOURCE=ON -DOPT_BUILD_LIMESDR_SOURCE=ON -DOPT_BUILD_AUDIO_SINK=ON -DOPT_BUILD_PORTAUDIO_SINK=OFF -DOPT_BUILD_NEW_PORTAUDIO_SINK=OFF -DOPT_BUILD_M17_DECODER=OFF -DOPT_BUILD_PERSEUS_SOURCE=OFF -DOPT_BUILD_AUDIO_SOURCE=OFF -DOPT_BUILD_RFNM_SOURCE=ON -DOPT_BUILD_FOBOSSDR_SOURCE=ON -DUSE_BUNDLE_DEFAULTS=ON -DCMAKE_BUILD_TYPE=Release\n          env:\n            BUILD_VERSION: ${{ env.BUILD_VERSION }}/g' .github/workflows/build_all.yml

# Add Info.plist version stamping after bundle creation in macOS ARM job
sed -i '' 's/          run: cd $GITHUB_WORKSPACE && sh make_macos_bundle.sh ${{runner.workspace}}\/build .\/SDR++CE.app/          run: cd $GITHUB_WORKSPACE && sh make_macos_bundle.sh ${{runner.workspace}}\/build .\/SDR++CE.app\n\n        - name: Stamp Info.plist with version\n          run: |\n            plutil -replace CFBundleShortVersionString -string "${{ env.VERSION_STR }}" "SDR++CE.app\/Contents\/Info.plist"\n            plutil -replace CFBundleVersion           -string "${{ env.VERSION_STR }}" "SDR++CE.app\/Contents\/Info.plist"/g' .github/workflows/build_all.yml

# Update the Windows checkout action too
sed -i '' 's/    build_windows:\n        runs-on: windows-latest\n\n        steps:\n        - uses: actions\/checkout@v4/    build_windows:\n        runs-on: windows-latest\n\n        steps:\n        - uses: actions\/checkout@v4\n          with:\n            fetch-depth: 0\n            fetch-tags: true/g' .github/workflows/build_all.yml

# Update the checkout action in the update_nightly_release job
sed -i '' 's/         - uses: actions\/checkout@v4/         - uses: actions\/checkout@v4\n           with:\n             fetch-depth: 0\n             fetch-tags: true/g' .github/workflows/build_all.yml

