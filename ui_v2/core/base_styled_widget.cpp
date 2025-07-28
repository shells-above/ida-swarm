#include "base_styled_widget.h"
#include <QPainter>
#include <QPainterPath>
#include <QTimer>
#include <QStyleOption>
#include <cmath>

namespace llm_re::ui_v2 {

BaseStyledWidget::BaseStyledWidget(QWidget* parent)
    : QWidget(parent) {
    
    // Connect to theme changes
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged,
            this, &BaseStyledWidget::onThemeManagerChanged);
    connect(&ThemeManager::instance(), &ThemeManager::colorsChanged,
            this, &BaseStyledWidget::onThemeManagerChanged);
    
    // Apply initial theme
    applyTheme();
    
    // Setup loading animation timer
    loadingTimer_ = new QTimer(this);
    loadingTimer_->setInterval(50);
    connect(loadingTimer_, &QTimer::timeout, [this]() {
        loadingAngle_ = (loadingAngle_ + 10) % 360;
        update();
    });
}

void BaseStyledWidget::applyTheme() {
    const auto& theme = ThemeManager::instance();
    backgroundColor_ = theme.colors().surface;
    borderColor_ = theme.colors().border;
    shadowColor_ = theme.colors().shadow;
    focusOutlineColor_ = theme.colors().primary;
    
    updateShadow();
    updateStyleSheet();
    onThemeChanged();
    update();
}

void BaseStyledWidget::setCustomStyleSheet(const QString& styleSheet) {
    customStyleSheet_ = styleSheet;
    updateStyleSheet();
}

void BaseStyledWidget::updateStyleSheet() {
    QString styleSheet = customStyleSheet_;
    
    // Add component-specific styles if available
    QString componentName = metaObject()->className();
    if (componentName.contains("::")) {
        componentName = componentName.split("::").last();
    }
    
    QString componentQss = ThemeManager::instance().componentQss(componentName);
    if (!componentQss.isEmpty()) {
        styleSheet += "\n" + componentQss;
    }
    
    setStyleSheet(styleSheet);
}

void BaseStyledWidget::setBackgroundColor(const QColor& color) {
    if (backgroundColor_ != color) {
        backgroundColor_ = color;
        update();
    }
}

void BaseStyledWidget::setBorderColor(const QColor& color) {
    if (borderColor_ != color) {
        borderColor_ = color;
        update();
    }
}

void BaseStyledWidget::setBorderRadius(int radius) {
    if (borderRadius_ != radius) {
        borderRadius_ = radius;
        update();
    }
}

void BaseStyledWidget::setBorderWidth(int width) {
    if (borderWidth_ != width) {
        borderWidth_ = width;
        update();
    }
}

void BaseStyledWidget::setShadowEnabled(bool enabled) {
    if (shadowEnabled_ != enabled) {
        shadowEnabled_ = enabled;
        updateShadow();
    }
}

void BaseStyledWidget::setShadowBlur(int blur) {
    shadowBlur_ = blur;
    if (shadowEffect_) {
        shadowEffect_->setBlurRadius(blur);
    }
}

void BaseStyledWidget::setShadowColor(const QColor& color) {
    shadowColor_ = color;
    if (shadowEffect_) {
        shadowEffect_->setColor(color);
    }
}

void BaseStyledWidget::setShadowOffset(const QPointF& offset) {
    shadowOffset_ = offset;
    if (shadowEffect_) {
        shadowEffect_->setOffset(offset);
    }
}

void BaseStyledWidget::updateShadow() {
    if (shadowEnabled_ && !shadowEffect_) {
        shadowEffect_ = std::make_unique<QGraphicsDropShadowEffect>();
        shadowEffect_->setBlurRadius(shadowBlur_);
        shadowEffect_->setColor(shadowColor_);
        shadowEffect_->setOffset(shadowOffset_);
        setGraphicsEffect(shadowEffect_.get());
    } else if (!shadowEnabled_ && shadowEffect_) {
        setGraphicsEffect(nullptr);
        shadowEffect_.reset();
    }
}

void BaseStyledWidget::setAnimationProgress(qreal progress) {
    animationProgress_ = progress;
    update();
}

void BaseStyledWidget::setHoverEnabled(bool enabled) {
    hoverEnabled_ = enabled;
    setAttribute(Qt::WA_Hover, enabled);
}

