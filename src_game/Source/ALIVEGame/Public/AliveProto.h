/**
 * Hand-written protobuf wire-format codec for the WCL wire schema.
 *
 * Mirrors firmware/wcl/proto/alive.proto. Only scalar fields (varint for
 * uint32/bool, fixed32 for float) are in play for BiometricFrame and
 * HapticCommand, so this is a small tailored codec rather than a dependency
 * on the engine's bundled full protobuf library -- keep it in lockstep with
 * alive.proto if that schema changes, the same coupling nanopb has on the
 * firmware side (see wcl.c).
 */
#pragma once

#include "CoreMinimal.h"
#include "AliveProto.generated.h"

// Mirrors alive.proto's BiometricFrame message (BAU -> AGE, topic alive/biometrics).
USTRUCT(BlueprintType)
struct ALIVEGAME_API FAliveBiometricFrame
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "ALIVE")
	int32 Seq = 0;

	UPROPERTY(BlueprintReadOnly, Category = "ALIVE")
	int32 TimestampMs = 0;

	UPROPERTY(BlueprintReadOnly, Category = "ALIVE")
	float ImuRoll = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "ALIVE")
	float ImuPitch = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "ALIVE")
	float ImuYaw = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "ALIVE")
	float HrBpm = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "ALIVE")
	float GsrConductance = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "ALIVE")
	bool bImuValid = false;

	UPROPERTY(BlueprintReadOnly, Category = "ALIVE")
	bool bHrValid = false;

	UPROPERTY(BlueprintReadOnly, Category = "ALIVE")
	bool bGsrValid = false;
};

namespace AliveProto
{
	// Decodes one alive.BiometricFrame message. Returns false on a malformed
	// byte stream; on success, fields absent from the wire (proto3 omits
	// default-valued scalars) are left at their FAliveBiometricFrame default.
	ALIVEGAME_API bool DecodeBiometricFrame(const TArray<uint8>& InBytes, FAliveBiometricFrame& OutFrame);

	// Encodes one alive.HapticCommand message (AGE -> BAU, topic alive/haptics).
	ALIVEGAME_API TArray<uint8> EncodeHapticCommand(uint32 EventId, uint32 Intensity, uint32 DurationMs);
}
