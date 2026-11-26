// OpenFIREScopeComponent.cpp - see OpenFIREScopeComponent.h.
//
// MIT License - Copyright (c) 2026 Alessandro Satanassi, Team OpenFIRE
#include "OpenFIREScopeComponent.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "Kismet/GameplayStatics.h"
#include "OpenFIREScopeClient.h"
#include "TextureResource.h"

UOpenFIREScopeComponent::UOpenFIREScopeComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bTickEvenWhenPaused = true;      // keeps the scope's last image in the pause menu
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;   // after the camera has moved this frame
}

void UOpenFIREScopeComponent::BeginPlay()
{
	Super::BeginPlay();
	Client = FOpenFIREScopeClient::Acquire();   // one connection for all the components of the game

	Target = NewObject<UTextureRenderTarget2D>(this);
	Target->RenderTargetFormat = RTF_RGBA8;
	Target->ClearColor = FLinearColor::Black;
	Target->InitAutoFormat(FOpenFIREScopeClient::Width, FOpenFIREScopeClient::Height);
	Target->UpdateResourceImmediate(true);

	Capture = NewObject<USceneCaptureComponent2D>(GetOwner());
	Capture->SetupAttachment(GetOwner()->GetRootComponent());
	Capture->RegisterComponent();
	Capture->SetUsingAbsoluteLocation(true);              // we place it in world space every frame
	Capture->SetUsingAbsoluteRotation(true);
	Capture->TextureTarget = Target;
	Capture->CaptureSource = SCS_FinalColorLDR;           // tone-mapped, as on the screen
	Capture->bCaptureEveryFrame = false;                  // only when an image is sent
	Capture->bCaptureOnMovement = false;
	Capture->bAlwaysPersistRenderingState = true;         // keeps exposure and temporal effects between captures
	if (APawn* Pawn = Cast<APawn>(GetOwner()))
	{
		Capture->HiddenActors.Add(Pawn);                  // not the player's own body and gun
	}
	Pixels.SetNumUninitialized(FOpenFIREScopeClient::Width * FOpenFIREScopeClient::Height);
}

void UOpenFIREScopeComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	if (Capture) Capture->DestroyComponent();
	Capture = nullptr;
	Client.Reset();   // the last component closes the connection: the scopes show NO SIGNAL about 0.5 s later
	Super::EndPlay(Reason);
}

bool UOpenFIREScopeComponent::HasScope() const
{
	return Client.IsValid() && Client->HasScope(Player);
}

void UOpenFIREScopeComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!Client.IsValid() || !Capture || !Target) return;
	if (bAlwaysSend) Client->bAlwaysSend = true;
	Client->Update();   // once per frame for all components; discovery and pause packets; never blocks
	if (!Client->HasScope(Player)) return;   // no scope: no capture, no cost

	if (UGameplayStatics::IsGamePaused(this))
	{
		Client->SetPaused(Player, true);   // pause menu: the scope keeps the last image
		return;
	}

	const double Now = FPlatformTime::Seconds();
	if (Now < NextSend) return;
	NextSend = FMath::Max(NextSend + 1.0 / MaxFps, Now - 1.0 / MaxFps);
	if (!AimCapture()) return;

	Capture->CaptureScene();
	// ReadPixels waits for the GPU. For a 240x240 target the wait is short; see the README for a fully
	// asynchronous version if your game needs every fraction of a millisecond.
	FTextureRenderTargetResource* Resource = Target->GameThread_GetRenderTargetResource();
	if (!Resource || !Resource->ReadPixels(Pixels) || Pixels.Num() != FOpenFIREScopeClient::Width * FOpenFIREScopeClient::Height)
	{
		return;
	}
	if (bDrawReticle) DrawReticle();
	// FColor is B, G, R, A in memory: exactly the BGRX layout of the daemon, top row first
	Client->SendFrame(Player, Pixels.GetData(), FOpenFIREScopeClient::EPixels::BGRX8888);
}

// Places the capture at the player's camera, looking along the ray through the aim point, with a narrower field
// of view. The image stays upright: the scope rotates it with the roll of the gun.
bool UOpenFIREScopeComponent::AimCapture()
{
	APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
	if (!PC || !PC->PlayerCameraManager) return false;
	if (bUseMouse)
	{
		float X = 0.f, Y = 0.f;
		if (PC->GetMousePosition(X, Y)) AimScreenPosition = FVector2D(X, Y);
	}
	FVector WorldPosition, Direction;
	if (!PC->DeprojectScreenPositionToWorld(AimScreenPosition.X, AimScreenPosition.Y, WorldPosition, Direction)) return false;

	const FRotator CameraRotation = PC->PlayerCameraManager->GetCameraRotation();
	const FVector Up = FRotationMatrix(CameraRotation).GetUnitAxis(EAxis::Z);
	Capture->SetWorldLocationAndRotation(PC->PlayerCameraManager->GetCameraLocation(),
		FRotationMatrix::MakeFromXZ(Direction, Up).Rotator());

	// the same magnification for any camera field of view: tan(fov/2) divided by the zoom
	const float HalfFov = FMath::DegreesToRadians(PC->PlayerCameraManager->GetFOVAngle() * 0.5f);
	Capture->FOVAngle = FMath::RadiansToDegrees(2.f * FMath::Atan(FMath::Tan(HalfFov) / FMath::Max(1.f, Zoom)));
	return true;
}

// A thin cross with a gap and a dot, in the centre (119.5, 119.5).
void UOpenFIREScopeComponent::DrawReticle()
{
	constexpr int32 W = FOpenFIREScopeClient::Width;
	for (int32 I = 30; I < 210; ++I)
	{
		if (I > 110 && I < 130) continue;
		Pixels[119 * W + I] = Pixels[120 * W + I] = ReticleColor;   // horizontal
		Pixels[I * W + 119] = Pixels[I * W + 120] = ReticleColor;   // vertical
	}
	for (int32 Y = 118; Y <= 121; ++Y)
		for (int32 X = 118; X <= 121; ++X) Pixels[Y * W + X] = FColor(255, 40, 40);
}
