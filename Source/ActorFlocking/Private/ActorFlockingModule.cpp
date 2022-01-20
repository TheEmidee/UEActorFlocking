#include "ActorFlockingModule.h"

class FActorFlockingModule final : public IActorFlockingModule
{
public:
    void StartupModule() override;
    void ShutdownModule() override;
};

IMPLEMENT_MODULE( FActorFlockingModule, ActorFlocking )

void FActorFlockingModule::StartupModule()
{
}

void FActorFlockingModule::ShutdownModule()
{
}