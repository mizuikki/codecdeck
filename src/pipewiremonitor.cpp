#include "pipewiremonitor.h"

#include <QMetaObject>
#include <QSet>

#include <pipewire/pipewire.h>
#include <spa/debug/types.h>
#include <spa/param/audio/raw-utils.h>
#include <spa/param/profile.h>
#include <spa/pod/builder.h>
#include <spa/pod/parser.h>
#include <spa/utils/result.h>

#include <map>
#include <memory>

namespace {
QString property(const spa_dict *props, const char *key)
{
    if (!props)
        return {};
    const char *value = spa_dict_lookup(props, key);
    return value ? QString::fromUtf8(value) : QString{};
}

QString podString(const spa_pod *pod, uint32_t key)
{
    const spa_pod_prop *prop = spa_pod_find_prop(pod, nullptr, key);
    if (!prop)
        return {};
    const char *value = nullptr;
    return spa_pod_get_string(&prop->value, &value) == 0 && value
        ? QString::fromUtf8(value) : QString{};
}

int podInt(const spa_pod *pod, uint32_t key, int fallback = -1)
{
    const spa_pod_prop *prop = spa_pod_find_prop(pod, nullptr, key);
    int32_t value = fallback;
    return prop && spa_pod_get_int(&prop->value, &value) == 0 ? value : fallback;
}

QString normalizedCodec(const QString &profileName, const QString &description)
{
    if (profileName == QStringLiteral("a2dp-sink-ldac"))
        return QStringLiteral("ldac");
    if (profileName == QStringLiteral("a2dp-sink-aac"))
        return QStringLiteral("aac");
    if (profileName == QStringLiteral("a2dp-sink-sbc_xq"))
        return QStringLiteral("sbc_xq");
    if (profileName == QStringLiteral("a2dp-sink-sbc"))
        return QStringLiteral("sbc");
    if (!profileName.startsWith(QStringLiteral("a2dp-sink")))
        return {};

    const qsizetype marker = description.indexOf(QStringLiteral("codec "), 0, Qt::CaseInsensitive);
    if (marker >= 0) {
        QString codec = description.mid(marker + 6);
        codec.remove(QLatin1Char(')'));
        codec = codec.trimmed().toLower();
        if (codec == QStringLiteral("lhdc v5"))
            return QStringLiteral("lhdc_v5");
        return codec.replace(QLatin1Char(' '), QLatin1Char('_'));
    }
    return QStringLiteral("a2dp");
}

QString displayCodec(const QString &codec)
{
    if (codec == QStringLiteral("lhdc_v5"))
        return QStringLiteral("LHDC v5");
    if (codec == QStringLiteral("sbc_xq"))
        return QStringLiteral("SBC-XQ");
    return codec.toUpper();
}
}

class PipeWireMonitorPrivate
{
public:
    struct Profile {
        int index = -1;
        QString name;
        QString description;
        QString codec;
    };

    struct Device {
        PipeWireMonitorPrivate *owner = nullptr;
        uint32_t id = PW_ID_ANY;
        pw_device *proxy = nullptr;
        spa_hook listener{};
        QString address;
        QString bluezPath;
        QString name;
        QString connection;
        int activeProfile = -1;
        std::map<int, Profile> profiles;
    };

    struct Node {
        PipeWireMonitorPrivate *owner = nullptr;
        uint32_t id = PW_ID_ANY;
        pw_node *proxy = nullptr;
        spa_hook listener{};
        QString address;
        QString codec;
        QString profile;
        QString format;
        int rate = 0;
        int channels = 0;
    };

    explicit PipeWireMonitorPrivate(PipeWireMonitor *qptr) : q(qptr) {}

    PipeWireMonitor *q = nullptr;
    pw_thread_loop *loop = nullptr;
    pw_context *context = nullptr;
    pw_core *core = nullptr;
    pw_registry *registry = nullptr;
    spa_hook coreListener{};
    spa_hook registryListener{};
    std::map<uint32_t, std::unique_ptr<Device>> devices;
    std::map<uint32_t, std::unique_ptr<Node>> nodes;
    bool connected = false;

