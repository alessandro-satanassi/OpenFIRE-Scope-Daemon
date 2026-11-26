// OpenFIREScopeComponent.h - the scope view of one player, for Unreal Engine 5.
//
// Add it to the player's pawn (or any actor). While that player has a paired scope it captures a 240x240 view along
// the ray through the aim point on the screen, with a zoom, draws a reticle and sends it to the scope. While the
// player has no scope it does nothing at all. Works in C++ and Blueprints.
//
// MIT License - Copyright (c) 2026 Alessandro Satanassi, Team OpenFIRE
#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "OpenFIREScopeComponent.generated.h"

class FOpenFIREScopeClient;
class USceneCaptureComponent2D;
class UTextureRenderTarget2D;

UCLASS(ClassGroup = (OpenFIRE), meta = (BlueprintSpawnableComponent))
class UOpenFIREScopeComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UOpenFIREScopeComponent();

	// The player whose scope shows this view.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OpenFIRE Scope", meta = (ClampMin = "1", ClampMax = "4"))
	int32 Player = 1;

	// Magnification: 4 means a field of view 4 times narrower than the player's camera.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OpenFIRE Scope", meta = (ClampMin = "1.0", ClampMax = "20.0"))
	float Zoom = 4.0f;

	// Images per second at most. The daemon shows 30 to 60 per second; more is wasted.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OpenFIRE Scope", meta = (ClampMin = "10", ClampMax = "60"))
	int32 MaxFps = 60;

	// Aim with the mouse cursor of the player controller. Untick it to set AimScreenPosition yourself.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OpenFIRE Scope")
	bool bUseMouse = true;

	// The aim point in viewport pixels (0,0 = top left), used when bUseMouse is off.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OpenFIRE Scope")
	FVector2D AimScreenPosition = FVector2D::ZeroVector;

	// Draw a simple crosshair (the scope draws none). Untick it if your capture draws its own reticle.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OpenFIRE Scope")
	bool bDrawReticle = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OpenFIRE Scope")
	FColor ReticleColor = FColor::Black;

	// Testing only: send even when no scope is paired, to watch the images in the daemon's Preview tab.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OpenFIRE Scope")
	bool bAlwaysSend = false;

	// True if this player has a paired scope right now.
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "OpenFIRE Scope")
	bool HasScope() const;

	// The scene capture, for your own settings (show flags, post-process, hidden actors...).
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "OpenFIRE Scope")
	USceneCaptureComponent2D* GetCapture() const { return Capture; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	bool AimCapture();
	void DrawReticle();

	UPROPERTY(Transient)
	TObjectPtr<USceneCaptureComponent2D> Capture;

	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> Target;

	TSharedPtr<FOpenFIREScopeClient> Client;
	TArray<FColor> Pixels;
	double NextSend = 0.0;
};
