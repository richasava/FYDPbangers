#include "ImuVisualizerActor.h"

#include "Common/UdpSocketBuilder.h"
#include "Common/UdpSocketReceiver.h"
#include "IPAddress.h"
#include "SocketSubsystem.h"
#include "Components/StaticMeshComponent.h"
#include "UObject/ConstructorHelpers.h"

AImuVisualizerActor::AImuVisualizerActor()
{
	PrimaryActorTick.bCanEverTick = true;

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

void AImuVisualizerActor::BeginPlay()
{
	Super::BeginPlay();

	AppStartTimeSeconds = FPlatformTime::Seconds();

	ListenSocket = FUdpSocketBuilder(TEXT("ImuListenSocket"))
		.AsNonBlocking()
		.AsReusable()
		.BoundToPort(ListenPort)
		.WithReceiveBufferSize(2 * 1024 * 1024);

	if (!ListenSocket)
	{
		UE_LOG(LogTemp, Error, TEXT("AImuVisualizerActor: failed to bind UDP socket on port %d"), ListenPort);
		return;
	}

	SocketReceiver = new FUdpSocketReceiver(ListenSocket, FTimespan::FromMilliseconds(10), TEXT("ImuSocketReceiverThread"));
	SocketReceiver->OnDataReceived().BindUObject(this, &AImuVisualizerActor::OnSocketDataReceived);
	SocketReceiver->Start();

	UE_LOG(LogTemp, Log, TEXT("AImuVisualizerActor: listening for IMU frames on UDP port %d"), ListenPort);
}

void AImuVisualizerActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (SocketReceiver)
	{
		delete SocketReceiver;
		SocketReceiver = nullptr;
	}

	if (ListenSocket)
	{
		ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(ListenSocket);
		ListenSocket = nullptr;
	}

	Super::EndPlay(EndPlayReason);
}

void AImuVisualizerActor::OnSocketDataReceived(const FArrayReaderPtr& ArrayReaderPtr, const FIPv4Endpoint& Endpoint)
{
	static constexpr int32 ExpectedFrameSize = 32; // 2x uint32 + 6x float, matches fake_imu_sender.py

	if (ArrayReaderPtr->Num() != ExpectedFrameSize)
	{
		return;
	}

	FImuFrame Frame;
	*ArrayReaderPtr << Frame.Seq;
	*ArrayReaderPtr << Frame.TimestampMs;

	// FArrayReader::operator<< byte-swaps floats via the network archive on
	// big-endian platforms; Mac/Windows/Linux dev machines and the STM32 are
	// all little-endian, so a raw serialize is correct here.
	ArrayReaderPtr->Serialize(&Frame.RollDeg, sizeof(float));
	ArrayReaderPtr->Serialize(&Frame.PitchDeg, sizeof(float));
	ArrayReaderPtr->Serialize(&Frame.YawDeg, sizeof(float));
	ArrayReaderPtr->Serialize(&Frame.AccelXg, sizeof(float));
	ArrayReaderPtr->Serialize(&Frame.AccelYg, sizeof(float));
	ArrayReaderPtr->Serialize(&Frame.AccelZg, sizeof(float));

	FScopeLock Lock(&FrameLock);
	PendingFrame = Frame;
	bHasPendingFrame = true;
}

void AImuVisualizerActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	FImuFrame Frame;
	bool bHadFrame = false;
	{
		FScopeLock Lock(&FrameLock);
		if (bHasPendingFrame)
		{
			Frame = PendingFrame;
			bHadFrame = true;
			bHasPendingFrame = false;
		}
	}

	if (!bHadFrame)
	{
		return;
	}

	LastRollDeg = Frame.RollDeg;
	LastPitchDeg = Frame.PitchDeg;
	LastYawDeg = Frame.YawDeg;
	FramesReceived++;

	const double NowMs = (FPlatformTime::Seconds() - AppStartTimeSeconds) * 1000.0;
	LastFrameAgeMs = static_cast<float>(NowMs - static_cast<double>(Frame.TimestampMs));

	if (bApplyRotationToMesh)
	{
		// Roll -> X (forward axis), Pitch -> Y, Yaw -> Z, matching the
		// firmware's roll/pitch/yaw convention from design doc Section 3.1.5.
		VisualMesh->SetRelativeRotation(FRotator(Frame.PitchDeg, Frame.YawDeg, Frame.RollDeg));
	}

	OnImuFrameReceived.Broadcast(Frame.RollDeg, Frame.PitchDeg, Frame.YawDeg);
}