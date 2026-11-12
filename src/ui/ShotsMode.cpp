#include "ui/ShotsMode.h"

#include <QApplication>
#include <QDir>
#include <QPixmap>
#include <QThread>
#include <QTimer>

#include <cstdio>

#include "ModelFactory.h"
#include "ui/LabWindow.h"

namespace lab::ui {

int runShotsMode(QApplication& app, int argc, char* argv[]) {
    QString outputDir = QStringLiteral("/tmp");
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--shots") == 0 && i + 1 < argc)
            outputDir = QString::fromLocal8Bit(argv[i + 1]);
    }
    QDir().mkpath(outputDir);

    LabWindow window;
    // Снимки должны быть репрезентативны: задаём типовой размер окна,
    // иначе платформа оставит дефолтный и компоновка сожмёт область сцены.
    window.resize(1440, 860);
    window.show();

    struct ShotJob {
        QString id;
        double fraction;
    };
    QVector<ShotJob> jobs;
    for (const ModelDescriptor& descriptor : allDescriptors()) {
        for (double fraction : {0.0, 0.55, 1.0})
            jobs.push_back({descriptor.id, fraction});
    }

    int failures = 0;
    int index = 0;
    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, [&]() {
        if (index >= jobs.size()) {
            app.quit();
            return;
        }
        const ShotJob& job = jobs[index];
        window.openModel(job.id);
        if (Simulation* model = window.activeModel())
            model->seekTo(model->duration() * job.fraction);
        // Даем сцене перерисоваться в новом размере окна.
        for (int i = 0; i < 4; ++i) {
            QApplication::processEvents();
            QThread::msleep(40);
        }

        const QPixmap pixmap = window.grab();
        const QString file = QStringLiteral("%1/shot_%2_%3.png")
                                 .arg(outputDir, job.id)
                                 .arg(int(job.fraction * 100));
        if (!pixmap.save(file, "PNG")) {
            ++failures;
            std::fprintf(stderr, "Не удалось сохранить снимок %s\n", file.toLocal8Bit().constData());
        }
        ++index;
    });
    timer.start(150);
    return app.exec();
}

}  // namespace lab::ui