    void publish()
    {
        QVariantList snapshot;
        for (const auto &[id, devicePtr] : devices) {
            const Device &device = *devicePtr;
            QVariantMap entry;
            entry.insert(QStringLiteral("id"), id);
            entry.insert(QStringLiteral("address"), device.address);
            entry.insert(QStringLiteral("bluezPath"), device.bluezPath);
            entry.insert(QStringLiteral("name"), device.name);
            entry.insert(QStringLiteral("connected"), device.connection == QStringLiteral("connected"));

            QVariantList profiles;
            QSet<QString> codecs;
            for (const auto &[profileIndex, profile] : device.profiles) {
                if (profile.codec.isEmpty() || codecs.contains(profile.codec))
                    continue;
                codecs.insert(profile.codec);
                QVariantMap value;
                value.insert(QStringLiteral("index"), profileIndex);
                value.insert(QStringLiteral("name"), profile.name);
                value.insert(QStringLiteral("codec"), profile.codec);
                value.insert(QStringLiteral("label"), displayCodec(profile.codec));
                value.insert(QStringLiteral("description"), profile.description);
                profiles.append(value);
            }
            entry.insert(QStringLiteral("profiles"), profiles);

            for (const auto &[nodeId, nodePtr] : nodes) {
                Q_UNUSED(nodeId)
                const Node &node = *nodePtr;
                if (node.address != device.address)
                    continue;
                entry.insert(QStringLiteral("codec"), node.codec);
                entry.insert(QStringLiteral("profile"), node.profile);
                entry.insert(QStringLiteral("format"), node.format);
                entry.insert(QStringLiteral("rate"), node.rate);
                entry.insert(QStringLiteral("channels"), node.channels);
                break;
            }
            if (!entry.contains(QStringLiteral("codec"))) {
                entry.insert(QStringLiteral("codec"), QString{});
                entry.insert(QStringLiteral("profile"), QString{});
                entry.insert(QStringLiteral("format"), QString{});
                entry.insert(QStringLiteral("rate"), 0);
                entry.insert(QStringLiteral("channels"), 0);
            }
            snapshot.append(entry);
        }

        QMetaObject::invokeMethod(q, [q = q, snapshot] { emit q->devicesChanged(snapshot); },
                                  Qt::QueuedConnection);
    }

    static void onCoreDone(void *data, uint32_t id, int seq)
    {
        Q_UNUSED(id)
        Q_UNUSED(seq)
        static_cast<PipeWireMonitorPrivate *>(data)->publish();
    }

    static void onCoreError(void *data, uint32_t id, int seq, int result, const char *message)
    {
        Q_UNUSED(seq)
        // Parameter enumeration can race with Bluetooth node removal during reconnects.
        // Only an error on the core object means the PipeWire connection itself failed.
        if (id != PW_ID_CORE)
            return;
        auto *self = static_cast<PipeWireMonitorPrivate *>(data);
        const QString error = QStringLiteral("PipeWire error %1: %2")
                                  .arg(result).arg(QString::fromUtf8(message ? message : "unknown"));
        QMetaObject::invokeMethod(self->q, [q = self->q, error] {
            emit q->connectionChanged(false, error);
        }, Qt::QueuedConnection);
    }

