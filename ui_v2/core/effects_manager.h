#pragma once

#include "ui_v2_common.h"
#include "ui_constants.h"
#include "theme_manager.h"

namespace llm_re::ui_v2 {

// Forward declaration
class EffectsManager;

// Ripple effect class
class RippleEffect : public QObject {
    Q_OBJECT
    Q_PROPERTY(qreal radius READ radius WRITE setRadius)
    Q_PROPERTY(qreal opacity READ opacity WRITE setOpacity)
    
public:
    explicit RippleEffect(QWidget* parent);
    
    void trigger(const QPoint& center);
    void setColor(const QColor& color);
    
    qreal radius() const { return radius_; }
    void setRadius(qreal radius);
    
    qreal opacity() const { return opacity_; }
    void setOpacity(qreal opacity);
    
protected:
    bool eventFilter(QObject* obj, QEvent* event) override;
    
private:
    void paint(QPainter* painter);
    
    QWidget* widget_;
    QPoint center_;
    QColor color_;
    qreal radius_ = 0;
    qreal maxRadius_ = 100;
    qreal opacity_ = 0.3;
    QPropertyAnimation* radiusAnim_ = nullptr;
    QPropertyAnimation* opacityAnim_ = nullptr;
};

// Shimmer effect class
class ShimmerEffect : public QObject {
    Q_OBJECT
    Q_PROPERTY(qreal position READ position WRITE setPosition)
    
public:
    explicit ShimmerEffect(QWidget* parent);
    
    void start();
    void stop();
    
    void setColors(const QColor& base, const QColor& shimmer);
    void setAngle(qreal angle);
    void setWidth(qreal width);
    
    qreal position() const { return position_; }
    void setPosition(qreal pos);
    
protected:
    bool eventFilter(QObject* obj, QEvent* event) override;
    
private:
    void paint(QPainter* painter);
    
    QWidget* widget_;
    QColor baseColor_;
    QColor shimmerColor_;
    qreal position_ = 0;
    qreal angle_ = 45;
    qreal width_ = 0.3;
    QPropertyAnimation* animation_ = nullptr;
};

class EffectsManager : public QObject {
    Q_OBJECT

public:
    // Effect types
    enum class EffectType {
        Shadow,
        Glow,
        Blur,
        GlassMorphism,
        Gradient,
        Reflection,
        Ripple,
        Shimmer
    };
    
    // Shadow styles
    enum class ShadowStyle {
        Subtle,      // Light shadow for depth
        Elevated,    // Medium shadow for cards
        Floating,    // Strong shadow for floating elements
        Inset,       // Inner shadow
        Colored,     // Shadow with color tint
        None
    };
    
    // Glow styles
    enum class GlowStyle {
        Soft,        // Subtle glow
        Neon,        // Strong neon-like glow
        Pulse,       // Animated pulsing glow
        Rainbow,     // Multi-color glow
        Halo         // Circular halo effect
    };
    
    // Gradient types
    enum class GradientType {
        Linear,
        Radial,
        Conical,
        Diamond
    };
    
    // Singleton access
    static EffectsManager& instance();
    
    // Shadow effects
    static QGraphicsDropShadowEffect* createShadow(
        ShadowStyle style = ShadowStyle::Elevated,
        const QColor& color = QColor(),
        qreal blur = -1,
        const QPointF& offset = QPointF(-999, -999));
    
    static void applyShadow(QWidget* widget, ShadowStyle style = ShadowStyle::Elevated);
    static void removeShadow(QWidget* widget);
    static void updateShadow(QWidget* widget, ShadowStyle style);
    
    // Glow effects
    static void applyGlow(QWidget* widget, GlowStyle style = GlowStyle::Soft,
                         const QColor& color = QColor());
    static void removeGlow(QWidget* widget);
    
    // Blur effects
    static void applyBlur(QWidget* widget, qreal radius = 10.0);
    static void removeBlur(QWidget* widget);
    
    // Glass morphism
    static void applyGlassMorphism(QWidget* widget, 
                                  qreal blurRadius = 20.0,
                                  qreal opacity = 0.8);
    
    // Gradient generators
    static QLinearGradient createLinearGradient(
        const QPointF& start, const QPointF& end,
        const QList<QPair<qreal, QColor>>& stops);
        
    static QRadialGradient createRadialGradient(
        const QPointF& center, qreal radius,
        const QList<QPair<qreal, QColor>>& stops);
        
    static QConicalGradient createConicalGradient(
        const QPointF& center, qreal angle,
        const QList<QPair<qreal, QColor>>& stops);
    
    // Theme-aware gradients
    static QLinearGradient primaryGradient(const QRectF& rect, qreal angle = 45.0);
    static QLinearGradient surfaceGradient(const QRectF& rect, bool subtle = true);
    static QRadialGradient glowGradient(const QPointF& center, qreal radius,
                                       const QColor& glowColor = QColor());
    
    // Painting helpers
    static void paintGlow(QPainter* painter, const QRectF& rect,
                         const QColor& glowColor, qreal radius = 20.0,
                         qreal intensity = 1.0);
                         
    static void paintInnerShadow(QPainter* painter, const QPainterPath& path,
                                const QColor& shadowColor = QColor(),
                                qreal blur = 10.0,
                                const QPointF& offset = QPointF(0, 2));
                                
    static void paintReflection(QPainter* painter, const QPixmap& source,
                               const QRectF& targetRect,
                               qreal opacity = 0.3,
                               qreal fadeHeight = 0.5);
    
    // Ripple effect
    static RippleEffect* addRippleEffect(QWidget* widget, 
                                        const QColor& color = QColor());
    
    // Shimmer effect
    static ShimmerEffect* addShimmerEffect(QWidget* widget);
    
    // Effect combinations
    struct EffectSet {
        ShadowStyle shadow = ShadowStyle::None;
        GlowStyle glow = GlowStyle::Soft;
        qreal blurRadius = 0;
        bool glassMorphism = false;
        bool ripple = false;
        bool shimmer = false;
    };
    
    static void applyEffectSet(QWidget* widget, const EffectSet& effects);
    static void removeAllEffects(QWidget* widget);
    
    // Global effect settings
    void setEffectsEnabled(bool enabled);
    bool effectsEnabled() const { return effectsEnabled_; }
    
    void setEffectQuality(int quality); // 0-100
    int effectQuality() const { return effectQuality_; }
    
signals:
    void effectsEnabledChanged(bool enabled);
    void effectQualityChanged(int quality);
    
private:
    EffectsManager();
    ~EffectsManager() = default;
    EffectsManager(const EffectsManager&) = delete;
    EffectsManager& operator=(const EffectsManager&) = delete;
    
    // Helper functions
    static QColor shadowColorForStyle(ShadowStyle style);
    static qreal shadowBlurForStyle(ShadowStyle style);
    static QPointF shadowOffsetForStyle(ShadowStyle style);
    
    // Track active effects
    void registerEffect(QWidget* widget, EffectType type, QObject* effect);
    void unregisterEffect(QWidget* widget, EffectType type);
    QObject* getEffect(QWidget* widget, EffectType type);
    
    // Member variables
    bool effectsEnabled_ = true;
    int effectQuality_ = 100;
    std::map<QWidget*, std::map<EffectType, QObject*>> activeEffects_;
};

// Convenience macros
#define ShadowEffect(style) EffectsManager::createShadow(style)
#define ApplyShadow(widget, style) EffectsManager::applyShadow(widget, style)
#define ApplyGlow(widget, style) EffectsManager::applyGlow(widget, style)

} // namespace llm_re::ui_v2