#include <QApplication>

#include <cstdio>
#include <cstring>

#include "AppIcon.h"
#include "ui/LabWindow.h"
#include "ui/ShotsMode.h"

/**
 * Точка входа приложения «Физическая лаборатория ЕГЭ (задача №10)».
 * Все вычисления выполняются локально, без обращения к сети.
 */
int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("PhysLabEge"));
    QApplication::setOrganizationName(QStringLiteral("PhysLabEge"));
    QApplication::setApplicationDisplayName(
        QStringLiteral("Физическая лаборатория ЕГЭ · задача №10"));
    QApplication::setWindowIcon(lab::appIcon());

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--shots") == 0)
            return lab::ui::runShotsMode(app, argc, argv);
    }

    lab::ui::LabWindow window;
    window.show();
    return app.exec();
}
