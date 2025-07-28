#include "focus_manager.h"
#include <QScrollArea>
#include <QAbstractScrollArea>
#include <QScrollBar>
#include <QTimer>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QPainter>
#include <QPainterPath>
#include <QApplication>
#include <QGraphicsOpacityEffect>
#include <algorithm>

namespace llm_re::ui_v2 {

// FocusManager implementation
FocusManager::FocusManager(QObject* parent)
    : QObject(parent)
{
    // No initialization needed - items will be added via registerWidget
}

FocusManager::~FocusManager()
{
    // Clean up animations
    for (auto* animation : scrollAnimations_) {
        animation->stop();
        delete animation;
    }
}

void FocusManager::registerWidget(QWidget* widget, const QString& group, int priority)
{
    if (!widget || widgetMap_.contains(widget)) return;
    
    FocusItem item;
    item.widget = widget;
    item.group = group;
    item.priority = priority;
    item.acceptsKeyboardFocus = widget->focusPolicy() != Qt::NoFocus;
    
    focusItems_.append(item);
    widgetMap_[widget] = &focusItems_.last();
    
    // Install event filters
    installEventFilters(widget);
    
    // Connect to destroyed signal
    connect(widget, &QObject::destroyed, this, &FocusManager::onWidgetDestroyed);
    
    // Mark focus chain as dirty
    focusChainDirty_ = true;
}

void FocusManager::unregisterWidget(QWidget* widget)
{
    if (!widget || !widgetMap_.contains(widget)) return;
    
    // Remove event filters
    removeEventFilters(widget);
    
    // Remove from maps and lists
    widgetMap_.remove(widget);
    focusItems_.removeIf([widget](const FocusItem& item) {
        return item.widget == widget;
    });
    
    // Remove from focus chain
    focusChain_.removeAll(widget);
    
    // Clear from history
    focusHistory_.removeAll(widget);
    
    // Disconnect signals
    disconnect(widget, nullptr, this, nullptr);
    
    focusChainDirty_ = true;
}

void FocusManager::clearWidgets()
{
    // Remove all event filters
    for (const auto& item : focusItems_) {
        if (item.widget) {
            removeEventFilters(item.widget);
            disconnect(item.widget, nullptr, this, nullptr);
        }
    }
    
    focusItems_.clear();
    widgetMap_.clear();
    focusChain_.clear();
    focusHistory_.clear();
    focusChainDirty_ = true;
}

void FocusManager::setFocus(QWidget* widget, bool ensureVisible)
{
    if (!widget || processingFocusChange_) return;
    
    processingFocusChange_ = true;
    
    QWidget* oldFocus = currentFocus_;
    
    // Set keyboard focus
    widget->setFocus(Qt::OtherFocusReason);
    
    // Ensure visible if requested
    if (ensureVisible) {
        scrollToWidget(widget, scrollSettings_.enabled);
    }
    
    processingFocusChange_ = false;
    
    // Handle focus change
    if (oldFocus != widget) {
        handleFocusChange(oldFocus, widget);
    }
}

void FocusManager::focusNext()
{
    if (!focusChainEnabled_) return;
    
    if (focusChainDirty_) {
        buildFocusChain();
    }
    
    if (focusChain_.isEmpty()) return;
    
    int currentIndex = findWidgetIndex(currentFocus_);
    int nextIndex = currentIndex + 1;
    
    if (nextIndex >= focusChain_.size()) {
        nextIndex = wrapAround_ ? 0 : focusChain_.size() - 1;
    }
    
    if (nextIndex != currentIndex && nextIndex < focusChain_.size()) {
        setFocus(focusChain_[nextIndex], true);
    }
}

void FocusManager::focusPrevious()
{
    if (!focusChainEnabled_) return;
    
    if (focusChainDirty_) {
        buildFocusChain();
    }
    
    if (focusChain_.isEmpty()) return;
    
    int currentIndex = findWidgetIndex(currentFocus_);
    int prevIndex = currentIndex - 1;
    
    if (prevIndex < 0) {
        prevIndex = wrapAround_ ? focusChain_.size() - 1 : 0;
    }
    
    if (prevIndex != currentIndex && prevIndex >= 0) {
        setFocus(focusChain_[prevIndex], true);
    }
}

void FocusManager::focusFirst()
{
    if (focusChainDirty_) {
        buildFocusChain();
    }
    
    if (!focusChain_.isEmpty()) {
        setFocus(focusChain_.first(), true);
    }
}

void FocusManager::focusLast()
{
    if (focusChainDirty_) {
        buildFocusChain();
    }
    
    if (!focusChain_.isEmpty()) {
        setFocus(focusChain_.last(), true);
    }
}

void FocusManager::focusGroup(const QString& group)
{
    for (const auto& item : focusItems_) {
        if (item.widget && item.group == group && item.acceptsKeyboardFocus) {
            setFocus(item.widget, true);
            break;
        }
    }
}

void FocusManager::ensureVisible(QWidget* widget, const QRect& rect)
{
    if (!widget) return;
    
    QAbstractScrollArea* scrollArea = findScrollArea(widget);
    if (!scrollArea) return;
    
    QRect targetRect = rect.isValid() ? rect : widget->rect();
    QPoint widgetPos = widget->mapTo(scrollArea->viewport(), targetRect.topLeft());
    QRect visibleRect(widgetPos, targetRect.size());
    
    // Add margin
    visibleRect.adjust(-scrollSettings_.scrollMargin, -scrollSettings_.scrollMargin,
                      scrollSettings_.scrollMargin, scrollSettings_.scrollMargin);
    
    // Calculate scroll position
    QScrollBar* hBar = scrollArea->horizontalScrollBar();
    QScrollBar* vBar = scrollArea->verticalScrollBar();
    
    int dx = 0, dy = 0;
    
    if (visibleRect.left() < 0) {
        dx = visibleRect.left();
    } else if (visibleRect.right() > scrollArea->viewport()->width()) {
        dx = visibleRect.right() - scrollArea->viewport()->width();
    }
    
    if (visibleRect.top() < 0) {
        dy = visibleRect.top();
    } else if (visibleRect.bottom() > scrollArea->viewport()->height()) {
        dy = visibleRect.bottom() - scrollArea->viewport()->height();
    }
    
    if (dx != 0 || dy != 0) {
        QPoint targetPos(hBar->value() + dx, vBar->value() + dy);
        
        if (scrollSettings_.enabled) {
            animateScroll(scrollArea, targetPos);
        } else {
            hBar->setValue(targetPos.x());
            vBar->setValue(targetPos.y());
        }
    }
}

void FocusManager::scrollToWidget(QWidget* widget, bool animate)
{
    ensureVisible(widget);
}

void FocusManager::saveScrollPosition(QWidget* widget)
{
    if (!widget || !widgetMap_.contains(widget)) return;
    
    QAbstractScrollArea* scrollArea = findScrollArea(widget);
    if (!scrollArea) return;
    
    FocusItem* item = widgetMap_[widget];
    item->lastScrollPosition = QPoint(
        scrollArea->horizontalScrollBar()->value(),
        scrollArea->verticalScrollBar()->value()
    );
    item->lastVisibleRect = scrollArea->viewport()->rect();
}

void FocusManager::restoreScrollPosition(QWidget* widget)
{
    if (!widget || !widgetMap_.contains(widget)) return;
    
    FocusItem* item = widgetMap_[widget];
    if (!item->restoreScrollPosition) return;
    
    QAbstractScrollArea* scrollArea = findScrollArea(widget);
    if (!scrollArea) return;
    
    if (scrollSettings_.enabled) {
        animateScroll(scrollArea, item->lastScrollPosition);
    } else {
        scrollArea->horizontalScrollBar()->setValue(item->lastScrollPosition.x());
        scrollArea->verticalScrollBar()->setValue(item->lastScrollPosition.y());
    }
}

void FocusManager::pushFocusHistory()
{
    if (currentFocus_) {
        focusHistory_.append(currentFocus_);
        
        // Limit history size
        while (focusHistory_.size() > maxHistorySize_) {
            focusHistory_.removeFirst();
        }
    }
}

void FocusManager::popFocusHistory()
{
    if (focusHistory_.isEmpty()) return;
    
    QWidget* widget = nullptr;
    
    // Find last valid widget
    while (!focusHistory_.isEmpty() && !widget) {
        widget = focusHistory_.takeLast();
    }
    
    if (widget) {
        setFocus(widget, true);
    }
}

void FocusManager::clearFocusHistory()
{
    focusHistory_.clear();
}

QStringList FocusManager::groups() const
{
    QStringList result;
    for (const auto& item : focusItems_) {
        if (!item.group.isEmpty() && !result.contains(item.group)) {
            result.append(item.group);
        }
    }
    return result;
}

QList<QWidget*> FocusManager::widgetsInGroup(const QString& group) const
{
    QList<QWidget*> result;
    for (const auto& item : focusItems_) {
        if (item.widget && item.group == group) {
            result.append(item.widget);
        }
    }
    return result;
}

QString FocusManager::currentFocusGroup() const
{
    if (currentFocus_ && widgetMap_.contains(currentFocus_)) {
        return widgetMap_[currentFocus_]->group;
    }
    return QString();
}

bool FocusManager::eventFilter(QObject* watched, QEvent* event)
{
    if (auto* widget = qobject_cast<QWidget*>(watched)) {
        switch (event->type()) {
        case QEvent::FocusIn:
            if (!processingFocusChange_) {
                handleFocusChange(currentFocus_, widget);
            }
            break;
            
        case QEvent::KeyPress:
            if (focusChainEnabled_) {
                auto* keyEvent = static_cast<QKeyEvent*>(event);
                if (keyEvent->key() == Qt::Key_Tab) {
                    if (keyEvent->modifiers() & Qt::ShiftModifier) {
                        focusPrevious();
                    } else {
                        focusNext();
                    }
                    return true;
                }
            }
            break;
            
        case QEvent::Show:
            if (autoRestoreFocus_ && widgetMap_.contains(widget)) {
                restoreScrollPosition(widget);
            }
            break;
            
        case QEvent::Hide:
            if (widgetMap_.contains(widget)) {
                saveScrollPosition(widget);
            }
            break;
            
        default:
            break;
        }
    }
    
    return QObject::eventFilter(watched, event);
}

void FocusManager::onWidgetDestroyed(QObject* obj)
{
    auto* widget = static_cast<QWidget*>(obj);
    unregisterWidget(widget);
}

void FocusManager::onScrollAnimationFinished()
{
    auto* animation = qobject_cast<QPropertyAnimation*>(sender());
    if (!animation) return;
    
    // Find and remove animation
    for (auto it = scrollAnimations_.begin(); it != scrollAnimations_.end(); ++it) {
        if (it.value() == animation) {
            emit scrollFinished(it.key()->parent());
            scrollAnimations_.erase(it);
            animation->deleteLater();
            break;
        }
    }
}

void FocusManager::updateFocusChain()
{
    buildFocusChain();
}

QAbstractScrollArea* FocusManager::findScrollArea(QWidget* widget) const
{
    if (!widget) return nullptr;
    
    // Check if widget itself is a scroll area
    if (auto* scrollArea = qobject_cast<QAbstractScrollArea*>(widget)) {
        return scrollArea;
    }
    
    // Find parent scroll area
    QWidget* parent = widget->parentWidget();
    while (parent) {
        if (auto* scrollArea = qobject_cast<QAbstractScrollArea*>(parent)) {
            return scrollArea;
        }
        parent = parent->parentWidget();
    }
    
    return nullptr;
}

void FocusManager::animateScroll(QAbstractScrollArea* scrollArea, const QPoint& targetPos)
{
    if (!scrollArea) return;
    
    // Stop existing animation
    if (scrollAnimations_.contains(scrollArea)) {
        scrollAnimations_[scrollArea]->stop();
        delete scrollAnimations_[scrollArea];
    }
    
    // Create property animation for scroll position
    auto* animation = new QPropertyAnimation(this);
    animation->setDuration(scrollSettings_.duration);
    animation->setEasingCurve(scrollSettings_.easingCurve);
    
    // Animate both scrollbars
    QPoint startPos(
        scrollArea->horizontalScrollBar()->value(),
        scrollArea->verticalScrollBar()->value()
    );
    
    animation->setStartValue(startPos);
    animation->setEndValue(targetPos);
    
    connect(animation, &QPropertyAnimation::valueChanged, [scrollArea](const QVariant& value) {
        QPoint pos = value.toPoint();
        scrollArea->horizontalScrollBar()->setValue(pos.x());
        scrollArea->verticalScrollBar()->setValue(pos.y());
    });
    
    connect(animation, &QPropertyAnimation::finished, this, &FocusManager::onScrollAnimationFinished);
    
    scrollAnimations_[scrollArea] = animation;
    animation->start(QAbstractAnimation::DeleteWhenStopped);
    
    emit scrollStarted(scrollArea->parent());
}

void FocusManager::installEventFilters(QWidget* widget)
{
    if (!widget) return;
    
    widget->installEventFilter(this);
    
    // Also install on parent scroll areas for scroll tracking
    if (auto* scrollArea = findScrollArea(widget)) {
        scrollArea->installEventFilter(this);
    }
}

void FocusManager::removeEventFilters(QWidget* widget)
{
    if (!widget) return;
    
    widget->removeEventFilter(this);
    
    if (auto* scrollArea = findScrollArea(widget)) {
        scrollArea->removeEventFilter(this);
    }
}

void FocusManager::handleFocusChange(QWidget* oldWidget, QWidget* newWidget)
{
    currentFocus_ = newWidget;
    
    // Update focus group
    QString oldGroup = oldWidget && widgetMap_.contains(oldWidget) ? 
        widgetMap_[oldWidget]->group : QString();
    QString newGroup = newWidget && widgetMap_.contains(newWidget) ? 
        widgetMap_[newWidget]->group : QString();
    
    if (oldGroup != newGroup) {
        lastFocusGroup_ = oldGroup;
        emit focusGroupChanged(oldGroup, newGroup);
    }
    
    emit focusChanged(oldWidget, newWidget);
    
    // Smart focus features
    if (smartFocusEnabled_ && newWidget) {
        // Auto-save old position
        if (oldWidget && widgetMap_.contains(oldWidget)) {
            saveScrollPosition(oldWidget);
        }
        
        // Push to history
        if (oldWidget) {
            pushFocusHistory();
        }
    }
}

void FocusManager::buildFocusChain()
{
    focusChain_.clear();
    
    // Collect focusable widgets
    QList<FocusItem*> focusableItems;
    for (auto& item : focusItems_) {
        if (item.widget && item.acceptsKeyboardFocus && 
            item.widget->isVisible() && item.widget->isEnabled()) {
            focusableItems.append(&item);
        }
    }
    
    // Sort by priority and group
    std::sort(focusableItems.begin(), focusableItems.end(),
              [](const FocusItem* a, const FocusItem* b) {
        if (a->group != b->group) {
            return a->group < b->group;
        }
        return a->priority > b->priority;
    });
    
    // Build chain
    for (auto* item : focusableItems) {
        focusChain_.append(item->widget);
    }
    
    focusChainDirty_ = false;
}

int FocusManager::findWidgetIndex(QWidget* widget) const
{
    if (!widget) return -1;
    
    for (int i = 0; i < focusChain_.size(); ++i) {
        if (focusChain_[i] == widget) {
            return i;
        }
    }
    
    return -1;
}

// FocusHighlight implementation
FocusHighlight::FocusHighlight(QWidget* parent)
    : QWidget(parent)
{
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_TransparentForMouseEvents);
    
