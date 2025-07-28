#pragma once

#include "ui_v2_common.h"

namespace llm_re::ui_v2 {

// Focus chain item
struct FocusItem {
    QPointer<QWidget> widget;
    QString group;
    int priority = 0;
    bool acceptsKeyboardFocus = true;
    bool restoreScrollPosition = true;
    QRect lastVisibleRect;
    QPoint lastScrollPosition;
};

// Scroll animation settings
struct ScrollAnimationSettings {
    bool enabled = true;
    int duration = 200;
    QEasingCurve::Type easingCurve = QEasingCurve::InOutQuad;
    bool smoothScroll = true;
    int scrollMargin = 20;
};

// Focus and scroll manager
class FocusManager : public QObject {
    Q_OBJECT
    
public:
    explicit FocusManager(QObject* parent = nullptr);
    ~FocusManager();
    
    // Registration
    void registerWidget(QWidget* widget, const QString& group = QString(), int priority = 0);
    void unregisterWidget(QWidget* widget);
    void clearWidgets();
    
    // Focus management
    void setFocus(QWidget* widget, bool ensureVisible = true);
    void focusNext();
    void focusPrevious();
    void focusFirst();
    void focusLast();
    void focusGroup(const QString& group);
    
    // Focus chain
    void setFocusChainEnabled(bool enabled) { focusChainEnabled_ = enabled; }
    bool isFocusChainEnabled() const { return focusChainEnabled_; }
    
    void setWrapAround(bool wrap) { wrapAround_ = wrap; }
    bool wrapAround() const { return wrapAround_; }
    
    // Smart focus
    void enableSmartFocus(bool enable) { smartFocusEnabled_ = enable; }
    bool isSmartFocusEnabled() const { return smartFocusEnabled_; }
    
    void setAutoRestoreFocus(bool restore) { autoRestoreFocus_ = restore; }
    bool autoRestoreFocus() const { return autoRestoreFocus_; }
    
    // Scroll management
    void ensureVisible(QWidget* widget, const QRect& rect = QRect());
    void scrollToWidget(QWidget* widget, bool animate = true);
    void saveScrollPosition(QWidget* widget);
    void restoreScrollPosition(QWidget* widget);
    
    // Scroll settings
    void setScrollAnimationSettings(const ScrollAnimationSettings& settings) { scrollSettings_ = settings; }
    ScrollAnimationSettings scrollAnimationSettings() const { return scrollSettings_; }
    
    // Focus history
    void pushFocusHistory();
    void popFocusHistory();
    void clearFocusHistory();
    bool canGoBack() const { return !focusHistory_.isEmpty(); }
    
    // Groups
    QStringList groups() const;
    QList<QWidget*> widgetsInGroup(const QString& group) const;
    
    // Current focus
    QWidget* currentFocusWidget() const { return currentFocus_; }
    QString currentFocusGroup() const;
    
signals:
    void focusChanged(QWidget* oldWidget, QWidget* newWidget);
    void focusGroupChanged(const QString& oldGroup, const QString& newGroup);
    void scrollStarted(QWidget* widget);
    void scrollFinished(QWidget* widget);
    
protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    
private slots:
    void onWidgetDestroyed(QObject* obj);
    void onScrollAnimationFinished();
    void updateFocusChain();
    
private:
    QAbstractScrollArea* findScrollArea(QWidget* widget) const;
    void animateScroll(QAbstractScrollArea* scrollArea, const QPoint& targetPos);
    void installEventFilters(QWidget* widget);
    void removeEventFilters(QWidget* widget);
    void handleFocusChange(QWidget* oldWidget, QWidget* newWidget);
    void buildFocusChain();
    int findWidgetIndex(QWidget* widget) const;
    
    // Focus items
    QList<FocusItem> focusItems_;
    QHash<QWidget*, FocusItem*> widgetMap_;
    
    // Focus chain
    QList<QWidget*> focusChain_;
    bool focusChainEnabled_ = true;
    bool wrapAround_ = true;
    bool focusChainDirty_ = true;
    
    // Current state
    QPointer<QWidget> currentFocus_;
    QString lastFocusGroup_;
    
    // Smart focus
    bool smartFocusEnabled_ = true;
    bool autoRestoreFocus_ = true;
    
    // Focus history
    QList<QPointer<QWidget>> focusHistory_;
    int maxHistorySize_ = 10;
    
    // Scroll animation
    ScrollAnimationSettings scrollSettings_;
    QHash<QAbstractScrollArea*, QPropertyAnimation*> scrollAnimations_;
    
    // Event handling
    bool processingFocusChange_ = false;
};

// Focus highlight widget
class FocusHighlight : public QWidget {
    Q_OBJECT
    
public:
    explicit FocusHighlight(QWidget* parent = nullptr);
    
