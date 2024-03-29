#include "AFFlockingComponent.h"

#include <Curves/CurveFloat.h>
#include <DrawDebugHelpers.h>
#include <Engine/World.h>
#include <GameFramework/Character.h>
#include <GameFramework/CharacterMovementComponent.h>
#include <TimerManager.h>

DECLARE_STATS_GROUP( TEXT( "Flocking" ), STATGROUP_Flocking, STATCAT_Advanced );
DECLARE_CYCLE_STAT( TEXT( "Flocking Tick" ), STAT_FlockingComponentTick, STATGROUP_Flocking );
DECLARE_CYCLE_STAT( TEXT( "Flocking RequestDirectMove" ), STAT_FlockingComponentRequestDirectMove, STATGROUP_Flocking );
DECLARE_CYCLE_STAT( TEXT( "Flocking Update Steering Velocity" ), STAT_FlockingComponentUpdateSteeringVelocity, STATGROUP_Flocking );

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
    bAllowSwapPositions = false;
    SwapPositionDelayInterval.Min = 0.0f;
    SwapPositionDelayInterval.Max = 0.0f;
    SwapPositionDistanceInterval.Min = 1;
    SwapPositionDistanceInterval.Max = 100;
    SwapPositionBoidCountInterval.Min = 1;
    SwapPositionBoidCountInterval.Max = 2;
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

FAFFlockingDebug::FAFFlockingDebug() :
    bDrawBoidSphere( false ),
    bDrawPursuitForce( false ),
    bDrawAlignmentForce( false ),
    bDrawCohesionForce( false ),
    bDrawSeparationForce( false )
{
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
    TransitionDuration = 0.0f;
    TransitionTimer = 0.0f;
}

void UAFFlockingComponent::RegisterMovementComponent( UCharacterMovementComponent * movement_component )
{
    if ( movement_component == nullptr )
    {
        return;
    }

    ensureMsgf( movement_component->IsFlying(), TEXT( "You should register flying actors to the flock" ) );

    BoidsMovementComponents.AddUnique( movement_component );
}

void UAFFlockingComponent::UnRegisterMovementComponent( UCharacterMovementComponent * movement_component )
{
    BoidsMovementComponents.Remove( movement_component );
}

void UAFFlockingComponent::BeginPlay()
{
    Super::BeginPlay();

    SetSettings( FlockSettingsData );
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
    FlockSettings.bAllowSwapPositions = new_settings->Settings.bAllowSwapPositions;
    FlockSettings.SwapPositionDistanceInterval = new_settings->Settings.SwapPositionDistanceInterval;
    FlockSettings.SwapPositionDelayInterval = new_settings->Settings.SwapPositionDelayInterval;
    FlockSettings.SwapPositionBoidCountInterval = new_settings->Settings.SwapPositionBoidCountInterval;

    if ( HasBegunPlay() )
    {
        TrySetSwapBoidsPositionsTimer();
    }
}

void UAFFlockingComponent::TickComponent( const float delta_time, const ELevelTick tick_type, FActorComponentTickFunction * this_tick_function )
{
    SCOPED_NAMED_EVENT( UAFFlockingComponent_TickComponent, FColor::Yellow );
    SCOPE_CYCLE_COUNTER( STAT_FlockingComponentTick );

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

    UpdateBoidsSteeringVelocity();

    SCOPE_CYCLE_COUNTER( STAT_FlockingComponentRequestDirectMove );
    for ( auto index = 0; index < BoidsData.Num(); ++index )
    {
        BoidsMovementComponents[ index ]->RequestDirectMove( BoidsData[ index ].SteeringVelocity, true );
    }
}

void UAFFlockingComponent::UpdateBoidsSteeringVelocity()
{
    SCOPE_CYCLE_COUNTER( STAT_FlockingComponentUpdateSteeringVelocity );

    const auto owner = GetOwner();
    const auto actor_forward_vector = owner->GetActorForwardVector();
    const auto owner_velocity = owner->GetVelocity();
    const auto owner_location = owner->GetActorLocation();

    for ( auto boid_index = 0; boid_index < BoidsData.Num(); ++boid_index )
    {
        auto & flock_data = BoidsData[ boid_index ];
        const auto velocity = flock_data.Velocity;

        FVector separation_force( 0.0f ),
            alignment_force( 0.0f ),
            cohesion_force( 0.0f );

        auto separation_boids_count = 0,
             alignment_boids_count = 0,
             cohesion_boids_count = 0;

        for ( auto other_boid_index = 0; other_boid_index < BoidsData.Num(); ++other_boid_index )
        {
            if ( other_boid_index == boid_index )
            {
                continue;
            }

            const auto & other_flock_data = BoidsData[ other_boid_index ];
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

        const auto pursuit_offset_multiplier = FlockSettings.QueueCurve != nullptr
                                                   ? FlockSettings.QueueCurve->GetFloatValue( boid_index )
                                                   : 1.0f;
        const auto pursuit_target = owner_location - actor_forward_vector * FlockSettings.PursuitDistanceBehind * pursuit_offset_multiplier;

        const auto seek_force = Pursuit( flock_data, pursuit_target, owner_velocity, FlockSettings.PursuitSlowdownRadius );

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

        flock_data.SteeringVelocity = direction * flock_data.MaxVelocity;
    }
}

void UAFFlockingComponent::TrySetSwapBoidsPositionsTimer()
{
    if ( FlockSettings.bAllowSwapPositions )
    {
        const auto delay = FMath::FRandRange( FlockSettings.SwapPositionDelayInterval.Min, FlockSettings.SwapPositionDelayInterval.Max );
        GetWorld()->GetTimerManager().SetTimer( SwapBoidPositionTimerHandle, this, &UAFFlockingComponent::RandomSwapBoidsPositions, delay );
    }
}

void UAFFlockingComponent::RandomSwapBoidsPositions()
{
    const auto boids_count = BoidsMovementComponents.Num();

    if ( boids_count == 2 )
    {
        BoidsMovementComponents.Swap( 0, 1 );
    }
    else if ( boids_count > 2 )
    {
        auto boids_to_swap_count = FMath::RandRange( FlockSettings.SwapPositionBoidCountInterval.Min, FlockSettings.SwapPositionBoidCountInterval.Max );

        while ( boids_to_swap_count > 0 )
        {
            const auto first_boid_index = FMath::RandRange( 0, boids_count - 1 );

            TArray< int > indices;
            indices.Reserve( BoidsMovementComponents.Num() - 1 );

            const auto first_index = FMath::Max( 0, first_boid_index - FlockSettings.SwapPositionDistanceInterval.Max );
            const auto last_index = FMath::Min( BoidsMovementComponents.Num(), first_boid_index + FlockSettings.SwapPositionDistanceInterval.Max );

            for ( auto index = first_index; index < last_index; ++index )
            {
                if ( index == first_boid_index )
                {
                    continue;
                }

                if ( FMath::Abs( index - first_boid_index ) < FlockSettings.SwapPositionDistanceInterval.Min )
                {
                    continue;
                }

                indices.Add( index );
            }

            if ( indices.Num() >= 2 )
            {
                const auto random_index = FMath::RandRange( 0, indices.Num() - 1 );
                const auto second_boid_index = indices[ random_index ];

                BoidsMovementComponents.Swap( first_boid_index, second_boid_index );
            }

            --boids_to_swap_count;
        }
    }

    TrySetSwapBoidsPositionsTimer();
}