    static void onDeviceInfo(void *data, const pw_device_info *info)
    {
        auto *device = static_cast<Device *>(data);
        if (info->props) {
            const QString name = property(info->props, PW_KEY_DEVICE_DESCRIPTION);
            if (!name.isEmpty())
                device->name = name;
            const QString address = property(info->props, "api.bluez5.address");
            if (!address.isEmpty())
                device->address = address;
            const QString bluezPath = property(info->props, "api.bluez5.path");
            if (!bluezPath.isEmpty())
                device->bluezPath = bluezPath;
            const QString connection = property(info->props, "api.bluez5.connection");
            if (!connection.isEmpty())
                device->connection = connection;
        }
        if (info->change_mask & PW_DEVICE_CHANGE_MASK_PARAMS) {
            device->profiles.clear();
            for (uint32_t i = 0; i < info->n_params; ++i) {
                if (info->params[i].id == SPA_PARAM_EnumProfile
                    && (info->params[i].flags & SPA_PARAM_INFO_READ)) {
                    pw_device_enum_params(device->proxy, 1, SPA_PARAM_EnumProfile, 0, UINT32_MAX, nullptr);
                } else if (info->params[i].id == SPA_PARAM_Profile
                           && (info->params[i].flags & SPA_PARAM_INFO_READ)) {
                    pw_device_enum_params(device->proxy, 2, SPA_PARAM_Profile, 0, UINT32_MAX, nullptr);
                }
            }
        }
    }

    static void onDeviceParam(void *data, int seq, uint32_t id, uint32_t index,
                              uint32_t next, const spa_pod *param)
    {
        Q_UNUSED(seq)
        Q_UNUSED(index)
        Q_UNUSED(next)
        auto *device = static_cast<Device *>(data);
        if (!param)
            return;
        if (id == SPA_PARAM_EnumProfile) {
            Profile profile;
            profile.index = podInt(param, SPA_PARAM_PROFILE_index);
            profile.name = podString(param, SPA_PARAM_PROFILE_name);
            profile.description = podString(param, SPA_PARAM_PROFILE_description);
            profile.codec = normalizedCodec(profile.name, profile.description);
            if (profile.index >= 0)
                device->profiles[profile.index] = profile;
        } else if (id == SPA_PARAM_Profile) {
            device->activeProfile = podInt(param, SPA_PARAM_PROFILE_index);
        }
        device->owner->publish();
    }

    static void onNodeInfo(void *data, const pw_node_info *info)
    {
        auto *node = static_cast<Node *>(data);
        if (info->props) {
            const QString address = property(info->props, "api.bluez5.address");
            if (!address.isEmpty())
                node->address = address;
            const QString codec = property(info->props, "api.bluez5.codec");
            if (!codec.isEmpty())
                node->codec = codec;
            const QString profile = property(info->props, "api.bluez5.profile");
            if (!profile.isEmpty())
                node->profile = profile;
        }
        if (info->change_mask & PW_NODE_CHANGE_MASK_PARAMS) {
            for (uint32_t i = 0; i < info->n_params; ++i) {
                if (info->params[i].id == SPA_PARAM_Format
                    && (info->params[i].flags & SPA_PARAM_INFO_READ)) {
                    pw_node_enum_params(node->proxy, 3, SPA_PARAM_Format, 0, UINT32_MAX, nullptr);
                }
            }
        }
    }

    static void onNodeParam(void *data, int seq, uint32_t id, uint32_t index,
                            uint32_t next, const spa_pod *param)
    {
        Q_UNUSED(seq)
        Q_UNUSED(index)
        Q_UNUSED(next)
        auto *node = static_cast<Node *>(data);
        if (id != SPA_PARAM_Format || !param)
            return;
        spa_audio_info_raw info{};
        if (spa_format_audio_raw_parse(param, &info) < 0)
            return;
        const char *formatName = spa_debug_type_find_short_name(spa_type_audio_format, info.format);
        node->format = QString::fromUtf8(formatName ? formatName : "unknown");
        node->rate = static_cast<int>(info.rate);
        node->channels = static_cast<int>(info.channels);
        node->owner->publish();
    }

