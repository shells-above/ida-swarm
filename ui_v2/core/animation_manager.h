#pragma once

#include "ui_v2_common.h"
#include "ui_constants.h"

namespace llm_re::ui_v2 {

class AnimationManager : public QObject {
    Q_OBJECT

public:
    // Animation types
    enum class AnimationType {
        FadeIn,
        FadeOut,
        SlideIn,
        SlideOut,
        Scale,
        Bounce,
        Shake,
        Pulse,
        TypeWriter,
        Elastic,
        Back,
        Rotate,
        Flip,
        Glow
    };
    
    // Easing curves
    enum class EasingType {
        Linear,
        InSine,
        OutSine,
        InOutSine,
        InQuad,
        OutQuad,
        InOutQuad,
        InCubic,
        OutCubic,
        InOutCubic,
        InQuart,
        OutQuart,
        InOutQuart,
        InQuint,
        OutQuint,
        InOutQuint,
        InExpo,
        OutExpo,
        InOutExpo,
        InCirc,
        OutCirc,
        InOutCirc,
        InElastic,
        OutElastic,
        InOutElastic,
        InBack,
        OutBack,
        InOutBack,
        InBounce,
        OutBounce,
        InOutBounce
    };
    
    // Direction for slide animations
    enum class SlideDirection {
        Left,
        Right,
        Top,
        Bottom
    };
    
    // Singleton access
    static AnimationManager& instance();
    
    // Core animation functions
    static QPropertyAnimation* animate(QObject* target, 
                                      const QByteArray& property,
                                      const QVariant& startValue,
                                      const QVariant& endValue,
                                      int duration = Design::ANIM_NORMAL,
                                      EasingType easing = EasingType::OutCubic,
                                      std::function<void()> onComplete = nullptr);
    
    // Convenience animations for widgets
    static void fadeIn(QWidget* widget, 
                      int duration = Design::ANIM_NORMAL,
                      std::function<void()> onComplete = nullptr);
                      
    static void fadeOut(QWidget* widget, 
                       int duration = Design::ANIM_NORMAL,
                       std::function<void()> onComplete = nullptr);
                       
    static void slideIn(QWidget* widget, 
                       SlideDirection direction,
                       int duration = Design::ANIM_NORMAL,
                       std::function<void()> onComplete = nullptr);
                       
    static void slideOut(QWidget* widget, 
                        SlideDirection direction,
                        int duration = Design::ANIM_NORMAL,
                        std::function<void()> onComplete = nullptr);
    
    static void scale(QWidget* widget,
                     qreal fromScale,
                     qreal toScale,
                     int duration = Design::ANIM_NORMAL,
                     std::function<void()> onComplete = nullptr);
    
    static void bounce(QWidget* widget,
                      int intensity = 20,
                      int duration = Design::ANIM_NORMAL,
                      std::function<void()> onComplete = nullptr);
    
    static void shake(QWidget* widget,
                     int intensity = 10,
                     int duration = Design::ANIM_FAST,
                     std::function<void()> onComplete = nullptr);
    
    static void pulse(QWidget* widget,
                     qreal maxScale = 1.1,
                     int duration = Design::ANIM_NORMAL,
                     std::function<void()> onComplete = nullptr);
    
    static void rotate(QWidget* widget,
                      int degrees,
                      int duration = Design::ANIM_NORMAL,
                      std::function<void()> onComplete = nullptr);
    
    static void flip(QWidget* widget,
                    Qt::Axis axis,
                    int duration = Design::ANIM_NORMAL,
                    std::function<void()> onComplete = nullptr);
    
    // Group animations
    static QParallelAnimationGroup* parallel(const QList<QPropertyAnimation*>& animations);
    static QSequentialAnimationGroup* sequential(const QList<QPropertyAnimation*>& animations);
    
    // Animation builders
    class AnimationBuilder {
    public:
        AnimationBuilder(QObject* target);
        
        AnimationBuilder& property(const QByteArray& prop);
        AnimationBuilder& from(const QVariant& value);
        AnimationBuilder& to(const QVariant& value);
        AnimationBuilder& duration(int ms);
        AnimationBuilder& easing(EasingType type);
        AnimationBuilder& delay(int ms);
        AnimationBuilder& loop(int count = -1);
        AnimationBuilder& onComplete(std::function<void()> callback);
        AnimationBuilder& onValueChanged(std::function<void(const QVariant&)> callback);
        
        QPropertyAnimation* build();
        void start(QAbstractAnimation::DeletionPolicy policy = QAbstractAnimation::DeleteWhenStopped);
        
    private:
        QObject* target_;
        QByteArray property_;
        QVariant startValue_;
        QVariant endValue_;
        int duration_ = Design::ANIM_NORMAL;
        EasingType easing_ = EasingType::OutCubic;
        int delay_ = 0;
        int loopCount_ = 1;
        std::function<void()> onComplete_;
        std::function<void(const QVariant&)> onValueChanged_;
    };
    
    // Create animation builder
    static AnimationBuilder create(QObject* target);
    
    // Utility functions
    static QEasingCurve easingCurve(EasingType type);
    static int standardDuration(AnimationType type);
    static bool isAnimating(QObject* object);
    static void stopAll(QObject* object);
    static void pauseAll(QObject* object);
    static void resumeAll(QObject* object);
    
    // Global animation settings
    void setGlobalSpeed(qreal speed);
    qreal globalSpeed() const { return globalSpeed_; }
    
    void setAnimationsEnabled(bool enabled);
    bool animationsEnabled() const { return animationsEnabled_; }
    
signals:
    void animationStarted(QObject* target, AnimationType type);
    void animationFinished(QObject* target, AnimationType type);
    
private:
    AnimationManager();
    ~AnimationManager() = default;
    AnimationManager(const AnimationManager&) = delete;
    AnimationManager& operator=(const AnimationManager&) = delete;
    
    // Helper functions
    static void connectAnimation(QAbstractAnimation* anim, std::function<void()> onComplete);
    static QPoint calculateSlidePosition(QWidget* widget, SlideDirection direction, bool out);
    
    // Track active animations
    void registerAnimation(QObject* target, QPropertyAnimation* animation);
    void unregisterAnimation(QObject* target, QPropertyAnimation* animation);
    
    // Member variables
    qreal globalSpeed_ = 1.0;
    bool animationsEnabled_ = true;
    std::map<QObject*, QList<QPropertyAnimation*>> activeAnimations_;
};

// Convenience macro for easy animation creation
#define Animate(target) AnimationManager::create(target)

} // namespace llm_re::ui_v2