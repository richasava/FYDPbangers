#include "AliveBiometricsActor.h"

#include "IMQTTCoreModule.h"
#include "IMQTTClient.h"
#include "MQTTClientMessage.h"
#include "Async/Async.h"
#include "Components/StaticMeshComponent.h"
#include "UObject/ConstructorHelpers.h"

AAliveBiometricsActor::AAliveBiometricsActor()
{
	PrimaryActorTick.bCanEverTick = false;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	VisualMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("VisualMesh"));
	VisualMesh->SetupAttachment(Root);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMeshAsset(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMeshAsset.Succeeded())
	{
		VisualMesh->SetStaticMesh(CubeMeshAsset.Object);
	}
	VisualMesh->SetWorldScale3D(FVector(1.0f, 0.6f, 0.2f)); // flat-ish, like a wearable puck
}

void AAliveBiometricsActor::BeginPlay()
{
	Super::BeginPlay();

	AppStartTimeSeconds = FPlatformTime::Seconds();

	const FMQTTURL Url(BrokerHost, static_cast<uint32>(BrokerPort));
	MqttClient = IMQTTCoreModule::Get().GetOrCreateClient(Url);
	if (!MqttClient.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("AAliveBiometricsActor: failed to create MQTT client for %s:%d"), *BrokerHost, BrokerPort);
		return;
	}

	MqttClient->OnConnect().AddUObject(this, &AAliveBiometricsActor::HandleConnect);
	MqttClient->Connect();
}

void AAliveBiometricsActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (MqttClient.IsValid())
	{
		MqttClient->Disconnect();
		MqttClient.Reset();
	}

	Super::EndPlay(EndPlayReason);
}

void AAliveBiometricsActor::HandleConnect(EMQTTConnectReturnCode ReturnCode)
{
	// Fires on the MQTT plugin's connection thread; hop to the game thread
	// before touching actor state.
	const bool bAccepted = (ReturnCode == EMQTTConnectReturnCode::Accepted);
	TWeakObjectPtr<AAliveBiometricsActor> WeakThis(this);

	AsyncTask(ENamedThreads::GameThread, [WeakThis, bAccepted]()
	{
		AAliveBiometricsActor* StrongThis = WeakThis.Get();
		if (!StrongThis)
		{
			return;
		}

		StrongThis->ApplyConnected(bAccepted);

		if (!bAccepted)
		{
			UE_LOG(LogTemp, Error, TEXT("AAliveBiometricsActor: MQTT connect rejected"));
			return;
		}

		if (StrongThis->MqttClient.IsValid())
		{
			// Not using IMQTTClient's templated Subscribe(Topic, Callable, QoS)
			// convenience overload here: its .Next() continuation captures the
			// callable by reference ([&]) off a parameter of an async call, so
			// by the time the continuation runs (after SUBACK, on the
			// connection thread) that reference is already dangling and the
			// message handler never actually gets wired up. Doing the
			// subscribe + bind ourselves with a by-value capture avoids it.
			StrongThis->MqttClient->Subscribe({MakeTuple(StrongThis->BiometricsTopic, EMQTTQualityOfService::Once)})
				.Next([WeakThis](const TArray<FMQTTSubscribeResult>& Results)
				{
					AAliveBiometricsActor* Self = WeakThis.Get();
					if (!Self || Results.Num() == 0 || !Results[0].Subscription.IsValid())
					{
						return;
					}

					Results[0].Subscription->OnSubscriptionMessage().AddLambda(
						[WeakThis](const FMQTTClientMessage& Message)
						{
							if (AAliveBiometricsActor* Inner = WeakThis.Get())
							{
								Inner->HandleBiometricsMessage(Message);
							}
						});
				});
		}

		UE_LOG(LogTemp, Log, TEXT("AAliveBiometricsActor: connected to %s:%d, subscribed to %s"),
			*StrongThis->BrokerHost, StrongThis->BrokerPort, *StrongThis->BiometricsTopic);
	});
}

void AAliveBiometricsActor::HandleBiometricsMessage(const FMQTTClientMessage& Message)
{
	// Also fires on the connection thread. Decoding is pure/thread-safe, so do
	// it here and only hop to the game thread to apply the result.
	FAliveBiometricFrame Frame;
	if (!AliveProto::DecodeBiometricFrame(Message.Payload, Frame))
	{
		return;
	}

	TWeakObjectPtr<AAliveBiometricsActor> WeakThis(this);
	AsyncTask(ENamedThreads::GameThread, [WeakThis, Frame]()
	{
		if (AAliveBiometricsActor* StrongThis = WeakThis.Get())
		{
			StrongThis->ApplyBiometricFrame(Frame);
		}
	});
}

void AAliveBiometricsActor::ApplyConnected(bool bConnected)
{
	bIsConnected = bConnected;
}

void AAliveBiometricsActor::ApplyBiometricFrame(const FAliveBiometricFrame& Frame)
{
	LastFrame = Frame;
	FramesReceived++;

	const double NowMs = (FPlatformTime::Seconds() - AppStartTimeSeconds) * 1000.0;
	LastFrameAgeMs = static_cast<float>(NowMs - static_cast<double>(Frame.TimestampMs));

	if (bApplyRotationToMesh && Frame.bImuValid)
	{
		// Roll -> X (forward axis), Pitch -> Y, Yaw -> Z, matching the
		// convention already used by AImuVisualizerActor.
		VisualMesh->SetRelativeRotation(FRotator(Frame.ImuPitch, Frame.ImuYaw, Frame.ImuRoll));
	}

	OnBiometricFrameReceived.Broadcast(Frame);
}

void AAliveBiometricsActor::SendHapticCommand(int32 EventId, int32 Intensity, int32 DurationMs)
{
	if (!MqttClient.IsValid() || !bIsConnected)
	{
		UE_LOG(LogTemp, Warning, TEXT("AAliveBiometricsActor: SendHapticCommand called while not connected"));
		return;
	}

	const TArray<uint8> Payload = AliveProto::EncodeHapticCommand(
		static_cast<uint32>(EventId), static_cast<uint32>(Intensity), static_cast<uint32>(DurationMs));

	MqttClient->Publish(HapticsTopic, Payload, EMQTTQualityOfService::AtLeastOnce);
}
