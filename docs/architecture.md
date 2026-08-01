# Architecture

CodecDeck keeps the desktop process unprivileged and separates live graph
control from persistent policy.

```text
Qt Quick / Kirigami
        |
  AppController
    |         |         |
    |         |         +-- BluezDeviceManager --> BlueZ ObjectManager / Device1
    |         +-- ConfigStore --> WirePlumber user drop-in
    |
    +-- PipeWireMonitor --> PipeWire registry and device parameters
              |
              +-- A2DP profile selection

  AppController --> user systemd D-Bus --> wireplumber.service
  AppController --> system D-Bus --> BlueZ Device1.Connect
  AppController --> Polkit --> codecdeck-helper --> one BlueZ cache file
```

## UI layer

`qml/Main.qml` is a single responsive work surface. The `Backend` QML
singleton owns application state and commands. QML does not run external
commands or parse service output.

## Application controller

`AppController` coordinates configuration, service restarts, and profile
selection. After WirePlumber restarts, it retries the requested codec until
the Bluetooth device and its profiles return to the PipeWire registry.
It keeps an unsuccessful codec request visible so the active transport can
show the requested and negotiated values together. Sample-rate preferences
remain unchanged when the transport negotiates a different rate.
When the selected codec cannot represent the saved rate, the selector keeps
that rate visible as unavailable instead of silently displaying another value.
While a reconnect temporarily removes the device and its profiles, controls
retain the requested codec context and do not infer unsupported capabilities
from the missing transport.

The controller keeps editable settings separate from the last configuration
written successfully. The settings controls edit a draft, while active
transport warnings compare negotiated values with the configured values.
Saving promotes the draft to configured state before the service reconnects.

## Bluetooth device inventory

`BluezDeviceManager` observes BlueZ's ObjectManager and Properties signals on
the system D-Bus. It publishes paired devices that advertise the A2DP Audio
Sink service, including devices that are currently disconnected. Connection
and disconnection call `org.bluez.Device1.Connect` and `Disconnect` without a
privileged helper.

`AppController` merges the BlueZ inventory with PipeWire snapshots by BlueZ
object path, with the device address as a compatibility fallback. BlueZ owns
identity and connection state; PipeWire owns profiles and negotiated transport
values. A connection is ready for codec control only after both BlueZ reports
the device connected and PipeWire publishes its A2DP profiles. Device selection
is kept stable by object path when either snapshot changes.

## PipeWire integration

`PipeWireMonitor` uses `pw_thread_loop` and registry proxies directly. It
binds Bluetooth devices and A2DP sink nodes, enumerates `EnumProfile`,
`Profile`, and `Format` parameters, and publishes immutable snapshots to the
Qt event loop. Codec selection writes a `SPA_PARAM_Profile` pod to the device.

The backend never parses localized output from `wpctl`, `pw-cli`, `pactl`, or
`bluetoothctl`.

## Persistent configuration

`ConfigStore` owns one file:

```text
~/.config/wireplumber/wireplumber.conf.d/51-codecdeck.conf
```

It validates all values and uses `QSaveFile` for atomic replacement. The
current schema contains:

```ini
monitor.bluez.properties = {
    bluez5.default.rate = 96000
    bluez5.a2dp.ldac.quality = "auto"
    bluez5.a2dp.lhdc.quality = "auto"
}
```

The service restart uses the user systemd D-Bus API. If stopping WirePlumber
releases the last A2DP transport and BlueZ disconnects the headset,
`AppController` reconnects the saved `org.bluez.Device1` object before it
restores the requested codec. Neither operation needs a root helper.

Refreshing cached codec capabilities is different. BlueZ stores BR/EDR SDP
records in a root-owned cache, so CodecDeck invokes a dedicated helper through
Polkit. The helper accepts only a strict BlueZ object path, resolves the local
adapter address through the BlueZ system D-Bus API with a sysfs fallback,
backs up the exact cache file, and removes it. It cannot accept arbitrary
filesystem paths or execute external commands.

## Codec adapters

The first release keeps the mappings in the controller and QML because they
are small:

- LDAC: `auto`, `hq`, `sq`, and `mq`
- LHDC v5: `auto`, `400k`, `500k`, `900k`, and `1000k`

When a third configurable codec is added, these mappings should move into a
C++ `CodecAdapter` interface that supplies rates, quality modes, validation,
and persistent property names.

## Bit depth

The active PCM format is observable through `SPA_PARAM_Format`. CodecDeck
currently treats it as read-only because PipeWire does not expose one stable
per-codec bit-depth preference:

- LHDC v5 bit depth is selected during A2DP transport negotiation.
- LDAC input format is selected during PipeWire raw-format negotiation.

A complete selector therefore needs a versioned PipeWire codec extension plus
capability reporting. The UI must only offer values supported by both the
local encoder and the remote endpoint, and it must verify the negotiated
format after reconnecting.
