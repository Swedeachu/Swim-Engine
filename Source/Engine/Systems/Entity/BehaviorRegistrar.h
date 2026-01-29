#pragma once

#include "BehaviorFactory.h"
#include "Behavior.h"

namespace 
{
  template<typename T>
  struct BehaviorRegistrar
  {
    BehaviorRegistrar(const std::string& name)
    {
      Engine::BehaviorFactory::GetInstance().Register<T>(name);
    }
  };
}

// Simple registration macro (for classes already declared)
#define REGISTER_BEHAVIOR(BehaviorType) \
    namespace { \
        inline BehaviorRegistrar<BehaviorType> behavior_registrar_instance_##BehaviorType(#BehaviorType); \
    }

// Optional macro to declare + register a behavior in one shot, like DEFINE_SCENE, only intended for quick behaviors with no fields
#define DEFINE_BEHAVIOR(BehaviorType)                                      \
    class BehaviorType : public Engine::Behavior                           \
    {                                                                      \
    public:                                                                \
        BehaviorType(Engine::Scene* scene, entt::entity owner)             \
            : Engine::Behavior(scene, owner) {}                            \
        int Awake() override;                                              \
        int Init() override;                                               \
        void Update(double dt) override;                                   \
        void FixedUpdate(unsigned int tickThisSecond) override;            \
        int Exit() override;                                               \
    };                                                                     \
    REGISTER_BEHAVIOR(BehaviorType)
