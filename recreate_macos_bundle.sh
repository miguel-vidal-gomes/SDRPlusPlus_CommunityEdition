# Remove any existing build directory
rm -rf build

# Create fresh build directory
mkdir build && cd build

cmake -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 .. \
  -DUSE_BUNDLE_DEFAULTS=ON \
  -DCMAKE_BUILD_TYPE=Release \
  -DOPT_BUILD_PLUTOSDR_SOURCE=OFF
make -j8
cd ..

# Create the app bundle using the official script
./make_macos_bundle.sh build ./SDR++CE.app
./SDR++CE.app/Contents/MacOS/sdrpp_ce