    // Create animations
    positionAnimation_ = new QPropertyAnimation(this, "pos", this);
    positionAnimation_->setDuration(animationDuration_);
    positionAnimation_->setEasingCurve(QEasingCurve::InOutQuad);
    
    sizeAnimation_ = new QPropertyAnimation(this, "size", this);
    sizeAnimation_->setDuration(animationDuration_);
    sizeAnimation_->setEasingCurve(QEasingCurve::InOutQuad);
    
    auto* effect = new QGraphicsOpacityEffect(this);
    setGraphicsEffect(effect);
    
    opacityAnimation_ = new QPropertyAnimation(effect, "opacity", this);
    opacityAnimation_->setDuration(animationDuration_);
    opacityAnimation_->setEasingCurve(QEasingCurve::InOutQuad);
    
    connect(opacityAnimation_, &QPropertyAnimation::finished, 
            this, &FocusHighlight::onAnimationFinished);
    
    hide();
}

void FocusHighlight::highlightWidget(QWidget* widget)
{
    if (!widget) {
        animateOut();
        return;
    }
    
    targetWidget_ = widget;
    
    // Connect to widget's events
    if (targetWidget_) {
        connect(targetWidget_, &QWidget::destroyed, this, &FocusHighlight::hide);
        
        // Use timer to update position
        auto* timer = new QTimer(this);
        timer->setInterval(16); // ~60 FPS
        connect(timer, &QTimer::timeout, this, &FocusHighlight::updatePosition);
        timer->start();
    }
    
    updatePosition();
    animateIn();
}

