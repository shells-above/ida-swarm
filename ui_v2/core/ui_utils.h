#pragma once

#include "ui_v2_common.h"
#include "ui_constants.h"

namespace llm_re::ui_v2 {

class UIUtils {
public:
    // Animation utilities
    static void fadeIn(QWidget* widget, int duration = Design::ANIM_NORMAL, 
                      std::function<void()> onComplete = nullptr);
    static void fadeOut(QWidget* widget, int duration = Design::ANIM_NORMAL,
                       std::function<void()> onComplete = nullptr);
    static void slideIn(QWidget* widget, Qt::Edge edge, int duration = Design::ANIM_NORMAL,
                       std::function<void()> onComplete = nullptr);
    static void slideOut(QWidget* widget, Qt::Edge edge, int duration = Design::ANIM_NORMAL,
                        std::function<void()> onComplete = nullptr);
    static void shake(QWidget* widget, int intensity = 10, int duration = Design::ANIM_FAST);
    static void pulse(QWidget* widget, qreal scale = 1.1, int duration = Design::ANIM_NORMAL);
    
    // Layout utilities
    static void setMargins(QLayout* layout, int margin);
    static void setMargins(QLayout* layout, int horizontal, int vertical);
    static void setMargins(QLayout* layout, int left, int top, int right, int bottom);
    static void clearLayout(QLayout* layout, bool deleteWidgets = true);
    
    // Focus management
    static void setFocusChain(const QList<QWidget*>& widgets);
    static void cycleFocus(QWidget* parent, bool forward = true);
    static QWidget* findNextFocusWidget(QWidget* current, bool forward = true);
    
    // Time formatting
    static QString formatTimestamp(const std::chrono::system_clock::time_point& time);
    static QString formatRelativeTime(const std::chrono::system_clock::time_point& time);
    static QString formatDuration(std::chrono::milliseconds duration);
    static QString humanizeBytes(qint64 bytes);
    
    // Text utilities
    static QString elideText(const QString& text, const QFont& font, int maxWidth);
    static QString highlightText(const QString& text, const QString& highlight, 
                                const QString& highlightClass = "highlight");
    static QString markdownToHtml(const QString& markdown);
    static QString escapeHtml(const QString& text);
    
    // Color utilities
    static QString colorToHex(const QColor& color);
    static QColor blendColors(const QColor& color1, const QColor& color2, qreal ratio = 0.5);
    static QColor contrastColor(const QColor& background);
    static bool isColorLight(const QColor& color);
    
    // Icon utilities
    static QPixmap colorizePixmap(const QPixmap& pixmap, const QColor& color);
    static QIcon createCircleIcon(const QColor& color, int size = 16);
    static QIcon createTextIcon(const QString& text, const QColor& textColor, 
                               const QColor& bgColor, int size = 32);
    
    // Widget utilities
    static void setWidgetVisible(QWidget* widget, bool visible, bool animated = true);
    static void scrollToWidget(QWidget* widget, QAbstractScrollArea* scrollArea);
    static QWidget* findParentOfType(QWidget* widget, const char* className);

    // Debug utilities
    static void dumpWidgetTree(QWidget* widget, int indent = 0);
    static QString widgetInfo(QWidget* widget);
    
private:
    // Helper for animation cleanup
    static void cleanupAnimation(QPropertyAnimation* anim, std::function<void()> onComplete);
};

// RAII class for saving/restoring widget update state
class UpdateBlocker {
public:
    explicit UpdateBlocker(QWidget* widget) : widget_(widget) {
        if (widget_) {
            wasBlocked_ = widget_->updatesEnabled();
            widget_->setUpdatesEnabled(false);
        }
    }
    
    ~UpdateBlocker() {
        if (widget_) {
            widget_->setUpdatesEnabled(wasBlocked_);
            if (wasBlocked_) {
                widget_->update();
            }
        }
    }
    
private:
    QWidget* widget_;
    bool wasBlocked_;
};

// RAII class for cursor override
class CursorOverride {
public:
    explicit CursorOverride(Qt::CursorShape shape) {
        QApplication::setOverrideCursor(shape);
    }
    
    ~CursorOverride() {
        QApplication::restoreOverrideCursor();
    }
};

// Smooth scroll helper
class SmoothScroller : public QObject {
    Q_OBJECT
    
public:
    static void smoothScrollTo(QAbstractScrollArea* area, const QPoint& target, 
                              int duration = Design::ANIM_NORMAL);
    static void smoothScrollToWidget(QAbstractScrollArea* area, QWidget* widget,
                                   int duration = Design::ANIM_NORMAL);

private:
    SmoothScroller(QAbstractScrollArea* area, const QPoint& target, int duration);
    
private slots:
    void updateScroll();
    
private:
    QAbstractScrollArea* area_;
    QPoint startPos_;
    QPoint targetPos_;
    QTimer* timer_;
    std::chrono::steady_clock::time_point startTime_;
    int duration_;
};

} // namespace llm_re::ui_v2