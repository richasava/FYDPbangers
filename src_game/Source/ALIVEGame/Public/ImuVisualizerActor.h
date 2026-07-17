#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HAL/CriticalSection.h"
#include "Interfaces/IPv4/IPv4Endpoint.h"
#include "Serialization/ArrayReader.h"
#include "ImuVisualizerActor.generated.h"

class FSocket;
class FUdpSocketReceiver;
using FArrayReaderPtr = TSharedPtr<FArrayReader, ESPMode::ThreadSafe>;

// Mirrors the 32-byte little-endian frame written by
// sensor_sim/fake_imu_sender.py (and, later, the STM32 firmware packet
// builder from design doc Section 3.1.1).
struct FImuFrame
{
	uint32 Seq = 0;
	uint32 TimestampMs = 0;
	float RollDeg = 0.f;
	float PitchDeg = 0.f;
	float YawDeg = 0.f;
	float AccelXg = 0.f;
	float AccelYg = 0.f;
	float AccelZg = 0.f;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnImuFrameReceived, float, RollDeg, float, PitchDeg, float, YawDeg);

// Drop this actor into a level and press Play: it opens a UDP socket,
// listens for IMU frames (see fake_imu_sender.py for the wire format),
// and applies the received orientation to VisualMesh every tick. Also
// broadcasts OnImuFrameReceived for HUD/telemetry widgets to bind to.
UCLASS()
class ALIVEGAME_API AImuVisualizerActor : public AActor
{
	GENERATED_BODY()

public:
	AImuVisualizerActor();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ALIVE|Network")
	int32 ListenPort = 9999;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ALIVE|Network")
	bool bApplyRotationToMesh = true;

	UPROPERTY(BlueprintReadOnly, Category = "ALIVE|Telemetry")
	float LastRollDeg = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "ALIVE|Telemetry")
	float LastPitchDeg = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "ALIVE|Telemetry")
	float LastYawDeg = 0.f;

	// Time between the frame's own timestamp and when the game thread
	// applied it -- this is the number to put on the telemetry HUD to
	// verify N1 (motion latency <= 100 ms).
	UPROPERTY(BlueprintReadOnly, Category = "ALIVE|Telemetry")
	float LastFrameAgeMs = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "ALIVE|Telemetry")
	int32 FramesReceived = 0;

	UPROPERTY(BlueprintAssignable, Category = "ALIVE|Telemetry")
	FOnImuFrameReceived OnImuFrameReceived;

	UPROPERTY(VisibleAnywhere, Category = "ALIVE")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, Category = "ALIVE")
	TObjectPtr<UStaticMeshComponent> VisualMesh;

private:
	void OnSocketDataReceived(const FArrayReaderPtr& ArrayReaderPtr, const FIPv4Endpoint& Endpoint);

	FSocket* ListenSocket = nullptr;
	FUdpSocketReceiver* SocketReceiver = nullptr;

	FCriticalSection FrameLock;
	FImuFrame PendingFrame;
	bool bHasPendingFrame = false;
	double AppStartTimeSeconds = 0.0;
};