void BaseStyledWidget::setFocusOutlineEnabled(bool enabled) {
    focusOutlineEnabled_ = enabled;
    update();
}

void BaseStyledWidget::setFocusOutlineColor(const QColor& color) {
    focusOutlineColor_ = color;
    if (hasFocus()) {
        update();
    }
}

void BaseStyledWidget::setFocusOutlineWidth(int width) {
    focusOutlineWidth_ = width;
    if (hasFocus()) {
        update();
    }
}

void BaseStyledWidget::setLoading(bool loading) {
    if (isLoading_ != loading) {
        isLoading_ = loading;
        if (loading) {
            loadingTimer_->start();
        } else {
            loadingTimer_->stop();
        }
        update();
    }
}

void BaseStyledWidget::paintEvent(QPaintEvent* event) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    
    // Paint in order
    paintBackground(&painter);
    paintBorder(&painter);
    paintContent(&painter);
    
    if (isLoading_) {
        paintLoadingIndicator(&painter);
    }
    
    if (hasFocus() && focusOutlineEnabled_) {
        paintFocusOutline(&painter);
    }
}

void BaseStyledWidget::paintBackground(QPainter* painter) {
    QPainterPath path;
    path.addRoundedRect(rect(), borderRadius_, borderRadius_);
    
    painter->fillPath(path, effectiveBackgroundColor());
}

void BaseStyledWidget::paintBorder(QPainter* painter) {
    if (borderWidth_ <= 0) return;
    
    QPainterPath path;
    QRectF borderRect = rect().adjusted(
        borderWidth_ / 2.0, borderWidth_ / 2.0,
        -borderWidth_ / 2.0, -borderWidth_ / 2.0
    );
    path.addRoundedRect(borderRect, borderRadius_, borderRadius_);
    
    painter->setPen(QPen(effectiveBorderColor(), borderWidth_));
    painter->setBrush(Qt::NoBrush);
    painter->drawPath(path);
}

void BaseStyledWidget::paintContent(QPainter* painter) {
    // Override in subclasses
}

void BaseStyledWidget::paintLoadingIndicator(QPainter* painter) {
    painter->save();
    
    // Draw circular loading indicator
    QPointF center = rect().center();
    int radius = 20;
    
    painter->translate(center);
    painter->rotate(loadingAngle_);
    
    // Draw arc
    QPen pen(ThemeManager::instance().colors().primary, 3);
    pen.setCapStyle(Qt::RoundCap);
    painter->setPen(pen);
    
    QRectF arcRect(-radius, -radius, radius * 2, radius * 2);
    painter->drawArc(arcRect, 0, 270 * 16); // 270 degree arc
    
    painter->restore();
}

void BaseStyledWidget::paintFocusOutline(QPainter* painter) {
    QPainterPath path;
    QRectF outlineRect = rect().adjusted(-1, -1, 1, 1);
    path.addRoundedRect(outlineRect, borderRadius_ + 1, borderRadius_ + 1);
    
    QPen pen(focusOutlineColor_, focusOutlineWidth_);
    pen.setStyle(Qt::SolidLine);
    painter->setPen(pen);
    painter->setBrush(Qt::NoBrush);
    painter->drawPath(path);
}

void BaseStyledWidget::enterEvent(QEvent* event) {
    QWidget::enterEvent(event);
    if (hoverEnabled_) {
        isHovered_ = true;
        startHoverAnimation(true);
    }
}

void BaseStyledWidget::leaveEvent(QEvent* event) {
    QWidget::leaveEvent(event);
    if (hoverEnabled_) {
        isHovered_ = false;
        startHoverAnimation(false);
    }
}

void BaseStyledWidget::focusInEvent(QFocusEvent* event) {
    QWidget::focusInEvent(event);
    if (focusOutlineEnabled_) {
        update();
    }
}

void BaseStyledWidget::focusOutEvent(QFocusEvent* event) {
    QWidget::focusOutEvent(event);
    if (focusOutlineEnabled_) {
        update();
    }
}

void BaseStyledWidget::changeEvent(QEvent* event) {
    QWidget::changeEvent(event);
    if (event->type() == QEvent::EnabledChange) {
        update();
    }
}

