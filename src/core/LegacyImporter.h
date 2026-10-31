#pragma once

#include <QString>
#include <QStringList>

#include <functional>

class Database;
class GeneCatalog;

struct LegacyImportResult
{
    bool ok = false;
    bool unsupportedFormat = false;
    bool unsupportedVersion = false;
    bool emptyFile = false;
    QString error;
    int imported = 0;
    int portraits = 0;
    int skipped = 0;
    QStringList warnings;
};

/// Imports both the original Electron app's CK3_DNA_DB.json (with sibling
/// img/*.png) and this app's exported ck3-dna-manager-qt JSON files.
class LegacyImporter
{
public:
    using ProgressCallback = std::function<void(int done, int total)>;

    static LegacyImportResult importFile(const QString &jsonPath,
                                         Database &database,
                                         const GeneCatalog &catalog,
                                         const GeneCatalog &legacyCatalog,
                                         const ProgressCallback &progress = {});
};