    static void onGlobal(void *data, uint32_t id, uint32_t permissions,
                         const char *type, uint32_t version, const spa_dict *props)
    {
        Q_UNUSED(permissions)
        Q_UNUSED(version)
        auto *self = static_cast<PipeWireMonitorPrivate *>(data);
        if (!type)
            return;

        if (qstrcmp(type, PW_TYPE_INTERFACE_Device) == 0) {
            if (property(props, PW_KEY_DEVICE_API) != QStringLiteral("bluez5"))
                return;
            auto device = std::make_unique<Device>();
            device->owner = self;
            device->id = id;
            device->address = property(props, "api.bluez5.address");
            device->bluezPath = property(props, "api.bluez5.path");
            device->name = property(props, PW_KEY_DEVICE_DESCRIPTION);
            device->connection = property(props, "api.bluez5.connection");
            device->proxy = static_cast<pw_device *>(pw_registry_bind(
                self->registry, id, PW_TYPE_INTERFACE_Device, PW_VERSION_DEVICE, 0));
            if (!device->proxy)
                return;
            static const pw_device_events events{
                .version = PW_VERSION_DEVICE_EVENTS,
                .info = onDeviceInfo,
                .param = onDeviceParam,
            };
            pw_device_add_listener(device->proxy, &device->listener, &events, device.get());
            self->devices[id] = std::move(device);
        } else if (qstrcmp(type, PW_TYPE_INTERFACE_Node) == 0
                   && (property(props, PW_KEY_MEDIA_CLASS).startsWith(QStringLiteral("Audio/Sink"))
                       || property(props, PW_KEY_NODE_NAME).startsWith(QStringLiteral("bluez_output.")))) {
            auto node = std::make_unique<Node>();
            node->owner = self;
            node->id = id;
            node->address = property(props, "api.bluez5.address");
            node->codec = property(props, "api.bluez5.codec");
            node->profile = property(props, "api.bluez5.profile");
            node->proxy = static_cast<pw_node *>(pw_registry_bind(
                self->registry, id, PW_TYPE_INTERFACE_Node, PW_VERSION_NODE, 0));
            if (!node->proxy)
                return;
            static const pw_node_events events{
                .version = PW_VERSION_NODE_EVENTS,
                .info = onNodeInfo,
                .param = onNodeParam,
            };
            pw_node_add_listener(node->proxy, &node->listener, &events, node.get());
            self->nodes[id] = std::move(node);
        }
        self->publish();
    }

    static void onGlobalRemove(void *data, uint32_t id)
    {
        auto *self = static_cast<PipeWireMonitorPrivate *>(data);
        if (auto it = self->devices.find(id); it != self->devices.end()) {
            spa_hook_remove(&it->second->listener);
            pw_proxy_destroy(reinterpret_cast<pw_proxy *>(it->second->proxy));
            self->devices.erase(it);
        }
        if (auto it = self->nodes.find(id); it != self->nodes.end()) {
            spa_hook_remove(&it->second->listener);
            pw_proxy_destroy(reinterpret_cast<pw_proxy *>(it->second->proxy));
            self->nodes.erase(it);
        }
        self->publish();
    }
};

PipeWireMonitor::PipeWireMonitor(QObject *parent)
    : QObject(parent), d(std::make_unique<PipeWireMonitorPrivate>(this))
{
}

PipeWireMonitor::~PipeWireMonitor()
{
    if (!d->loop)
        return;
    pw_thread_loop_stop(d->loop);
    for (auto &[id, node] : d->nodes) {
        Q_UNUSED(id)
        spa_hook_remove(&node->listener);
        pw_proxy_destroy(reinterpret_cast<pw_proxy *>(node->proxy));
    }
    for (auto &[id, device] : d->devices) {
        Q_UNUSED(id)
        spa_hook_remove(&device->listener);
        pw_proxy_destroy(reinterpret_cast<pw_proxy *>(device->proxy));
    }
    if (d->registry)
        pw_proxy_destroy(reinterpret_cast<pw_proxy *>(d->registry));
    if (d->core)
        pw_core_disconnect(d->core);
    if (d->context)
        pw_context_destroy(d->context);
    pw_thread_loop_destroy(d->loop);
    pw_deinit();
}

