// Verify SearchableCombo fixes the reported bug: a click on the field opens the full dropdown (the
// stock editable combo only opened from the arrow → "nothing drops down"), and typing filters the list
// while leaving selection read-back via currentData() intact.
#include "app/SearchableCombo.h"
#include <QApplication>
#include <QListView>
#include <QMouseEvent>
#include <QPointF>
#include <cstdio>

// Synthesize a left-button press+release on a widget (drives the same eventFilter path a real click
// would, without QtTest's qmake-only input helpers).
static void clickWidget(QWidget* w)
{
    const QPointF c = QPointF(w->rect().center());
    QMouseEvent press(QEvent::MouseButtonPress, c, w->mapToGlobal(c.toPoint()),
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(w, &press);
    QMouseEvent rel(QEvent::MouseButtonRelease, c, w->mapToGlobal(c.toPoint()),
                    Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(w, &rel);
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    SearchableCombo cb;
    for (int i = 0; i < 200; ++i) cb.addItem(QStringLiteral("Item %1 name").arg(i), QStringLiteral("smd/%1").arg(i));
    cb.setCurrentIndex(5);
    cb.resize(240, 28);
    cb.show();
    app.processEvents();

    int fails = 0;
    auto req = [&](bool ok, const char* m){ if (!ok) { printf("FAIL: %s\n", m); ++fails; } };

    // 1. Popup is closed initially.
    req(!cb.view()->isVisible(), "popup should start closed");

    // 2. A left-click on the line-edit opens the full dropdown.
    clickWidget(cb.lineEdit());
    app.processEvents();
    req(cb.view()->isVisible(), "click on the field did not open the dropdown");
    printf("after click: popup visible=%d, item count in view=%d\n",
           cb.view()->isVisible(), cb.count());
    cb.hidePopup(); app.processEvents();

    // 3. currentData()/currentIndex() still read back the real selection (unchanged by the click).
    req(cb.currentIndex() == 5, "selection changed unexpectedly");
    req(cb.currentData().toString() == QStringLiteral("smd/5"), "currentData() wrong after click");

    // 4. Programmatic selection (as repopulate/preset paths do) is honoured.
    const int at = cb.findData(QStringLiteral("smd/42"));
    cb.setCurrentIndex(at);
    req(cb.currentData().toString() == QStringLiteral("smd/42"), "setCurrentIndex/currentData round-trip failed");

    // 5. Typing a substring filters via the completer (contains match).
    cb.lineEdit()->setText(QStringLiteral("Item 13"));
    app.processEvents();
    QCompleter* c = cb.completer();
    c->setCompletionPrefix(QStringLiteral("Item 13"));
    const int matches = c->completionCount();
    printf("completer matches for 'Item 13': %d (expect >=1: 13,130..139)\n", matches);
    req(matches >= 1, "contains-completer did not filter");

    printf(fails ? "RESULT: %d FAILURE(S)\n" : "RESULT: PASS\n", fails);
    return fails ? 1 : 0;
}
