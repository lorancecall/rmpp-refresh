// rmpp-refresh: a "Refresh screen" button for the reMarkable Paper Pro (xochitl, loaded with LD_PRELOAD via
// rmpp-preload). It sits in the notebook toolbar's convert menu, next to xochitl's own "Convert to text", and does a
// full e-paper refresh (clears ghosting) through xochitl's own ghost buster: DocumentView.ghostBuster.userRequestedClear().
// The same refresh also runs once after the on-screen keyboard opens.
#include <functional>

#include <QtCore/QCoreApplication>
#include <QtCore/QPointer>
#include <QtCore/QTimer>
#include <QtGui/QGuiApplication>
#include <QtGui/QInputMethod>
#include <QtGui/QWindow>
#include <QtQml/QQmlComponent>
#include <QtQml/QQmlContext>
#include <QtQml/QQmlEngine>
#include <QtQml/QQmlPropertyMap>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>

namespace {

QQmlPropertyMap *g_state = nullptr;   // "command", written by the button

QQuickItem *findItem(QQuickItem *root, const std::function<bool(QQuickItem *)> &pred, int depth = 0) {
    if (!root || depth > 250) return nullptr;
    if (pred(root)) return root;
    for (QQuickItem *k : root->childItems())
        if (QQuickItem *f = findItem(k, pred, depth + 1)) return f;
    return nullptr;
}

QQuickItem *findAnywhere(const std::function<bool(QQuickItem *)> &pred) {
    for (QWindow *w : QGuiApplication::allWindows())
        if (auto *qw = qobject_cast<QQuickWindow *>(w))
            if (QQuickItem *f = findItem(qw->contentItem(), pred)) return f;
    return nullptr;
}

bool classStarts(QObject *o, const char *prefix) {
    return qstrncmp(o->metaObject()->className(), prefix, qstrlen(prefix)) == 0;
}

// Close the toolbar menu first (so it is not left on the screen), then refresh the whole screen.
void refresh() {
    QQuickItem *dv = findAnywhere([](QQuickItem *i) { return classStarts(i, "DocumentView_QMLTYPE") && i->isVisible(); });
    if (!dv) return;
    if (QQuickItem *sv = findItem(dv, [](QQuickItem *i) { return i->metaObject()->indexOfSignal("closeToolbarFoldout()") >= 0; }))
        QMetaObject::invokeMethod(sv, "closeToolbarFoldout");
    QPointer<QQuickItem> dvp(dv);
    QTimer::singleShot(400, dv, [dvp] {
        QObject *gb = dvp ? dvp->property("ghostBuster").value<QObject *>() : nullptr;
        if (!gb || !QMetaObject::invokeMethod(gb, "userRequestedClear")) qWarning("[rmpp-refresh] no ghost buster");
    });
}

const char *kButtonQml = R"QML(
import QtQuick
import xofm.libs.toolbar
ToolbarTool {
    objectName: "rmppRefreshButton"
    type: ToolbarTool.Type.FoldoutButton
    shouldShow: toolbar.notePage
    label: "Refresh screen"
    iconSource: "qrc:/ark/icons/arrows_counter_clockwise"
    onClicked: rmppRefresh.command = "refresh#" + Date.now()
}
)QML";

// Insert the button after xochitl's "Convert to text" tool (and after other add-ons' tools placed there).
void tryInject() {
    QQuickItem *native = findAnywhere([](QQuickItem *i) { return i->objectName() == QLatin1String("convertToTextButton"); });
    if (!native || !native->parentItem() || native->property("_rmppRefresh").toBool()) return;
    QQmlEngine *eng = qmlEngine(native);
    QQmlContext *base = qmlContext(native);
    if (!eng || !base) return;
    native->setProperty("_rmppRefresh", true);
    // the tool may sit in single-child wrappers (loaders): insert in the first container that lays out several tools
    QQuickItem *anchor = native, *box = native->parentItem();
    while (box && box->childItems().size() == 1 && box->parentItem()) { anchor = box; box = box->parentItem(); }
    auto *ctx = new QQmlContext(base, box);
    ctx->setContextProperty(QStringLiteral("rmppRefresh"), g_state);
    QQmlComponent comp(eng);
    comp.setData(QByteArray(kButtonQml), QUrl(QStringLiteral("qrc:/rmpprefresh/Button.qml")));
    QObject *o = comp.createWithInitialProperties({{QStringLiteral("toolbar"), native->property("toolbar")}}, ctx);
    auto *item = qobject_cast<QQuickItem *>(o);
    if (!item) {
        for (const auto &e : comp.errors()) qWarning("[rmpp-refresh] %s", qPrintable(e.toString()));
        delete o;
        return;
    }
    item->setParentItem(box);
    item->setParent(box);
    // last of the tools that follow "Convert to text" (keeps other add-ons' convert tools together)
    QQuickItem *after = anchor;
    const auto kids = box->childItems();
    for (int i = int(kids.indexOf(anchor)) + 1; i < kids.size(); ++i)
        if (kids[i] != item && kids[i]->objectName().startsWith(QLatin1String("rmpp"))) after = kids[i];
    item->stackAfter(after);
    qWarning("[rmpp-refresh] button added");
}

// Full refresh once the on-screen keyboard has slid in: opening it leaves ghosting in the notebook. Same ghost
// buster as the button, only while a document is open.
void refreshAfterKeyboard() {
    QQuickItem *dv = findAnywhere([](QQuickItem *i) { return classStarts(i, "DocumentView_QMLTYPE") && i->isVisible(); });
    QObject *gb = dv ? dv->property("ghostBuster").value<QObject *>() : nullptr;
    if (gb && QMetaObject::invokeMethod(gb, "userRequestedClear"))
        qWarning("[rmpp-refresh] refresh (keyboard shown)");
}

void watchKeyboard() {
    static bool connected = false;
    QInputMethod *im = QGuiApplication::inputMethod();
    if (connected || !im) return;
    connected = true;
    static QTimer *later = new QTimer(qApp);
    later->setSingleShot(true);
    later->setInterval(700);   // after the keyboard's slide animation
    QObject::connect(later, &QTimer::timeout, qApp, [] { refreshAfterKeyboard(); });
    // only when it opens: closing it already makes xochitl redraw the page, a second refresh there flashed twice
    QObject::connect(im, &QInputMethod::visibleChanged, qApp, [] {
        if (QGuiApplication::inputMethod()->isVisible()) later->start(); else later->stop();
    });
}

void init() {
    auto *app = QCoreApplication::instance();
    g_state = new QQmlPropertyMap(app);
    g_state->insert(QStringLiteral("command"), QString());
    QObject::connect(g_state, &QQmlPropertyMap::valueChanged, app, [](const QString &key, const QVariant &v) {
        if (key == QLatin1String("command") && v.toString().startsWith(QLatin1String("refresh#"))) refresh();
    });
    auto *t = new QTimer(app);
    QObject::connect(t, &QTimer::timeout, app, [] {
        tryInject();
        watchKeyboard();
    });
    t->start(1500);
    qWarning("[rmpp-refresh] loaded");
}

} // namespace

Q_COREAPP_STARTUP_FUNCTION(init)
