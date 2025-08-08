#include "animation_manager.h"

namespace llm_re::ui_v2 {

AnimationManager& AnimationManager::instance() {
    static AnimationManager instance;
    return instance;
}

AnimationManager::AnimationManager() {
}

QPropertyAnimation* AnimationManager::animate(QObject* target, 
                                             const QByteArray& property,
                                             const QVariant& startValue,
                                             const QVariant& endValue,
                                             int duration,
                                             EasingType easing,
                                             std::function<void()> onComplete) {
    auto& manager = instance();
    if (!manager.animationsEnabled_) {
        // If animations are disabled, set the end value immediately
        target->setProperty(property, endValue);
        if (onComplete) onComplete();
        return nullptr;
    }
    
    auto* animation = new QPropertyAnimation(target, property);
    animation->setStartValue(startValue);
    animation->setEndValue(endValue);
    animation->setDuration(static_cast<int>(duration / manager.globalSpeed_));
    animation->setEasingCurve(easingCurve(easing));
    
    manager.registerAnimation(target, animation);
    connectAnimation(animation, onComplete);
    
    return animation;
}

void AnimationManager::fadeIn(QWidget* widget, int duration, std::function<void()> onComplete) {
    if (!widget) return;
    
    // Ensure widget has opacity effect
    auto* effect = widget->graphicsEffect();
    QGraphicsOpacityEffect* opacityEffect = nullptr;
    
    if (!effect || !qobject_cast<QGraphicsOpacityEffect*>(effect)) {
        opacityEffect = new QGraphicsOpacityEffect(widget);
        widget->setGraphicsEffect(opacityEffect);
    } else {
        opacityEffect = qobject_cast<QGraphicsOpacityEffect*>(effect);
    }
    
    opacityEffect->setOpacity(0.0);
    widget->show();
    
    auto* anim = animate(opacityEffect, "opacity", 0.0, 1.0, duration, EasingType::OutCubic, onComplete);
    if (anim) anim->start(QAbstractAnimation::DeleteWhenStopped);
}

void AnimationManager::fadeOut(QWidget* widget, int duration, std::function<void()> onComplete) {
    if (!widget) return;
    
    auto* effect = widget->graphicsEffect();
    QGraphicsOpacityEffect* opacityEffect = nullptr;
    
    if (!effect || !qobject_cast<QGraphicsOpacityEffect*>(effect)) {
        opacityEffect = new QGraphicsOpacityEffect(widget);
        widget->setGraphicsEffect(opacityEffect);
    } else {
        opacityEffect = qobject_cast<QGraphicsOpacityEffect*>(effect);
    }
    
    auto* anim = animate(opacityEffect, "opacity", opacityEffect->opacity(), 0.0, 
                        duration, EasingType::OutCubic, [widget, onComplete]() {
        widget->hide();
        if (onComplete) onComplete();
    });
    if (anim) anim->start(QAbstractAnimation::DeleteWhenStopped);
}

void AnimationManager::slideIn(QWidget* widget, SlideDirection direction, 
                              int duration, std::function<void()> onComplete) {
    if (!widget) return;
    
    QPoint startPos = calculateSlidePosition(widget, direction, true);
    QPoint endPos = widget->pos();
    
    widget->move(startPos);
    widget->show();
    
    auto* anim = animate(widget, "pos", startPos, endPos, duration, EasingType::OutCubic, onComplete);
    if (anim) anim->start(QAbstractAnimation::DeleteWhenStopped);
}

void AnimationManager::slideOut(QWidget* widget, SlideDirection direction,
                               int duration, std::function<void()> onComplete) {
    if (!widget) return;
    
    QPoint startPos = widget->pos();
    QPoint endPos = calculateSlidePosition(widget, direction, false);
    
    auto* anim = animate(widget, "pos", startPos, endPos, duration, EasingType::InCubic, 
                        [widget, onComplete]() {
        widget->hide();
        if (onComplete) onComplete();
    });
    if (anim) anim->start(QAbstractAnimation::DeleteWhenStopped);
}

void AnimationManager::scale(QWidget* widget, qreal fromScale, qreal toScale,
                            int duration, std::function<void()> onComplete) {
    if (!widget) return;
    
    // Create a property for scale
    widget->setProperty("animScale", fromScale);
    
    auto* anim = animate(widget, "animScale", fromScale, toScale, duration, 
                        EasingType::InOutCubic, onComplete);
    
    if (anim) {
        QObject::connect(anim, &QPropertyAnimation::valueChanged, [widget](const QVariant& value) {
            qreal scale = value.toReal();
            QTransform transform;
            transform.translate(widget->width() / 2, widget->height() / 2);
            transform.scale(scale, scale);
            transform.translate(-widget->width() / 2, -widget->height() / 2);
            widget->setProperty("transform", transform);
            widget->update();
        });
        anim->start(QAbstractAnimation::DeleteWhenStopped);
    }
}

void AnimationManager::bounce(QWidget* widget, int intensity, int duration, 
                             std::function<void()> onComplete) {
    if (!widget) return;
    
    QPoint originalPos = widget->pos();
    
    auto* group = new QSequentialAnimationGroup();
    
    // Bounce up
    auto* up = animate(widget, "pos", originalPos, originalPos - QPoint(0, intensity),
                      duration / 4, EasingType::OutQuad);
    // Bounce down
    auto* down = animate(widget, "pos", originalPos - QPoint(0, intensity), originalPos,
                        duration / 4, EasingType::InQuad);
    // Small bounce up
    auto* up2 = animate(widget, "pos", originalPos, originalPos - QPoint(0, intensity / 2),
                       duration / 4, EasingType::OutQuad);
    // Final settle
    auto* down2 = animate(widget, "pos", originalPos - QPoint(0, intensity / 2), originalPos,
                         duration / 4, EasingType::InQuad);
    
    group->addAnimation(up);
    group->addAnimation(down);
    group->addAnimation(up2);
    group->addAnimation(down2);
    
    connectAnimation(group, onComplete);
    group->start(QAbstractAnimation::DeleteWhenStopped);
}

void AnimationManager::shake(QWidget* widget, int intensity, int duration,
                            std::function<void()> onComplete) {
    if (!widget) return;
    
    QPoint originalPos = widget->pos();
    
    auto* group = new QSequentialAnimationGroup();
    
    int shakes = 6;
    int shakeDuration = duration / shakes;
    
    for (int i = 0; i < shakes; ++i) {
        int offset = (i % 2 == 0) ? intensity : -intensity;
        offset = offset * (shakes - i) / shakes; // Decay
        
        auto* shake = animate(widget, "pos", widget->pos(), originalPos + QPoint(offset, 0),
                             shakeDuration, EasingType::InOutSine);
        group->addAnimation(shake);
    }
    
    // Ensure we end at original position
    auto* reset = animate(widget, "pos", widget->pos(), originalPos, 50, EasingType::OutQuad);
    group->addAnimation(reset);
    
    connectAnimation(group, onComplete);
    group->start(QAbstractAnimation::DeleteWhenStopped);
}

void AnimationManager::pulse(QWidget* widget, qreal maxScale, int duration,
                            std::function<void()> onComplete) {
    if (!widget) return;
    
    auto* group = new QSequentialAnimationGroup();
    
    // Scale up
    auto* scaleUp = create(widget).property("animScale").from(1.0).to(maxScale)
                    .duration(duration / 2).easing(EasingType::OutSine).build();
    
    // Scale down
    auto* scaleDown = create(widget).property("animScale").from(maxScale).to(1.0)
                      .duration(duration / 2).easing(EasingType::InSine).build();
    
    // Connect scale changes to transform
    auto applyScale = [widget](const QVariant& value) {
        qreal scale = value.toReal();
        QTransform transform;
        transform.translate(widget->width() / 2, widget->height() / 2);
        transform.scale(scale, scale);
        transform.translate(-widget->width() / 2, -widget->height() / 2);
        widget->setProperty("transform", transform);
        widget->update();
    };
    
    QObject::connect(scaleUp, &QPropertyAnimation::valueChanged, applyScale);
    QObject::connect(scaleDown, &QPropertyAnimation::valueChanged, applyScale);
    
    group->addAnimation(scaleUp);
    group->addAnimation(scaleDown);
    
    connectAnimation(group, onComplete);
    group->start(QAbstractAnimation::DeleteWhenStopped);
}

void AnimationManager::rotate(QWidget* widget, int degrees, int duration,
                             std::function<void()> onComplete) {
    if (!widget) return;
    
    widget->setProperty("animRotation", 0);
    
    auto* anim = animate(widget, "animRotation", 0, degrees, duration,
                        EasingType::InOutCubic, onComplete);
    
    if (anim) {
        QObject::connect(anim, &QPropertyAnimation::valueChanged, [widget](const QVariant& value) {
            int rotation = value.toInt();
            QTransform transform;
            transform.translate(widget->width() / 2, widget->height() / 2);
            transform.rotate(rotation);
            transform.translate(-widget->width() / 2, -widget->height() / 2);
            widget->setProperty("transform", transform);
            widget->update();
        });
        anim->start(QAbstractAnimation::DeleteWhenStopped);
    }
}

void AnimationManager::flip(QWidget* widget, Qt::Axis axis, int duration,
                           std::function<void()> onComplete) {
    // TODO: Implement 3D flip animation
    // For now, just do a scale animation
    if (axis == Qt::XAxis) {
        scale(widget, 1.0, -1.0, duration / 2, [widget, duration, onComplete]() {
            scale(widget, -1.0, 1.0, duration / 2, onComplete);
        });
    } else {
        scale(widget, 1.0, -1.0, duration / 2, [widget, duration, onComplete]() {
            scale(widget, -1.0, 1.0, duration / 2, onComplete);
        });
    }
}

QParallelAnimationGroup* AnimationManager::parallel(const QList<QPropertyAnimation*>& animations) {
    auto* group = new QParallelAnimationGroup();
    for (auto* anim : animations) {
        group->addAnimation(anim);
    }
    return group;
}

QSequentialAnimationGroup* AnimationManager::sequential(const QList<QPropertyAnimation*>& animations) {
    auto* group = new QSequentialAnimationGroup();
    for (auto* anim : animations) {
        group->addAnimation(anim);
    }
    return group;
}

AnimationManager::AnimationBuilder AnimationManager::create(QObject* target) {
    return AnimationBuilder(target);
}

QEasingCurve AnimationManager::easingCurve(EasingType type) {
    switch (type) {
        case EasingType::Linear: return QEasingCurve::Linear;
        case EasingType::InSine: return QEasingCurve::InSine;
        case EasingType::OutSine: return QEasingCurve::OutSine;
        case EasingType::InOutSine: return QEasingCurve::InOutSine;
        case EasingType::InQuad: return QEasingCurve::InQuad;
        case EasingType::OutQuad: return QEasingCurve::OutQuad;
        case EasingType::InOutQuad: return QEasingCurve::InOutQuad;
        case EasingType::InCubic: return QEasingCurve::InCubic;
        case EasingType::OutCubic: return QEasingCurve::OutCubic;
        case EasingType::InOutCubic: return QEasingCurve::InOutCubic;
        case EasingType::InQuart: return QEasingCurve::InQuart;
        case EasingType::OutQuart: return QEasingCurve::OutQuart;
        case EasingType::InOutQuart: return QEasingCurve::InOutQuart;
        case EasingType::InQuint: return QEasingCurve::InQuint;
        case EasingType::OutQuint: return QEasingCurve::OutQuint;
        case EasingType::InOutQuint: return QEasingCurve::InOutQuint;
        case EasingType::InExpo: return QEasingCurve::InExpo;
        case EasingType::OutExpo: return QEasingCurve::OutExpo;
        case EasingType::InOutExpo: return QEasingCurve::InOutExpo;
        case EasingType::InCirc: return QEasingCurve::InCirc;
        case EasingType::OutCirc: return QEasingCurve::OutCirc;
        case EasingType::InOutCirc: return QEasingCurve::InOutCirc;
        case EasingType::InElastic: return QEasingCurve::InElastic;
        case EasingType::OutElastic: return QEasingCurve::OutElastic;
        case EasingType::InOutElastic: return QEasingCurve::InOutElastic;
        case EasingType::InBack: return QEasingCurve::InBack;
        case EasingType::OutBack: return QEasingCurve::OutBack;
        case EasingType::InOutBack: return QEasingCurve::InOutBack;
        case EasingType::InBounce: return QEasingCurve::InBounce;
        case EasingType::OutBounce: return QEasingCurve::OutBounce;
        case EasingType::InOutBounce: return QEasingCurve::InOutBounce;
        default: return QEasingCurve::Linear;
    }
}

int AnimationManager::standardDuration(AnimationType type) {
    switch (type) {
        case AnimationType::FadeIn:
        case AnimationType::FadeOut:
            return Design::ANIM_NORMAL;
        case AnimationType::SlideIn:
        case AnimationType::SlideOut:
            return Design::ANIM_NORMAL;
        case AnimationType::Scale:
            return Design::ANIM_FAST;
        case AnimationType::Bounce:
            return Design::ANIM_SLOW;
        case AnimationType::Shake:
            return Design::ANIM_FAST;
        case AnimationType::Pulse:
            return Design::ANIM_NORMAL;
        case AnimationType::TypeWriter:
            return Design::ANIM_SLOW;
        case AnimationType::Elastic:
            return Design::ANIM_SLOW;
        case AnimationType::Back:
            return Design::ANIM_NORMAL;
        case AnimationType::Rotate:
            return Design::ANIM_NORMAL;
        case AnimationType::Flip:
            return Design::ANIM_NORMAL;
        case AnimationType::Glow:
            return Design::ANIM_SLOW;
        default:
            return Design::ANIM_NORMAL;
    }
}

bool AnimationManager::isAnimating(QObject* object) {
    auto& manager = instance();
    return manager.activeAnimations_.contains(object) && 
           !manager.activeAnimations_[object].isEmpty();
}

void AnimationManager::stopAll(QObject* object) {
    auto& manager = instance();
    if (manager.activeAnimations_.contains(object)) {
        for (auto* anim : manager.activeAnimations_[object]) {
            anim->stop();
        }
    }
}

void AnimationManager::pauseAll(QObject* object) {
    auto& manager = instance();
    if (manager.activeAnimations_.contains(object)) {
        for (auto* anim : manager.activeAnimations_[object]) {
            anim->pause();
        }
    }
}

void AnimationManager::resumeAll(QObject* object) {
    auto& manager = instance();
    if (manager.activeAnimations_.contains(object)) {
        for (auto* anim : manager.activeAnimations_[object]) {
            anim->resume();
        }
    }
}

void AnimationManager::setGlobalSpeed(qreal speed) {
    globalSpeed_ = qMax(0.1, speed);
}

void AnimationManager::setAnimationsEnabled(bool enabled) {
    animationsEnabled_ = enabled;
}

void AnimationManager::connectAnimation(QAbstractAnimation* anim, std::function<void()> onComplete) {
    if (!anim) return;
    
    if (onComplete) {
        QObject::connect(anim, &QAbstractAnimation::finished, onComplete);
    }
    
    // Clean up when animation finishes - only for property animations
    auto* propAnim = qobject_cast<QPropertyAnimation*>(anim);
    if (propAnim) {
        QObject::connect(propAnim, &QPropertyAnimation::finished, [propAnim]() {
            auto& manager = instance();
            QObject* target = propAnim->targetObject();
            if (target) {
                manager.unregisterAnimation(target, propAnim);
            }
        });
    }
}

QPoint AnimationManager::calculateSlidePosition(QWidget* widget, SlideDirection direction, bool out) {
    if (!widget || !widget->parentWidget()) return widget->pos();
    
    QRect parentRect = widget->parentWidget()->rect();
    QPoint pos = widget->pos();
    
    switch (direction) {
        case SlideDirection::Left:
            pos.setX(out ? -widget->width() : parentRect.width());
            break;
        case SlideDirection::Right:
            pos.setX(out ? parentRect.width() : -widget->width());
            break;
        case SlideDirection::Top:
            pos.setY(out ? -widget->height() : parentRect.height());
            break;
        case SlideDirection::Bottom:
            pos.setY(out ? parentRect.height() : -widget->height());
            break;
    }
    
    return pos;
}

void AnimationManager::registerAnimation(QObject* target, QPropertyAnimation* animation) {
    if (!target || !animation) return;
    activeAnimations_[target].append(animation);
}

void AnimationManager::unregisterAnimation(QObject* target, QPropertyAnimation* animation) {
    if (!target || !animation) return;
    
    if (activeAnimations_.contains(target)) {
        activeAnimations_[target].removeOne(animation);
        if (activeAnimations_[target].isEmpty()) {
            activeAnimations_.erase(target);
        }
    }
}

// AnimationBuilder implementation
AnimationManager::AnimationBuilder::AnimationBuilder(QObject* target)
    : target_(target) {
}

AnimationManager::AnimationBuilder& AnimationManager::AnimationBuilder::property(const QByteArray& prop) {
    property_ = prop;
    return *this;
}

AnimationManager::AnimationBuilder& AnimationManager::AnimationBuilder::from(const QVariant& value) {
    startValue_ = value;
    return *this;
}

AnimationManager::AnimationBuilder& AnimationManager::AnimationBuilder::to(const QVariant& value) {
    endValue_ = value;
    return *this;
}

AnimationManager::AnimationBuilder& AnimationManager::AnimationBuilder::duration(int ms) {
    duration_ = ms;
    return *this;
}

AnimationManager::AnimationBuilder& AnimationManager::AnimationBuilder::easing(EasingType type) {
    easing_ = type;
    return *this;
}

AnimationManager::AnimationBuilder& AnimationManager::AnimationBuilder::delay(int ms) {
    delay_ = ms;
    return *this;
}

AnimationManager::AnimationBuilder& AnimationManager::AnimationBuilder::loop(int count) {
    loopCount_ = count;
    return *this;
}

AnimationManager::AnimationBuilder& AnimationManager::AnimationBuilder::onComplete(std::function<void()> callback) {
    onComplete_ = callback;
    return *this;
}

AnimationManager::AnimationBuilder& AnimationManager::AnimationBuilder::onValueChanged(std::function<void(const QVariant&)> callback) {
    onValueChanged_ = callback;
    return *this;
}

QPropertyAnimation* AnimationManager::AnimationBuilder::build() {
    if (!target_ || property_.isEmpty()) return nullptr;
    
    auto* anim = AnimationManager::animate(target_, property_, startValue_, endValue_,
                                          duration_, easing_, onComplete_);
    
    if (anim) {
        if (delay_ > 0) {
            auto* delayTimer = new QTimer();
            delayTimer->setSingleShot(true);
            QObject::connect(delayTimer, &QTimer::timeout, [anim, delayTimer]() {
                anim->start();
                delayTimer->deleteLater();
            });
            delayTimer->start(delay_);
        }
        
        if (loopCount_ != 1) {
            anim->setLoopCount(loopCount_);
        }
        
        if (onValueChanged_) {
            QObject::connect(anim, &QPropertyAnimation::valueChanged, onValueChanged_);
        }
    }
    
    return anim;
}

void AnimationManager::AnimationBuilder::start(QAbstractAnimation::DeletionPolicy policy) {
    auto* anim = build();
    if (anim && delay_ == 0) {
        anim->start(policy);
    }
}

} // namespace llm_re::ui_v2