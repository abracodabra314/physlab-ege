#include "ui/ShotsMode.h"

#include <QApplication>
#include <QDir>
#include <QPainter>
#include <QPixmap>
#include <QThread>
#include <QTimer>

#include <cstdio>

#include "ModelFactory.h"
#include "scenes/Style.h"
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
    // иначе платформа оставит дефолтный и компоновка сожмёт сцену.
    window.resize(1440, 860);
    window.show();
    for (int i = 0; i < 6; ++i)
        QApplication::processEvents();

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

        // Отдельно рендерим саму сцену в фиксированном размере: так видно
        // содержимое схемы независимо от того, сколько места осталось макету.
        if (LabScene* scene = window.activeScene()) {
            const QRect viewGeom = window.activeViewGeometry();
            std::fprintf(stderr,
                         "[geom] %s: окно %dx%d, view %dx%d @(%d,%d), viewport %dx%d, "
                         "sceneRect %.0f,%.0f %.0fx%.0f, scale %.3f\n",
                         job.id.toUtf8().constData(), window.width(), window.height(), viewGeom.width(),
                         viewGeom.height(), viewGeom.x(), viewGeom.y(), window.activeViewportSize().width(),
                         window.activeViewportSize().height(), scene->sceneRect().x(),
                         scene->sceneRect().y(), scene->sceneRect().width(), scene->sceneRect().height(),
                         window.activeViewScale());
            std::fprintf(stderr,
                         "[geom2] view.sceneRect %.0f,%.0f %.0fx%.0f | видимая область сцены "
                         "%.0f,%.0f %.0fx%.0f | items %lld\n",
                         window.activeView()->sceneRect().x(), window.activeView()->sceneRect().y(),
                         window.activeView()->sceneRect().width(), window.activeView()->sceneRect().height(),
                         window.activeVisibleSceneRegion().x(), window.activeVisibleSceneRegion().y(),
                         window.activeVisibleSceneRegion().width(), window.activeVisibleSceneRegion().height(),
                         (long long)scene->items().size());
            // Снимок только представления — так видно, что именно показывает view.
            QPixmap viewShot = window.activeView()->grab();
            viewShot.save(QStringLiteral("%1/view_%2_%3.png")
                              .arg(outputDir, job.id)
                              .arg(int(job.fraction * 100)));

            const QRectF area(20.0, 12.0, 1240.0, 620.0);
            scene->requestViewArea(area);
            scene->reconfigure(area);
            QPixmap sceneImage(QSize(1240, 620));
            sceneImage.fill(lab::style::background());
            QPainter painter(&sceneImage);
            scene->render(&painter, QRectF(QPointF(0, 0), QSize(1240, 620)), area);
            painter.end();
            const QString sceneFile = QStringLiteral("%1/scene_%2_%3.png")
                                          .arg(outputDir, job.id)
                                          .arg(int(job.fraction * 100));
            if (!sceneImage.save(sceneFile, "PNG")) {
                ++failures;
                std::fprintf(stderr, "Не удалось сохранить снимок %s\n", sceneFile.toLocal8Bit().constData());
            }
        }
        ++index;
    });
    timer.start(150);
    return app.exec();
}

}  // namespace lab::ui
