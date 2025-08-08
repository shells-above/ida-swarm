#include "effects_manager.h"
#include "color_constants.h"
#include "theme_manager.h"

namespace llm_re::ui_v2 {

EffectsManager& EffectsManager::instance() {
    static EffectsManager instance;
    return instance;
}

EffectsManager::EffectsManager() {
}

QGraphicsDropShadowEffect* EffectsManager::createShadow(ShadowStyle style, 
                                                       const QColor& color,
                                                       qreal blur,
                                                       const QPointF& offset) {
    auto& manager = instance();
    if (!manager.effectsEnabled_) return nullptr;
    
    auto* shadow = new QGraphicsDropShadowEffect();
    
    // Use provided values or defaults for style
    QColor shadowColor = color.isValid() ? color : shadowColorForStyle(style);
    qreal shadowBlur = (blur >= 0) ? blur : shadowBlurForStyle(style);
    QPointF shadowOffset = (offset.x() != -999) ? offset : shadowOffsetForStyle(style);
    
    // Adjust for quality
    shadowBlur *= manager.effectQuality_ / 100.0;
    
    shadow->setColor(shadowColor);
    shadow->setBlurRadius(shadowBlur);
    shadow->setOffset(shadowOffset);
    
    return shadow;
}

void EffectsManager::applyShadow(QWidget* widget, ShadowStyle style) {
    if (!widget) return;
    
    auto& manager = instance();
    
    // Remove existing shadow
    removeShadow(widget);
    
    if (style == ShadowStyle::None || !manager.effectsEnabled_) return;
    
    auto* shadow = createShadow(style);
    if (shadow) {
        widget->setGraphicsEffect(shadow);
        manager.registerEffect(widget, EffectType::Shadow, shadow);
    }
}

void EffectsManager::removeShadow(QWidget* widget) {
    if (!widget) return;
    
    auto& manager = instance();
    auto* effect = manager.getEffect(widget, EffectType::Shadow);
    
    if (effect && widget->graphicsEffect() == effect) {
        widget->setGraphicsEffect(nullptr);
        manager.unregisterEffect(widget, EffectType::Shadow);
    }
}

void EffectsManager::updateShadow(QWidget* widget, ShadowStyle style) {
    applyShadow(widget, style);
}

void EffectsManager::applyGlow(QWidget* widget, GlowStyle style, const QColor& color) {
    if (!widget) return;
    
    auto& manager = instance();
    if (!manager.effectsEnabled_) return;
    
    // For glow, we use a shadow effect with specific parameters
    QColor glowColor = color.isValid() ? color : ThemeManager::instance().colors().primary;
    
    qreal blur = 20.0;
    QPointF offset(0, 0);
    
    switch (style) {
    case GlowStyle::Soft:
        blur = 15.0;
        glowColor.setAlpha(80);
        break;
    case GlowStyle::Neon:
        blur = 30.0;
        glowColor.setAlpha(200);
        break;
    case GlowStyle::Pulse:
        blur = 25.0;
        glowColor.setAlpha(150);
        // TODO: Add pulsing animation
        break;
    case GlowStyle::Rainbow:
        // TODO: Implement rainbow effect
        blur = 25.0;
        break;
    case GlowStyle::Halo:
        blur = 40.0;
        glowColor.setAlpha(60);
        break;
    }
    
    auto* glow = new QGraphicsDropShadowEffect();
    glow->setColor(glowColor);
    glow->setBlurRadius(blur * manager.effectQuality_ / 100.0);
    glow->setOffset(offset);
    
    widget->setGraphicsEffect(glow);
    manager.registerEffect(widget, EffectType::Glow, glow);
}

void EffectsManager::removeGlow(QWidget* widget) {
    if (!widget) return;
    
    auto& manager = instance();
    auto* effect = manager.getEffect(widget, EffectType::Glow);
    
    if (effect && widget->graphicsEffect() == effect) {
        widget->setGraphicsEffect(nullptr);
        manager.unregisterEffect(widget, EffectType::Glow);
    }
}

void EffectsManager::applyBlur(QWidget* widget, qreal radius) {
    if (!widget) return;
    
    auto& manager = instance();
    if (!manager.effectsEnabled_) return;
    
    auto* blur = new QGraphicsBlurEffect();
    blur->setBlurRadius(radius * manager.effectQuality_ / 100.0);
    
    widget->setGraphicsEffect(blur);
    manager.registerEffect(widget, EffectType::Blur, blur);
}

void EffectsManager::removeBlur(QWidget* widget) {
    if (!widget) return;
    
    auto& manager = instance();
    auto* effect = manager.getEffect(widget, EffectType::Blur);
    
    if (effect && widget->graphicsEffect() == effect) {
        widget->setGraphicsEffect(nullptr);
        manager.unregisterEffect(widget, EffectType::Blur);
    }
}

void EffectsManager::applyGlassMorphism(QWidget* widget, qreal blurRadius, qreal opacity) {
    if (!widget) return;
    
    auto& manager = instance();
    if (!manager.effectsEnabled_) return;
    
    // Apply blur
    applyBlur(widget, blurRadius);
    
    // Set opacity
    widget->setWindowOpacity(opacity);
    
    // Add glass-like styling
    widget->setStyleSheet(widget->styleSheet() + 
        QString("\nbackground-color: rgba(255, 255, 255, %1);"
                "border: 1px solid rgba(255, 255, 255, %2);")
        .arg(int(opacity * 20))
        .arg(int(opacity * 50)));
}

QLinearGradient EffectsManager::createLinearGradient(const QPointF& start, const QPointF& end,
                                                     const QList<QPair<qreal, QColor>>& stops) {
    QLinearGradient gradient(start, end);
    for (const auto& stop : stops) {
        gradient.setColorAt(stop.first, stop.second);
    }
    return gradient;
}

QRadialGradient EffectsManager::createRadialGradient(const QPointF& center, qreal radius,
                                                     const QList<QPair<qreal, QColor>>& stops) {
    QRadialGradient gradient(center, radius);
    for (const auto& stop : stops) {
        gradient.setColorAt(stop.first, stop.second);
    }
    return gradient;
}

QConicalGradient EffectsManager::createConicalGradient(const QPointF& center, qreal angle,
                                                       const QList<QPair<qreal, QColor>>& stops) {
    QConicalGradient gradient(center, angle);
    for (const auto& stop : stops) {
        gradient.setColorAt(stop.first, stop.second);
    }
    return gradient;
}

QLinearGradient EffectsManager::primaryGradient(const QRectF& rect, qreal angle) {
    const auto& colors = ThemeManager::instance().colors();
    
    // Convert angle to start/end points
    qreal rad = angle * M_PI / 180.0;
    QPointF center = rect.center();
    qreal dx = cos(rad) * rect.width() / 2;
    qreal dy = sin(rad) * rect.height() / 2;
    
    QPointF start(center.x() - dx, center.y() - dy);
    QPointF end(center.x() + dx, center.y() + dy);
    
    return createLinearGradient(start, end, {
        {0.0, colors.primaryActive},
        {0.5, colors.primary},
        {1.0, colors.primaryHover}
    });
}

QLinearGradient EffectsManager::surfaceGradient(const QRectF& rect, bool subtle) {
    const auto& colors = ThemeManager::instance().colors();
    
    if (subtle) {
        return createLinearGradient(rect.topLeft(), rect.bottomRight(), {
            {0.0, colors.surface},
            {1.0, ThemeManager::lighten(colors.surface, 5)}
        });
    } else {
        return createLinearGradient(rect.topLeft(), rect.bottomRight(), {
            {0.0, ThemeManager::darken(colors.surface, 10)},
            {0.5, colors.surface},
            {1.0, ThemeManager::lighten(colors.surface, 10)}
        });
    }
}

QRadialGradient EffectsManager::glowGradient(const QPointF& center, qreal radius,
                                             const QColor& glowColor) {
    QColor color = glowColor.isValid() ? glowColor : ThemeManager::instance().colors().primary;
    
    QColor transparent = color;
    transparent.setAlpha(0);
    
    return createRadialGradient(center, radius, {
        {0.0, color},
        {0.3, QColor(color.red(), color.green(), color.blue(), 150)},
        {0.7, QColor(color.red(), color.green(), color.blue(), 50)},
        {1.0, transparent}
    });
}

void EffectsManager::paintGlow(QPainter* painter, const QRectF& rect,
                              const QColor& glowColor, qreal radius, qreal intensity) {
    if (!instance().effectsEnabled_) return;
    
    painter->save();
    
    // Create glow gradient
    QRadialGradient gradient = glowGradient(rect.center(), radius);
    
    // Adjust intensity
    QColor color = glowColor;
    color.setAlphaF(color.alphaF() * intensity * instance().effectQuality_ / 100.0);
    
    // Paint multiple layers for stronger effect
    painter->setCompositionMode(QPainter::CompositionMode_Screen);
    for (int i = 0; i < 3; ++i) {
        painter->setBrush(gradient);
        painter->setPen(Qt::NoPen);
        painter->drawEllipse(rect.adjusted(-radius * i / 3, -radius * i / 3,
                                          radius * i / 3, radius * i / 3));
    }
    
    painter->restore();
}

void EffectsManager::paintInnerShadow(QPainter* painter, const QPainterPath& path,
                                     const QColor& shadowColor, qreal blur,
                                     const QPointF& offset) {
    if (!instance().effectsEnabled_) return;
    
    painter->save();
    
    // Create inner shadow by painting outside the path
    QPainterPath outerPath;
    outerPath.addRect(painter->viewport());
    outerPath = outerPath.subtracted(path);
    
    // Apply blur effect manually (simplified)
    QColor shadow = shadowColor.isValid() ? shadowColor : ThemeManager::instance().colors().shadow;
    shadow.setAlphaF(shadow.alphaF() * instance().effectQuality_ / 100.0);
    
    painter->translate(offset);
    painter->fillPath(outerPath, shadow);
    
    painter->restore();
}

void EffectsManager::paintReflection(QPainter* painter, const QPixmap& source,
                                    const QRectF& targetRect, qreal opacity,
                                    qreal fadeHeight) {
    if (!instance().effectsEnabled_) return;
    
    painter->save();
    
    // Flip vertically
    QTransform transform;
    transform.scale(1, -1);
    QPixmap reflected = source.transformed(transform);
    
    // Create fade gradient
    QLinearGradient fade(0, 0, 0, reflected.height() * fadeHeight);
    // Use theme background color for reflection fade
    QColor fadeColor = ThemeManager::instance().colors().background;
    fadeColor.setAlpha(int(255 * opacity));
    fade.setColorAt(0, fadeColor);
    fade.setColorAt(1, Qt::transparent);
    
    // Apply gradient to reflection
    QPainter reflectPainter(&reflected);
    reflectPainter.setCompositionMode(QPainter::CompositionMode_DestinationIn);
    reflectPainter.fillRect(reflected.rect(), fade);
    
    // Draw reflection
    painter->drawPixmap(targetRect.toRect(), reflected);
    
    painter->restore();
}

// RippleEffect implementation
RippleEffect::RippleEffect(QWidget* parent)
    : QObject(parent), widget_(parent) {
    widget_->installEventFilter(this);
    color_ = ThemeManager::instance().colors().primary;
}

void RippleEffect::trigger(const QPoint& center) {
    center_ = center;
    
    // Stop any existing animation
    if (radiusAnim_) radiusAnim_->stop();
    if (opacityAnim_) opacityAnim_->stop();
    
    // Calculate max radius
    QRect rect = widget_->rect();
    qreal dx = qMax(center.x(), rect.width() - center.x());
    qreal dy = qMax(center.y(), rect.height() - center.y());
    maxRadius_ = qSqrt(dx * dx + dy * dy);
    
    // Animate radius
    radiusAnim_ = new QPropertyAnimation(this, "radius", this);
    radiusAnim_->setDuration(600);
    radiusAnim_->setStartValue(0);
    radiusAnim_->setEndValue(maxRadius_);
    radiusAnim_->setEasingCurve(QEasingCurve::OutQuad);
    
    // Animate opacity
    opacityAnim_ = new QPropertyAnimation(this, "opacity", this);
    opacityAnim_->setDuration(600);
    opacityAnim_->setStartValue(opacity_);
    opacityAnim_->setEndValue(0);
    
    connect(radiusAnim_, &QPropertyAnimation::finished, [this]() {
        radius_ = 0;
        widget_->update();
    });
    
    radiusAnim_->start(QAbstractAnimation::DeleteWhenStopped);
    opacityAnim_->start(QAbstractAnimation::DeleteWhenStopped);
}

void RippleEffect::setColor(const QColor& color) {
    color_ = color;
}

void RippleEffect::setRadius(qreal radius) {
    radius_ = radius;
    widget_->update();
}

void RippleEffect::setOpacity(qreal opacity) {
    opacity_ = opacity;
    widget_->update();
}

bool RippleEffect::eventFilter(QObject* obj, QEvent* event) {
    if (obj == widget_) {
        if (event->type() == QEvent::Paint) {
            QPainter painter(widget_);
            paint(&painter);
        } else if (event->type() == QEvent::MouseButtonPress) {
            auto* mouseEvent = static_cast<QMouseEvent*>(event);
            trigger(mouseEvent->pos());
        }
    }
    return false;
}

void RippleEffect::paint(QPainter* painter) {
    if (radius_ <= 0 || opacity_ <= 0) return;
    
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    
    QColor rippleColor = color_;
    rippleColor.setAlphaF(opacity_);
    
    painter->setPen(Qt::NoPen);
    painter->setBrush(rippleColor);
    painter->drawEllipse(QPointF(center_), radius_, radius_);
    
    painter->restore();
}

RippleEffect* EffectsManager::addRippleEffect(QWidget* widget, 
                                                              const QColor& color) {
    if (!widget || !instance().effectsEnabled_) return nullptr;
    
    auto* ripple = new RippleEffect(widget);
    if (color.isValid()) {
        ripple->setColor(color);
    }
    
    instance().registerEffect(widget, EffectType::Ripple, ripple);
    return ripple;
}

// ShimmerEffect implementation
ShimmerEffect::ShimmerEffect(QWidget* parent)
    : QObject(parent), widget_(parent) {
    widget_->installEventFilter(this);
    
    const auto& colors = ThemeManager::instance().colors();
    baseColor_ = colors.surface;
    shimmerColor_ = ThemeManager::lighten(colors.surface, 20);
}

void ShimmerEffect::start() {
    if (animation_) animation_->stop();
    
    animation_ = new QPropertyAnimation(this, "position", this);
    animation_->setDuration(2000);
    animation_->setStartValue(-0.5);
    animation_->setEndValue(1.5);
    animation_->setLoopCount(-1);
    animation_->setEasingCurve(QEasingCurve::InOutQuad);
    animation_->start();
}

void ShimmerEffect::stop() {
    if (animation_) {
        animation_->stop();
        animation_->deleteLater();
        animation_ = nullptr;
    }
    position_ = 0;
    widget_->update();
}

void ShimmerEffect::setColors(const QColor& base, const QColor& shimmer) {
    baseColor_ = base;
    shimmerColor_ = shimmer;
}

void ShimmerEffect::setAngle(qreal angle) {
    angle_ = angle;
}

void ShimmerEffect::setWidth(qreal width) {
    width_ = qBound(0.1, width, 1.0);
}

void ShimmerEffect::setPosition(qreal pos) {
    position_ = pos;
    widget_->update();
}

bool ShimmerEffect::eventFilter(QObject* obj, QEvent* event) {
    if (obj == widget_ && event->type() == QEvent::Paint) {
        QPainter painter(widget_);
        paint(&painter);
    }
    return false;
}

void ShimmerEffect::paint(QPainter* painter) {
    if (!animation_ || !animation_->state() == QAbstractAnimation::Running) return;
    
    painter->save();
    
    QRectF rect = widget_->rect();
    
    // Create shimmer gradient
    qreal gradientWidth = rect.width() * width_;
    qreal x = rect.left() + (rect.width() + gradientWidth) * position_ - gradientWidth;
    
    QLinearGradient gradient;
    gradient.setStart(x, rect.top());
    gradient.setFinalStop(x + gradientWidth, rect.bottom());
    
    gradient.setColorAt(0, baseColor_);
    gradient.setColorAt(0.5, shimmerColor_);
    gradient.setColorAt(1, baseColor_);
    
    // Apply gradient with clipping
    painter->setClipRect(rect);
    painter->fillRect(rect, gradient);
    
    painter->restore();
}

ShimmerEffect* EffectsManager::addShimmerEffect(QWidget* widget) {
    if (!widget || !instance().effectsEnabled_) return nullptr;
    
    auto* shimmer = new ShimmerEffect(widget);
    instance().registerEffect(widget, EffectType::Shimmer, shimmer);
    return shimmer;
}

void EffectsManager::applyEffectSet(QWidget* widget, const EffectSet& effects) {
    if (!widget) return;
    
    removeAllEffects(widget);
    
    if (effects.shadow != ShadowStyle::None) {
        applyShadow(widget, effects.shadow);
    }
    
    if (effects.glassMorphism) {
        applyGlassMorphism(widget, effects.blurRadius);
    } else if (effects.blurRadius > 0) {
        applyBlur(widget, effects.blurRadius);
    }
    
    if (effects.ripple) {
        addRippleEffect(widget);
    }
    
    if (effects.shimmer) {
        auto* shimmer = addShimmerEffect(widget);
        if (shimmer) shimmer->start();
    }
}

void EffectsManager::removeAllEffects(QWidget* widget) {
    if (!widget) return;
    
    auto& manager = instance();
    if (manager.activeEffects_.contains(widget)) {
        widget->setGraphicsEffect(nullptr);
        manager.activeEffects_.erase(widget);
    }
}

void EffectsManager::setEffectsEnabled(bool enabled) {
    if (effectsEnabled_ == enabled) return;
    
    effectsEnabled_ = enabled;
    
    if (!enabled) {
        // Disable all active effects
        for (auto& [widget, effects] : activeEffects_) {
            if (widget) {
                widget->setGraphicsEffect(nullptr);
            }
        }
    }
    
    emit effectsEnabledChanged(enabled);
}

void EffectsManager::setEffectQuality(int quality) {
    quality = qBound(0, quality, 100);
    if (effectQuality_ == quality) return;
    
    effectQuality_ = quality;
    
    // TODO: Update all active effects with new quality
    
    emit effectQualityChanged(quality);
}

QColor EffectsManager::shadowColorForStyle(ShadowStyle style) {
    const auto& colors = ThemeManager::instance().colors();
    
    switch (style) {
    case ShadowStyle::Subtle:
        return ThemeManager::adjustAlpha(colors.shadow, 30);  // Light shadow
    case ShadowStyle::Elevated:
        return colors.shadow;
    case ShadowStyle::Floating:
        return ThemeManager::adjustAlpha(colors.shadow, 80);  // Dark shadow
    case ShadowStyle::Inset:
        return QColor(colors.shadow.red(), colors.shadow.green(), 
                     colors.shadow.blue(), 80);  // Dark opacity
    case ShadowStyle::Colored:
        return ThemeManager::adjustAlpha(colors.primary, 60);
    default:
        return colors.shadow;
    }
}

qreal EffectsManager::shadowBlurForStyle(ShadowStyle style) {
    switch (style) {
    case ShadowStyle::Subtle:
        return 5.0;
    case ShadowStyle::Elevated:
        return 10.0;
    case ShadowStyle::Floating:
        return 20.0;
    case ShadowStyle::Inset:
        return 8.0;
    case ShadowStyle::Colored:
        return 15.0;
    default:
        return 10.0;
    }
}

QPointF EffectsManager::shadowOffsetForStyle(ShadowStyle style) {
    switch (style) {
    case ShadowStyle::Subtle:
        return QPointF(0, 1);
    case ShadowStyle::Elevated:
        return QPointF(0, 2);
    case ShadowStyle::Floating:
        return QPointF(0, 4);
    case ShadowStyle::Inset:
        return QPointF(0, -2);
    case ShadowStyle::Colored:
        return QPointF(0, 0);
    default:
        return QPointF(0, 2);
    }
}

void EffectsManager::registerEffect(QWidget* widget, EffectType type, QObject* effect) {
    if (!widget || !effect) return;
    activeEffects_[widget][type] = effect;
}

void EffectsManager::unregisterEffect(QWidget* widget, EffectType type) {
    if (!widget) return;
    
    if (activeEffects_.contains(widget)) {
        activeEffects_[widget].erase(type);
        if (activeEffects_[widget].empty()) {
            activeEffects_.erase(widget);
        }
    }
}

QObject* EffectsManager::getEffect(QWidget* widget, EffectType type) {
    if (!widget) return nullptr;
    
    if (activeEffects_.contains(widget)) {
        auto& effects = activeEffects_[widget];
        if (effects.contains(type)) {
            return effects[type];
        }
    }
    
    return nullptr;
}

} // namespace llm_re::ui_v2