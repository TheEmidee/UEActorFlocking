#pragma once

#include <Components/ActorComponent.h>
#include <CoreMinimal.h>
#include <Engine/DataAsset.h>

#include "AFFlockingComponent.generated.h"

class UCharacterMovementComponent;
class UCurveFloat;

USTRUCT()
struct FAFFlockSettings
{
    GENERATED_USTRUCT_BODY()

    FAFFlockSettings();

    void LerpBetween( const FAFFlockSettings & start, const FAFFlockSettings & end, float ratio );

    /* How much of the steering force computed to follow the owner is kept */
    UPROPERTY( EditAnywhere, meta = ( ClampMin = "0.0" ) )
    float PursuitWeight;

    /* Radius around the owner which applies a deceleration to the boids when they enter it (The closer the boids are from the owner, the less velocity toward the owner they have) */
    UPROPERTY( EditAnywhere, meta = ( ClampMin = "0.0" ) )
    float PursuitSlowdownRadius;

    /* How much units behind the owner to apply to the target the boids try to reach */
    UPROPERTY( EditAnywhere, meta = ( ClampMin = "0.0" ) )
    float PursuitDistanceBehind;

    /* By how much the resulting steering velocity is multiplied when it does not match the owner velocity. This allows to make the units not move backwards. */
    UPROPERTY( EditAnywhere, meta = ( ClampMin = "0.0" ) )
    float NonForwardVelocityBrakingFactor;

    /* How much of the steering force computed to make boids move in the same direction is kept */
    UPROPERTY( EditAnywhere, meta = ( ClampMin = "0.0" ) )
    float AlignmentWeight;

    /* Radius around each boid where the alignment force is computed (all boids in that radius will try to move in the same direction) */
    UPROPERTY( EditAnywhere, meta = ( ClampMin = "0.0" ) )
    float AlignmentRadius;

    /* How much of the steering force computed to make boids move close to each other is kept */
    UPROPERTY( EditAnywhere, meta = ( ClampMin = "0.0" ) )
    float CohesionWeight;

    /* Radius around each boid where the cohesion force is computed (all boids in that radius will try to move close to each other)*/
    UPROPERTY( EditAnywhere, meta = ( ClampMin = "0.0" ) )
    float CohesionRadius;

    /* How much of the steering force computed to make boids move away from each other is kept */
    UPROPERTY( EditAnywhere, meta = ( ClampMin = "0.0" ) )
    float SeparationWeight;

    /* Radius around each boid where the separation force is computed (all boids in that radius will try to move away from each other)*/
    UPROPERTY( EditAnywhere, meta = ( ClampMin = "0.0" ) )
    float SeparationRadius;

    /* Allows to create groups of boids. The X-Axis is the boid index. The Y-Axis is the multiplier to PursuitDistanceBehind.
     * You will most likely configure the curve to use constant interpolation, to have steps between values.
     * For example, if you set a value to the coordinates (0;1) and a value to the coordinates (3;2),
     * the first 3 boids will target (owner location + PursuitDistanceBehind * 1), and the boids after will target (owner location + PursuitDistanceBehind * 2)
     */
    UPROPERTY( EditAnywhere )
    UCurveFloat * QueueCurve;

    // Set to true to allow to randomly swap boids positions.
    UPROPERTY( EditAnywhere )
    uint8 bAllowSwapPositions : 1;

    // Range of the delay at which boids positions can be swapped
    UPROPERTY( EditAnywhere, meta = ( EditCondition = "bAllowSwapPositions", UIMin = "0", ClampMin = "0" ) )
    FFloatInterval SwapPositionDelayInterval;

    /* Distance interval between boids to swap.
     * For example, if the minimum is set to 2, and the maximum to 4, and the first selected boid is at index 4,
     * only boids at index 0,1,2,6,7,8 would be eligible for a swap
     */
    UPROPERTY( EditAnywhere, meta = ( EditCondition = "bAllowSwapPositions", UIMin = "1", ClampMin = "1" ) )
    FInt32Interval SwapPositionDistanceInterval;

    // Range of the number of boids to move
    UPROPERTY( EditAnywhere, meta = ( EditCondition = "bAllowSwapPositions", UIMin = "1", ClampMin = "1" ) )
    FInt32Interval SwapPositionBoidCountInterval;
};

UCLASS( BlueprintType )
class UAFFlockSettingsData final : public UDataAsset
{
    GENERATED_BODY()

public:
    UAFFlockSettingsData()
    {
        TransitionDuration = 1.0f;
    }

    UPROPERTY( EditInstanceOnly )
    float TransitionDuration;

    UPROPERTY( EditInstanceOnly )
    FAFFlockSettings Settings;
};

USTRUCT()
struct FAFFlockingDebug
{
    GENERATED_USTRUCT_BODY()

    UPROPERTY( EditInstanceOnly )
    uint8 bDrawBoidSphere : 1;

    UPROPERTY( EditInstanceOnly )
    uint8 bDrawPursuitForce : 1;

    UPROPERTY( EditInstanceOnly )
    uint8 bDrawAlignmentForce : 1;

    UPROPERTY( EditInstanceOnly )
    uint8 bDrawCohesionForce : 1;

    UPROPERTY( EditInstanceOnly )
    uint8 bDrawSeparationForce : 1;
};

USTRUCT()
struct FAFBoidsData
{
    GENERATED_USTRUCT_BODY()

    FAFBoidsData() = default;
    FAFBoidsData( const UCharacterMovementComponent & movement_component );

    FVector Center;
    FVector Velocity;
    float MaxVelocity;
    FVector SteeringVelocity;
};

UCLASS( ClassGroup = Movement, hidecategories = ( Object, LOD, Lighting, Transform, Sockets, TextureStreaming ), meta = ( BlueprintSpawnableComponent ) )
class ACTORFLOCKING_API UAFFlockingComponent final : public UActorComponent
{
    GENERATED_BODY()

public:
    UAFFlockingComponent();

    UFUNCTION( BlueprintCallable )
    void RegisterMovementComponent( UCharacterMovementComponent * movement_component );

    UFUNCTION( BlueprintCallable )
    void UnRegisterMovementComponent( UCharacterMovementComponent * movement_component );

    void BeginPlay() override;

#if WITH_EDITOR
    void PostEditChangeProperty( FPropertyChangedEvent & property_changed_event ) override;
#endif

    UFUNCTION( BlueprintCallable )
    void SetSettings( UAFFlockSettingsData * new_settings );

    void TickComponent( float delta_time, ELevelTick tick_type, FActorComponentTickFunction * this_tick_function ) override;

private:
    void UpdateBoidsSteeringVelocity();
    void TrySetSwapBoidsPositionsTimer();
    void RandomSwapBoidsPositions();

    UPROPERTY( EditAnywhere )
    FAFFlockingDebug Debug;

    UPROPERTY( EditAnywhere )
    UAFFlockSettingsData * FlockSettingsData;

    UPROPERTY( VisibleInstanceOnly )
    FAFFlockSettings FlockSettings;

    TArray< FAFBoidsData > BoidsData;
    TArray< UCharacterMovementComponent * > BoidsMovementComponents;
    FAFFlockSettings FlockInitialSettings;
    FAFFlockSettings FlockTargetSettings;
    float TransitionDuration;
    float TransitionTimer;
    FTimerHandle SwapBoidPositionTimerHandle;
};
