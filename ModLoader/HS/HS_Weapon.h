#pragma once
#include "Instance.h"

// Typed wrapper for HS weapon instances.
//
// Adding a new property:
//   1. Pick the matching property template from Instance.h:
//        StringProperty    for strings
//        RealProperty      for numerics / booleans
//        RValueProperty    for arrays / objects / raw access
//   2. Declare the member as `name { this, "gmlVarName" }`. The variable ID is
//      resolved at runtime from the global VariableMap, so no offset / ID
//      hardcoding is needed.
//   3. Variables not declared here can still be reached generically via
//      `weapon["someName"]`.

namespace HS {

	class HS_Weapon : public Instance
	{
	public:
		using Instance::Instance;

        RealProperty    Acceleration{ this, "Acceleration" };
        RealProperty    Activate{ this, "Activate" };
        StringProperty  Adjectives{ this, "Adjectives" };
        RealProperty    AimDelay{ this, "AimDelay" };
        RealProperty    AimMode{ this, "AimMode" };
        RealProperty    AimRange{ this, "AimRange" };
        RealProperty    AimSprite{ this, "AimSprite" };
        RealProperty    AimTime{ this, "AimTime" };
        RealProperty    Ammo{ this, "Ammo" };
        RealProperty    AmmoType{ this, "AmmoType" };
        RealProperty    Arrived{ this, "Arrived" };
        StringProperty  AttachmentName{ this, "AttachmentName" };
        StringProperty  AttachmentSlot{ this, "AttachmentSlot" };
        RealProperty    AudibleThroughWalls{ this, "AudibleThroughWalls" };
        RealProperty    AutoAimAngleRange{ this, "AutoAimAngleRange" };
        RealProperty    AutoAimTolerance{ this, "AutoAimTolerance" };
        RealProperty    AutoDash{ this, "AutoDash" };
        StringProperty  BaseName{ this, "BaseName" };
        RealProperty    BaseRange{ this, "BaseRange" };
        RealProperty    BounceFactor{ this, "BounceFactor" };
        RealProperty    BounceSound{ this, "BounceSound" };
        RealProperty    BounceSoundRadius{ this, "BounceSoundRadius" };
        RealProperty    BulletKnockbackSpeed{ this, "BulletKnockbackSpeed" };
        RealProperty    BulletSpeed{ this, "BulletSpeed" };
        RealProperty    BulletSprite{ this, "BulletSprite" };
        RealProperty    Capacity{ this, "Capacity" };
        RealProperty    Carried{ this, "Carried" };
        RealProperty    ChargeRate{ this, "ChargeRate" };
        StringProperty  Checksum{ this, "Checksum" };
        StringProperty  ChecksumString{ this, "ChecksumString" };
        RealProperty    CircleFactor{ this, "CircleFactor" };
        RealProperty    CollisionLength{ this, "CollisionLength" };
        RealProperty    CollisionWidth{ this, "CollisionWidth" };
        RealProperty    CostToFire{ this, "CostToFire" };
        RealProperty    Count{ this, "Count" };
        RealProperty    CullCounter{ this, "CullCounter" };
        RealProperty    CurrentSpeed{ this, "CurrentSpeed" };
        RealProperty    DashSpeed{ this, "DashSpeed" };
        RealProperty    DashTarget{ this, "DashTarget" };
        RealProperty    Deactivated{ this, "Deactivated" };
        RealProperty    Deceleration{ this, "Deceleration" };
        RealProperty    DecelerationDistance{ this, "DecelerationDistance" };
        RealProperty    DefaultKick{ this, "DefaultKick" };
        StringProperty  Description{ this, "Description" };
        RealProperty    DestinationFore{ this, "DestinationFore" };
        RealProperty    DestinationObject{ this, "DestinationObject" };
        RealProperty    DestinationStarboard{ this, "DestinationStarboard" };
        RealProperty    Duration{ this, "Duration" };
        RealProperty    ExtremeRapidFire{ this, "ExtremeRapidFire" };
        RealProperty    FailedChecksum{ this, "FailedChecksum" };
        RealProperty    Fire{ this, "Fire" };
        RealProperty    Fore{ this, "Fore" };
        RealProperty    ForeSpeed{ this, "ForeSpeed" };
        RealProperty    ForeSpeedDesired{ this, "ForeSpeedDesired" };
        RealProperty    ForeSpeedUnit{ this, "ForeSpeedUnit" };
        RealProperty    ForSale{ this, "ForSale" };
        RealProperty    FramesFailedToRemoveFromWall{ this, "FramesFailedToRemoveFromWall" };
        RealProperty    Generated{ this, "Generated" };
        RealProperty    GenerateWithAmmo{ this, "GenerateWithAmmo" };
        RealProperty    GiftID{ this, "GiftID" };
        StringProperty  Grade{ this, "Grade" };
        RealProperty    GunshotSound{ this, "GunshotSound" };
        RealProperty    GunshotSoundIndex{ this, "GunshotSoundIndex" };
        RealProperty    HighVelocityBulletSpeed{ this, "HighVelocityBulletSpeed" };
        RealProperty    HitHumanSound{ this, "HitHumanSound" };
        RealProperty    HitWallLastStep{ this, "HitWallLastStep" };
        RealProperty    HorizontalSpeed{ this, "HorizontalSpeed" };
        RealProperty    HumanHitSoundRadius{ this, "HumanHitSoundRadius" };
        RealProperty    ImpactMomentumSignal{ this, "ImpactMomentumSignal" };
        RealProperty    InventoryIndex{ this, "InventoryIndex" };
        RealProperty    JustDropped{ this, "JustDropped" };
        RealProperty    Kick{ this, "Kick" };
        RealProperty    KickForShotgun{ this, "KickForShotgun" };
        RealProperty    KickForSilenced{ this, "KickForSilenced" };
        RealProperty    KickForSuperShotgun{ this, "KickForSuperShotgun" };
        RealProperty    LastDirection{ this, "LastDirection" };
        RealProperty    LastGoodFore{ this, "LastGoodFore" };
        RealProperty    LastGoodStarboard{ this, "LastGoodStarboard" };
        RealProperty    LastOwnerTechnophobe{ this, "LastOwnerTechnophobe" };
        RealProperty    LoadedChecksum{ this, "LoadedChecksum" };
        StringProperty  LoadedChecksumString{ this, "LoadedChecksumString" };
        RealProperty    LockedToShip{ this, "LockedToShip" };
        RealProperty    LoudNoise{ this, "LoudNoise" };
        RealProperty    MeasuredSpeed{ this, "MeasuredSpeed" };
        RealProperty    MinSpeed{ this, "MinSpeed" };
        RealProperty    MotionFactor{ this, "MotionFactor" };
        RealProperty    MultipliedValue{ this, "MultipliedValue" };
        RealProperty    MyImageAngle{ this, "MyImageAngle" };
        RealProperty    MyPath{ this, "MyPath" };
        RealProperty    MyShop{ this, "MyShop" };
        RealProperty    Mysterious{ this, "Mysterious" };
        StringProperty  Name{ this, "Name" };
        RealProperty    Noise{ this, "Noise" };
        RealProperty    NoiseMultiplier{ this, "NoiseMultiplier" };
        RealProperty    NoiseRadius{ this, "NoiseRadius" };
        StringProperty  ObjectiveText{ this, "ObjectiveText" };
        RealProperty    On{ this, "On" };
        RealProperty    OnlyUseUnlockedTraits{ this, "OnlyUseUnlockedTraits" };
        StringProperty  OriginalCharacter{ this, "OriginalCharacter" };
        RealProperty    OriginalOwner{ this, "OriginalOwner" };
        RealProperty    Owner{ this, "Owner" };
        RealProperty    Parent{ this, "Parent" };
        RealProperty    Passive{ this, "Passive" };
        RealProperty    PathDirection{ this, "PathDirection" };
        RealProperty    PathProgress{ this, "PathProgress" };
        RealProperty    PersonalMissionItem{ this, "PersonalMissionItem" };
        RealProperty    PersonImStuckIn{ this, "PersonImStuckIn" };
        RealProperty    PriceMultiplier{ this, "PriceMultiplier" };
        RealProperty    Projectiles{ this, "Projectiles" };
        RealProperty    QuietNoise{ this, "QuietNoise" };
        StringProperty  Quote{ this, "Quote" };
        StringProperty  QuoteCredit{ this, "QuoteCredit" };
        RealProperty    Radius{ this, "Radius" };
        RealProperty    RadiusAlpha{ this, "RadiusAlpha" };
        RealProperty    Range{ this, "Range" };
        RealProperty    RapidFire{ this, "RapidFire" };
        RealProperty    Rarity{ this, "Rarity" };
        RealProperty    Rechargeable{ this, "Rechargeable" };
        RealProperty    Scale{ this, "Scale" };
        RealProperty    SecondsBetweenUses{ this, "SecondsBetweenUses" };
        RealProperty    SecondsOn{ this, "SecondsOn" };
        RealProperty    SecondsSinceTeleported{ this, "SecondsSinceTeleported" };
        RealProperty    SecondsUntilReady{ this, "SecondsUntilReady" };
        RealProperty    SectorX{ this, "SectorX" };
        RealProperty    SectorY{ this, "SectorY" };
        RealProperty    SellOnExit{ this, "SellOnExit" };
        RealProperty    Shiplocked{ this, "Shiplocked" };
        RealProperty    Shop{ this, "Shop" };
        RealProperty    ShotgunProjectiles{ this, "ShotgunProjectiles" };
        RealProperty    ShotgunSpread{ this, "ShotgunSpread" };
        RealProperty    SilencedNoise{ this, "SilencedNoise" };
        RealProperty    SoldByPlayer{ this, "SoldByPlayer" };
        RealProperty    SpecialItem{ this, "SpecialItem" };
        RealProperty    Speed{ this, "Speed" };
        RealProperty    Spin{ this, "Spin" };
        RealProperty    SpinBuffer{ this, "SpinBuffer" };
        RealProperty    Spread{ this, "Spread" };
        RealProperty    SpriteSize{ this, "SpriteSize" };
        RealProperty    Squishy{ this, "Squishy" };
        RealProperty    Starboard{ this, "Starboard" };
        RealProperty    StarboardSpeed{ this, "StarboardSpeed" };
        RealProperty    StarboardSpeedDesired{ this, "StarboardSpeedDesired" };
        RealProperty    StarboardSpeedUnit{ this, "StarboardSpeedUnit" };
        RealProperty    StashedForHandLuggageMission{ this, "StashedForHandLuggageMission" };
        RealProperty    StashOnExit{ this, "StashOnExit" };
        RealProperty    StuckTo{ this, "StuckTo" };
        RealProperty    Suckable{ this, "Suckable" };
        RealProperty    Sucking{ this, "Sucking" };
        RealProperty    SuperShotgunProjectiles{ this, "SuperShotgunProjectiles" };
        RealProperty    SuperShotgunSpread{ this, "SuperShotgunSpread" };
        RealProperty    SwitchOnPickup{ this, "SwitchOnPickup" };
        RealProperty    TargetX{ this, "TargetX" };
        RealProperty    TargetY{ this, "TargetY" };
        RealProperty    TextColour{ this, "TextColour" };
        RealProperty    ThrownBy{ this, "ThrownBy" };
        RealProperty    Thrust{ this, "Thrust" };
        RealProperty    TimeSincePickedUp{ this, "TimeSincePickedUp" };
        RealProperty    TimesUsed{ this, "TimesUsed" };
        StringProperty  TorsoAimAnim{ this, "TorsoAimAnim" };
        StringProperty  TorsoAttackAnim{ this, "TorsoAttackAnim" };
        StringProperty  TorsoDashAnim{ this, "TorsoDashAnim" };
        StringProperty  TorsoIdleAnim{ this, "TorsoIdleAnim" };
        StringProperty  TorsoRecoveryAnim{ this, "TorsoRecoveryAnim" };
        StringProperty  TorsoRunAnim{ this, "TorsoRunAnim" };
        StringProperty  TorsoThrowAnim{ this, "TorsoThrowAnim" };
        RealProperty    TrailLength{ this, "TrailLength" };
        RealProperty    TraitCount{ this, "TraitCount" };
        RValueProperty  Traits{ this, "Traits" };
        StringProperty  Type{ this, "Type" };
        RealProperty    UnlocksUniqueItem{ this, "UnlocksUniqueItem" };
        RealProperty    Updated{ this, "Updated" };
        RealProperty    UseHandle{ this, "UseHandle" };
        RealProperty    Uses{ this, "Uses" };
        RealProperty    Value{ this, "Value" };
        StringProperty  Verb{ this, "Verb" };
        RealProperty    VerticalSpeed{ this, "VerticalSpeed" };
        RealProperty    WallSlideVelocityRetentionFactor{ this, "WallSlideVelocityRetentionFactor" };
        RealProperty    Weight{ this, "Weight" };
        RealProperty    xReal{ this, "xReal" };
        RealProperty    xSpeed{ this, "xSpeed" };
        RealProperty    xThrust{ this, "xThrust" };
        RealProperty    yReal{ this, "yReal" };
        RealProperty    ySpeed{ this, "ySpeed" };
        RealProperty    yThrust{ this, "yThrust" };
	};

} // namespace HS
