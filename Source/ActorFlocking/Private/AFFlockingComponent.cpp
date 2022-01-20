#include "AFFlockingComponent.h"

#include "GameFramework/CharacterMovementComponent.h"

#include <Curves/CurveFloat.h>
#include <DrawDebugHelpers.h>
#include <Engine/World.h>
#include <GameFramework/Character.h>
#include <TimerManager.h>

namespace
{
    FVector Seek( const FAFBoidsData & flock_data, const FVector & target, const float slowdown_distance = 100.0f )
    {
        const auto to_target = target - flock_data.Center;
        const auto to_target_direction = to_target.GetSafeNormal();

        auto desired_velocity = to_target_direction * flock_data.MaxVelocity;

        if ( slowdown_distance > 0.0f )
        {
            const auto distance_to_target = to_target.Size();
            const auto slowdown_falloff = FMath::Clamp( distance_to_target / slowdown_distance, 0.0f, 1.0f );

            desired_velocity *= slowdown_falloff;
        }

        return desired_velocity;
    }

    FVector Flee( const FAFBoidsData & flock_data, const FVector & from )
    {
        const auto desired_velocity = ( flock_data.Center - from ).GetSafeNormal() * flock_data.MaxVelocity;
        return desired_velocity;
    }

    FVector Pursuit( const FAFBoidsData & flock_data, const FVector & target, const FVector & target_velocity, const float slowdown_distance = 100.0f )
    {
        const auto distance = ( target - flock_data.Center ).Size();
        const auto time = distance / flock_data.MaxVelocity;
        const auto future_position = target + target_velocity * time;
        return Seek( flock_data, future_position, slowdown_distance );
    }

    FVector Evade( const FAFBoidsData & flock_data, const FVector & target, const FVector & target_velocity )
    {
        const auto distance = ( target - flock_data.Center ).Size();
        const auto time = distance / flock_data.MaxVelocity;
        const auto future_position = target + target_velocity * time;
        return Flee( flock_data, future_position );
    }

    FVector FollowLeader( const FAFBoidsData & flock_data, const FAFBoidsData & leader_flock_data, const float distance = 100.0f )
    {
        const auto target = leader_flock_data.Velocity.GetSafeNormal() * -1.0f * distance;
        return Seek( flock_data, target );
    }
}

FAFFlockSettings::FAFFlockSettings()
{
    PursuitWeight = 1.0f;
    PursuitSlowdownRadius = 500.0f;
    PursuitDistanceBehind = 500.0f;
    NonForwardVelocityBrakingFactor = 1.0f;
    AlignmentWeight = 1.0f;
    AlignmentRadius = 300.0f;
    CohesionWeight = 1.0f;
    CohesionRadius = 500.0f;
    SeparationWeight = 1.0f;
    SeparationRadius = 300.0f;
    QueueCurve = nullptr;
}

void FAFFlockSettings::LerpBetween( const FAFFlockSettings & start, const FAFFlockSettings & end, const float ratio )
{
    PursuitWeight = FMath::Lerp( start.PursuitWeight, end.PursuitWeight, ratio );
    PursuitSlowdownRadius = FMath::Lerp( start.PursuitSlowdownRadius, end.PursuitSlowdownRadius, ratio );
    PursuitDistanceBehind = FMath::Lerp( start.PursuitDistanceBehind, end.PursuitDistanceBehind, ratio );
    NonForwardVelocityBrakingFactor = FMath::Lerp( start.NonForwardVelocityBrakingFactor, end.NonForwardVelocityBrakingFactor, ratio );
    AlignmentWeight = FMath::Lerp( start.AlignmentWeight, end.AlignmentWeight, ratio );
    AlignmentRadius = FMath::Lerp( start.AlignmentRadius, end.AlignmentRadius, ratio );
    CohesionWeight = FMath::Lerp( start.CohesionWeight, end.CohesionWeight, ratio );
    CohesionRadius = FMath::Lerp( start.CohesionRadius, end.CohesionRadius, ratio );
    SeparationWeight = FMath::Lerp( start.SeparationWeight, end.SeparationWeight, ratio );
    SeparationRadius = FMath::Lerp( start.SeparationRadius, end.SeparationRadius, ratio );
}

