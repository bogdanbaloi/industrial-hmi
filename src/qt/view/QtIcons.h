#pragma once

#include <QIcon>
#include <QPainter>
#include <QPixmap>
#include <QString>
#include <QSvgRenderer>
#include <Qt>

// Inline SVG nav icons rendered to a QIcon at load time -- no .qrc, no shipped
// asset files. Line ("feather") style; %1 is the stroke colour. A native app
// can embed SVG freely (unlike a sandboxed web artifact). Kept monochrome in a
// neutral grey that reads on every palette ground.
namespace app::view::icons {

inline constexpr int kNavIconPx = 18;

inline QString navColor() { return QStringLiteral("#8a8a8a"); }

inline QIcon fromSvg(const QString& svg) {
    QSvgRenderer renderer(svg.toUtf8());
    QPixmap pixmap(kNavIconPx, kNavIconPx);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    renderer.render(&painter);
    painter.end();
    return QIcon(pixmap);
}

inline QIcon overview() {
    return fromSvg(
        QStringLiteral(
            "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24'"
            " fill='none' stroke='%1' stroke-width='2' stroke-linecap='round'>"
            "<line x1='5' y1='21' x2='5' y2='10'/>"
            "<line x1='12' y1='21' x2='12' y2='4'/>"
            "<line x1='19' y1='21' x2='19' y2='14'/></svg>")
            .arg(navColor()));
}

inline QIcon alerts() {
    return fromSvg(
        QStringLiteral(
            "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24'"
            " fill='none' stroke='%1' stroke-width='2' stroke-linecap='round'"
            " stroke-linejoin='round'>"
            "<path d='M18 8a6 6 0 1 0-12 0c0 7-3 9-3 9h18s-3-2-3-9'/>"
            "<path d='M13.7 21a2 2 0 0 1-3.4 0'/></svg>")
            .arg(navColor()));
}

inline QIcon inventory() {
    return fromSvg(
        QStringLiteral(
            "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24'"
            " fill='none' stroke='%1' stroke-width='2' stroke-linecap='round'"
            " stroke-linejoin='round'>"
            "<path d='M21 16V8a2 2 0 0 0-1-1.73l-7-4a2 2 0 0 0-2 0l-7 4A2 2 0 0"
            " 0 3 8v8a2 2 0 0 0 1 1.73l7 4a2 2 0 0 0 2 0l7-4A2 2 0 0 0 21 16z'/>"
            "<polyline points='3.27 6.96 12 12.01 20.73 6.96'/>"
            "<line x1='12' y1='22.08' x2='12' y2='12'/></svg>")
            .arg(navColor()));
}

inline QIcon settings() {
    return fromSvg(
        QStringLiteral(
            "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24'"
            " fill='none' stroke='%1' stroke-width='2' stroke-linecap='round'"
            " stroke-linejoin='round'><circle cx='12' cy='12' r='3'/>"
            "<path d='M19.4 15a1.65 1.65 0 0 0 .33 1.82l.06.06a2 2 0 1 1-2.83"
            " 2.83l-.06-.06a1.65 1.65 0 0 0-1.82-.33 1.65 1.65 0 0 0-1 1.51V21a2"
            " 2 0 0 1-4 0v-.09A1.65 1.65 0 0 0 9 19.4a1.65 1.65 0 0 0-1.82.33l"
            "-.06.06a2 2 0 1 1-2.83-2.83l.06-.06a1.65 1.65 0 0 0 .33-1.82 1.65"
            " 1.65 0 0 0-1.51-1H3a2 2 0 0 1 0-4h.09A1.65 1.65 0 0 0 4.6 9a1.65"
            " 1.65 0 0 0-.33-1.82l-.06-.06a2 2 0 1 1 2.83-2.83l.06.06a1.65 1.65"
            " 0 0 0 1.82.33H9a1.65 1.65 0 0 0 1-1.51V3a2 2 0 0 1 4 0v.09a1.65"
            " 1.65 0 0 0 1 1.51 1.65 1.65 0 0 0 1.82-.33l.06-.06a2 2 0 1 1 2.83"
            " 2.83l-.06.06a1.65 1.65 0 0 0-.33 1.82V9a1.65 1.65 0 0 0 1.51 1H21a2"
            " 2 0 0 1 0 4h-.09a1.65 1.65 0 0 0-1.51 1z'/></svg>")
            .arg(navColor()));
}

}  // namespace app::view::icons
