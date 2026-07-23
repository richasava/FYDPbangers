#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AliveProto.h"
#include "MQTTShared.h"
#include "MQTTProtocol.h"
#include "AliveBiometricsActor.generated.h"

class IMQTTClient;
struct FMQTTClientMessage;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnBiometricFrameReceived, const FAliveBiometricFrame&, Frame);

// WCL consumer: connects to the Mosquitto broker over MQTT, subscribes to
// BiometricsTopic and decodes alive.BiometricFrame protobuf payloads (see
// firmware/wcl/proto/alive.proto), and can publish alive.HapticCommand back
// on HapticsTopic. Kept separate from AImuVisualizerActor, which stays wired
// to the pre-hardware UDP simulator (sensor_sim/fake_imu_sender.py) for
// offline testing without a broker running.
UCLASS()
class ALIVEGAME_API AAliveBiometricsActor : public AActor
{
	GENERATED_BODY()

public:
	AAliveBiometricsActor();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(VisibleAnywhere, Category = "ALIVE")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, Category = "ALIVE")
	TObjectPtr<UStaticMeshComponent> VisualMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ALIVE")
	bool bApplyRotationToMesh = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ALIVE|Network")
	FString BrokerHost = TEXT("10.120.6.136");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ALIVE|Network")
	int32 BrokerPort = 1883;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ALIVE|Network")
	FString BiometricsTopic = TEXT("alive/biometrics");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ALIVE|Network")
	FString HapticsTopic = TEXT("alive/haptics");

	UPROPERTY(BlueprintReadOnly, Category = "ALIVE|Network")
	bool bIsConnected = false;

	UPROPERTY(BlueprintReadOnly, Category = "ALIVE|Telemetry")
	FAliveBiometricFrame LastFrame;

	// Time between the frame's own timestamp and when the game thread applied
	// it -- mirrors the N1 motion-latency check in AImuVisualizerActor.
	UPROPERTY(BlueprintReadOnly, Category = "ALIVE|Telemetry")
	float LastFrameAgeMs = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "ALIVE|Telemetry")
	int32 FramesReceived = 0;

	UPROPERTY(BlueprintAssignable, Category = "ALIVE|Telemetry")
	FOnBiometricFrameReceived OnBiometricFrameReceived;

	// Encodes and publishes an alive.HapticCommand to HapticsTopic (QoS 1,
	// matching the "at least once" contract in alive.proto).
	UFUNCTION(BlueprintCallable, Category = "ALIVE|Network")
	void SendHapticCommand(int32 EventId, int32 Intensity, int32 DurationMs);

private:
	// Callback targets for the MQTT plugin's C++ delegates. These fire on the
	// plugin's connection thread, not the game thread, so each hops back to
	// the game thread (via a weak pointer, in case the actor is torn down in
	// the meantime) before touching actor state.
	void HandleConnect(EMQTTConnectReturnCode ReturnCode);
	void HandleBiometricsMessage(const FMQTTClientMessage& Message);

	void ApplyConnected(bool bConnected);
	void ApplyBiometricFrame(const FAliveBiometricFrame& Frame);

	// Uses the plugin's plain C++ interface (IMQTTCoreModule/IMQTTClient)
	// rather than its UMQTTClientObject Blueprint wrapper: in this engine
	// version UMQTTClientObject/UMQTTSubscriptionObject are declared without
	// the MQTTCORE_API export macro, so their methods link fine when called
	// from Blueprint (reflection-based dispatch) but fail to link when
	// called directly from another C++ module's dylib, as this one is.
	TSharedPtr<IMQTTClient, ESPMode::ThreadSafe> MqttClient;

	double AppStartTimeSeconds = 0.0;
};
