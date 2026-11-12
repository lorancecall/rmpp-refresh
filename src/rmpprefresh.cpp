// rmpp-refresh: a "Refresh screen" button for the reMarkable Paper Pro (xochitl, loaded with LD_PRELOAD via
// rmpp-preload). It sits in the notebook toolbar right above the three dots, and in the library's sidebar right above
// Trash. It does a full e-paper refresh (clears ghosting) through xochitl's own ghost buster:
// DocumentView.ghostBuster (notebook) or MainView.ghostBuster (library), userRequestedClear().
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

// Library: the ghost buster of the main view (no document open).
void refreshLibrary() {
    QQuickItem *mv = findAnywhere([](QQuickItem *i) { return classStarts(i, "MainView") && i->isVisible(); });
    QObject *gb = mv ? mv->property("ghostBuster").value<QObject *>() : nullptr;
    if (!gb || !QMetaObject::invokeMethod(gb, "userRequestedClear")) qWarning("[rmpp-refresh] no library ghost buster");
}

const char *kSidebarQml = R"QML(
import QtQuick
import QtQuick.Layouts
import ark.controls as ArkControls
ArkControls.SidebarItem {
    objectName: "rmppRefreshSidebar"
    text: "Refresh screen"
    iconSource: "qrc:/ark/icons/arrows_counter_clockwise"
    focusPolicy: Qt.StrongFocus
    Layout.fillWidth: true
    onTriggered: rmppRefresh.command = "library#" + Date.now()
}
)QML";

// Library sidebar (#Sidebar, a ColumnLayout: the filters at the top, a spacer, then Trash, Guides, Settings): the
// button goes directly before Trash, so it sits right above it and the layout keeps other add-ons' rows (inserted
// at the top or elsewhere) from overlapping it.
void tryInjectSidebar() {
    QQuickItem *trash = findAnywhere([](QQuickItem *i) { return i->objectName() == QLatin1String("filterTrashed"); });
    if (!trash || !trash->parentItem() || trash->property("_rmppRefresh").toBool()) return;
    QQmlEngine *eng = qmlEngine(trash);
    QQmlContext *base = qmlContext(trash);
    if (!eng || !base) return;
    trash->setProperty("_rmppRefresh", true);
    QQuickItem *layout = trash->parentItem();
    auto *ctx = new QQmlContext(base, layout);
    ctx->setContextProperty(QStringLiteral("rmppRefresh"), g_state);
    QQmlComponent comp(eng);
    comp.setData(QByteArray(kSidebarQml), QUrl(QStringLiteral("qrc:/rmpprefresh/SidebarButton.qml")));
    QObject *o = comp.create(ctx);
    auto *item = qobject_cast<QQuickItem *>(o);
    if (!item) {
        for (const auto &e : comp.errors()) qWarning("[rmpp-refresh] %s", qPrintable(e.toString()));
        delete o;
        return;
    }
    item->setParentItem(layout);
    item->setParent(layout);
    item->stackBefore(trash);
    qWarning("[rmpp-refresh] sidebar button added");
}

const char *kButtonQml = R"QML(
import QtQuick
import xofm.libs.toolbar
ToolbarTool {
    objectName: "rmppRefreshButton"
    type: ToolbarTool.Type.ToolbarButton
    group: 2
    visible: !!toolbar && toolbar.notePage
    label: "Refresh screen"
    iconSource: "qrc:/ark/icons/arrows_counter_clockwise"
    onClicked: rmppRefresh.command = "refresh#" + Date.now()
}
)QML";

// Insert the button into the toolbar's layout (#toolbarLayout, a GridLayout of tool loaders with a spacer between the
// editing tools and the bottom group) directly before the "Notebook settings" tool: the spacer gets shorter, so the
// bottom tools move up by one button. One toolbar per open document; the loader is marked once done.
void tryInject() {
    QQuickItem *settings = findAnywhere([](QQuickItem *i) {
        return i->objectName() == QLatin1String("secondaryToolLoader_settingsMenu") && i->isVisible();
    });
    if (!settings || !settings->parentItem() || settings->property("_rmppRefresh").toBool()) return;
    QQuickItem *menu = findItem(settings, [](QQuickItem *i) { return i->objectName() == QLatin1String("settingsMenu"); });
    QQmlEngine *eng = qmlEngine(settings);
    QQmlContext *base = qmlContext(settings);
    if (!menu || !eng || !base) return;
    settings->setProperty("_rmppRefresh", true);
    QQuickItem *layout = settings->parentItem();
    auto *ctx = new QQmlContext(base, layout);
    ctx->setContextProperty(QStringLiteral("rmppRefresh"), g_state);
    QQmlComponent comp(eng);
    comp.setData(QByteArray(kButtonQml), QUrl(QStringLiteral("qrc:/rmpprefresh/Button.qml")));
    QObject *o = comp.createWithInitialProperties({{QStringLiteral("toolbar"), menu->property("toolbar")}}, ctx);
    auto *item = qobject_cast<QQuickItem *>(o);
    if (!item) {
        for (const auto &e : comp.errors()) qWarning("[rmpp-refresh] %s", qPrintable(e.toString()));
        delete o;
        return;
    }
    item->setParentItem(layout);
    item->setParent(layout);
    item->stackBefore(settings);
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
        if (key != QLatin1String("command")) return;
        if (v.toString().startsWith(QLatin1String("refresh#"))) refresh();
        else if (v.toString().startsWith(QLatin1String("library#"))) refreshLibrary();
    });
    auto *t = new QTimer(app);
    QObject::connect(t, &QTimer::timeout, app, [] {
        tryInject();
        tryInjectSidebar();
        watchKeyboard();
    });
    t->start(1500);
    qWarning("[rmpp-refresh] loaded");
}

} // namespace

Q_COREAPP_STARTUP_FUNCTION(init)
