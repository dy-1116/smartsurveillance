// 入口	
// QApplication 初始化，加载样式表，启动主窗口


#include <QApplication>
#include <QFile>
#include <QTextStream>
#include "MainWindow.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("SmartMonitor");
    app.setApplicationVersion("1.0");

    // 加载深色主题QSS
    QFile styleFile(":/resources/style.qss");
    if (styleFile.open(QFile::ReadOnly | QFile::Text)) {
        QTextStream stream(&styleFile);
        app.setStyleSheet(stream.readAll());
        styleFile.close();
    } else {
        // 内嵌基础样式
        app.setStyleSheet(
            "QMainWindow, QWidget { background-color: #0f0f1a; color: #e0e0e0; }"
            "QStatusBar { background-color: #1a1a2e; color: #a0a0a0; }"
            "QListWidget { background-color: #1a1a2e; border: 1px solid #16213e; border-radius: 4px; }"
            "QListWidget::item { padding: 6px; border-bottom: 1px solid #16213e; }"
            "QListWidget::item:selected { background-color: #0f3460; }"
            "QTabWidget::pane { border: 1px solid #16213e; }"
            "QTabBar::tab { background-color: #1a1a2e; padding: 8px 16px; }"
            "QTabBar::tab:selected { background-color: #0f3460; }"
        );
    }

    MainWindow window;
    window.show();

    return app.exec();
}
