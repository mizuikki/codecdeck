#pragma once

#include <QString>

struct CodecSettings {
    int sampleRate = 96000;
    QString ldacQuality = QStringLiteral("auto");
    QString lhdcQuality = QStringLiteral("auto");

    bool operator==(const CodecSettings &) const = default;
};

class ConfigStore
{
public:
    explicit ConfigStore(QString filePath = {});

    [[nodiscard]] QString filePath() const;
    [[nodiscard]] CodecSettings load() const;
    [[nodiscard]] bool save(const CodecSettings &settings, QString *error = nullptr) const;

    static bool isValid(const CodecSettings &settings);

private:
    QString m_filePath;
};
