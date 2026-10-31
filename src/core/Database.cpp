#include "core/Database.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QUuid>

namespace {

void assignError(QString *error, const QString &context, const QSqlError &sqlError)
{
    if (!error)
        return;
    const QString detail = sqlError.text().isEmpty()
        ? QStringLiteral("unknown database error")
        : sqlError.text();
    *error = QStringLiteral("%1: %2").arg(context, detail);
}

QString newId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

/// SQLite treats a null QString as SQL NULL, but the schema requires notes to be
/// non-null. Normalise here so every caller can leave the field untouched.
QString nonNullText(const QString &text)
{
    return text.isNull() ? QStringLiteral("") : text;
}

} // namespace

Database::~Database()
{
    close();
}

bool Database::open(const QString &filePath, QString *error)
{
    close();

    const QFileInfo info(filePath);
    if (!QDir().mkpath(info.absolutePath())) {
        if (error)
            *error = QStringLiteral("Cannot create data folder: %1").arg(info.absolutePath());
        return false;
    }

    m_connectionName = QStringLiteral("ck3dna_%1")
                           .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    m_database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    m_database.setDatabaseName(filePath);

    if (!m_database.open()) {
        assignError(error, QStringLiteral("Cannot open the DNA database"), m_database.lastError());
        QSqlDatabase::removeDatabase(m_connectionName);
        m_connectionName.clear();
        return false;
    }

    m_filePath = filePath;
    m_open = true;

    QSqlQuery pragma(m_database);
    pragma.exec(QStringLiteral("PRAGMA journal_mode=WAL"));
    pragma.exec(QStringLiteral("PRAGMA synchronous=NORMAL"));
    pragma.exec(QStringLiteral("PRAGMA foreign_keys=ON"));

    if (!createSchema(error)) {
        close();
        return false;
    }

    if (error)
        error->clear();
    return true;
}

bool Database::createSchema(QString *error)
{
    QSqlQuery query(m_database);
    const QString sql = QStringLiteral(R"(
        CREATE TABLE IF NOT EXISTS entries (
            id          TEXT PRIMARY KEY,
            name        TEXT NOT NULL,
            notes       TEXT NOT NULL DEFAULT '',
            dna         BLOB NOT NULL,
            portrait    BLOB,
            thumbnail   BLOB,
            favorite    INTEGER NOT NULL DEFAULT 0,
            created_at  INTEGER NOT NULL,
            updated_at  INTEGER NOT NULL
        );
    )");
    if (!query.exec(sql)) {
        assignError(error, QStringLiteral("Cannot create the entries table"), query.lastError());
        return false;
    }

    bool hasRulerMeta = false;
    if (!query.exec(QStringLiteral("PRAGMA table_info(entries)"))) {
        assignError(error, QStringLiteral("Cannot inspect the entries table"), query.lastError());
        return false;
    }
    while (query.next()) {
        if (query.value(1).toString() == QStringLiteral("ruler_meta")) {
            hasRulerMeta = true;
            break;
        }
    }
    if (!hasRulerMeta) {
        if (!query.exec(QStringLiteral(
                "ALTER TABLE entries ADD COLUMN ruler_meta TEXT NOT NULL DEFAULT ''"))) {
            assignError(error, QStringLiteral("Cannot upgrade the entries table"),
                        query.lastError());
            return false;
        }
    }

    if (!query.exec(QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_entries_updated ON entries(updated_at DESC)"))) {
        assignError(error, QStringLiteral("Cannot create the entries index"), query.lastError());
        return false;
    }

    return true;
}

void Database::close()
{
    if (m_database.isValid()) {
        if (m_database.isOpen())
            m_database.close();
        m_database = QSqlDatabase();
    }
    if (!m_connectionName.isEmpty()) {
        QSqlDatabase::removeDatabase(m_connectionName);
        m_connectionName.clear();
    }
    m_open = false;
}

QVector<EntryRecord> Database::entries(QString *error) const
{
    QVector<EntryRecord> result;
    if (!m_open) {
        if (error)
            *error = QStringLiteral("The DNA database is not open.");
        return result;
    }

    QSqlQuery query(m_database);
    const QString sql = QStringLiteral(
        "SELECT id, name, notes, dna, thumbnail, favorite, created_at, updated_at, ruler_meta "
        "FROM entries ORDER BY favorite DESC, updated_at DESC");
    if (!query.exec(sql)) {
        assignError(error, QStringLiteral("Cannot load DNA entries"), query.lastError());
        return result;
    }

    while (query.next()) {
        EntryRecord entry;
        entry.id = query.value(0).toString();
        entry.name = query.value(1).toString();
        entry.notes = query.value(2).toString();
        entry.dna = query.value(3).toByteArray();
        entry.thumbnail = query.value(4).toByteArray();
        entry.favorite = query.value(5).toInt() != 0;
        entry.createdAt = query.value(6).toLongLong();
        entry.updatedAt = query.value(7).toLongLong();
        entry.rulerMeta = query.value(8).toString();
        result.append(std::move(entry));
    }

    if (error)
        error->clear();
    return result;
}

bool Database::loadEntry(const QString &id, EntryRecord *entry, QString *error) const
{
    if (!entry) {
        if (error)
            *error = QStringLiteral("No entry target was supplied.");
        return false;
    }
    if (!m_open) {
        if (error)
            *error = QStringLiteral("The DNA database is not open.");
        return false;
    }

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT id, name, notes, dna, portrait, thumbnail, favorite, created_at, updated_at, "
        "ruler_meta FROM entries WHERE id = :id"));
    query.bindValue(QStringLiteral(":id"), id);

    if (!query.exec()) {
        assignError(error, QStringLiteral("Cannot load the DNA entry"), query.lastError());
        return false;
    }
    if (!query.next()) {
        if (error)
            *error = QStringLiteral("The DNA entry no longer exists.");
        return false;
    }

    entry->id = query.value(0).toString();
    entry->name = query.value(1).toString();
    entry->notes = query.value(2).toString();
    entry->dna = query.value(3).toByteArray();
    entry->portrait = query.value(4).toByteArray();
    entry->thumbnail = query.value(5).toByteArray();
    entry->favorite = query.value(6).toInt() != 0;
    entry->createdAt = query.value(7).toLongLong();
    entry->updatedAt = query.value(8).toLongLong();
    entry->rulerMeta = query.value(9).toString();

    if (error)
        error->clear();
    return true;
}

