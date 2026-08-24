#pragma once

#include <QHash>
#include <QTranslator>

class AppTranslator final : public QTranslator {
    Q_OBJECT

public:
    explicit AppTranslator(QObject *parent = nullptr);

    QString translate(const char *context, const char *sourceText,
                      const char *disambiguation = nullptr, int n = -1) const override;

private:
    QHash<QString, QString> translations_;
};
