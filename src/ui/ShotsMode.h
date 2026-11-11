#pragma once

class QApplication;

namespace lab::ui {

/**
 * Служебный режим «--shots <папка>»: поочерёдно открывает все модели,
 * прокручивает время (начало / середина / конец эксперимента) и сохраняет
 * снимки окна в PNG. Используется для самопроверки сцен без интерактивного
 * запуска и в ручной QA-проверке сборки.
 */
int runShotsMode(QApplication& app, int argc, char* argv[]);

}  // namespace lab::ui