bool Database::insert(EntryRecord &entry, QString *error, bool preserveTimestamps)
{
    if (!m_open) {
        if (error)
            *error = QStringLiteral("The DNA database is not open.");
        return false;
    }
    if (entry.dna.isEmpty()) {
        if (error)
            *error = QStringLiteral("Cannot save an empty DNA payload.");
        return false;
    }

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (entry.id.isEmpty())
        entry.id = newId();
    if (entry.name.isEmpty())
        entry.name = QStringLiteral("Untitled");
    entry.notes = nonNullText(entry.notes);
    if (entry.createdAt <= 0)
        entry.createdAt = now;
    if (!preserveTimestamps || entry.updatedAt <= 0)
        entry.updatedAt = now;

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT INTO entries (id, name, notes, dna, portrait, thumbnail, favorite, created_at, updated_at, ruler_meta) "
        "VALUES (:id, :name, :notes, :dna, :portrait, :thumbnail, :favorite, :created_at, :updated_at, :ruler_meta)"));
    query.bindValue(QStringLiteral(":id"), entry.id);
    query.bindValue(QStringLiteral(":name"), entry.name);
    query.bindValue(QStringLiteral(":notes"), entry.notes);
    query.bindValue(QStringLiteral(":dna"), entry.dna);
    query.bindValue(QStringLiteral(":portrait"), entry.portrait);
    query.bindValue(QStringLiteral(":thumbnail"), entry.thumbnail);
    query.bindValue(QStringLiteral(":favorite"), entry.favorite ? 1 : 0);
    query.bindValue(QStringLiteral(":created_at"), entry.createdAt);
    query.bindValue(QStringLiteral(":updated_at"), entry.updatedAt);
    query.bindValue(QStringLiteral(":ruler_meta"), nonNullText(entry.rulerMeta));

    if (!query.exec()) {
        assignError(error, QStringLiteral("Cannot insert the DNA entry"), query.lastError());
        return false;
    }

    if (error)
        error->clear();
    return true;
}

bool Database::update(const EntryRecord &entry, QString *error)
{
    if (!m_open) {
        if (error)
            *error = QStringLiteral("The DNA database is not open.");
        return false;
    }

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "UPDATE entries SET name = :name, notes = :notes, dna = :dna, portrait = :portrait, "
        "thumbnail = :thumbnail, favorite = :favorite, updated_at = :updated_at, "
        "ruler_meta = :ruler_meta WHERE id = :id"));
    query.bindValue(QStringLiteral(":name"), entry.name);
    query.bindValue(QStringLiteral(":notes"), nonNullText(entry.notes));
    query.bindValue(QStringLiteral(":dna"), entry.dna);
    query.bindValue(QStringLiteral(":portrait"), entry.portrait);
    query.bindValue(QStringLiteral(":thumbnail"), entry.thumbnail);
    query.bindValue(QStringLiteral(":favorite"), entry.favorite ? 1 : 0);
    query.bindValue(QStringLiteral(":updated_at"), QDateTime::currentMSecsSinceEpoch());
    query.bindValue(QStringLiteral(":ruler_meta"), nonNullText(entry.rulerMeta));
    query.bindValue(QStringLiteral(":id"), entry.id);

    if (!query.exec()) {
        assignError(error, QStringLiteral("Cannot update the DNA entry"), query.lastError());
        return false;
    }
    if (query.numRowsAffected() == 0) {
        if (error)
            *error = QStringLiteral("The DNA entry no longer exists.");
        return false;
    }

    if (error)
        error->clear();
    return true;
}

bool Database::remove(const QString &id, QString *error)
{
    if (!m_open) {
        if (error)
            *error = QStringLiteral("The DNA database is not open.");
        return false;
    }

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("DELETE FROM entries WHERE id = :id"));
    query.bindValue(QStringLiteral(":id"), id);
    if (!query.exec()) {
        assignError(error, QStringLiteral("Cannot delete the DNA entry"), query.lastError());
        return false;
    }

    if (error)
        error->clear();
    return true;
}