void FocusHighlight::animateIn()
{
    show();
    raise();
    
    opacityAnimation_->setStartValue(0.0);
    opacityAnimation_->setEndValue(1.0);
    opacityAnimation_->start();
}

void FocusHighlight::animateOut()
{
    opacityAnimation_->setStartValue(1.0);
    opacityAnimation_->setEndValue(0.0);
    opacityAnimation_->start();
}

void FocusHighlight::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event)
    
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    
    // Draw highlight border
    QPen pen(highlightColor_, highlightWidth_);
    pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    
    QRect rect = this->rect().adjusted(
        highlightWidth_ / 2, highlightWidth_ / 2,
        -highlightWidth_ / 2, -highlightWidth_ / 2
    );
    
    painter.drawRoundedRect(rect, 4, 4);
}

void FocusHighlight::updatePosition()
{
    if (!targetWidget_ || !targetWidget_->isVisible()) {
        hide();
        return;
    }
    
    // Get global position and size
    QPoint globalPos = targetWidget_->mapToGlobal(QPoint(0, 0));
    QSize targetSize = targetWidget_->size();
    
    // Add padding for highlight
    int padding = highlightWidth_ + 2;
    globalPos -= QPoint(padding, padding);
    targetSize += QSize(padding * 2, padding * 2);
    
    // Animate to new position/size
    if (pos() != globalPos) {
        if (isVisible()) {
            positionAnimation_->setStartValue(pos());
            positionAnimation_->setEndValue(globalPos);
            positionAnimation_->start();
        } else {
            move(globalPos);
        }
    }
    
    if (size() != targetSize) {
        if (isVisible()) {
            sizeAnimation_->setStartValue(size());
            sizeAnimation_->setEndValue(targetSize);
            sizeAnimation_->start();
        } else {
            resize(targetSize);
        }
    }
}

