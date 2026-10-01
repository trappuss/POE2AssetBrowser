#pragma once
#include <QObject>
#include <QString>

// App-wide export completion notifier (template §15, ported/adapted from D4AssetBrowser). Any export
// path calls ExportNotifier::instance().notify(summary, folder) after writing files; MainWindow
// listens and shows ONE consistent notice — the summary on the status bar plus a "Show in folder"
// button that reveals `folder` — so every export reports the same way instead of a mix of status
// text with no way to get to the files. `folder` is the directory to reveal (empty = no reveal).
//
// D4's glbOptionsLine (a settings-derived "Blender axes, scale x100" suffix) is deliberately NOT
// ported: it is coupled to D4's ModelExporter::Options, and each POE2 export path already knows
// exactly what it wrote and passes its own summary string.
class ExportNotifier : public QObject {
    Q_OBJECT
public:
    static ExportNotifier& instance() { static ExportNotifier n; return n; }
    void notify(const QString& text, const QString& folder = QString()) { emit exported(text, folder); }

signals:
    void exported(const QString& text, const QString& folder);

private:
    ExportNotifier() = default;
};