FAFBoidsData::FAFBoidsData( const UCharacterMovementComponent & movement_component ) :
    Center( movement_component.GetOwner()->GetActorLocation() ),
    Velocity( movement_component.GetOwner()->GetVelocity() ),
    MaxVelocity( movement_component.GetMaxSpeed() ),
    SteeringVelocity( 0.0f, 0.0f, 0.0f )
{
}

UAFFlockingComponent::UAFFlockingComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = true;
}

void UAFFlockingComponent::RegisterMovementComponent( UCharacterMovementComponent * movement_component )
{
    BoidsMovementComponents.AddUnique( movement_component );
}

void UAFFlockingComponent::UnRegisterMovementComponent( UCharacterMovementComponent * movement_component )
{
    BoidsMovementComponents.Remove( movement_component );
}

void UAFFlockingComponent::BeginPlay()
{
    Super::BeginPlay();

    if ( FlockSettingsData != nullptr )
    {
        SetSettings( FlockSettingsData );
    }
}

#if WITH_EDITOR
void UAFFlockingComponent::PostEditChangeProperty( FPropertyChangedEvent & property_changed_event )
{
    Super::PostEditChangeProperty( property_changed_event );

    static const FName SettingsDataName( TEXT( "FlockSettingsData" ) );

    if ( property_changed_event.GetPropertyName() == SettingsDataName )
    {
        SetSettings( FlockSettingsData );
    }
}
#endif

void UAFFlockingComponent::SetSettings( UAFFlockSettingsData * new_settings )
{
    if ( new_settings == nullptr )
    {
        return;
    }

    PrimaryComponentTick.SetTickFunctionEnable( true );
    FlockTargetSettings = new_settings->Settings;
    FlockInitialSettings = FlockSettings;
    TransitionDuration = new_settings->TransitionDuration;
    TransitionTimer = TransitionDuration;
    FlockSettings.QueueCurve = new_settings->Settings.QueueCurve;
}

void UAFFlockingComponent::TickComponent( const float delta_time, const ELevelTick tick_type, FActorComponentTickFunction * this_tick_function )
{
    Super::TickComponent( delta_time, tick_type, this_tick_function );

    TransitionTimer -= delta_time;

    if ( TransitionTimer <= 0.0f )
    {
        TransitionTimer = 0.0f;
    }
    else
    {
        FlockSettings.LerpBetween( FlockInitialSettings, FlockTargetSettings, 1.0f - ( TransitionTimer / TransitionDuration ) );
    }

    BoidsData.Reset( BoidsMovementComponents.Num() );

    for ( const auto * boid_movement_component : BoidsMovementComponents )
    {
        BoidsData.Emplace( *boid_movement_component );
    }

    for ( auto index = 0; index < BoidsData.Num(); ++index )
    {
        UpdateSteeringVelocityIgnoringUID( BoidsData[ index ], index );
    }

    for ( auto index = 0; index < BoidsData.Num(); ++index )
    {
        BoidsMovementComponents[ index ]->RequestDirectMove( BoidsData[ index ].SteeringVelocity, false );
    }
}