void FocusHighlight::onAnimationFinished()
{
    if (opacityAnimation_->endValue().toReal() == 0.0) {
        hide();
    }
}

// ScrollPositionTracker implementation
ScrollPositionTracker::ScrollPositionTracker(QObject* parent)
    : QObject(parent)
{
    saveTimer_ = new QTimer(this);
    saveTimer_->setSingleShot(true);
    connect(saveTimer_, &QTimer::timeout, this, &ScrollPositionTracker::onSaveTimeout);
}

void ScrollPositionTracker::trackWidget(QWidget* widget)
{
    if (!widget) return;
    
    widget->installEventFilter(this);
    
    // Also track parent scroll area
    QWidget* parent = widget->parentWidget();
    while (parent) {
        if (qobject_cast<QAbstractScrollArea*>(parent)) {
            parent->installEventFilter(this);
            break;
        }
        parent = parent->parentWidget();
    }
}

void ScrollPositionTracker::untrackWidget(QWidget* widget)
{
    if (!widget) return;
    
    widget->removeEventFilter(this);
    positions_.remove(widget);
}

void ScrollPositionTracker::savePosition(QWidget* widget)
{
    if (!widget) return;
    
    // Find parent scroll area
    QAbstractScrollArea* scrollArea = nullptr;
    QWidget* parent = widget->parentWidget();
    while (parent) {
        if ((scrollArea = qobject_cast<QAbstractScrollArea*>(parent))) {
            break;
        }
        parent = parent->parentWidget();
    }
    
    if (!scrollArea) return;
    
    PositionInfo info;
    info.scrollPosition = QPoint(
        scrollArea->horizontalScrollBar()->value(),
        scrollArea->verticalScrollBar()->value()
    );
    info.visibleRect = scrollArea->viewport()->rect();
    info.timestamp = QDateTime::currentDateTime();
    
    positions_[widget] = info;
    
    emit positionSaved(widget, info.scrollPosition);
}

