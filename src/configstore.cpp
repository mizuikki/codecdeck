#include "configstore.h"

#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTextStream>

namespace {
const QList<int> validRates{44100, 48000, 88200, 96000, 192000};
const QStringList validLdacQualities{
    QStringLiteral("auto"), QStringLiteral("hq"), QStringLiteral("sq"), QStringLiteral("mq")};
const QStringList validLhdcQualities{
    QStringLiteral("auto"), QStringLiteral("400k"), QStringLiteral("500k"),
    QStringLiteral("900k"), QStringLiteral("1000k")};

QString capture(const QString &text, const QString &property)
{
    const QRegularExpression expression(
        QStringLiteral("%1\\s*=\\s*\\\"?([A-Za-z0-9]+)\\\"?")
            .arg(QRegularExpression::escape(property)));
    const QRegularExpressionMatch match = expression.match(text);
    return match.hasMatch() ? match.captured(1) : QString{};
}
}

ConfigStore::ConfigStore(QString filePath)
{
    if (filePath.isEmpty()) {
        filePath = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)
            + QStringLiteral("/wireplumber/wireplumber.conf.d/51-codecdeck.conf");
    }
    m_filePath = std::move(filePath);
}

QString ConfigStore::filePath() const
{
    return m_filePath;
}

CodecSettings ConfigStore::load() const
{
    CodecSettings settings;
    QFile file(m_filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return settings;

    const QString text = QString::fromUtf8(file.readAll());
    bool ok = false;
    const int rate = capture(text, QStringLiteral("bluez5.default.rate")).toInt(&ok);
    if (ok && validRates.contains(rate))
        settings.sampleRate = rate;

    const QString ldac = capture(text, QStringLiteral("bluez5.a2dp.ldac.quality"));
    if (validLdacQualities.contains(ldac))
        settings.ldacQuality = ldac;

    const QString lhdc = capture(text, QStringLiteral("bluez5.a2dp.lhdc.quality"));
    if (validLhdcQualities.contains(lhdc))
        settings.lhdcQuality = lhdc;
    return settings;
}

bool ConfigStore::save(const CodecSettings &settings, QString *error) const
{
    if (!isValid(settings)) {
        if (error)
            *error = QStringLiteral("Invalid codec settings.");
        return false;
    }

    const QFileInfo fileInfo(m_filePath);
    if (!QDir().mkpath(fileInfo.absolutePath())) {
        if (error)
            *error = QStringLiteral("Could not create %1.").arg(fileInfo.absolutePath());
        return false;
    }

    QSaveFile file(m_filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error)
            *error = file.errorString();
        return false;
    }

    QTextStream stream(&file);
    stream << "# Managed by CodecDeck. Manual changes may be overwritten.\n"
           << "monitor.bluez.properties = {\n"
           << "    bluez5.default.rate = " << settings.sampleRate << "\n"
           << "    bluez5.a2dp.ldac.quality = \"" << settings.ldacQuality << "\"\n"
           << "    bluez5.a2dp.lhdc.quality = \"" << settings.lhdcQuality << "\"\n"
           << "}\n";

    if (!file.commit()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    return true;
}

bool ConfigStore::isValid(const CodecSettings &settings)
{
    return validRates.contains(settings.sampleRate)
        && validLdacQualities.contains(settings.ldacQuality)
        && validLhdcQualities.contains(settings.lhdcQuality);
}
