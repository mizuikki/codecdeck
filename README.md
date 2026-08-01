# CodecDeck

CodecDeck is a Linux desktop control panel for Bluetooth audio codecs on
PipeWire and WirePlumber. It shows the active transport format, switches among
the A2DP profiles exposed by PipeWire, and manages LDAC and LHDC v5 quality
preferences from one interface.

## Current features

- Native PipeWire device and node discovery, without parsing command output
- Active codec, profile, PCM format, sample rate, and channel count
- Inline requested-versus-active codec and sample-rate fallback indicators
- Separate draft, configured, and negotiated sample-rate state
- Codec switching through PipeWire device profile parameters
- LDAC quality modes: Auto, High, Standard, and Mobile
- LHDC v5 quality modes: Auto, 400, 500, 900, and 1000 kbit/s
- Preferred Bluetooth sample rate
- Atomic WirePlumber configuration updates
- WirePlumber restart through the user systemd D-Bus API
- Bluetooth reconnection through the BlueZ system D-Bus API
- Polkit-authorized refresh of stale BlueZ codec capabilities

The displayed bit depth is the negotiated PCM input format. Explicit bit-depth
selection is intentionally not exposed yet because upstream PipeWire does not
provide a stable per-codec setting for it.

See [Architecture](docs/architecture.md) for the runtime data flow and the
bit-depth extension boundary.

## Requirements

- Qt 6.8 or newer with Qt Quick Controls and Kirigami
- PipeWire 1.4 or newer
- WirePlumber 0.5 or newer
- BlueZ 5
- A PipeWire build with the desired Bluetooth codec libraries enabled

On Ubuntu:

```sh
sudo apt install cmake ninja-build extra-cmake-modules \
  qt6-base-dev qt6-declarative-dev qml6-module-qtquick-controls \
  qml6-module-org-kde-kirigami libpipewire-0.3-dev
```

## Build

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build
ctest --test-dir build --output-on-failure
./build/codecdeck
```

## Install

Install the desktop application for the current user and the narrowly scoped
privileged helper system-wide:

```sh
cmake --install build --prefix "$HOME/.local" --component Application
sudo cmake --install build --prefix /usr --component PrivilegedHelper
```

The system component installs the helper executable and its Polkit policy.
Without it, CodecDeck still monitors and configures codecs, but cannot refresh
stale BlueZ capability caches.

## Configuration

CodecDeck writes only this user-owned drop-in:

```text
~/.config/wireplumber/wireplumber.conf.d/51-codecdeck.conf
```

It does not modify distribution files under `/usr` or `/etc`. Applying changes
restarts the current user's `wireplumber.service`, so Bluetooth audio is
briefly interrupted while the transport reconnects.

`Refresh Bluetooth capabilities` is the only privileged operation. Polkit
runs `/usr/libexec/codecdeck-helper`, which validates the selected BlueZ object
path, disconnects that device, backs up its cache under
`/var/backups/codecdeck`, and removes only that cache file. The GUI itself
always remains unprivileged.

## License

[MIT](LICENSE)