void ScrollPositionTracker::restorePosition(QWidget* widget, bool animate)
{
    if (!widget || !positions_.contains(widget)) return;
    
    const PositionInfo& info = positions_[widget];
    
    // Find parent scroll area
    QAbstractScrollArea* scrollArea = nullptr;
    QWidget* parent = widget->parentWidget();
    while (parent) {
        if ((scrollArea = qobject_cast<QAbstractScrollArea*>(parent))) {
            break;
        }
        parent = parent->parentWidget();
    }
    
    if (!scrollArea) return;
    
    if (animate) {
        // Could add animation support here
        scrollArea->horizontalScrollBar()->setValue(info.scrollPosition.x());
        scrollArea->verticalScrollBar()->setValue(info.scrollPosition.y());
    } else {
        scrollArea->horizontalScrollBar()->setValue(info.scrollPosition.x());
        scrollArea->verticalScrollBar()->setValue(info.scrollPosition.y());
    }
    
    emit positionRestored(widget, info.scrollPosition);
}

bool ScrollPositionTracker::eventFilter(QObject* watched, QEvent* event)
{
    if (!autoSave_) return false;
    
    auto* widget = qobject_cast<QWidget*>(watched);
    if (!widget) return false;
    
    switch (event->type()) {
    case QEvent::Scroll:
    case QEvent::Resize:
        // Delay save to avoid too many saves
        pendingSaveWidget_ = widget;
        saveTimer_->stop();
        saveTimer_->start(saveDelay_);
        break;
        
    case QEvent::Hide:
        // Save immediately when hiding
        savePosition(widget);
        break;
        
    case QEvent::Show:
        // Restore when showing
        restorePosition(widget, false);
        break;
        
    default:
        break;
    }
    
    return false;
}

