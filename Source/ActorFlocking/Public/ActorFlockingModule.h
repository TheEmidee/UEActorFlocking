#pragma once

#include <CoreMinimal.h>
#include <Modules/ModuleInterface.h>
#include <Modules/ModuleManager.h>

class ACTORFLOCKING_API IActorFlockingModule : public IModuleInterface
{

public:
    static IActorFlockingModule & Get()
    {
        static auto & singleton = FModuleManager::LoadModuleChecked< IActorFlockingModule >( "ActorFlocking" );
        return singleton;
    }

    static bool IsAvailable()
    {
        return FModuleManager::Get().IsModuleLoaded( "ActorFlocking" );
    }
};