void BaseStyledWidget::animateProperty(const QByteArray& property, 
                                       const QVariant& endValue, 
                                       int duration) {
    // Stop existing animation for this property
    stopAnimation(property);
    
    QPropertyAnimation* anim = new QPropertyAnimation(this, property, this);
    anim->setDuration(duration);
    anim->setEndValue(endValue);
    anim->setEasingCurve(QEasingCurve::OutCubic);
    
    animations_[property] = anim;
    
    connect(anim, &QPropertyAnimation::finished, [this, property]() {
        animations_.erase(property);
    });
    
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

void BaseStyledWidget::stopAnimation(const QByteArray& property) {
    auto it = animations_.find(property);
    if (it != animations_.end()) {
        it->second->stop();
        delete it->second;
        animations_.erase(it);
    }
}

void BaseStyledWidget::onThemeChanged() {
    // Override in subclasses for specific theme handling
}

void BaseStyledWidget::onThemeManagerChanged() {
    applyTheme();
}

QColor BaseStyledWidget::effectiveBackgroundColor() const {
    QColor color = backgroundColor_;
    
    if (!isEnabled()) {
        color = ThemeManager::mix(color, ThemeManager::instance().colors().background, 0.5);
    } else if (isHovered_ && hoverEnabled_) {
        color = ThemeManager::instance().colors().surfaceHover;
    }
    
    return color;
}

QColor BaseStyledWidget::effectiveBorderColor() const {
    QColor color = borderColor_;
    
    if (!isEnabled()) {
        color = ThemeManager::adjustAlpha(color, 128);
    } else if (hasFocus()) {
        color = ThemeManager::instance().colors().primary;
    } else if (isHovered_ && hoverEnabled_) {
        color = ThemeManager::instance().colors().borderStrong;
    }
    
    return color;
}

qreal BaseStyledWidget::effectiveOpacity() const {
    if (!isEnabled()) {
        return disabledOpacity_;
    } else if (isHovered_ && hoverEnabled_) {
        return hoverOpacity_;
    }
    return 1.0;
}

void BaseStyledWidget::startHoverAnimation(bool hovering) {
    if (hovering) {
        animateProperty("scale", hoverScale_);
        animateProperty("opacity", hoverOpacity_);
    } else {
        animateProperty("scale", 1.0);
        animateProperty("opacity", 1.0);
    }
}

// CardWidget implementation
CardWidget::CardWidget(QWidget* parent)
    : BaseStyledWidget(parent) {
    setShadowEnabled(true);
    updateElevation();
}

void CardWidget::setElevation(int level) {
    elevation_ = qBound(0, level, 5);
    updateElevation();
}

void CardWidget::updateElevation() {
    // Shadow properties based on elevation level
    const int blurRadius[] = {0, 4, 8, 12, 16, 24};
    const QPointF offsets[] = {
        QPointF(0, 0), QPointF(0, 2), QPointF(0, 4),
        QPointF(0, 6), QPointF(0, 8), QPointF(0, 12)
    };
    
    setShadowBlur(blurRadius[elevation_]);
    setShadowOffset(offsets[elevation_]);
    
    // Adjust shadow opacity based on theme
    int alpha = ThemeManager::instance().currentTheme() == ThemeManager::Theme::Dark ? 
                60 + elevation_ * 10 : 30 + elevation_ * 8;
    setShadowColor(ThemeManager::adjustAlpha(ThemeManager::instance().colors().shadow, alpha));
}

void CardWidget::onThemeChanged() {
    BaseStyledWidget::onThemeChanged();
    updateElevation(); // Reapply elevation with new theme colors
}

// PanelWidget implementation
PanelWidget::PanelWidget(QWidget* parent)
    : BaseStyledWidget(parent) {
    setBorderWidth(0);
    setBorderRadius(0);
}

void PanelWidget::setInset(bool inset) {
    if (inset_ != inset) {
        inset_ = inset;
        onThemeChanged();
    }
}

void PanelWidget::onThemeChanged() {
    BaseStyledWidget::onThemeChanged();
    
    const auto& theme = ThemeManager::instance();
    if (inset_) {
        setBackgroundColor(theme.colors().background);
        setBorderWidth(1);
        setBorderColor(theme.colors().border);
    } else {
        setBackgroundColor(theme.colors().surface);
        setBorderWidth(0);
    }
}

} // namespace llm_re::ui_v2