void ScrollPositionTracker::onSaveTimeout()
{
    if (pendingSaveWidget_) {
        savePosition(pendingSaveWidget_);
        pendingSaveWidget_ = nullptr;
    }
}

// KeyboardNavigator implementation
KeyboardNavigator::KeyboardNavigator(QObject* parent)
    : QObject(parent)
{
}

void KeyboardNavigator::setNextKey(Qt::Key key, Qt::KeyboardModifiers modifiers)
{
    nextKey_ = key;
    nextModifiers_ = modifiers;
}

void KeyboardNavigator::setPreviousKey(Qt::Key key, Qt::KeyboardModifiers modifiers)
{
    previousKey_ = key;
    previousModifiers_ = modifiers;
}

void KeyboardNavigator::setActivateKey(Qt::Key key, Qt::KeyboardModifiers modifiers)
{
    activateKey_ = key;
    activateModifiers_ = modifiers;
}

void KeyboardNavigator::registerNavigationKey(Qt::Key key, Qt::KeyboardModifiers modifiers,
                                            std::function<void()> action)
{
    KeyBinding binding;
    binding.key = key;
    binding.modifiers = modifiers;
    binding.action = action;
    
    keyBindings_.append(binding);
}

void KeyboardNavigator::clearNavigationKeys()
{
    keyBindings_.clear();
}

