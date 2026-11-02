#include "ui/AppStyle.h"
#include "ui/MainWindow.h"

#include <QApplication>
#include <QGuiApplication>
#include <QIcon>
#include <QMessageBox>
#include <QSettings>

namespace {

// Carry over the library and preferences from the first Qt build, which used
// the Welshtokki organization folder. This runs once, before the database opens.
void migrateLegacyData()
{
    QSettings currentSettings;
    if (currentSettings.allKeys().isEmpty()) {
        QSettings legacySettings(QSettings::NativeFormat, QSettings::UserScope,
                                 QStringLiteral("Welshtokki"),
                                 QStringLiteral("CK3 DNA Manager"));
        const QStringList keys = legacySettings.allKeys();
        for (const QString &key : keys)
            currentSettings.setValue(key, legacySettings.value(key));
    }
}

} // namespace

int main(int argc, char *argv[])
{
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    QApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("CK3 DNA Manager Qt"));
    QCoreApplication::setApplicationName(QStringLiteral("CK3 DNA Manager Qt"));
    QCoreApplication::setApplicationVersion(QStringLiteral(CK3DNA_VERSION));
    QApplication::setApplicationDisplayName(QStringLiteral("CK3 DNA Manager Qt"));

    migrateLegacyData();

    applyAppStyle(application);
    application.setWindowIcon(QIcon(QStringLiteral(":/icons/app.png")));

    MainWindow window;
    QString error;
    if (!window.initialize(&error)) {
        QMessageBox box(QMessageBox::Critical,
                        QStringLiteral("CK3 DNA Manager Qt"),
                        QStringLiteral("The application could not start:\n\n%1").arg(error),
                        QMessageBox::NoButton);
        box.addButton(QStringLiteral("Close"), QMessageBox::AcceptRole);
        box.exec();
        return 1;
    }

    window.show();
    return application.exec();
}
