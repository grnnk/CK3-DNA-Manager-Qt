#pragma once

#include <QByteArray>
#include <QSqlDatabase>
#include <QString>
#include <QVector>

struct EntryRecord
{
    QString id;
    QString name;
    QString notes = QStringLiteral("");
    QByteArray dna;
    QByteArray portrait;
    QByteArray thumbnail;
    QString rulerMeta;
    bool favorite = false;
    qint64 createdAt = 0;
    qint64 updatedAt = 0;

    bool hasPortrait() const { return !portrait.isEmpty(); }
};

/// SQLite persistence for DNA entries and their portrait blobs.
class Database
{
public:
    Database() = default;
    ~Database();

    Database(const Database &) = delete;
    Database &operator=(const Database &) = delete;

    bool open(const QString &filePath, QString *error = nullptr);
    void close();
    bool isOpen() const { return m_open; }
    QString filePath() const { return m_filePath; }

    /// Entries ordered by favourite first, then most recently updated.
    QVector<EntryRecord> entries(QString *error = nullptr) const;
    bool loadEntry(const QString &id, EntryRecord *entry, QString *error = nullptr) const;

    /// preserveTimestamps keeps imported createdAt/updatedAt values; normal
    /// new entries still get the current update time.
    bool insert(EntryRecord &entry, QString *error = nullptr,
                bool preserveTimestamps = false);
    bool update(const EntryRecord &entry, QString *error = nullptr);
    bool remove(const QString &id, QString *error = nullptr);

private:
    bool createSchema(QString *error);

    QSqlDatabase m_database;
    QString m_connectionName;
    QString m_filePath;
    bool m_open = false;
};
