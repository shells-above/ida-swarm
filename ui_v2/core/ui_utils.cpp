#include "ui_v2_common.h"
#include "ui_utils.h"
#include "theme_manager.h"
#include "ui_constants.h"

namespace llm_re::ui_v2 {

void UIUtils::fadeIn(QWidget* widget, int duration, std::function<void()> onComplete) {
    if (!widget) return;
    
    QGraphicsOpacityEffect* effect = new QGraphicsOpacityEffect(widget);
    widget->setGraphicsEffect(effect);
    
    QPropertyAnimation* anim = new QPropertyAnimation(effect, "opacity", widget);
    anim->setDuration(duration);
    anim->setStartValue(0.0);
    anim->setEndValue(1.0);
    anim->setEasingCurve(QEasingCurve::OutCubic);
    
    widget->show();
    
    cleanupAnimation(anim, [widget, effect, onComplete]() {
        widget->setGraphicsEffect(nullptr);
        delete effect;
        if (onComplete) onComplete();
    });
    
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

void UIUtils::fadeOut(QWidget* widget, int duration, std::function<void()> onComplete) {
    if (!widget) return;
    
    QGraphicsOpacityEffect* effect = new QGraphicsOpacityEffect(widget);
    widget->setGraphicsEffect(effect);
    
    QPropertyAnimation* anim = new QPropertyAnimation(effect, "opacity", widget);
    anim->setDuration(duration);
    anim->setStartValue(1.0);
    anim->setEndValue(0.0);
    anim->setEasingCurve(QEasingCurve::InCubic);
    
    cleanupAnimation(anim, [widget, effect, onComplete]() {
        widget->hide();
        widget->setGraphicsEffect(nullptr);
        delete effect;
        if (onComplete) onComplete();
    });
    
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

void UIUtils::slideIn(QWidget* widget, Qt::Edge edge, int duration, std::function<void()> onComplete) {
    if (!widget) return;
    
    QPoint startPos = widget->pos();
    QPoint endPos = startPos;
    
    switch (edge) {
        case Qt::LeftEdge:
            startPos.setX(-widget->width());
            break;
        case Qt::RightEdge:
            startPos.setX(widget->parentWidget() ? widget->parentWidget()->width() : 0);
            break;
        case Qt::TopEdge:
            startPos.setY(-widget->height());
            break;
        case Qt::BottomEdge:
            startPos.setY(widget->parentWidget() ? widget->parentWidget()->height() : 0);
            break;
    }
    
    widget->move(startPos);
    widget->show();
    
    QPropertyAnimation* anim = new QPropertyAnimation(widget, "pos", widget);
    anim->setDuration(duration);
    anim->setStartValue(startPos);
    anim->setEndValue(endPos);
    anim->setEasingCurve(QEasingCurve::OutCubic);
    
    cleanupAnimation(anim, onComplete);
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

void UIUtils::slideOut(QWidget* widget, Qt::Edge edge, int duration, std::function<void()> onComplete) {
    if (!widget) return;
    
    QPoint startPos = widget->pos();
    QPoint endPos = startPos;
    
    switch (edge) {
        case Qt::LeftEdge:
            endPos.setX(-widget->width());
            break;
        case Qt::RightEdge:
            endPos.setX(widget->parentWidget() ? widget->parentWidget()->width() : 0);
            break;
        case Qt::TopEdge:
            endPos.setY(-widget->height());
            break;
        case Qt::BottomEdge:
            endPos.setY(widget->parentWidget() ? widget->parentWidget()->height() : 0);
            break;
    }
    
    QPropertyAnimation* anim = new QPropertyAnimation(widget, "pos", widget);
    anim->setDuration(duration);
    anim->setStartValue(startPos);
    anim->setEndValue(endPos);
    anim->setEasingCurve(QEasingCurve::InCubic);
    
    cleanupAnimation(anim, [widget, onComplete]() {
        widget->hide();
        if (onComplete) onComplete();
    });
    
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

void UIUtils::shake(QWidget* widget, int intensity, int duration) {
    if (!widget) return;
    
    QPoint originalPos = widget->pos();
    
    QPropertyAnimation* anim = new QPropertyAnimation(widget, "pos", widget);
    anim->setDuration(duration);
    anim->setLoopCount(4);
    
    QVariantList values;
    values << originalPos
           << originalPos + QPoint(-intensity, 0)
           << originalPos + QPoint(intensity, 0)
           << originalPos + QPoint(0, -intensity/2)
           << originalPos + QPoint(0, intensity/2)
           << originalPos;
    
    anim->setKeyValues({
        {0.0, values[0]},
        {0.2, values[1]},
        {0.4, values[2]},
        {0.6, values[3]},
        {0.8, values[4]},
        {1.0, values[5]}
    });
    
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

void UIUtils::pulse(QWidget* widget, qreal scale, int duration) {
    if (!widget) return;
    
    // Use widget's scale property if it has one
    QPropertyAnimation* anim = new QPropertyAnimation(widget, "scale", widget);
    anim->setDuration(duration);
    anim->setStartValue(1.0);
    anim->setEndValue(scale);
    anim->setLoopCount(2);
    anim->setDirection(QAbstractAnimation::Forward);
    anim->setEasingCurve(QEasingCurve::InOutQuad);
    
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

void UIUtils::cleanupAnimation(QPropertyAnimation* anim, std::function<void()> onComplete) {
    if (!anim || !onComplete) return;
    
    QObject::connect(anim, &QPropertyAnimation::finished, [onComplete]() {
        onComplete();
    });
}

void UIUtils::setMargins(QLayout* layout, int margin) {
    if (layout) {
        layout->setContentsMargins(margin, margin, margin, margin);
    }
}

void UIUtils::setMargins(QLayout* layout, int horizontal, int vertical) {
    if (layout) {
        layout->setContentsMargins(horizontal, vertical, horizontal, vertical);
    }
}

void UIUtils::setMargins(QLayout* layout, int left, int top, int right, int bottom) {
    if (layout) {
        layout->setContentsMargins(left, top, right, bottom);
    }
}

void UIUtils::clearLayout(QLayout* layout, bool deleteWidgets) {
    if (!layout) return;
    
    QLayoutItem* item;
    while ((item = layout->takeAt(0))) {
        if (deleteWidgets) {
            if (QWidget* widget = item->widget()) {
                delete widget;
            }
        }
        if (QLayout* childLayout = item->layout()) {
            clearLayout(childLayout, deleteWidgets);
        }
        delete item;
    }
}

void UIUtils::setFocusChain(const QList<QWidget*>& widgets) {
    if (widgets.size() < 2) return;
    
    for (int i = 0; i < widgets.size(); ++i) {
        int nextIndex = (i + 1) % widgets.size();
        widgets[i]->setTabOrder(widgets[i], widgets[nextIndex]);
    }
}

void UIUtils::cycleFocus(QWidget* parent, bool forward) {
    if (!parent) return;
    
    QWidget* current = parent->focusWidget();
    QWidget* next = findNextFocusWidget(current, forward);
    if (next) {
        next->setFocus();
    }
}

QWidget* UIUtils::findNextFocusWidget(QWidget* current, bool forward) {
    if (!current) return nullptr;
    
    QWidget* next = forward ? current->nextInFocusChain() : current->previousInFocusChain();
    
    // Skip invisible or disabled widgets
    while (next && next != current && (!next->isVisible() || !next->isEnabled())) {
        next = forward ? next->nextInFocusChain() : next->previousInFocusChain();
    }
    
    return (next != current) ? next : nullptr;
}

QString UIUtils::formatTimestamp(const std::chrono::system_clock::time_point& time) {
    auto time_t = std::chrono::system_clock::to_time_t(time);
    auto now_t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    
    struct tm time_tm, now_tm;
    localtime_r(&time_t, &time_tm);
    localtime_r(&now_t, &now_tm);
    
    char buffer[100];
    
    // If same day, show only time
    if (time_tm.tm_year == now_tm.tm_year && 
        time_tm.tm_mon == now_tm.tm_mon && 
        time_tm.tm_mday == now_tm.tm_mday) {
        std::strftime(buffer, sizeof(buffer), "%H:%M:%S", &time_tm);
    } else {
        // Otherwise show date and time
        std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &time_tm);
    }
    
    return QString::fromLocal8Bit(buffer);
}

QString UIUtils::formatRelativeTime(const std::chrono::system_clock::time_point& time) {
    auto now = std::chrono::system_clock::now();
    auto diff = now - time;
    
    auto seconds = std::chrono::duration_cast<std::chrono::seconds>(diff).count();
    
    if (seconds < 60) return "just now";
    if (seconds < 3600) return QString("%1m ago").arg(seconds / 60);
    if (seconds < 86400) return QString("%1h ago").arg(seconds / 3600);
    if (seconds < 604800) return QString("%1d ago").arg(seconds / 86400);
    
    return formatTimestamp(time);
}

QString UIUtils::formatDuration(std::chrono::milliseconds duration) {
    auto ms = duration.count();
    
    if (ms < 1000) return QString("%1ms").arg(ms);
    if (ms < 60000) return QString("%1.%2s").arg(ms / 1000).arg((ms % 1000) / 100);
    
    auto seconds = ms / 1000;
    auto minutes = seconds / 60;
    seconds %= 60;
    
    if (minutes < 60) return QString("%1:%2").arg(minutes).arg(seconds, 2, 10, QChar('0'));
    
    auto hours = minutes / 60;
    minutes %= 60;
    
    return QString("%1:%2:%3").arg(hours).arg(minutes, 2, 10, QChar('0')).arg(seconds, 2, 10, QChar('0'));
}

QString UIUtils::humanizeBytes(qint64 bytes) {
    const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    int unitIndex = 0;
    double size = bytes;
    
    while (size >= 1024 && unitIndex < 4) {
        size /= 1024;
        unitIndex++;
    }
    
    return QString("%1 %2").arg(size, 0, 'f', unitIndex > 0 ? 2 : 0).arg(units[unitIndex]);
}

QString UIUtils::elideText(const QString& text, const QFont& font, int maxWidth) {
    QFontMetrics metrics(font);
    return metrics.elidedText(text, Qt::ElideRight, maxWidth);
}

QString UIUtils::highlightText(const QString& text, const QString& highlight, const QString& highlightClass) {
    if (highlight.isEmpty()) return text;
    
    QString escaped = escapeHtml(text);
    QString escapedHighlight = escapeHtml(highlight);
    
    QString result = escaped;
    result.replace(escapedHighlight, 
                  QString("<span class='%1'>%2</span>").arg(highlightClass).arg(escapedHighlight),
                  Qt::CaseInsensitive);
    
    return result;
}

QString UIUtils::markdownToHtml(const QString& markdown) {
    QTextDocument doc;
    doc.setMarkdown(markdown);
    return doc.toHtml();
}

QString UIUtils::escapeHtml(const QString& text) {
    QString escaped = text;
    escaped.replace("&", "&amp;");
    escaped.replace("<", "&lt;");
    escaped.replace(">", "&gt;");
    escaped.replace("\"", "&quot;");
    escaped.replace("'", "&#39;");
    return escaped;
}

QString UIUtils::colorToHex(const QColor& color) {
    return color.name(QColor::HexArgb);
}

QColor UIUtils::blendColors(const QColor& color1, const QColor& color2, qreal ratio) {
    return QColor(
        color1.red() * ratio + color2.red() * (1 - ratio),
        color1.green() * ratio + color2.green() * (1 - ratio),
        color1.blue() * ratio + color2.blue() * (1 - ratio),
        color1.alpha() * ratio + color2.alpha() * (1 - ratio)
    );
}

QColor UIUtils::contrastColor(const QColor& background) {
    ThemeManager &tm = ThemeManager::instance();
    return isColorLight(background) ? tm.colors().textPrimary : tm.colors().background;
}

bool UIUtils::isColorLight(const QColor& color) {
    // Calculate relative luminance
    qreal r = color.redF();
    qreal g = color.greenF();
    qreal b = color.blueF();
    
    // Apply gamma correction
    r = (r <= 0.03928) ? r / 12.92 : std::pow((r + 0.055) / 1.055, 2.4);
    g = (g <= 0.03928) ? g / 12.92 : std::pow((g + 0.055) / 1.055, 2.4);
    b = (b <= 0.03928) ? b / 12.92 : std::pow((b + 0.055) / 1.055, 2.4);
    
    qreal luminance = 0.2126 * r + 0.7152 * g + 0.0722 * b;
    
    return luminance > 0.5;
}

QPixmap UIUtils::colorizePixmap(const QPixmap& pixmap, const QColor& color) {
    QPixmap colored = pixmap;
    QPainter painter(&colored);
    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    painter.fillRect(colored.rect(), color);
    return colored;
}

QIcon UIUtils::createCircleIcon(const QColor& color, int size) {
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);
    
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    painter.drawEllipse(0, 0, size, size);
    
    return QIcon(pixmap);
}

QIcon UIUtils::createTextIcon(const QString& text, const QColor& textColor, 
                             const QColor& bgColor, int size) {
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);
    
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    
    // Draw background
    painter.setPen(Qt::NoPen);
    painter.setBrush(bgColor);
    painter.drawEllipse(0, 0, size, size);
    
    // Draw text
    painter.setPen(textColor);
    QFont font = painter.font();
    font.setPixelSize(size * 0.6);
    font.setBold(true);
    painter.setFont(font);
    painter.drawText(pixmap.rect(), Qt::AlignCenter, text);
    
    return QIcon(pixmap);
}

void UIUtils::setWidgetVisible(QWidget* widget, bool visible, bool animated) {
    if (!widget) return;
    
    if (animated) {
        if (visible) {
            fadeIn(widget);
        } else {
            fadeOut(widget);
        }
    } else {
        widget->setVisible(visible);
    }
}

void UIUtils::scrollToWidget(QWidget* widget, QAbstractScrollArea* scrollArea) {
    if (!widget || !scrollArea) return;
    
    if (QScrollArea* sa = qobject_cast<QScrollArea*>(scrollArea)) {
        sa->ensureWidgetVisible(widget);
    }
}

QWidget* UIUtils::findParentOfType(QWidget* widget, const char* className) {
    if (!widget) return nullptr;
    
    QWidget* parent = widget->parentWidget();
    while (parent) {
        if (parent->inherits(className)) {
            return parent;
        }
        parent = parent->parentWidget();
    }
    
    return nullptr;
}

void UIUtils::dumpWidgetTree(QWidget* widget, int indent) {
    if (!widget) return;
    
    QString indentStr(indent * 2, ' ');
    qDebug() << qPrintable(indentStr) << qPrintable(widgetInfo(widget));
    
    for (QObject* child : widget->children()) {
        if (QWidget* childWidget = qobject_cast<QWidget*>(child)) {
            dumpWidgetTree(childWidget, indent + 1);
        }
    }
}

QString UIUtils::widgetInfo(QWidget* widget) {
    if (!widget) return "null";
    
    return QString("%1 [%2] (%3x%4 at %5,%6) %7")
        .arg(widget->metaObject()->className())
        .arg(widget->objectName())
        .arg(widget->width()).arg(widget->height())
        .arg(widget->x()).arg(widget->y())
        .arg(widget->isVisible() ? "visible" : "hidden");
}

// SmoothScroller implementation
SmoothScroller::SmoothScroller(QAbstractScrollArea* area, const QPoint& target, int duration)
    : QObject(area), area_(area), targetPos_(target), duration_(duration) {
    
    startPos_ = QPoint(area->horizontalScrollBar()->value(),
                      area->verticalScrollBar()->value());
    startTime_ = std::chrono::steady_clock::now();
    
    timer_ = new QTimer(this);
    timer_->setInterval(16); // ~60 FPS
    connect(timer_, &QTimer::timeout, this, &SmoothScroller::updateScroll);
    timer_->start();
}

void SmoothScroller::updateScroll() {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - startTime_).count();
    
    if (elapsed >= duration_) {
        area_->horizontalScrollBar()->setValue(targetPos_.x());
        area_->verticalScrollBar()->setValue(targetPos_.y());
        timer_->stop();
        deleteLater();
        return;
    }
    
    qreal progress = qreal(elapsed) / duration_;
    qreal eased = QEasingCurve(QEasingCurve::InOutQuad).valueForProgress(progress);
    
    int x = startPos_.x() + (targetPos_.x() - startPos_.x()) * eased;
    int y = startPos_.y() + (targetPos_.y() - startPos_.y()) * eased;
    
    area_->horizontalScrollBar()->setValue(x);
    area_->verticalScrollBar()->setValue(y);
}

void SmoothScroller::smoothScrollTo(QAbstractScrollArea* area, const QPoint& target, int duration) {
    new SmoothScroller(area, target, duration);
}

void SmoothScroller::smoothScrollToWidget(QAbstractScrollArea* area, QWidget* widget, int duration) {
    if (!area || !widget) return;
    
    // Calculate target position to center the widget
    QRect widgetRect = widget->geometry();
    QRect viewRect = area->viewport()->rect();
    
    int targetX = widgetRect.center().x() - viewRect.width() / 2;
    int targetY = widgetRect.center().y() - viewRect.height() / 2;
    
    targetX = qBound(0, targetX, area->horizontalScrollBar()->maximum());
    targetY = qBound(0, targetY, area->verticalScrollBar()->maximum());
    
    smoothScrollTo(area, QPoint(targetX, targetY), duration);
}

} // namespace llm_re::ui_v2