bool KeyboardNavigator::eventFilter(QObject* watched, QEvent* event)
{
    if (!enabled_ || event->type() != QEvent::KeyPress) {
        return false;
    }
    
    auto* keyEvent = static_cast<QKeyEvent*>(event);
    return handleKeyPress(keyEvent);
}

bool KeyboardNavigator::handleKeyPress(QKeyEvent* event)
{
    Qt::Key key = static_cast<Qt::Key>(event->key());
    Qt::KeyboardModifiers modifiers = event->modifiers();
    
    // Check custom bindings first
    for (const auto& binding : keyBindings_) {
        if (binding.key == key && binding.modifiers == modifiers) {
            binding.action();
            return true;
        }
    }
    
    // Check default navigation keys
    if (key == nextKey_ && modifiers == nextModifiers_) {
        emit navigateNext();
        return true;
    }
    
    if (key == previousKey_ && modifiers == previousModifiers_) {
        emit navigatePrevious();
        return true;
    }
    
    if (key == activateKey_ && modifiers == activateModifiers_) {
        emit activate();
        return true;
    }
    
    
    return false;
}

// FocusScope implementation
FocusScope::FocusScope(QWidget* parent)
    : QWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
}

void FocusScope::setFocusProxy(QWidget* proxy)
{
    QWidget::setFocusProxy(proxy);
}

void FocusScope::addWidget(QWidget* widget)
{
    if (!widget || widgets_.contains(widget)) return;
    
    widgets_.append(widget);
    widget->setParent(this);
}

void FocusScope::removeWidget(QWidget* widget)
{
    widgets_.removeAll(widget);
}

void FocusScope::focusInEvent(QFocusEvent* event)
{
    emit focusEntered();
    
    // Focus first widget
    for (auto* widget : widgets_) {
        if (widget->isVisible() && widget->isEnabled() && 
            widget->focusPolicy() != Qt::NoFocus) {
            widget->setFocus(event->reason());
            break;
        }
    }
    
    QWidget::focusInEvent(event);
}

void FocusScope::focusOutEvent(QFocusEvent* event)
{
    emit focusLeft();
    QWidget::focusOutEvent(event);
}

bool FocusScope::focusNextPrevChild(bool next)
{
    if (!trapFocus_) {
        return QWidget::focusNextPrevChild(next);
    }
    
    // Find current focus widget
    QWidget* current = QApplication::focusWidget();
    if (!current || !widgets_.contains(current)) {
        // Focus first/last widget
        if (!widgets_.isEmpty()) {
            if (next) {
                widgets_.first()->setFocus();
            } else {
                widgets_.last()->setFocus();
            }
        }
        return true;
    }
    
    // Find next/previous widget
    int index = widgets_.indexOf(current);
    int newIndex = next ? index + 1 : index - 1;
    
    if (newIndex < 0) {
        newIndex = widgets_.size() - 1;
    } else if (newIndex >= widgets_.size()) {
        newIndex = 0;
    }
    
    if (newIndex != index && newIndex < widgets_.size()) {
        widgets_[newIndex]->setFocus();
        return true;
    }
    
    return false;
}

// FocusManagerFactory implementation
FocusManager* FocusManagerFactory::globalManager_ = nullptr;

FocusManager* FocusManagerFactory::createFocusManager(QWidget* rootWidget)
{
    auto* manager = new FocusManager(rootWidget);
    
    // Auto-register children
    if (rootWidget) {
        QList<QWidget*> children = rootWidget->findChildren<QWidget*>();
        for (auto* child : children) {
            if (child->focusPolicy() != Qt::NoFocus) {
                manager->registerWidget(child);
            }
        }
    }
    
    return manager;
}

void FocusManagerFactory::installGlobalFocusManager(FocusManager* manager)
{
    globalManager_ = manager;
}

FocusManager* FocusManagerFactory::globalFocusManager()
{
    return globalManager_;
}

} // namespace llm_re::ui_v2