    void highlightWidget(QWidget* widget);
    void setHighlightColor(const QColor& color) { highlightColor_ = color; update(); }
    void setHighlightWidth(int width) { highlightWidth_ = width; update(); }
    void setAnimationDuration(int ms) { animationDuration_ = ms; }
    
    void animateIn();
    void animateOut();
    
protected:
    void paintEvent(QPaintEvent* event) override;
    
private slots:
    void updatePosition();
    void onAnimationFinished();
    
private:
    QPointer<QWidget> targetWidget_;
    QColor highlightColor_ = QColor(0, 120, 215);
    int highlightWidth_ = 2;
    int animationDuration_ = 200;
    
    QPropertyAnimation* positionAnimation_ = nullptr;
    QPropertyAnimation* sizeAnimation_ = nullptr;
    QPropertyAnimation* opacityAnimation_ = nullptr;
};

// Scroll position tracker
class ScrollPositionTracker : public QObject {
    Q_OBJECT
    
public:
    explicit ScrollPositionTracker(QObject* parent = nullptr);
    
    void trackWidget(QWidget* widget);
    void untrackWidget(QWidget* widget);
    void savePosition(QWidget* widget);
    void restorePosition(QWidget* widget, bool animate = true);
    
    void setAutoSave(bool autoSave) { autoSave_ = autoSave; }
    bool autoSave() const { return autoSave_; }
    
    void setSaveDelay(int ms) { saveDelay_ = ms; }
    int saveDelay() const { return saveDelay_; }
    
signals:
    void positionSaved(QWidget* widget, const QPoint& position);
    void positionRestored(QWidget* widget, const QPoint& position);
    
protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    
private slots:
    void onSaveTimeout();
    
private:
    struct PositionInfo {
        QPoint scrollPosition;
        QRect visibleRect;
        QDateTime timestamp;
    };
    
    QHash<QWidget*, PositionInfo> positions_;
    QPointer<QWidget> pendingSaveWidget_;
    QTimer* saveTimer_ = nullptr;
    bool autoSave_ = true;
    int saveDelay_ = 500;
};

// Keyboard navigation helper
class KeyboardNavigator : public QObject {
    Q_OBJECT
    
public:
    explicit KeyboardNavigator(QObject* parent = nullptr);
    
    void setEnabled(bool enabled) { enabled_ = enabled; }
    bool isEnabled() const { return enabled_; }
    
    // Navigation keys
    void setNextKey(Qt::Key key, Qt::KeyboardModifiers modifiers = Qt::NoModifier);
    void setPreviousKey(Qt::Key key, Qt::KeyboardModifiers modifiers = Qt::NoModifier);
    void setActivateKey(Qt::Key key, Qt::KeyboardModifiers modifiers = Qt::NoModifier);
    
    // Custom navigation
    void registerNavigationKey(Qt::Key key, Qt::KeyboardModifiers modifiers,
                              std::function<void()> action);
    void clearNavigationKeys();
    
    
signals:
    void navigateNext();
    void navigatePrevious();
    void activate();
    void customNavigation(const QString& action);
    
protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    
private:
    struct KeyBinding {
        Qt::Key key;
        Qt::KeyboardModifiers modifiers;
        std::function<void()> action;
    };
    
    bool handleKeyPress(QKeyEvent* event);
    
    bool enabled_ = true;
    
    QList<KeyBinding> keyBindings_;
    
    // Default keys
    Qt::Key nextKey_ = Qt::Key_Tab;
    Qt::KeyboardModifiers nextModifiers_ = Qt::NoModifier;
    Qt::Key previousKey_ = Qt::Key_Tab;
    Qt::KeyboardModifiers previousModifiers_ = Qt::ShiftModifier;
    Qt::Key activateKey_ = Qt::Key_Return;
    Qt::KeyboardModifiers activateModifiers_ = Qt::NoModifier;
};

// Focus scope widget
class FocusScope : public QWidget {
    Q_OBJECT
    
public:
    explicit FocusScope(QWidget* parent = nullptr);
    
    void setFocusProxy(QWidget* proxy);
    void setTrapFocus(bool trap) { trapFocus_ = trap; }
    bool trapFocus() const { return trapFocus_; }
    
    void addWidget(QWidget* widget);
    void removeWidget(QWidget* widget);
    
signals:
    void focusEntered();
    void focusLeft();
    
protected:
    void focusInEvent(QFocusEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    bool focusNextPrevChild(bool next) override;
    
private:
    QList<QWidget*> widgets_;
    bool trapFocus_ = false;
};

// Focus manager factory
class FocusManagerFactory {
public:
    static FocusManager* createFocusManager(QWidget* rootWidget);
    static void installGlobalFocusManager(FocusManager* manager);
    static FocusManager* globalFocusManager();
    
private:
    static FocusManager* globalManager_;
};

} // namespace llm_re::ui_v2