void UAFFlockingComponent::UpdateSteeringVelocityIgnoringUID( FAFBoidsData & flock_data, const int32 ignore_this_uid )
{
    const auto velocity = flock_data.Velocity;
    /*const auto flock_index_in_array = Boids.IndexOfByPredicate( [ &flock_data ]( const FAFBoidsData & item ) {
        return item.Index == flock_data.Index;
    } );*/
    const auto owner = GetOwner();
    const auto actor_forward_vector = owner->GetActorForwardVector();

    FVector separation_force( 0.0f ),
        alignment_force( 0.0f ),
        cohesion_force( 0.0f );

    auto separation_boids_count = 0,
         alignment_boids_count = 0,
         cohesion_boids_count = 0;

    for ( auto index = 0; index < BoidsData.Num(); index++ )
    {
        if ( ignore_this_uid == index )
        {
            continue;
        }

        auto & other_flock_data = BoidsData[ index ];

        const auto to_other = other_flock_data.Center - flock_data.Center;
        const auto distance = to_other.Size();

        if ( distance < FlockSettings.AlignmentRadius )
        {
            alignment_force += other_flock_data.Velocity;
            alignment_boids_count++;
        }

        if ( distance < FlockSettings.CohesionRadius )
        {
            cohesion_force += other_flock_data.Center;
            cohesion_boids_count++;
        }

        if ( distance < FlockSettings.SeparationRadius )
        {
            separation_force += to_other * ( 1.0f - FMath::Clamp( distance / FlockSettings.SeparationRadius, 0.0f, 1.0f ) );
            separation_boids_count++;
        }
    }

    if ( alignment_boids_count > 0 )
    {
        alignment_force /= alignment_boids_count;
    }

    if ( cohesion_boids_count > 0 )
    {
        cohesion_force /= cohesion_boids_count;
        cohesion_force -= flock_data.Center;
        cohesion_force.Normalize();
        cohesion_force *= flock_data.MaxVelocity;
    }

    if ( separation_boids_count > 0 )
    {
        separation_force /= separation_boids_count;
        separation_force *= -1.0f;
        separation_force.Normalize();
        separation_force *= flock_data.MaxVelocity;
    }

    const auto pursuit_offset_multiplier = 1.0f;
    /*FlockSettings.QueueCurve != nullptr
                                               ? FlockSettings.QueueCurve->GetFloatValue( flock_index_in_array )
                                               : 1.0f;*/

    const auto pursuit_target = owner->GetActorLocation() - actor_forward_vector * FlockSettings.PursuitDistanceBehind * pursuit_offset_multiplier;

    const auto seek_force = Pursuit( flock_data, pursuit_target, owner->GetVelocity(), FlockSettings.PursuitSlowdownRadius );

    const auto draw_debug_line = [ world = GetWorld(), &flock_data ]( const FVector & end_offset, const FColor & color ) {
        DrawDebugLine( world, flock_data.Center, flock_data.Center + end_offset, color, false, -1.0f, SDPG_World, 5.0f );
    };

    if ( Debug.bDrawPursuitForce )
    {
        draw_debug_line( seek_force * FlockSettings.PursuitWeight, FColor::Green );
    }
    if ( Debug.bDrawAlignmentForce )
    {
        draw_debug_line( cohesion_force * FlockSettings.CohesionWeight, FColor::Yellow );
    }
    if ( Debug.bDrawCohesionForce )
    {
        draw_debug_line( alignment_force * FlockSettings.AlignmentWeight, FColor::Blue );
    }
    if ( Debug.bDrawSeparationForce )
    {
        draw_debug_line( separation_force * FlockSettings.SeparationWeight, FColor::Magenta );
    }
    if ( Debug.bDrawBoidSphere )
    {
        DrawDebugSphere( GetWorld(), flock_data.Center, 125.0f, 32, FColor::Blue );
    }

    auto result = velocity + seek_force * FlockSettings.PursuitWeight + cohesion_force * FlockSettings.CohesionWeight + alignment_force * FlockSettings.AlignmentWeight + separation_force * FlockSettings.SeparationWeight;
    const auto direction = result.GetSafeNormal();

    const auto dot = FVector::DotProduct( direction, actor_forward_vector );

    if ( dot < 0.0f )
    {
        result += result * -FlockSettings.NonForwardVelocityBrakingFactor;
    }

    flock_data.SteeringVelocity = result.GetSafeNormal() * flock_data.MaxVelocity;
}