#ifndef NATIVEBACKEND_H
#define NATIVEBACKEND_H

#include "effects/effectsbackend.h"

class NativeBackend : public EffectsBackend {
    Q_OBJECT
  public:
    NativeBackend(QObject* pParent=NULL);
    virtual ~NativeBackend();

    const QList<EffectManifest> getAvailableEffects() const;
    EffectPointer instantiateEffect(const EffectManifest& manifest);
  private:
    QString debugString() const {
        return "NativeBackend";
    }

    QList<EffectManifest> m_effectManifests;
    QList<EffectPointer> m_instantiatedEffects;
};

#endif /* NATIVEBACKEND_H */
