#include "AliveProto.h"

namespace
{
	constexpr uint8 WireTypeVarint = 0;
	constexpr uint8 WireTypeFixed32 = 5;

	bool ReadVarint(const TArray<uint8>& Bytes, int32& Index, uint64& OutValue)
	{
		OutValue = 0;
		int32 Shift = 0;
		while (Index < Bytes.Num())
		{
			const uint8 Byte = Bytes[Index++];
			OutValue |= static_cast<uint64>(Byte & 0x7Fu) << Shift;
			if ((Byte & 0x80u) == 0)
			{
				return true;
			}
			Shift += 7;
			if (Shift >= 64)
			{
				return false; // malformed: varint too long
			}
		}
		return false; // ran out of bytes mid-varint
	}

	void WriteVarint(TArray<uint8>& Bytes, uint64 Value)
	{
		while (Value >= 0x80u)
		{
			Bytes.Add(static_cast<uint8>((Value & 0x7Fu) | 0x80u));
			Value >>= 7;
		}
		Bytes.Add(static_cast<uint8>(Value));
	}

	void WriteTag(TArray<uint8>& Bytes, uint32 FieldNumber, uint8 WireType)
	{
		WriteVarint(Bytes, (static_cast<uint64>(FieldNumber) << 3) | WireType);
	}
}

namespace AliveProto
{
	bool DecodeBiometricFrame(const TArray<uint8>& InBytes, FAliveBiometricFrame& OutFrame)
	{
		OutFrame = FAliveBiometricFrame{};

		int32 Index = 0;
		while (Index < InBytes.Num())
		{
			uint64 Tag = 0;
			if (!ReadVarint(InBytes, Index, Tag))
			{
				return false;
			}

			const uint32 FieldNumber = static_cast<uint32>(Tag >> 3);
			const uint8 WireType = static_cast<uint8>(Tag & 0x7u);

			if (WireType == WireTypeVarint)
			{
				uint64 Value = 0;
				if (!ReadVarint(InBytes, Index, Value))
				{
					return false;
				}

				switch (FieldNumber)
				{
				case 1: OutFrame.Seq = static_cast<int32>(Value); break;
				case 2: OutFrame.TimestampMs = static_cast<int32>(Value); break;
				case 8: OutFrame.bImuValid = Value != 0; break;
				case 9: OutFrame.bHrValid = Value != 0; break;
				case 10: OutFrame.bGsrValid = Value != 0; break;
				default: break; // unknown field, tag+value already consumed
				}
			}
			else if (WireType == WireTypeFixed32)
			{
				if (Index + 4 > InBytes.Num())
				{
					return false;
				}

				float Value = 0.f;
				FMemory::Memcpy(&Value, &InBytes[Index], sizeof(float));
				Index += 4;

				switch (FieldNumber)
				{
				case 3: OutFrame.ImuRoll = Value; break;
				case 4: OutFrame.ImuPitch = Value; break;
				case 5: OutFrame.ImuYaw = Value; break;
				case 6: OutFrame.HrBpm = Value; break;
				case 7: OutFrame.GsrConductance = Value; break;
				default: break;
				}
			}
			else
			{
				// Length-delimited/64-bit wire types aren't part of this schema.
				return false;
			}
		}

		return true;
	}

	TArray<uint8> EncodeHapticCommand(uint32 EventId, uint32 Intensity, uint32 DurationMs)
	{
		TArray<uint8> Bytes;
		WriteTag(Bytes, /*FieldNumber=*/1, WireTypeVarint);
		WriteVarint(Bytes, EventId);
		WriteTag(Bytes, /*FieldNumber=*/2, WireTypeVarint);
		WriteVarint(Bytes, Intensity);
		WriteTag(Bytes, /*FieldNumber=*/3, WireTypeVarint);
		WriteVarint(Bytes, DurationMs);
		return Bytes;
	}
}