bool PipeWireMonitor::start(QString *error)
{
    pw_init(nullptr, nullptr);
    d->loop = pw_thread_loop_new("codecdeck-pipewire", nullptr);
    if (!d->loop) {
        if (error)
            *error = QStringLiteral("Could not create the PipeWire event loop.");
        return false;
    }
    d->context = pw_context_new(pw_thread_loop_get_loop(d->loop), nullptr, 0);
    if (!d->context) {
        if (error)
            *error = QStringLiteral("Could not create the PipeWire context.");
        return false;
    }
    d->core = pw_context_connect(d->context, nullptr, 0);
    if (!d->core) {
        if (error)
            *error = QStringLiteral("Could not connect to PipeWire.");
        return false;
    }

    static const pw_core_events coreEvents{
        .version = PW_VERSION_CORE_EVENTS,
        .done = PipeWireMonitorPrivate::onCoreDone,
        .error = PipeWireMonitorPrivate::onCoreError,
    };
    pw_core_add_listener(d->core, &d->coreListener, &coreEvents, d.get());

    d->registry = pw_core_get_registry(d->core, PW_VERSION_REGISTRY, 0);
    static const pw_registry_events registryEvents{
        .version = PW_VERSION_REGISTRY_EVENTS,
        .global = PipeWireMonitorPrivate::onGlobal,
        .global_remove = PipeWireMonitorPrivate::onGlobalRemove,
    };
    pw_registry_add_listener(d->registry, &d->registryListener, &registryEvents, d.get());

    if (pw_thread_loop_start(d->loop) < 0) {
        if (error)
            *error = QStringLiteral("Could not start the PipeWire event loop.");
        return false;
    }
    d->connected = true;
    emit connectionChanged(true, {});
    return true;
}

void PipeWireMonitor::refresh()
{
    if (!d->core || !d->loop)
        return;
    pw_thread_loop_lock(d->loop);
    for (auto &[id, device] : d->devices) {
        Q_UNUSED(id)
        device->profiles.clear();
        pw_device_enum_params(device->proxy, 1, SPA_PARAM_EnumProfile, 0, UINT32_MAX, nullptr);
        pw_device_enum_params(device->proxy, 2, SPA_PARAM_Profile, 0, UINT32_MAX, nullptr);
    }
    for (auto &[id, node] : d->nodes) {
        Q_UNUSED(id)
        pw_node_enum_params(node->proxy, 3, SPA_PARAM_Format, 0, UINT32_MAX, nullptr);
    }
    pw_core_sync(d->core, PW_ID_CORE, 0);
    pw_thread_loop_unlock(d->loop);
}

bool PipeWireMonitor::selectCodec(const QString &address, const QString &codec, QString *error)
{
    if (!d->loop) {
        if (error)
            *error = QStringLiteral("PipeWire is not connected.");
        return false;
    }

    pw_thread_loop_lock(d->loop);
    PipeWireMonitorPrivate::Device *target = nullptr;
    int profileIndex = -1;
    for (auto &[id, device] : d->devices) {
        Q_UNUSED(id)
        if (device->address != address)
            continue;
        target = device.get();
        for (const auto &[index, profile] : device->profiles) {
            if (profile.codec == codec) {
                profileIndex = index;
                break;
            }
        }
        break;
    }

    if (!target || profileIndex < 0) {
        pw_thread_loop_unlock(d->loop);
        if (error)
            *error = QStringLiteral("The selected codec is not available for this device.");
        return false;
    }

    uint8_t buffer[256];
    spa_pod_builder builder = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
    const spa_pod *param = reinterpret_cast<const spa_pod *>(
        spa_pod_builder_add_object(&builder,
            SPA_TYPE_OBJECT_ParamProfile, SPA_PARAM_Profile,
            SPA_PARAM_PROFILE_index, SPA_POD_Int(profileIndex),
            SPA_PARAM_PROFILE_save, SPA_POD_Bool(true)));
    const int result = pw_device_set_param(target->proxy, SPA_PARAM_Profile, 0, param);
    pw_thread_loop_unlock(d->loop);
    if (result < 0) {
        if (error)
            *error = QStringLiteral("PipeWire rejected the codec profile: %1")
                         .arg(QString::fromUtf8(spa_strerror(result)));
        return false;
    }
    return true;
}
