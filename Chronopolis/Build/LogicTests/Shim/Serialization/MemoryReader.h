#pragma once
#include "CoreMinimal.h"
class FMemoryReader : public FArchive { public: const TArray<uint8>& Bytes; int64 Offset = 0; explicit FMemoryReader(const TArray<uint8>& In) : Bytes(In) { bLoading = true; }
	void Serialize(void* Data, int64 Num) override { if (Offset + Num > Bytes.Num()) { SetError(); std::memset(Data, 0, Num); return; } std::memcpy(Data, Bytes.GetData() + Offset, Num); Offset += Num; } };
